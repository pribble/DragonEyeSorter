#!/usr/bin/env python3
"""mb_client.py — Modbus-TCP client for DragonEyeSorter (plays the line PLC / SCADA over Ethernet).

Register map: ps/src/hw/modbus_tcp.hpp (docs/软件与UI.md「Modbus-TCP」). Only the standard library: the same
requests any PLC (Siemens MB_CLIENT, Mitsubishi / Omron Modbus-TCP, KingView / WinCC ...) would send.

  python mb_client.py status                    one read of all input + holding registers, decoded
  python mb_client.py watch [--seconds N]       poll every 0.2 s; print state changes and each new part
  python mb_client.py cmd start|stop|pause|resume|reset
  python mb_client.py speed 45.5                belt speed set point, mm/s
  python mb_client.py recipe 2
  python mb_client.py time                      send this PC's clock (the board has no RTC)
  python mb_client.py lamp red buzzer           tower light on the RS485 relay module (Ethernet -> RS485), any of
                                                green / yellow / red / buzzer / off; "lamp auto" = back to the line state
  python mb_client.py bench [-n 1000]           round-trip time of a full status read
Options: --host <板子IP> --port 502   （--host 缺省取环境变量 BOARD_IP，仓库根 .env 有）
"""
import argparse
import os
import socket
import struct
import sys
import time

# ---- register map (keep in step with ps/src/hw/modbus_tcp.hpp) ----
HR_CMD, HR_SPEED, HR_RECIPE, HR_RESULT, HR_SEQ, HR_TIME_HI, HR_TIME_LO, HR_LAMP, N_HR = 0, 1, 2, 3, 4, 5, 6, 7, 8
IR_STATE, IR_FLAGS, IR_TOTAL, IR_OK, IR_NG, IR_RECHECK = 0, 1, 2, 4, 6, 8
IR_PART_SEQ, IR_PART_ID, IR_PART_CLS, IR_PART_DISP, IR_PART_DEST, IR_PART_SIZE, IR_PART_CONF = range(10, 17)
IR_SPEED, IR_HB, IR_UPTIME, IR_ALARM, IR_RECIPE, IR_VERSION = 17, 18, 19, 20, 21, 22
IR_485_FLAGS, IR_485_LAMPS, IR_485_INPUTS, IR_485_OK, IR_485_ERR, IR_485_RESP = range(24, 30)   # map v1.1 (0x0101)
N_IR, N_IR_V10 = 32, 24
LAMPS = {"green": 1, "yellow": 2, "red": 4, "buzzer": 8, "off": 0}

CMDS = {"start": 1, "stop": 2, "pause": 3, "resume": 4, "reset": 5}
ACK = {0: "OK", 1: "REJECTED（当前状态不允许）", 2: "BAD_VALUE（取值不对）", 3: "UNKNOWN（未知命令）"}
STATES = {0: "IDLE 停止", 1: "RUN 运行", 2: "PAUSED 暂停", 3: "ESTOP 急停", 4: "FAULT 故障"}
FLAGS = [(0x01, "皮带在动"), (0x02, "机械臂忙"), (0x04, "报警"), (0x08, "PLC 在线"), (0x10, "已标定"), (0x20, "RS485 在线")]
DISP = {0: "抓取", 1: "挡板", 2: "复检"}


class ModbusError(Exception):
    pass


