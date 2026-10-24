#!/usr/bin/env python3
"""rs485_tool.py — USB-RS485 adapter on the PC: test the Modbus-RTU relay module + tower light without the board,
watch the board's RS485 traffic, or stand in for the relay module (docs/到货联调清单.md §7b).

  python rs485_tool.py ports                              list serial ports (the adapter: "USB-SERIAL CH340" / FTDI ...)
  python rs485_tool.py scan  --port COM10                 find the module: addresses 1..16, 9600 / 19200 / 4800 / 38400 /
                                                          115200 baud, parity N / E (reads coil 0..3 until one answers)
  python rs485_tool.py coils --port COM10 [--inputs 4]    read the relays (FC 01) (+ digital inputs, FC 02)
  python rs485_tool.py set   --port COM10 2 on            one relay (FC 05; relays count from 0)
  python rs485_tool.py lamp  --port COM10 red buzzer      all relays at once (FC 0F): green / yellow / red / buzzer / off
  python rs485_tool.py demo  --port COM10                 what des_app shows: idle (green blinks) / run / paused / e-stop
  python rs485_tool.py sniff --port COM10                 listen only: every frame with time, gap, CRC (board <-> module)
  python rs485_tool.py slave --port COM10                 be the 4-relay module (address --addr): the board talks to the PC
Common: --addr 1  --baud 9600  --parity N  --lamp-coils 0,1,2,3 (relay of green, yellow, red, buzzer; same as
des_app.json rs485.lamp_coils). Needs pyserial (pip install pyserial). A / B swapped = no reply: swap them (harmless).
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
        if rep.startswith(req):          # adapters that hear themselves
            rep = rep[len(req):]
        if not rep:
            return None, "no reply", ms
        if not crc_ok(rep):
            return None, "CRC error: " + hexs(rep), ms
        rep = rep[:-2]
        if rep[0] != pdu[0]:
            return None, "reply from address %d" % rep[0], ms
        if rep[1] == pdu[1] | 0x80:
            return None, "exception %d (%s)" % (rep[2], EXC.get(rep[2], "?")), ms
        return rep, "", ms


def read_bits(bus, addr, fc, n):
    rep, err, ms = bus.xact(bytes([addr, fc, 0, 0, 0, n]))
    if rep is None:
        return None, err, ms
    v = int.from_bytes(rep[3:3 + rep[2]], "little")
    return v & ((1 << n) - 1), "", ms


def write_coils(bus, addr, n, bits):
    nb = (n + 7) // 8
    pdu = bytes([addr, 0x0F, 0, 0, 0, n, nb]) + (bits & ((1 << n) - 1)).to_bytes(nb, "little")
    rep, err, ms = bus.xact(pdu)
    return rep is not None, err, ms


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
            for addr in range(1, 17):
                v, err, ms = read_bits(bus, addr, 1, a.coils)
                if v is not None or err.startswith("exception"):
                    print("address %d answers (%s, %.1f ms)" % (addr, "coils %s" % format(v, "0%db" % a.coils)
                                                                 if v is not None else err, ms))
                    return 0
            print("nothing")
    print("no module found: power (24 V), A / B swapped, address outside 1..16, or another baud rate")
    return 1


def cmd_demo(bus, a, lc):
    steps = [("idle: green blinks", [["green"], []], 4), ("run: green", [["green"]], 3),
             ("paused: yellow", [["yellow"]], 3), ("e-stop: red + buzzer 1 s", [["red", "buzzer"]], 1),
             ("e-stop: red", [["red"]], 2), ("fault: red blinks", [["red"], []], 4)]
    for title, pats, secs in steps:
        print(title)
        end, i = time.time() + secs, 0
        while time.time() < end:
            ok, err, ms = write_coils(bus, a.addr, a.coils, lamps_to_bits(pats[i % len(pats)], lc))
            if not ok:
                print("  ", err)
            i += 1
            time.sleep(0.5)
    write_coils(bus, a.addr, a.coils, 0)
    return 0


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
    ap.add_argument("op", choices=["ports", "scan", "coils", "set", "lamp", "demo", "sniff", "slave"])
    ap.add_argument("args", nargs="*")
    ap.add_argument("--port", default="")
    ap.add_argument("--addr", type=int, default=1)
    ap.add_argument("--baud", type=int, default=9600)
    ap.add_argument("--parity", default="N", choices=["N", "E", "O"])
    ap.add_argument("--coils", type=int, default=4)
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
            rep, err, ms = bus.xact(bytes([a.addr, 5, 0, k, 0xFF if a.args[1] == "on" else 0, 0]))
            print("relay %d %s: %s, %.1f ms" % (k, a.args[1], "ok" if rep else err, ms))
            return 0 if rep else 1
        if a.op == "lamp":
            bad = [x for x in a.args if x not in LAMPS and x != "off"]
            if bad or not a.args:
                sys.exit("lamp <green|yellow|red|buzzer|off>...")
            ok, err, ms = write_coils(bus, a.addr, a.coils, lamps_to_bits(a.args, lc))
            print("lamps %s: %s, %.1f ms" % (" ".join(a.args), "ok" if ok else err, ms))
            return 0 if ok else 1
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
