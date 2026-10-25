#!/usr/bin/env python3
"""test_des_feed.py — des_feed.py 的协议与会话重开冒烟（无板）：本地假板注入 H / R / S 帧，断言两个会话
各得正确的 parts.csv、断线后干净退出、会话重开不泄漏文件句柄（/proc/self/fd 存在的平台上数 fd）。
退出码 = 失败项数（与 ps/tests 同一口约）。

    python pc/feed/test_des_feed.py
"""
import argparse
import os
import socket
import struct
import sys
import tempfile
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import des_feed as F  # noqa: E402

fail = 0


def check(cond, what):
    global fail
    print(('ok   ' if cond else 'FAIL ') + what)
    if not cond:
        fail += 1


def frame(ftype, name=b'', payload=b''):
    return b'DF' + ftype.encode() + b'\x00' + struct.pack('>HI', len(name), len(payload)) + name + payload


def main():
    out = tempfile.mkdtemp(prefix='des_feed_test_')
    srv = socket.socket()
    srv.bind(('127.0.0.1', 0))
    srv.listen(1)
    port = srv.getsockname()[1]

    def board():
        conn, _ = srv.accept()
        # 两个会话，第二个是板端重开（run 必须先关第一个的 CSV 句柄），各带一条记录
        conn.sendall(frame('R', payload=b'0,orphan,0') +
                     frame('H', b'sess1', b'track,label,disp') + frame('R', payload=b'1,gear_ok,0') +
                     frame('I', name=b'img/sess1_1_gear_ok.jpg', payload=b'fakejpeg') +
                     frame('I', name=b'img/nested/deep/x.jpg', payload=b'nested') +
                     frame('H', b'sess2', b'track,label,disp') + frame('R', payload=b'2,gear_short_l,1') +
                     frame('S', payload=b'ok'))
        time.sleep(0.3)   # let the reader drain
        conn.shutdown(socket.SHUT_RDWR)
        conn.close()

    threading.Thread(target=board, daemon=True).start()
    fds0 = len(os.listdir('/proc/self/fd')) if os.path.exists('/proc/self/fd') else None
    a = argparse.Namespace(host='127.0.0.1', port=port, out=out, quiet=True, once=True, seconds=0)
    how = 'returned'
    try:
        F.run(a)
    except (ConnectionError, OSError):
        how = 'disconnected'   # the board closing the socket ends run()
    check(how == 'disconnected', 'run ends on disconnect (%s)' % how)
    img = os.path.join(out, 'sess1', 'img', 'sess1_1_gear_ok.jpg')
    check(os.path.exists(img) and open(img, 'rb').read() == b'fakejpeg',
          'session 1 image stored under img/ with its payload')
    nested = os.path.join(out, 'sess1', 'img', 'nested', 'deep', 'x.jpg')
    check(os.path.exists(nested) and open(nested, 'rb').read() == b'nested',
          'nested image names create intermediate directories')
    check(not os.path.exists(os.path.join(out, 'parts.csv')),
          'a record before any session header is dropped, not written to <out>')
    for sess, line in (('sess1', '1,gear_ok,0'), ('sess2', '2,gear_short_l,1')):
        p = os.path.join(out, sess, 'parts.csv')
        ok = False
        if os.path.exists(p):
            body = open(p, encoding='utf-8').read().splitlines()
            ok = len(body) == 2 and body[0] == 'track,label,disp' and body[1] == line
        check(ok, '%s/parts.csv = header + %r' % (sess, line))
    if fds0 is not None:
        fds1 = len(os.listdir('/proc/self/fd'))
        check(fds1 <= fds0, 'no fd leak across the session reopen (%d -> %d)' % (fds0, fds1))
    srv.close()
    return 1 if fail else 0


if __name__ == '__main__':
    sys.exit(main())
