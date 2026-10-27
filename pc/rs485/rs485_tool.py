#!/usr/bin/env python3
"""rs485_tool.py — USB-RS485 adapter on the PC: test the Modbus-RTU relay module + tower light without the board,
watch the board's RS485 traffic, or stand in for the relay module (docs/到货联调清单.md §7b).

  python rs485_tool.py ports                              list serial ports (the adapter: "USB-SERIAL CH340" / FTDI ...)
  python rs485_tool.py scan  --port COM10                 find the module: per baud rate (9600 / 19200 / 4800 / 38400 /
                                                          115200, parity N / E) the broadcast address query, then FC 01
                                                          at addresses 1..16 and 255
  python rs485_tool.py addr  --port COM10                 ask the (only) module on the bus for its address (broadcast)
  python rs485_tool.py setaddr --port COM10 1             give the (only) module on the bus a new address (broadcast)
  python rs485_tool.py coils --port COM10 [--inputs 4]    read the relays (FC 01) (+ digital inputs, FC 02)
  python rs485_tool.py set   --port COM10 2 on            one relay (FC 05; relays count from 0)
  python rs485_tool.py lamp  --port COM10 red             lamp relays to exactly this: green / yellow / red / buzzer / off
                                                          (one FC 05 per relay that changes, then read back)
  python rs485_tool.py demo  --port COM10 [--buzzer]      what des_app shows: idle (dark; rs485.idle green / blink) / run / paused / e-stop /
                                                          fault; the buzzer only with --buzzer
  python rs485_tool.py sniff --port COM10                 listen only: every frame with time, gap, CRC (board <-> module)
  python rs485_tool.py slave --port COM10                 be the relay module (address --addr): the board talks to the PC
Common: --addr 1  --baud 9600  --parity N  --coils 8  --lamp-coils 0,1,2,3 (relay of green, yellow, red, buzzer; same as
des_app.json rs485.lamp_coils). Needs pyserial (pip install pyserial). A / B swapped = no reply: swap them (harmless).

The 10-26 module (bestep 4-ch Modbus relay board) — found on the real board, see docs/到货联调清单.md §7b:
  factory address 255; address query = broadcast FC 03 register 0 (reply 00 03 02 00 <addr>); the address is set only by
  broadcast FC 10 register 0 (FC 06 ignored, no reply either way); FC 01 answered only for 8 coils (4 -> silence);
  FC 0F ignored unless every bit is 0 -> lamps are written one relay at a time with FC 05 (reply = echo of the request).
"""
import argparse
import sys
import time

try:
    import serial
    import serial.tools.list_ports
except ImportError:
    serial = None

LAMPS = {"green": 0, "yellow": 1, "red": 2, "buzzer": 3}
EXC = {1: "illegal function", 2: "illegal data address", 3: "illegal data value", 4: "slave device failure",
       5: "acknowledge", 6: "slave device busy"}


def crc16(b):
    c = 0xFFFF
    for x in b:
        c ^= x
        for _ in range(8):
            c = (c >> 1) ^ 0xA001 if c & 1 else c >> 1
    return c


def with_crc(pdu):
    c = crc16(pdu)
    return bytes(pdu) + bytes([c & 0xFF, c >> 8])


def crc_ok(frame):
    return len(frame) >= 4 and crc16(frame[:-2]) == (frame[-2] | frame[-1] << 8)


def hexs(b):
    return " ".join("%02X" % x for x in b)


def char_s(baud, parity):
    return (11 if parity != "N" else 10) / baud   # start + 8 data (+ parity) + stop


