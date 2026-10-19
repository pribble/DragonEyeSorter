#!/usr/bin/env python3
"""ZQWL USB-CAN(FD) adapters (智嵌物联 ZQWL-UCAN-101E / UCANFD-100E / ...): USB CDC virtual COM port + the vendor's
binary protocol ("ZQWL-USBCANFD 二次开发通讯协议 V1.05"). No DLL and no driver: Windows binds its own usbser.sys
(Device Manager: "USB 串行设备 (COMn)", VID 0x3562). The serial baud rate is ignored by the adapter.

Host <-> adapter byte streams (all fixed layouts, no checksum):
  config   22 B  49 3B | func | 52 read / 57 write | D0..D15 | 45 2E      (the adapter answers with the same layout)
           func 40 device info (read) / 41 serial no. (read) / 42 CAN params / 43 filters / 44 system control
           42: D0 channel, D1 0 = preset rate / 1 = custom, D2 preset code (high nibble arbitration, low nibble FD
               data rate), D3..D7 / D8..D12 custom SJW, TSEG1, TSEG2, BRP(u16 big-endian) arbitration / data,
               D13 filters on.  Custom: baud = 60 MHz / ((SJW+1) + (TSEG1+1) + (TSEG2+1)) / BRP
           44: D0 1 = save to flash, D1 1 = reset, D2..D5 CAN0..CAN3 on.  CAN params apply only after a 44 write.
  frame    5A | ch.b0<<7 | DLC | type<<6 | ch.b2..1<<3 | ext<<2 | rtr<<1 | brs | ID u32 big-endian (b31 = CAN FD) |
           data (DLC bytes) | A5            (same both ways; the adapter answers every host frame with a heartbeat)
  heartbeat (adapter -> host, 1 Hz and after each host frame; byte 1 = FF, a 4-channel device uses FE / 32 B)
           5A FF | CAN0 tx/s u16 | CAN0 rx/s u16 | CAN1 tx/s u16 | CAN1 rx/s u16 | status | 0 |
           CAN0 TEC | CAN0 REC | CAN1 TEC | CAN1 REC | A5
           status: b5 CAN0 on, b4 CAN1 on, b3..2 CAN0 bus, b1..0 CAN1 bus (0 ok, 1 warning, 2 error passive, 3 off)
           ⚠ UCAN-101E fw V2.7, 2026-10-19: one frame sent with no other node on the bus -> error passive with the
           byte labelled "CAN0 REC" = 135 and "TEC" = 0, although an un-ACKed sender raises its TEC -> the two
           counters may be swapped in the manual; check in the two-node test. Reopening the channel clears them.

    python zqwl.py                       # find the adapter, read model / firmware / serial / CAN params (no change)
    python zqwl.py listen [-s 10]        # open CAN0 at 500 kbit/s, print received frames + heartbeats (sends nothing)
    python zqwl.py send 123#11223344     # open CAN0, send one classic frame, print what comes back for 1 s
    python zqwl.py selftest              # offline check of the frame encoder / parser
Opening a channel makes the adapter a bus node: it ACKs every frame on the bus. Close the vendor tool first (the COM
port is exclusive). Settings are not saved to the adapter's flash unless --save.
"""
import argparse
import queue
import struct
import sys
import threading
import time

VID = 0x3562
MODELS = {0x0100: "2 通道 CANFD", 0x0101: "1 通道 CANFD", 0x0102: "2 通道 CAN", 0x0103: "1 通道 CAN",
          0x0104: "4 通道 CAN", 0x0105: "4 通道 CANFD"}
ARB_CODE = {1000000: 0x0, 800000: 0x1, 500000: 0x2, 400000: 0x3, 250000: 0x4, 200000: 0x5, 125000: 0x6, 100000: 0x7,
            50000: 0x8, 40000: 0x9, 25000: 0xA, 20000: 0xB, 15000: 0xC, 10000: 0xD, 5000: 0xE}
DATA_CODE = {5000000: 0x0, 4000000: 0x1, 2000000: 0x2, 1000000: 0x3, 800000: 0x4, 500000: 0x5, 400000: 0x6,
             250000: 0x7, 200000: 0x8, 125000: 0x9, 100000: 0xA}
F_INFO, F_SERIAL, F_CAN, F_FILTER, F_SYS = 0x40, 0x41, 0x42, 0x43, 0x44
RD, WR = 0x52, 0x57
BUS_STATE = {0: "正常", 1: "警告", 2: "被动错误", 3: "离线"}


def cfg_frame(func, rw, data=b""):
    d = bytes(data)[:16]
    return b"\x49\x3B" + bytes([func, rw]) + d + bytes(16 - len(d)) + b"\x45\x2E"


