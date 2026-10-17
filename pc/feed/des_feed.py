#!/usr/bin/env python3
"""des_feed.py — receive the board's live inspection records over Ethernet (ps/src/app/net_feed.hpp).

The board (des_app run, des_app.json storage.feed_port, default 9500) is the server; this script connects, saves every
record and part image under <out>/<session>/ (parts.csv + img/), prints one line per part and the board's status every
2 s, and reconnects when the board restarts. Standard library only.

    python pc/feed/des_feed.py                       # board at 192.168.137.100:9500, saves to ./received
    python pc/feed/des_feed.py --host des-board --out D:/data/received --quiet
"""
import argparse
import os
import socket
import struct
import sys
import time


def read_exact(sock, n):
    buf = bytearray()
    while len(buf) < n:
        chunk = sock.recv(n - len(buf))
        if not chunk:
            raise ConnectionError('board closed the connection')
        buf += chunk
    return bytes(buf)


def read_frame(sock):
    hdr = read_exact(sock, 10)
    if hdr[:2] != b'DF':
        raise ConnectionError('bad frame header %r' % hdr[:2])
    ftype = chr(hdr[2])
    name_len, payload_len = struct.unpack('>HI', hdr[4:10])
    name = read_exact(sock, name_len).decode('utf-8', 'replace') if name_len else ''
    payload = read_exact(sock, payload_len) if payload_len else b''
    return ftype, name, payload


def run(a):
    sock = socket.create_connection((a.host, a.port), timeout=5)
    sock.settimeout(10)   # the board sends a status frame every 2 s
    print('connected to %s:%d' % (a.host, a.port))
    session_dir, csv, cols, n = None, None, [], 0
    try:
        while True:
            ftype, name, payload = read_frame(sock)
            if ftype == 'H':
                session_dir = os.path.join(a.out, name or 'session')
                os.makedirs(os.path.join(session_dir, 'img'), exist_ok=True)
                path = os.path.join(session_dir, 'parts.csv')
                new = not os.path.exists(path)
                csv = open(path, 'a', encoding='utf-8', newline='')
                cols = payload.decode('utf-8').split(',')
                if new:
                    csv.write(payload.decode('utf-8') + '\n')
                csv.flush()
                print('session %s -> %s' % (name, session_dir))
            elif ftype == 'R' and csv:
                line = payload.decode('utf-8', 'replace')
                csv.write(line + '\n')
                csv.flush()
                n += 1
                if not a.quiet:
                    f = dict(zip(cols, line.split(',')))
                    print('#%s %-14s %-7s -> %-9s size %s %s' % (f.get('track'), f.get('label'), f.get('disp'),
                                                                f.get('dest'), f.get('size_mm'), f.get('why')))
            elif ftype == 'I' and session_dir:
                p = os.path.join(session_dir, name.replace('/', os.sep))
                os.makedirs(os.path.dirname(p), exist_ok=True)
                with open(p, 'wb') as fimg:
                    fimg.write(payload)
            elif ftype == 'S' and not a.quiet:
                print('[board] %s  (received %d)' % (payload.decode('utf-8', 'replace'), n))
    finally:
        if csv:
            csv.close()
        sock.close()


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--host', default='192.168.137.100')
    ap.add_argument('--port', type=int, default=9500)
    ap.add_argument('--out', default='received')
    ap.add_argument('--quiet', action='store_true', help='no per-part lines')
    ap.add_argument('--once', action='store_true', help='exit on disconnect instead of reconnecting')
    ap.add_argument('--seconds', type=float, default=0, help='stop after this long (0 = run until Ctrl-C)')
    a = ap.parse_args()
    sys.stdout.reconfigure(line_buffering=True)   # live lines also when piped
    if a.seconds > 0:   # test runs: hard stop after the given time
        import threading
        t = threading.Timer(a.seconds, lambda: os._exit(0))
        t.daemon = True
        t.start()
    while True:
        try:
            run(a)
        except KeyboardInterrupt:
            return 0
        except (OSError, ConnectionError) as e:
            print('disconnected: %s' % e)
            if a.once:
                return 1
            time.sleep(2)


if __name__ == '__main__':
    sys.exit(main())
