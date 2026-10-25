"""CAN back ends for the PLC simulator. Each has send(arb_id, data), recv(timeout_s) -> (arb_id, bytes) | None, close().

  virtual    : an in-process fake board that behaves like des_app run (state machine, ACKs, 10 Hz STATUS, 1 Hz
               COUNTS, random PART results while running) -- exercises the GUI with no hardware
  python-can : any python-can interface, e.g. --iface socketcan --channel can0 (Linux), --iface pcan ...
  zqwl       : 智嵌物联 ZQWL USB-CAN(FD) (our ZQWL-UCAN-101E): USB virtual COM port + the vendor's serial protocol,
               no DLL / driver (zqwl.py; --port COMn, default = found by USB VID 0x3562)
  controlcan : other USB-CAN adapters with the ZLG-compatible Windows library ControlCAN.dll (VCI_OpenDevice /
               VCI_InitCAN / VCI_Transmit / VCI_Receive): put the DLL next to this file (or --dll) and give the device
               type number from its manual (--dev-type; ZLG USBCAN-2A / CANalyst-II = 4, USBCAN-I = 3)
"""
import ctypes
import os
import queue
import random
import threading
import time

import can_proto as P


class VirtualBoard:
    """Fake DES: answers like ps/src/app/runner.cpp (same accept / reject rules)."""

    def __init__(self, n_classes=4):   # label_list.json (scripts/deploy/out/): gear ok / short / flash / dirty
        self.rx = queue.Queue()   # frames to the PLC
        self.state = 0
        self.speed = 50.0
        self.alarm = 0
        self.t0 = time.time()
        self.hb = 0
        self.counts = [0, 0, 0, 0]
        self.next_part = 1
        self.n_classes = n_classes
        self.last_plc_hb = 0.0
        self.clock_offset_ms = None
        self.recipes, self.recipe = (1, 2, 3), 1   # ids in ps/src/recipes.json
        self.quit = False
        self.th = threading.Thread(target=self._loop, daemon=True)
        self.th.start()

    def send(self, arb_id, data):
        d = bytes(data)
        if arb_id == P.ID_CMD:
            if len(d) < 2:
                return   # short frame: drop (the real board ACKs BAD_VALUE; keep the fake simple)
            c, seq = d[0], d[1]
            ok = 0
            if c == P.CMD_START:
                if self.state in (0, 2): self.state = 1
                elif self.state != 1: ok = 1
            elif c == P.CMD_STOP: self.state = 0
            elif c == P.CMD_PAUSE:
                if self.state == 1: self.state = 2
                elif self.state != 2: ok = 1
            elif c == P.CMD_RESUME:
                if self.state == 2: self.state = 1
                else: ok = 1
            elif c == P.CMD_RESET_ALARM:
                self.alarm = 0
                if self.state in (3, 4): self.state = 0
            else: ok = 3
            self.rx.put(P.ack(arb_id, seq, ok))
        elif arb_id == P.ID_SPEED:
            if len(d) < 3:
                return
            v = (d[0] | d[1] << 8) / 10.0
            if 0 < v <= 200: self.speed = v
            self.rx.put(P.ack(arb_id, d[2], 0 if 0 < v <= 200 else 2))
        elif arb_id == P.ID_RECIPE:   # same rules as Runner::select_recipe: known id, not while running
            if len(d) < 2:
                return
            r = d[0]
            res = 2 if r not in self.recipes else 1 if self.state == 1 else 0
            if res == 0:
                self.recipe = r
            self.rx.put(P.ack(arb_id, d[1], res))
        elif arb_id == P.ID_PLC_HB:
            self.last_plc_hb = time.time()
        elif arb_id == P.ID_TIME:
            if len(d) < 6:
                return
            self.clock_offset_ms = P.time_ms(d) - int(time.time() * 1000)

    def recv(self, timeout):
        try:
            return self.rx.get(timeout=timeout)
        except queue.Empty:
            return None

    def close(self):
        self.quit = True

    def _loop(self):
        last_status = last_counts = last_part = 0.0
        while not self.quit:
            now = time.time()
            if now - last_status >= 0.1:
                last_status = now
                self.hb += 1
                flags = (P.FLAG_BELT_MOVING if self.state == 1 else 0) | (
                    P.FLAG_PLC_LINK if now - self.last_plc_hb < 1.0 else 0) | (P.FLAG_ALARM if self.alarm else 0)
                self.rx.put(P.status(self.state, flags, self.speed if self.state == 1 else 0, self.alarm, self.hb,
                                     int(now - self.t0)))
            if self.state == 1 and now - last_part >= 50.0 / max(self.speed, 1):   # ~1 part per 50 mm of belt
                last_part = now
                cls = random.choices(range(self.n_classes), weights=([6] + [1] * (self.n_classes - 1)))[0]
                ok = cls == 0
                disp = 0 if ok else random.choice([1, 1, 2])
                size = 32.0 + random.gauss(0, 0.05)
                self.rx.put(P.part(self.next_part, cls, disp, 0 if disp == 0 else (1 if disp == 1 else 0),
                                   random.uniform(0.8, 0.99), size))
                self.next_part += 1
                self.counts[0] += 1
                self.counts[1 if disp == 0 else 2 if disp == 1 else 3] += 1
            if now - last_counts >= 1.0:
                last_counts = now
                self.rx.put(P.counts(*self.counts))
            time.sleep(0.01)