def data_frame(arb_id, data, ch=0, ext=False, rtr=False, fd=False, brs=False, single=False):
    d = bytes(data)
    if len(d) > (64 if fd else 8):
        raise ValueError("too much data")
    b1 = (ch & 1) << 7 | len(d)
    b2 = (1 if single else 0) << 6 | ((ch >> 1) & 3) << 3 | ext << 2 | rtr << 1 | brs
    i = (arb_id & (0x1FFFFFFF if ext else 0x7FF)) | (0x80000000 if fd else 0)
    return bytes([0x5A, b1, b2]) + struct.pack(">I", i) + d + b"\xA5"


def parse_heartbeat(b):
    """b = 17-byte 1/2-channel heartbeat -> dict"""
    tx0, rx0, tx1, rx1 = struct.unpack(">4H", b[2:10])
    st = b[10]
    return {"tx0": tx0, "rx0": rx0, "tx1": tx1, "rx1": rx1, "can0_on": bool(st & 0x20), "can1_on": bool(st & 0x10),
            "bus0": (st >> 2) & 3, "bus1": st & 3, "tec0": b[12], "rec0": b[13], "tec1": b[14], "rec1": b[15],
            "t": time.time()}


class Parser:
    """byte stream -> events ('cfg', func, rw, D0..D15) / ('hb', dict) / ('hb4', raw) / ('frame', dict)"""

    def __init__(self):
        self.buf = bytearray()
        self.dropped = 0

    def feed(self, data):
        self.buf += data
        out = []
        b = self.buf
        while b:
            if b[0] == 0x49:
                if len(b) < 22:
                    break
                if b[1] == 0x3B and b[20] == 0x45 and b[21] == 0x2E:
                    out.append(("cfg", b[2], b[3], bytes(b[4:20])))
                    del b[:22]
                    continue
            elif b[0] == 0x5A:
                if len(b) < 2:
                    break
                if b[1] == 0xFF or b[1] == 0xFE:
                    n = 17 if b[1] == 0xFF else 32
                    if len(b) < n:
                        break
                    if b[n - 1] == 0xA5:
                        out.append(("hb", parse_heartbeat(b[:n])) if n == 17 else ("hb4", bytes(b[:n])))
                        del b[:n]
                        continue
                else:
                    dlc = b[1] & 0x7F
                    n = 7 + dlc + 1
                    if len(b) < n:
                        break
                    if dlc <= 64 and b[n - 1] == 0xA5:
                        i = struct.unpack(">I", b[3:7])[0]
                        ext = bool(b[2] & 0x04)
                        out.append(("frame", {"ch": (b[1] >> 7) | ((b[2] >> 3) & 3) << 1,
                                              "id": i & (0x1FFFFFFF if ext else 0x7FF), "ext": ext,
                                              "rtr": bool(b[2] & 0x02), "brs": bool(b[2] & 0x01),
                                              "fd": bool(i & 0x80000000), "data": bytes(b[7:7 + dlc]),
                                              "t": time.time()}))
                        del b[:n]
                        continue
            del b[:1]   # not a frame start / bad tail: resync on the next byte
            self.dropped += 1
        return out


def find_port():
    from serial.tools import list_ports
    hits = [p for p in list_ports.comports() if p.vid == VID]
    if not hits:
        raise SystemExit("找不到 ZQWL USB-CAN（VID 0x3562）：插好了吗？设备管理器里应是「USB 串行设备 (COMn)」")
    return hits[0].device, hits[0].pid


