#!/usr/bin/env python3
"""test_mb_client.py — mb_client.py 的 Client 协议层冒烟（无板）：本地有状态假从站回正常与畸形的 MBAP 帧。
契约：畸形短回复（MBAP 长度 < 3）抛 ModbusError 而不是 IndexError；流从此失步（长度说谎无法重同步），
调用方重连即可恢复；write_checked 的「写入 → 等 SEQ 变化 → 读 RESULT」契约（happy / FC16 多写 / 超时）。
退出码 = 失败项数。

    python pc/modbus/test_mb_client.py
"""
import os
import socket
import struct
import sys
import threading

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import mb_client as M  # noqa: E402

fail = 0


def check(cond, what):
    global fail
    print(('ok   ' if cond else 'FAIL ') + what)
    if not cond:
        fail += 1


def mbap(tid, pdu, unit=1):
    return struct.pack(">HHHB", tid, 0, len(pdu) + 1, unit) + pdu


class FakeSlave(threading.Thread):
    """一个连接的有状态假从站：HR 表 + FC03 读 / FC06 单写 / FC16 多写；execute=True 时模拟板端
    「执行一次」（RESULT=0、SEQ+1），False 则 SEQ 永不前进（write_checked 的超时路径）。
    script 给定时按序发送预制回复（畸形帧用），None = 关闭连接。"""

    def __init__(self, srv, hr_result=0, seq0=5, execute=True, script=None):
        super().__init__(daemon=True)
        self.srv, self.execute, self.script = srv, execute, script
        self.hr = {3: hr_result, 4: seq0}
        self.ir = {0: 11, 1: 22}
        self._done = False

    def run(self):
        conn, _ = self.srv.accept()
        served = 0
        try:
            self.serve(conn)
        except (ConnectionError, OSError):
            pass   # the desync test's client closes abruptly (RST): a normal way to end
        conn.close()

    def serve(self, conn):
        served = 0
        while True:
            req = conn.recv(256)
            if not req:
                break
            tid = struct.unpack(">H", req[:2])[0]
            fc = req[7]
            addr, qty = struct.unpack(">HH", req[8:12])
            if self.script is not None:
                if served >= len(self.script):
                    break
                reply = self.script[served]
                served += 1
                if reply is None:
                    break
                conn.sendall(reply)
                continue
            if fc == 3:   # read holding: RESULT / SEQ from the table
                pdu = bytes([3, qty * 2]) + b"".join(struct.pack(">H", self.hr.get(addr + i, 0)) for i in range(qty))
            elif fc == 4:   # read input registers
                pdu = bytes([4, qty * 2]) + b"".join(struct.pack(">H", self.ir.get(addr + i, 0)) for i in range(qty))
            elif fc == 6:   # write single: echo, then the board "executes" it once
                self.hr[addr] = struct.unpack(">H", req[10:12])[0]
                pdu = req[7:12]
                if self.execute and not self._done:
                    self._done = True
                    self.hr[3], self.hr[4] = 0, self.hr[4] + 1
            elif fc == 16:   # write multiple: echo (addr, qty), then the board "executes" it once
                nb = req[12]
                if nb != 2 * qty:   # the real board rejects a lying byte count (exception 03)
                    conn.sendall(mbap(tid, bytes([fc | 0x80, 3])))
                    served += 1
                    continue
                values = struct.unpack(">%dH" % qty, req[13:13 + 2 * qty])
                for i, v in enumerate(values):
                    self.hr[addr + i] = v
                pdu = req[7:12]
                if self.execute and not self._done:
                    self._done = True
                    self.hr[3], self.hr[4] = 0, self.hr[4] + 1
            else:
                pdu = bytes([fc | 0x80, 1])
            conn.sendall(mbap(tid, pdu))
        conn.close()


def main():
    srv = socket.socket()
    srv.bind(('127.0.0.1', 0))
    srv.listen(8)
    port = srv.getsockname()[1]
    pdu_ok = bytes([4, 4]) + struct.pack(">HH", 11, 22)

    # 1) 正常读 + 畸形短回复（流失步契约）
    threading.Thread(target=FakeSlave(srv, script=[mbap(1, pdu_ok), mbap(2, bytes([4]))]).run, daemon=True).start()
    cl = M.Client('127.0.0.1', port)
    vals = cl.read_input(0, 2)
    check(vals == [11, 22], "normal FC04 read: %s" % vals)
    err = None
    try:
        cl.read_input(0, 2)   # the short reply: header claims a body, client raises before consuming it
    except M.ModbusError as e:
        err = str(e)
    check(err is not None and "short reply" in err, "short reply raises ModbusError (%s)" % err)
    cl.close()

    # 2) write_checked happy：写入 → 板端执行（SEQ+1）→ RESULT=0
    threading.Thread(target=FakeSlave(srv, execute=True).run, daemon=True).start()
    cl = M.Client('127.0.0.1', port)
    res = cl.write_checked(0, [7], timeout=1.0)
    check(res == 0, "write_checked returns the board's RESULT (%s)" % res)
    cl.close()

    # 3) write_checked FC16 多寄存器：两个值都落表
    threading.Thread(target=FakeSlave(srv, execute=True).run, daemon=True).start()
    cl = M.Client('127.0.0.1', port)
    res = cl.write_checked(5, [0x1111, 0x2222], timeout=1.0)
    check(res == 0, "write_checked FC16 returns the board's RESULT (%s)" % res)
    got = cl.read_holding(5, 2)
    check(got == [0x1111, 0x2222], "FC16 landed both registers (%s)" % (got,))
    cl.close()

    # 4) write_checked 超时：板端不执行（SEQ 不动）→ 超时抛 ModbusError
    threading.Thread(target=FakeSlave(srv, execute=False).run, daemon=True).start()
    cl = M.Client('127.0.0.1', port)
    err = None
    try:
        cl.write_checked(0, [7], timeout=0.3)
    except M.ModbusError as e:
        err = str(e)
    check(err is not None and "did not execute" in err, "write_checked times out when the board never executes (%s)" % err)
    cl.close()

    # 5) 重连后照常（畸形的失步连接已弃）
    threading.Thread(target=FakeSlave(srv).run, daemon=True).start()
    cl = M.Client('127.0.0.1', port)
    try:
        vals = cl.read_input(0, 2)
        check(vals == [11, 22], "fresh connection works (%s)" % vals)
    except Exception as e:   # noqa: BLE001 - anything here is a failure reported below
        check(False, "fresh connection works (%s: %s)" % (type(e).__name__, e))
    cl.close()
    srv.close()
    return 1 if fail else 0


if __name__ == '__main__':
    sys.exit(main())
