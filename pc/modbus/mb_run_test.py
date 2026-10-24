#!/usr/bin/env python3
"""mb_run_test.py — on-hardware check of the Modbus-TCP line interface: the belt really starts / changes speed /
pauses / stops on PLC writes, and the board rejects writes the run rules forbid. MOVES THE BELT (keep it clear).

    python mb_run_test.py [--host 192.168.137.100] [--speeds 30,80] [--hold 5]

Each step writes through Client.write_checked (waits until the board executed it, returns the ACK code) and then
reads the input registers. The board's actual belt speed register (IR17) comes from the stepper; compare it with
the stepper position the board logs (desreg) if you need an independent rate.
Exit code 0 = every expected ACK and state seen.
"""
import argparse
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mb_client as M  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="192.168.137.100")
    ap.add_argument("--port", type=int, default=502)
    ap.add_argument("--speeds", default="30,80", help="set points to try while running, mm/s")
    ap.add_argument("--hold", type=float, default=5.0, help="seconds at each speed")
    a = ap.parse_args()
    c = M.Client(a.host, a.port)
    fails = []
    t0 = time.time()

    def ir():
        return M.read_status(c)

    def log(msg):
        r = ir()
        print(f"{time.time() - t0:6.2f}s  {msg:34s} state {M.STATES.get(r[M.IR_STATE], r[M.IR_STATE])}  "
              f"flags 0x{r[M.IR_FLAGS]:02X}  speed {r[M.IR_SPEED] / 10:.1f} mm/s  alarm {r[M.IR_ALARM]}")
        return r

    def write(addr, vals, want, what):
        tw = time.perf_counter()
        res = c.write_checked(addr, vals)
        ms = (time.perf_counter() - tw) * 1000
        ok = res == want
        if not ok:
            fails.append(f"{what}: ACK {res}, expected {want}")
        log(f"{what} -> {M.ACK.get(res, res)} ({ms:.0f} ms){'' if ok else '  ✗'}")
        return res

    def expect_state(st, what):
        r = ir()
        if r[M.IR_STATE] != st:
            fails.append(f"{what}: state {r[M.IR_STATE]}, expected {st}")
        return r

    r = log("start of test")
    if r[M.IR_STATE] != 0:
        write(M.HR_CMD, [M.CMDS["stop"]], 0, "stop (bring to IDLE)")
    write(M.HR_SPEED, [0], 2, "speed 0 (must be BAD_VALUE)")
    write(M.HR_RECIPE, [99], 2, "recipe 99 (unknown: BAD_VALUE)")
    write(M.HR_CMD, [M.CMDS["resume"]], 1, "resume while IDLE (REJECTED)")
    write(M.HR_SPEED, [500], 0, "speed 50.0 mm/s")
    write(M.HR_CMD, [M.CMDS["start"]], 0, "start")
    time.sleep(1.0)
    r = expect_state(1, "after start")
    if not r[M.IR_FLAGS] & 0x01:
        fails.append("after start: belt-moving flag not set")
    write(M.HR_RECIPE, [2], 1, "recipe 2 while running (REJECTED)")
    time.sleep(max(0.0, a.hold - 1))
    log("cruise 50")
    for s in [float(x) for x in a.speeds.split(",") if x]:
        write(M.HR_SPEED, [int(round(s * 10))], 0, f"speed {s:.1f} mm/s while running")
        time.sleep(a.hold)
        r = log(f"cruise {s:.0f}")
        if abs(r[M.IR_SPEED] / 10 - s) > 0.5:
            fails.append(f"speed {s}: board reports {r[M.IR_SPEED] / 10}")
    write(M.HR_CMD, [M.CMDS["pause"]], 0, "pause (interlock)")
    time.sleep(2.0)
    r = expect_state(2, "after pause")
    log("paused")
    if r[M.IR_FLAGS] & 0x01:
        fails.append("paused: belt still flagged moving after 2 s")
    write(M.HR_CMD, [M.CMDS["resume"]], 0, "resume")
    time.sleep(2.0)
    expect_state(1, "after resume")
    log("running again")
    write(M.HR_CMD, [M.CMDS["stop"]], 0, "stop")
    time.sleep(2.0)
    r = expect_state(0, "after stop")
    log("stopped")
    if r[M.IR_FLAGS] & 0x01:
        fails.append("stopped: belt still flagged moving after 2 s")
    write(M.HR_SPEED, [500], 0, "speed back to 50.0 mm/s")
    c.close()
    print("\n== %s" % ("PASS" if not fails else "FAIL:\n   " + "\n   ".join(fails)))
    return 0 if not fails else 1


if __name__ == "__main__":
    sys.exit(main())