class Client:
    def __init__(self, host, port, timeout=2.0, unit=1):
        self.s = socket.create_connection((host, port), timeout=timeout)
        self.s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        self.tid = 0
        self.unit = unit

    def close(self):
        self.s.close()

    def _recv(self, n):
        b = b""
        while len(b) < n:
            c = self.s.recv(n - len(b))
            if not c:
                raise ModbusError("connection closed by the board")
            b += c
        return b

    def request(self, pdu):
        self.tid = (self.tid + 1) & 0xFFFF
        self.s.sendall(struct.pack(">HHHB", self.tid, 0, len(pdu) + 1, self.unit) + pdu)
        tid, proto, n, _unit = struct.unpack(">HHHB", self._recv(7))
        if tid != self.tid or proto != 0:
            raise ModbusError("bad reply header")
        if n < 3:   # MBAP length covers unit id + function code + at least one data byte
            raise ModbusError("short reply (MBAP length %d)" % n)
        body = self._recv(n - 1)
        if body[0] & 0x80:
            raise ModbusError("exception %d (function %d)" % (body[1], body[0] & 0x7F))
        return body

    def read(self, fc, addr, n):
        body = self.request(struct.pack(">BHH", fc, addr, n))
        return list(struct.unpack(">%dH" % n, body[2:2 + 2 * n]))

    def read_input(self, addr, n):
        return self.read(4, addr, n)

    def read_holding(self, addr, n):
        return self.read(3, addr, n)

    def write(self, addr, value):
        self.request(struct.pack(">BHH", 6, addr, value & 0xFFFF))

    def write_many(self, addr, values):
        self.request(struct.pack(">BHHB", 16, addr, len(values), 2 * len(values)) +
                     struct.pack(">%dH" % len(values), *values))

    def write_checked(self, addr, values, timeout=1.0):
        """write, wait until the board has executed it (SEQ changes), return the CANP_ACK_* result"""
        seq0 = self.read_holding(HR_SEQ, 1)[0]
        if len(values) == 1:
            self.write(addr, values[0])
        else:
            self.write_many(addr, values)
        end = time.time() + timeout
        while time.time() < end:
            res, seq = self.read_holding(HR_RESULT, 2)
            if seq != seq0:
                return res
            time.sleep(0.01)
        raise ModbusError("the board did not execute the write within %.1f s (des_app run not running?)" % timeout)


def read_status(cl):
    """all input registers; a board with register map v1.0 (24 registers) is padded with zeros"""
    try:
        return cl.read_input(0, N_IR)
    except ModbusError:
        return cl.read_input(0, N_IR_V10) + [0] * (N_IR - N_IR_V10)


def lamp_text(bits):
    return "+".join(n for n, b in LAMPS.items() if b and bits & b) or "灭"


def u32(r, i):
    return (r[i] << 16) | r[i + 1]