class Bus:
    def __init__(self, a):
        if serial is None:
            sys.exit("pyserial is missing: pip install pyserial")
        self.a = a
        self.open(a.baud, a.parity)

    def open(self, baud, parity):
        if getattr(self, "s", None):
            self.s.close()
        par = {"N": serial.PARITY_NONE, "E": serial.PARITY_EVEN, "O": serial.PARITY_ODD}[parity]
        self.baud, self.parity = baud, parity
        self.s = serial.Serial(self.a.port, baud, bytesize=8, parity=par, stopbits=1, timeout=0)
        self.gap = max(0.004, 3.5 * char_s(baud, parity))   # end of frame (Windows timers: >= a few ms)

    def read_frame(self, first_timeout):
        """bytes until a silence of one inter-frame gap; b'' when nothing came within first_timeout"""
        buf, t_end, last = bytearray(), time.perf_counter() + first_timeout, None
        while True:
            chunk = self.s.read(256)
            now = time.perf_counter()
            if chunk:
                buf += chunk
                last = now
            elif last is not None and now - last > self.gap:
                return bytes(buf)
            elif last is None and now > t_end:
                return b""
            time.sleep(0.0005)

    def xact(self, pdu, timeout=0.3):
        """request (CRC added) -> (reply without CRC or None, error text, reply time ms)"""
        req = with_crc(pdu)
        self.s.reset_input_buffer()
        t0 = time.perf_counter()
        self.s.write(req)
        self.s.flush()
        rep = self.read_frame(timeout + len(req) * char_s(self.baud, self.parity))
        ms = (time.perf_counter() - t0) * 1000
        # Adapters that hear themselves put the request in front of the reply. Only strip it when more follows: the
        # FC 05 / 06 reply IS a copy of the request (lamp checks it by reading back, so an echo-only adapter with a
        # silent module still shows up there).
        if rep.startswith(req) and len(rep) > len(req):
            rep = rep[len(req):]
        if not rep:
            return None, "no reply", ms
        if not crc_ok(rep):
            return None, "CRC error: " + hexs(rep), ms
        rep = rep[:-2]
        if rep[0] != pdu[0]:
            return None, "reply from address %d" % rep[0], ms
        if rep[1] == pdu[1] | 0x80:
            if len(rep) < 3:
                return None, "short exception reply", ms
            return None, "exception %d (%s)" % (rep[2], EXC.get(rep[2], "?")), ms
        return rep, "", ms


def read_bits(bus, addr, fc, n):
    rep, err, ms = bus.xact(bytes([addr, fc, 0, 0, 0, n]))
    if rep is None:
        return None, err, ms
    v = int.from_bytes(rep[3:3 + rep[2]], "little")
    return v & ((1 << n) - 1), "", ms


def write_coil(bus, addr, k, on):
    rep, err, ms = bus.xact(bytes([addr, 5, k >> 8, k & 0xFF, 0xFF if on else 0, 0]))
    return rep is not None, err, ms


def lamp_mask(lc):
    return sum(1 << k for k in lc if k >= 0)


def set_lamp_relays(bus, a, lc, bits, cur=None):
    """lamp relays to bits, one FC 05 per relay that differs from cur (None = write them all); returns (ok, err, ms)"""
    ok, err, ms_all = True, "", 0.0
    for k in sorted(k for k in lc if k >= 0):
        on = bool(bits >> k & 1)
        if cur is not None and bool(cur >> k & 1) == on:
            continue
        ok, err, ms = write_coil(bus, a.addr, k, on)
        ms_all += ms
        if not ok:
            return False, "relay %d: %s" % (k, err), ms_all
    return ok, err, ms_all


def query_addr(bus):
    """broadcast FC 03 register 0: the address of the only module on the bus, or None"""
    rep, err, ms = bus.xact(bytes([0, 3, 0, 0, 0, 1]))
    if rep is None or len(rep) < 5 or rep[1] != 3:
        return None, err or "bad reply", ms
    return rep[3] << 8 | rep[4], "", ms


def lamps_to_bits(names, lc):
    return sum(1 << lc[LAMPS[n]] for n in names if n in LAMPS and lc[LAMPS[n]] >= 0)


def bits_to_lamps(bits, lc):
    on = [n for n, k in LAMPS.items() if lc[k] >= 0 and bits >> lc[k] & 1]
    return "+".join(on) if on else "all off"


def cmd_scan(bus, a):
    for baud in (9600, 19200, 4800, 38400, 115200):
        for par in ("N", "E"):
            bus.open(baud, par)
            print("%6d 8%s1: " % (baud, par), end="", flush=True)
            addr, err, ms = query_addr(bus)
            if addr is not None:
                print("address query: module at address %d (%.1f ms)" % (addr, ms))
                return 0
            for addr in list(range(1, 17)) + [255]:
                v, err, ms = read_bits(bus, addr, 1, a.coils)
                if v is not None or err.startswith("exception"):
                    print("address %d answers (%s, %.1f ms)" % (addr, "coils %s" % format(v, "0%db" % a.coils)
                                                                 if v is not None else err, ms))
                    return 0
            print("nothing")
    print("no module found: power (24 V), A / B swapped, another address / baud rate, or a module that answers FC 01 "
          "only for another --coils count (the bestep board: 8)")
    return 1


