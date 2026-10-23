"""can_soak.py — headless CAN link test against the board (des_app run --can can0), for the test report
(总线稳定性 / 命令响应). Plays the PLC without moving anything: heartbeat 2 Hz, and every 0.5 s a STOP (the line is
idle anyway) or a belt-speed write of the current value; never START. Measures:
  - command -> ACK round trip on the PC clock (USB adapter both ways included), ACK result codes
  - STATUS frames: rate, gaps > 150 ms, lost frames from the board's heartbeat counter, PLC-link flag
  - the adapter's own bus state / error counters at the end

    python can_soak.py [--backend zqwl] [-s 120] [--speed 50]
"""
import argparse
import statistics
import struct
import time

import can_proto as P
from backends import open_bus


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--backend", default="zqwl", choices=["zqwl", "python-can", "controlcan", "virtual"])
    ap.add_argument("--port")
    ap.add_argument("-s", "--seconds", type=float, default=120)
    ap.add_argument("--speed", type=float, default=50, help="belt speed written back (mm/s): use the current one")
    ap.add_argument("--no-cmd", action="store_true", help="heartbeat only (no commands -> the board sends no ACKs)")
    a = ap.parse_args()
    bus = open_bus(a.backend, port=a.port)
    pending = {}   # (cmd id low byte, seq) -> send time
    rtt, results, unanswered = [], {}, 0
    st_t, st_hb, gaps, lost, link_seen = [], None, 0, 0, 0
    n_counts = 0
    seq, hb = 0, 0
    t0 = time.time()
    next_hb = next_cmd = t0
    try:
        while time.time() - t0 < a.seconds:
            now = time.time()
            if now >= next_hb:
                bus.send(*P.plc_hb(hb))
                hb += 1
                next_hb += 0.5
            if now >= next_cmd and not a.no_cmd:
                seq = (seq + 1) & 0xFF
                arb, data = P.cmd(P.CMD_STOP, seq) if seq % 2 else P.speed(a.speed, seq)
                pending[(arb & 0xFF, seq)] = time.time()
                bus.send(arb, data)
                next_cmd += 0.5
            for k, ts in list(pending.items()):   # no ACK within 1 s
                if now - ts > 1.0:
                    unanswered += 1
                    del pending[k]
            f = bus.recv(0.005)
            if not f:
                continue
            arb, d = f
            t = time.time()
            if arb == P.ID_ACK and len(d) >= 3:
                k = (d[0], d[1])
                if k in pending:
                    rtt.append((t - pending.pop(k)) * 1000)
                    results[d[2]] = results.get(d[2], 0) + 1
            elif arb == P.ID_STATUS and len(d) >= 8:
                state, flags, _sp, _al, h, _up = struct.unpack("<BBHBBH", bytes(d[:8]))
                if st_t and t - st_t[-1] > 0.150:
                    gaps += 1
                if st_hb is not None:
                    lost += (h - st_hb - 1) & 0xFF
                st_hb = h
                st_t.append(t)
                link_seen += 1 if flags & 0x08 else 0
            elif arb == P.ID_COUNTS:
                n_counts += 1
    finally:
        adapter = bus.status() if hasattr(bus, "status") else ""
        bus.close()
    dur = time.time() - t0
    print(f"== {dur:.0f} s, backend {a.backend}")
    if rtt:
        q = sorted(rtt)
        print(f"command -> ACK round trip (PC clock, USB both ways): n {len(q)}  min {q[0]:.2f}  "
              f"median {statistics.median(q):.2f}  p95 {q[int(0.95 * (len(q) - 1))]:.2f}  max {q[-1]:.2f} ms")
    print(f"ACK results {results}  (0 = ok)   unanswered {unanswered}")
    print(f"STATUS {len(st_t)} frames ({len(st_t) / dur:.2f}/s), gaps > 150 ms {gaps}, lost (heartbeat counter) {lost},"
          f" PLC-link flag set in {link_seen}")
    print(f"COUNTS {n_counts} frames ({n_counts / dur:.2f}/s)")
    if adapter:
        print("adapter:", adapter)


if __name__ == "__main__":
    main()