def decode(ir, hr):
    flags = ", ".join(n for b, n in FLAGS if ir[IR_FLAGS] & b) or "-"
    cls = "-" if ir[IR_PART_CLS] == 0xFFFF else str(ir[IR_PART_CLS])
    lines = [
        "状态 %s   标志 %s   报警 %d   配方 %d   映射版本 %04X" % (
            STATES.get(ir[IR_STATE], ir[IR_STATE]), flags, ir[IR_ALARM], ir[IR_RECIPE], ir[IR_VERSION]),
        "计数 总 %d  OK %d  NG %d  复检 %d" % (u32(ir, IR_TOTAL), u32(ir, IR_OK), u32(ir, IR_NG), u32(ir, IR_RECHECK)),
        "带速 实际 %.1f mm/s  设定 %.1f mm/s   心跳 %d   运行 %d s" % (
            ir[IR_SPEED] / 10, hr[HR_SPEED] / 10, ir[IR_HB], ir[IR_UPTIME]),
        "最近零件 #%d（序号 %d）类别 %s  处置 %s  去向 %d  尺寸 %.2f mm  置信 %d%%" % (
            ir[IR_PART_ID], ir[IR_PART_SEQ], cls, DISP.get(ir[IR_PART_DISP], ir[IR_PART_DISP]), ir[IR_PART_DEST],
            ir[IR_PART_SIZE] / 100, ir[IR_PART_CONF]),
        "上次写入结果 %s（写入序号 %d）" % (ACK.get(hr[HR_RESULT], hr[HR_RESULT]), hr[HR_SEQ]),
    ]
    f485 = ir[IR_485_FLAGS]
    if f485 & 1:   # RS485 worker running (des_app.json rs485.enabled, bitstream >= v16)
        lines.append("RS485 %s%s  灯（读回）%s  成功 %d  失败 %d  应答 %.2f ms" % (
            "在线" if f485 & 2 else "无应答", "  PLC 接管灯" if f485 & 4 else "",
            lamp_text(ir[IR_485_LAMPS]), ir[IR_485_OK], ir[IR_485_ERR], ir[IR_485_RESP] / 100))
    else:
        lines.append("RS485 没在跑（des_app.json rs485.enabled 关着，或比特流 < v16）")
    return "\n".join(lines)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--host", default=os.environ.get("BOARD_IP"))
    ap.add_argument("--port", type=int, default=502)
    sub = ap.add_subparsers(dest="op", required=True)
    sub.add_parser("status")
    w = sub.add_parser("watch")
    w.add_argument("--seconds", type=float, default=0)
    c = sub.add_parser("cmd")
    c.add_argument("name", choices=sorted(CMDS))
    s = sub.add_parser("speed")
    s.add_argument("mm_s", type=float)
    r = sub.add_parser("recipe")
    r.add_argument("id", type=int)
    sub.add_parser("time")
    lp = sub.add_parser("lamp")
    lp.add_argument("what", nargs="+", choices=sorted(LAMPS) + ["auto"])
    b = sub.add_parser("bench")
    b.add_argument("-n", type=int, default=1000)
    a = ap.parse_args()
    if not a.host:
        ap.error("--host 未指定，环境变量 BOARD_IP 也没设（仓库根 .env 有；或 --host <板子IP>）")
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace", line_buffering=True)
    except (AttributeError, ValueError):
        pass

    try:
        cl = Client(a.host, a.port)
    except OSError as e:
        print("连不上 %s:%d：%s（板上 des_app run 在跑吗？des_app.json 的 plc.modbus_port）" % (a.host, a.port, e))
        return 2
    try:
        if a.op == "status":
            print(decode(read_status(cl), cl.read_holding(0, N_HR)))
        elif a.op == "cmd":
            res = cl.write_checked(HR_CMD, [CMDS[a.name]])
            print("%s -> %s" % (a.name, ACK.get(res, res)))
            print(decode(read_status(cl), cl.read_holding(0, N_HR)))
            return 0 if res == 0 else 1
        elif a.op == "speed":
            res = cl.write_checked(HR_SPEED, [int(round(a.mm_s * 10))])
            print("speed %.1f mm/s -> %s" % (a.mm_s, ACK.get(res, res)))
            return 0 if res == 0 else 1
        elif a.op == "recipe":
            res = cl.write_checked(HR_RECIPE, [a.id])
            print("recipe %d -> %s" % (a.id, ACK.get(res, res)))
            return 0 if res == 0 else 1
        elif a.op == "time":
            t = int(time.time())
            res = cl.write_checked(HR_TIME_HI, [t >> 16, t & 0xFFFF])
            print("time %s -> %s" % (time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(t)), ACK.get(res, res)))
            return 0 if res == 0 else 1
        elif a.op == "lamp":
            v = 0 if "auto" in a.what else 0x100 | sum(LAMPS[w] for w in a.what)
            res = cl.write_checked(HR_LAMP, [v])
            print("lamp %s -> %s" % (" ".join(a.what), ACK.get(res, res)))
            if res == 1:
                print("  REJECTED：板上 RS485 没在跑（des_app.json rs485.enabled、比特流 v16、继电器模块接好了吗）")
            return 0 if res == 0 else 1
        elif a.op == "bench":
            t0 = time.perf_counter()
            for _ in range(a.n):
                cl.read_input(0, N_IR)
            dt = (time.perf_counter() - t0) / a.n * 1000
            print("读 %d 个输入寄存器：平均往返 %.2f ms（%d 次）" % (N_IR, dt, a.n))
        elif a.op == "watch":
            end = time.time() + a.seconds if a.seconds > 0 else None
            last_state, last_seq, last_hb, stale = None, None, None, 0
            while end is None or time.time() < end:
                ir = read_status(cl)   # v1.0 (24 寄存器) 的板子也能 watch，不被异常 02 打断
                if ir[IR_STATE] != last_state:
                    print("%s 状态 -> %s" % (time.strftime("%H:%M:%S"), STATES.get(ir[IR_STATE], ir[IR_STATE])))
                    last_state = ir[IR_STATE]
                if last_seq is not None and ir[IR_PART_SEQ] != last_seq:
                    missed = (ir[IR_PART_SEQ] - last_seq - 1) & 0xFFFF
                    cls = "-" if ir[IR_PART_CLS] == 0xFFFF else ir[IR_PART_CLS]
                    print("%s 零件 #%d 类别 %s %s 去向 %d 尺寸 %.2f mm%s" % (
                        time.strftime("%H:%M:%S"), ir[IR_PART_ID], cls, DISP.get(ir[IR_PART_DISP], "?"),
                        ir[IR_PART_DEST], ir[IR_PART_SIZE] / 100, "（漏看 %d 件）" % missed if missed else ""))
                last_seq = ir[IR_PART_SEQ]
                stale = stale + 1 if ir[IR_HB] == last_hb else 0
                if stale == 10:
                    print("%s 心跳 2 s 不变：des_app 卡住或已停" % time.strftime("%H:%M:%S"))
                last_hb = ir[IR_HB]
                time.sleep(0.2)
    except (ModbusError, OSError) as e:
        print("Modbus 出错：%s" % e)
        return 2
    finally:
        cl.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