def cmd_demo(bus, a, lc):
    red = ["red", "buzzer"] if a.buzzer else ["red"]
    steps = [("idle: dark (des_app.json rs485.idle off)", [[]], 2), ("run: green", [["green"]], 3),
             ("paused: yellow", [["yellow"]], 3),
             ("e-stop: red + buzzer 1 s" if a.buzzer else "e-stop: red (buzzer skipped, --buzzer adds it)", [red], 1),
             ("e-stop: red", [["red"]], 2), ("fault: red blinks", [["red"], []], 4)]
    cur = None
    for title, pats, secs in steps:
        print(title)
        end, i = time.time() + secs, 0
        while time.time() < end:
            bits = lamps_to_bits(pats[i % len(pats)], lc)
            ok, err, ms = set_lamp_relays(bus, a, lc, bits, cur)
            cur = bits if ok else None
            if not ok:
                print("  ", err)
            i += 1
            time.sleep(0.5)
    set_lamp_relays(bus, a, lc, 0, None)
    return 0


def cmd_lamp(bus, a, lc):
    bad = [x for x in a.args if x not in LAMPS and x != "off"]
    if bad or not a.args:
        sys.exit("lamp <green|yellow|red|buzzer|off>...")
    bits = lamps_to_bits(a.args, lc)
    cur, err, ms = read_bits(bus, a.addr, 1, a.coils)
    if cur is None:
        print("read before writing: %s (--coils 8 for the bestep board); writing every lamp relay" % err)
    ok, err, ms = set_lamp_relays(bus, a, lc, bits, cur)
    if not ok:
        print("lamps %s: %s" % (" ".join(a.args), err))
        return 1
    rb, err, ms2 = read_bits(bus, a.addr, 1, a.coils)
    if rb is None:
        print("lamps %s: written (%.1f ms), read-back failed: %s" % (" ".join(a.args), ms, err))
        return 1
    match = (rb ^ bits) & lamp_mask(lc) == 0
    print("lamps %s: written (%.1f ms), read back %s = %s%s" % (" ".join(a.args), ms, format(rb, "0%db" % a.coils),
                                                               bits_to_lamps(rb, lc), "" if match else "  MISMATCH"))
    return 0 if match else 1


def cmd_setaddr(bus, a):
    if len(a.args) != 1 or not 1 <= int(a.args[0]) <= 247:
        sys.exit("setaddr <1..247>   (only ONE module on the bus: the request is a broadcast)")
    new = int(a.args[0])
    old, err, _ = query_addr(bus)
    print("before: %s" % ("address %d" % old if old is not None else err))
    bus.xact(bytes([0, 0x10, 0, 0, 0, 1, 2, new >> 8, new & 0xFF]))   # broadcast: no reply expected
    time.sleep(0.3)
    now, err, _ = query_addr(bus)
    print("after:  %s" % ("address %d" % now if now is not None else err))
    return 0 if now == new else 1


def cmd_sniff(bus, a):
    print("listening at %d 8%s1 (Ctrl+C stops)" % (bus.baud, bus.parity))
    t0, prev = time.perf_counter(), None
    while True:
        f = bus.read_frame(3600)
        if not f:
            continue
        now = time.perf_counter()
        gap = "" if prev is None else "  +%.1f ms" % ((now - prev) * 1000)
        prev = now
        ok = crc_ok(f)
        what = ""
        if ok and len(f) >= 4:
            fc = f[1]
            what = {1: "read coils", 2: "read inputs", 3: "read holding", 4: "read input regs", 5: "write coil",
                    6: "write reg", 15: "write coils", 16: "write regs"}.get(fc & 0x7F, "fc %d" % fc)
            if fc & 0x80:
                what += " EXCEPTION %d" % f[2]
        print("%9.3f s%s  addr %d  %-16s %s  %s" % (now - t0, gap, f[0], what, hexs(f), "CRC ok" if ok else "CRC BAD"))


