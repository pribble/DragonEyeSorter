"""CAN back ends for the PLC simulator. Each has send(arb_id, data), recv(timeout_s) -> (arb_id, bytes) | None, close().

  virtual  : an in-process fake board that behaves like des_app run (state machine, ACKs, 10 Hz STATUS, 1 Hz
             COUNTS, random PART results while running) -- exercises the GUI with no hardware
  python-can : any python-can interface, e.g. --iface socketcan --channel can0 (Linux), --iface pcan ...
  zqwl     : ZQWL-UCAN-101E (智嵌物联) through its Windows SDK DLL -- ⚠ to be filled in when the SDK from the seller
             is at hand (DLL name, open / init / transmit / receive signatures); see open_bus()
"""
import queue
import random
import threading
import time

import can_proto as P


class VirtualBoard:
    """Fake DES: answers like ps/app/runner.cpp (same accept / reject rules)."""

    def __init__(self, n_classes=8):
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
        self.quit = False
        self.th = threading.Thread(target=self._loop, daemon=True)
        self.th.start()

    def send(self, arb_id, data):
        d = bytes(data)
        if arb_id == P.ID_CMD:
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
            v = (d[0] | d[1] << 8) / 10.0
            if 0 < v <= 200: self.speed = v
            self.rx.put(P.ack(arb_id, d[2], 0 if 0 < v <= 200 else 2))
        elif arb_id == P.ID_RECIPE:
            self.rx.put(P.ack(arb_id, d[1], 0))
        elif arb_id == P.ID_PLC_HB:
            self.last_plc_hb = time.time()
        elif arb_id == P.ID_TIME:
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
                cls = random.choices(range(self.n_classes), weights=[6, 1, 1, 1, 6, 1, 1, 1][: self.n_classes])[0]
                ok = cls in (0, 4)
                disp = 0 if ok else random.choice([1, 1, 2])
                size = (32.0 if cls < 4 else 50.0) + random.gauss(0, 0.05)
                self.rx.put(P.part(self.next_part, cls, disp, (cls >= 4) if disp == 0 else (1 if disp == 1 else 0),
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


def open_bus(kind, iface="socketcan", channel="can0"):
    if kind == "virtual":
        return VirtualBoard()
    if kind == "python-can":
        return PythonCanBus(iface, channel)
    if kind == "zqwl":
        raise SystemExit("zqwl back end: waiting for the ZQWL-UCAN-101E SDK (DLL + header from the seller). "
                         "Put it under pc/plc_sim/zqwl_sdk/ and implement ZqwlBus here (ctypes).")
    raise SystemExit(f"unknown back end {kind}")
