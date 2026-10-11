"""DragonEyeSorter <-> "PLC" CAN protocol v1.1 — Python side of ps/bus/can_proto.h (keep the two in step).

Classic CAN 2.0A, 500 kbit/s, little-endian multi-byte fields.
  PLC -> DES: 0x100 CMD [cmd, seq] / 0x101 SPEED [speed_x10 u16, seq] / 0x102 RECIPE [recipe, seq]
              0x103 PLC_HB [counter] (>= 2 Hz) / 0x104 TIME [unix_s u32, ms u16] (no ACK)
  DES -> PLC: 0x180 ACK [id_lo, seq, result] / 0x181 STATUS [state, flags, speed_x10 u16, alarm, hb, uptime_s u16]
              0x182 COUNTS [total, ok, ng, recheck u16] / 0x183 PART [id u16, class, disp, dest, conf_pct, size_x100 u16]
              0x184 ALARM [code, severity, detail u16]
"""
import struct
import time

BITRATE = 500000

ID_CMD, ID_SPEED, ID_RECIPE, ID_PLC_HB, ID_TIME = 0x100, 0x101, 0x102, 0x103, 0x104
ID_ACK, ID_STATUS, ID_COUNTS, ID_PART, ID_ALARM = 0x180, 0x181, 0x182, 0x183, 0x184

CMD_START, CMD_STOP, CMD_PAUSE, CMD_RESUME, CMD_RESET_ALARM = 1, 2, 3, 4, 5
CMD_NAMES = {CMD_START: "start", CMD_STOP: "stop", CMD_PAUSE: "pause", CMD_RESUME: "resume",
             CMD_RESET_ALARM: "reset alarm"}
ACK_NAMES = {0: "OK", 1: "rejected", 2: "bad value", 3: "unknown"}
STATE_NAMES = {0: "IDLE", 1: "RUN", 2: "PAUSED", 3: "ESTOP", 4: "FAULT"}
FLAG_BELT_MOVING, FLAG_ARM_BUSY, FLAG_ALARM, FLAG_PLC_LINK, FLAG_CALIBRATED = 0x01, 0x02, 0x04, 0x08, 0x10
DISP_NAMES = {0: "pick", 1: "flap", 2: "recheck"}


# ---- PLC -> DES (arbitration id, payload bytes) ----
def cmd(c, seq):
    return ID_CMD, bytes([c & 0xFF, seq & 0xFF])


def speed(mm_s, seq):
    v = max(0, min(65535, int(mm_s * 10 + 0.5)))
    return ID_SPEED, struct.pack("<HB", v, seq & 0xFF)


def recipe(r, seq):
    return ID_RECIPE, bytes([r & 0xFF, seq & 0xFF])


def plc_hb(counter):
    return ID_PLC_HB, bytes([counter & 0xFF])


def time_frame(now=None):
    t = time.time() if now is None else now
    s = int(t)
    return ID_TIME, struct.pack("<IH", s & 0xFFFFFFFF, int((t - s) * 1000) % 1000)


# ---- DES -> PLC ----
def decode(arb_id, data):
    """-> (kind, dict) or (None, None) for frames that are not ours"""
    d = bytes(data)
    if arb_id == ID_ACK and len(d) >= 3:
        return "ack", {"cmd_id": 0x100 | d[0], "seq": d[1], "result": d[2], "result_name": ACK_NAMES.get(d[2], "?")}
    if arb_id == ID_STATUS and len(d) >= 8:
        st, flags, sp, alarm, hb, up = struct.unpack("<BBHBBH", d[:8])
        return "status", {"state": st, "state_name": STATE_NAMES.get(st, "?"), "flags": flags, "speed": sp / 10.0,
                          "alarm": alarm, "hb": hb, "uptime_s": up}
    if arb_id == ID_COUNTS and len(d) >= 8:
        total, ok, ng, rc = struct.unpack("<HHHH", d[:8])
        return "counts", {"total": total, "ok": ok, "ng": ng, "recheck": rc}
    if arb_id == ID_PART and len(d) >= 8:
        pid, cls, disp, dest, conf, size = struct.unpack("<HBBBBH", d[:8])
        return "part", {"id": pid, "cls": cls, "disp": disp, "disp_name": DISP_NAMES.get(disp, "?"), "dest": dest,
                        "conf": conf / 100.0, "size_mm": size / 100.0}
    if arb_id == ID_ALARM and len(d) >= 4:
        code, sev, detail = struct.unpack("<BBH", d[:4])
        return "alarm", {"code": code, "severity": sev, "detail": detail}
    return None, None


# ---- the board side, for the fake board / tests ----
def ack(cmd_id, seq, result):
    return ID_ACK, bytes([cmd_id & 0xFF, seq & 0xFF, result & 0xFF])


def status(state, flags, speed_mm_s, alarm, hb, uptime_s):
    return ID_STATUS, struct.pack("<BBHBBH", state, flags, max(0, min(65535, int(speed_mm_s * 10 + 0.5))), alarm,
                                  hb & 0xFF, uptime_s & 0xFFFF)


def counts(total, ok, ng, rc):
    return ID_COUNTS, struct.pack("<HHHH", total, ok, ng, rc)


def part(pid, cls, disp, dest, conf, size_mm):
    return ID_PART, struct.pack("<HBBBBH", pid & 0xFFFF, cls, disp, dest, int(conf * 100),
                                max(0, min(65535, int(size_mm * 100 + 0.5))))


def time_ms(data):
    s, ms = struct.unpack("<IH", bytes(data[:6]))
    return s * 1000 + ms


if __name__ == "__main__":   # self-test against the byte layouts of can_proto.h
    assert speed(80, 7) == (0x101, bytes([0x20, 0x03, 7]))            # 800 = 0x0320
    assert decode(*status(1, 0x09, 80.0, 0, 5, 300))[1]["speed"] == 80.0
    k, p = decode(*part(513, 2, 1, 2, 0.87, 32.01))
    assert k == "part" and p["id"] == 513 and p["size_mm"] == 32.01 and p["disp_name"] == "flap"
    assert decode(*ack(0x101, 7, 2))[1] == {"cmd_id": 0x101, "seq": 7, "result": 2, "result_name": "bad value"}
    assert time_ms(time_frame(1760000000.25)[1]) == 1760000000250
    print("can_proto self-test ok")