class PythonCanBus:
    def __init__(self, iface, channel, bitrate=P.BITRATE):
        import can   # pip install python-can
        self.can = can
        self.bus = can.Bus(interface=iface, channel=channel, bitrate=bitrate)

    def send(self, arb_id, data):
        self.bus.send(self.can.Message(arbitration_id=arb_id, data=bytes(data), is_extended_id=False))

    def recv(self, timeout):
        m = self.bus.recv(timeout)
        return None if m is None or m.is_extended_id else (m.arbitration_id, bytes(m.data))

    def close(self):
        self.bus.shutdown()


class _VciInitConfig(ctypes.Structure):
    _fields_ = [("AccCode", ctypes.c_uint32), ("AccMask", ctypes.c_uint32), ("Reserved", ctypes.c_uint32),
                ("Filter", ctypes.c_ubyte), ("Timing0", ctypes.c_ubyte), ("Timing1", ctypes.c_ubyte),
                ("Mode", ctypes.c_ubyte)]


class _VciCanObj(ctypes.Structure):
    _fields_ = [("ID", ctypes.c_uint32), ("TimeStamp", ctypes.c_uint32), ("TimeFlag", ctypes.c_ubyte),
                ("SendType", ctypes.c_ubyte), ("RemoteFlag", ctypes.c_ubyte), ("ExternFlag", ctypes.c_ubyte),
                ("DataLen", ctypes.c_ubyte), ("Data", ctypes.c_ubyte * 8), ("Reserved", ctypes.c_ubyte * 3)]


# SJA1000 bit timing (BTR0, BTR1) as in the ZLG manual (16 MHz controller clock)
VCI_TIMING = {1000000: (0x00, 0x14), 500000: (0x00, 0x1C), 250000: (0x01, 0x1C), 125000: (0x03, 0x1C)}


class ControlCanBus:
    """ZLG-compatible ControlCAN.dll (stdcall). Python and the DLL must have the same bitness (x86 / x64)."""

    def __init__(self, dll=None, dev_type=4, dev_index=0, can_index=0, bitrate=P.BITRATE):
        here = os.path.dirname(os.path.abspath(__file__))
        path = dll or os.path.join(here, "ControlCAN.dll")
        if not os.path.exists(path):
            raise SystemExit(f"{path} not found: copy the USB-CAN vendor's ControlCAN.dll here or pass --dll")
        loader = ctypes.WinDLL if os.name == "nt" else ctypes.CDLL
        try:
            self.lib = loader(path)
        except OSError as e:
            raise SystemExit(f"cannot load {path}: {e} (32/64-bit mismatch with this Python?)")
        if bitrate not in VCI_TIMING:
            raise SystemExit(f"bitrate {bitrate} not in {sorted(VCI_TIMING)}")
        self.t, self.d, self.c = dev_type, dev_index, can_index
        if self.lib.VCI_OpenDevice(self.t, self.d, 0) != 1:
            raise SystemExit(f"VCI_OpenDevice(type {self.t}, index {self.d}) failed: plugged in, driver installed, "
                             "vendor tool closed, right --dev-type?")
        t0, t1 = VCI_TIMING[bitrate]
        cfg = _VciInitConfig(0, 0xFFFFFFFF, 0, 1, t0, t1, 0)   # accept everything, normal mode
        if self.lib.VCI_InitCAN(self.t, self.d, self.c, ctypes.byref(cfg)) != 1:
            self.lib.VCI_CloseDevice(self.t, self.d)
            raise SystemExit("VCI_InitCAN failed")
        self.lib.VCI_ClearBuffer(self.t, self.d, self.c)
        if self.lib.VCI_StartCAN(self.t, self.d, self.c) != 1:
            self.lib.VCI_CloseDevice(self.t, self.d)
            raise SystemExit("VCI_StartCAN failed")
        self.buf = (_VciCanObj * 64)()
        self.pending = []

    def send(self, arb_id, data):
        d = bytes(data)
        o = _VciCanObj()
        o.ID, o.SendType, o.RemoteFlag, o.ExternFlag, o.DataLen = arb_id, 0, 0, 0, len(d)
        for i, b in enumerate(d):
            o.Data[i] = b
        if self.lib.VCI_Transmit(self.t, self.d, self.c, ctypes.byref(o), 1) != 1:
            raise IOError("VCI_Transmit failed (bus off / no other node acknowledging?)")

    def recv(self, timeout):
        end = time.time() + timeout
        while not self.pending:
            n = self.lib.VCI_Receive(self.t, self.d, self.c, self.buf, len(self.buf), 0)
            if n == 0xFFFFFFFF or n < 0:   # some DLLs return -1 on error
                n = 0
            for k in range(n):
                o = self.buf[k]
                if not o.ExternFlag and not o.RemoteFlag:
                    self.pending.append((o.ID, bytes(o.Data[: o.DataLen])))
            if self.pending or time.time() >= end:
                break
            time.sleep(0.002)
        return self.pending.pop(0) if self.pending else None

    def close(self):
        self.lib.VCI_CloseDevice(self.t, self.d)


def open_bus(kind, iface="socketcan", channel="can0", dll=None, dev_type=4, dev_index=0, can_index=0, port=None):
    if kind == "virtual":
        return VirtualBoard()
    if kind == "python-can":
        return PythonCanBus(iface, channel)
    if kind == "zqwl":
        from zqwl import ZqwlSerialBus   # pip install pyserial
        return ZqwlSerialBus(port, P.BITRATE, can_index)
    if kind == "controlcan":
        return ControlCanBus(dll, dev_type, dev_index, can_index)
    raise SystemExit(f"unknown back end {kind}")