class ZqwlSerialBus:
    """plc_sim back end: send(arb_id, data) / recv(timeout) -> (arb_id, bytes) | None / close(); classic CAN 2.0A"""

    def __init__(self, port=None, bitrate=500000, channel=0, save=False, verbose=False):
        import serial   # pip install pyserial
        pid = None
        if port is None:
            port, pid = find_port()
        if bitrate not in ARB_CODE:
            raise SystemExit(f"bitrate {bitrate} 不在预置表 {sorted(ARB_CODE)}")
        try:
            self.ser = serial.Serial(port, 2000000, timeout=0.05)   # CDC: the baud rate is not used
        except serial.SerialException as e:
            raise SystemExit(f"打不开 {port}：{e}（厂家调试软件 / 串口助手占着？先关掉）")
        self.port, self.pid, self.ch, self.verbose = port, pid, channel, verbose
        self.parser = Parser()
        self.frames = queue.Queue()
        self.cfg = queue.Queue()
        self.hb = None
        self.n_hb = self.n_rx = self.n_tx = 0
        self.quit = False
        self.ser.reset_input_buffer()
        self.th = threading.Thread(target=self._rx_loop, daemon=True)
        self.th.start()
        self.opened = False
        # preset rate: high nibble arbitration, low nibble FD data rate (unused for classic CAN; keep 2 Mbit/s)
        self.request(F_CAN, WR, [channel, 0, ARB_CODE[bitrate] << 4 | DATA_CODE[2000000]])
        on = [0, 0, 0, 0]
        on[channel] = 1
        self.request(F_SYS, WR, [1 if save else 0, 0] + on)
        self.opened = True
        end = time.time() + 2.5
        while time.time() < end:
            h = self.hb
            if h and h["can1_on" if channel == 1 else "can0_on"]:
                break
            time.sleep(0.05)
        else:
            raise SystemExit(f"{port}：通道 CAN{channel} 打开后 2.5 s 内没收到「已开启」心跳（拔插 USB 再试）")

    def request(self, func, rw, data=b"", timeout=1.0):
        """send a config frame, return the adapter's answer D0..D15"""
        while not self.cfg.empty():
            self.cfg.get_nowait()
        self.ser.write(cfg_frame(func, rw, data))
        end = time.time() + timeout
        while True:
            try:
                f, r, d = self.cfg.get(timeout=max(0.0, end - time.time()))
            except queue.Empty:
                raise SystemExit(f"{self.port}：配置命令 0x{func:02X} 没有应答（厂家软件占着 / 不是 ZQWL 设备？拔插 USB 再试）")
            if f == func:
                return d

    def _rx_loop(self):
        while not self.quit:
            try:
                data = self.ser.read(4096)
            except Exception:   # port closed / unplugged
                break
            if not data:
                continue
            for ev in self.parser.feed(data):
                if ev[0] == "cfg":
                    self.cfg.put(ev[1:])
                elif ev[0] == "hb":
                    self.hb = ev[1]
                    self.n_hb += 1
                    if self.verbose:
                        print("  心跳", status_text(ev[1]))
                elif ev[0] == "frame":
                    self.n_rx += 1
                    self.frames.put(ev[1])

    def send(self, arb_id, data):
        self.ser.write(data_frame(arb_id, data, self.ch))
        self.n_tx += 1

    def recv(self, timeout):
        end = time.time() + timeout
        while True:
            try:
                f = self.frames.get(timeout=max(0.0, end - time.time()))
            except queue.Empty:
                return None
            if f["ch"] == self.ch and not f["ext"] and not f["rtr"] and not f["fd"]:
                return f["id"], f["data"]

    def status(self):
        """one-line adapter state for the GUI (bus state, error counters)"""
        return status_text(self.hb) if self.hb else "无心跳"

    def close(self):
        try:
            if self.opened:   # close the channel so the adapter stops acknowledging frames on the bus
                self.ser.write(cfg_frame(F_SYS, WR, [0, 0, 0, 0, 0, 0]))
                time.sleep(0.1)
        finally:
            self.quit = True
            self.ser.close()


def status_text(h):
    return (f"CAN0 {'开' if h['can0_on'] else '关'} {BUS_STATE[h['bus0']]} TEC {h['tec0']} REC {h['rec0']} "
            f"收 {h['rx0']}/s 发 {h['tx0']}/s")


def cmd_info(port):
    import serial
    pid = None
    if port is None:
        port, pid = find_port()
    print(f"端口 {port}" + (f"  PID 0x{pid:04X} = {MODELS.get(pid, '?')}" if pid is not None else ""))
    s = serial.Serial(port, 2000000, timeout=0.05)
    p = Parser()
    s.reset_input_buffer()
    s.write(b"\x00" * 22)   # flush a half-received frame in the adapter's parser (left by other software)
    time.sleep(0.2)
    s.reset_input_buffer()

    def ask(func, data=b""):
        s.write(cfg_frame(func, RD, data))
        end = time.time() + 1.0
        while time.time() < end:
            for ev in p.feed(s.read(256)):
                if ev[0] == "cfg" and ev[1] == func:
                    return ev[3]
        return None

    info, sn, can0 = ask(F_INFO), ask(F_SERIAL), ask(F_CAN, [0])
    s.close()
    if info is None:
        raise SystemExit("没有应答：厂家软件占着端口？拔插 USB 再试")
    print("设备信息 D0..D15:", info.hex(" "), " ASCII:", info.rstrip(b"\0").decode("ascii", "replace"))
    print("序列号:", sn.hex(" ") if sn else "无应答")
    if can0:
        code = can0[2]
        arb = {v: k for k, v in ARB_CODE.items()}.get(code >> 4)
        dat = {v: k for k, v in DATA_CODE.items()}.get(code & 0xF)
        print(f"CAN0 参数: {can0.hex(' ')}  ->  {'自定义' if can0[1] else '预置'} 仲裁 {arb} 数据域 {dat} "
              f"滤波 {'开' if can0[13] else '关'}")