def cmd_slave(bus, a, lc):
    coils, inputs = 0, 0
    print("relay module at address %d, %d coils, %d 8%s1 (Ctrl+C stops); lamps follow the board's writes"
          % (a.addr, a.coils, bus.baud, bus.parity))
    while True:
        f = bus.read_frame(3600)
        if not f or not crc_ok(f) or f[0] != a.addr or len(f) < 8:
            continue
        fc, start, cnt = f[1], f[2] << 8 | f[3], f[4] << 8 | f[5]
        before = coils
        if fc in (1, 2):
            v = (coils if fc == 1 else inputs) >> start
            nb = (cnt + 7) // 8
            rep = bytes([a.addr, fc, nb]) + (v & ((1 << cnt) - 1)).to_bytes(nb, "little")
        elif fc == 5:
            coils = coils | (1 << start) if cnt == 0xFF00 else coils & ~(1 << start)
            rep = f[:6]
        elif fc == 15:
            data = int.from_bytes(f[7:7 + f[6]], "little")
            for i in range(cnt):
                coils = coils | (1 << (start + i)) if data >> i & 1 else coils & ~(1 << (start + i))
            rep = f[:6]
        else:
            rep = bytes([a.addr, fc | 0x80, 1])
        time.sleep(0.002)   # a real module answers after a few ms
        bus.s.write(with_crc(rep))
        if coils != before:
            print("%s  lamps: %s" % (time.strftime("%H:%M:%S"), bits_to_lamps(coils, lc)))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("op", choices=["ports", "scan", "addr", "setaddr", "coils", "set", "lamp", "demo", "sniff", "slave"])
    ap.add_argument("args", nargs="*")
    ap.add_argument("--port", default="")
    ap.add_argument("--addr", type=int, default=1)
    ap.add_argument("--baud", type=int, default=9600)
    ap.add_argument("--parity", default="N", choices=["N", "E", "O"])
    ap.add_argument("--coils", type=int, default=8)
    ap.add_argument("--buzzer", action="store_true", help="demo: include the buzzer")
    ap.add_argument("--inputs", type=int, default=0)
    ap.add_argument("--lamp-coils", default="0,1,2,3")
    a = ap.parse_args()
    try:
        sys.stdout.reconfigure(line_buffering=True)
    except AttributeError:
        pass
    if a.op == "ports":
        if serial is None:
            sys.exit("pyserial is missing: pip install pyserial")
        for p in serial.tools.list_ports.comports():
            print("%-8s %s" % (p.device, p.description))
        return 0
    if not a.port:
        sys.exit("--port COMn (python rs485_tool.py ports lists them)")
    lc = [int(x) for x in a.lamp_coils.split(",")]
    if len(lc) != 4:
        sys.exit("--lamp-coils needs 4 numbers: green, yellow, red, buzzer (-1 = not wired)")
    bus = Bus(a)
    try:
        if a.op == "scan":
            return cmd_scan(bus, a)
        if a.op == "addr":
            v, err, ms = query_addr(bus)
            print("module at address %d, %.1f ms" % (v, ms) if v is not None else err)
            return 0 if v is not None else 1
        if a.op == "setaddr":
            return cmd_setaddr(bus, a)
        if a.op == "coils":
            v, err, ms = read_bits(bus, a.addr, 1, a.coils)
            print("coils %s (%s), %.1f ms" % (format(v, "0%db" % a.coils), bits_to_lamps(v, lc), ms) if v is not None else err)
            if a.inputs:
                v, err, ms = read_bits(bus, a.addr, 2, a.inputs)
                print("inputs %s, %.1f ms" % (format(v, "0%db" % a.inputs), ms) if v is not None else err)
            return 0 if v is not None else 1
        if a.op == "set":
            if len(a.args) != 2 or a.args[1] not in ("on", "off"):
                sys.exit("set <relay> on|off")
            k = int(a.args[0])
            if not 0 <= k <= 255:
                sys.exit("set <relay 0..255> on|off")
            ok, err, ms = write_coil(bus, a.addr, k, a.args[1] == "on")
            print("relay %d %s: %s, %.1f ms" % (k, a.args[1], "ok" if ok else err, ms))
            return 0 if ok else 1
        if a.op == "lamp":
            return cmd_lamp(bus, a, lc)
        if a.op == "demo":
            return cmd_demo(bus, a, lc)
        if a.op == "sniff":
            return cmd_sniff(bus, a)
        if a.op == "slave":
            return cmd_slave(bus, a, lc)
    except KeyboardInterrupt:
        return 0
    finally:
        bus.s.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