def selftest():
    """offline: encode -> parse round trips, the manual's heartbeat example, split reads, garbage resync"""
    p = Parser()
    hb = bytes.fromhex("5A FF 00 00 00 00 00 00 00 00 20 00 00 00 00 00 A5")   # manual p.10, CAN0 just opened
    f1 = data_frame(0x181, bytes([1, 9, 0xF4, 1, 0, 7, 3, 0]))
    f2 = data_frame(0x12345678, bytes([0x5A, 0xA5, 0x49]), ext=True)   # payload bytes that look like frame marks
    f3 = data_frame(0x100, b"", ch=1)
    cfg = cfg_frame(F_SYS, WR, [0, 0, 1])
    junk, broken = bytes([0x00, 0x13]), bytes([0x5A, 0x05, 0x00])   # noise, a frame start cut short
    stream = junk + hb + f1 + cfg + f2 + broken + f3 + hb   # the cut frame holds f3 until more bytes arrive
    ev = []
    for k in range(0, len(stream), 5):   # 5-byte reads: frames split across reads
        ev += p.feed(stream[k:k + 5])
    kinds = [e[0] for e in ev]
    assert kinds == ["hb", "frame", "cfg", "frame", "frame", "hb"], kinds
    assert ev[0][1]["can0_on"] and ev[0][1]["bus0"] == 0 and not ev[0][1]["can1_on"]
    a = ev[1][1]
    assert (a["id"], a["data"], a["ext"], a["fd"], a["ch"]) == (0x181, bytes([1, 9, 0xF4, 1, 0, 7, 3, 0]), False,
                                                                False, 0)
    assert ev[2][1:3] == (F_SYS, WR) and ev[2][3][2] == 1
    b = ev[3][1]
    assert (b["id"], b["data"], b["ext"]) == (0x12345678, bytes([0x5A, 0xA5, 0x49]), True)
    assert ev[4][1]["ch"] == 1 and ev[4][1]["data"] == b""
    assert f1.hex(" ") == "5a 08 00 00 00 01 81 01 09 f4 01 00 07 03 00 a5"
    assert cfg_frame(F_CAN, WR, [0, 0, 0x45]).hex(" ") == (   # manual p.9 example: 250k / FD 500k
        "49 3b 42 57 00 00 45 00 00 00 00 00 00 00 00 00 00 00 00 00 45 2e")
    print("zqwl selftest OK,", len(ev), "events, dropped", p.dropped, "bytes")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("action", nargs="?", default="info", choices=["info", "listen", "send", "selftest"])
    ap.add_argument("frame", nargs="?", help="send: ID#hex, e.g. 123#11223344")
    ap.add_argument("--port", help="COM 口（默认按 VID 0x3562 自动找）")
    ap.add_argument("--bitrate", type=int, default=500000)
    ap.add_argument("-s", "--seconds", type=float, default=10)
    ap.add_argument("--save", action="store_true", help="把 CAN 参数存进适配器 flash（默认不存）")
    a = ap.parse_args()
    if a.action == "info":
        cmd_info(a.port)
        return
    if a.action == "selftest":
        selftest()
        return
    bus = ZqwlSerialBus(a.port, a.bitrate, 0, a.save, verbose=True)
    print(f"{bus.port} CAN0 已开 {a.bitrate} bit/s：", bus.status())
    try:
        if a.action == "send":
            if not a.frame or "#" not in a.frame:
                raise SystemExit("send 要 ID#hex，例如 123#11223344")
            i, d = a.frame.split("#", 1)
            bus.send(int(i, 16), bytes.fromhex(d))
            print(f"已发 {int(i, 16):03X} [{len(bytes.fromhex(d))}] {d}")
            a.seconds = min(a.seconds, 1.0)
        end = time.time() + a.seconds
        while time.time() < end:
            r = bus.recv(0.2)
            if r:
                print(f"  收 {r[0]:03X} [{len(r[1])}] {r[1].hex(' ')}")
    finally:
        print("统计：收", bus.n_rx, "帧，发", bus.n_tx, "帧，心跳", bus.n_hb, "，丢弃字节", bus.parser.dropped)
        bus.close()


if __name__ == "__main__":
    sys.exit(main())
