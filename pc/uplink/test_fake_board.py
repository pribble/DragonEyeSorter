#!/usr/bin/env python3
"""test_fake_board.py: fake_board.py 的自检，不需要网络。
MQTT 包的编解码、90 s 剧本，以及对着本机一个极简 MQTT 服务端完整跑一遍（连接带遗嘱、保留状态、回答 ping、退出发 offline）。
    python test_fake_board.py        退出码 = 失败项数
"""
import io
import json
import socket
import threading

import fake_board as fb

passed = failed = 0


def check(cond, msg):
    global passed, failed
    if cond:
        passed += 1
    else:
        failed += 1
        print('FAIL', msg)


def test_codec():
    p = fb.mq_packet(0x30, b'x' * 200)
    check(p[:3] == bytes([0x30, 0xC8, 0x01]) and len(p) == 203, 'remaining length 200 = C8 01')
    t, f, body, size = fb.mq_parse(p)
    check(t == 3 and f == 0 and len(body) == 200 and size == 203, 'parse a full packet')
    check(fb.mq_parse(p[:100]) is None and fb.mq_parse(p[:1]) is None, 'incomplete packet -> None')
    check(fb.mq_parse(fb.mq_packet(0xC0, b'')) == (12, 0, b'', 2), 'zero-length packet')
    big = fb.mq_packet(0x30, b'y' * 20000)
    check(fb.mq_parse(big)[3] == len(big) and big[1:4] == bytes([0xA0, 0x9C, 0x01]), 'three-byte length')
    try:
        fb.mq_parse(bytes([0x30, 0x80, 0x80, 0x80, 0x80, 0x01]))
        check(False, 'five length bytes must be rejected')
    except ValueError:
        check(True, '')
    t, f, body, _ = fb.mq_parse(fb.mq_publish('a/b', '中文', retain=True))
    check(t == 3 and f & 1 and body[:5] == b'\x00\x03a/b' and body[5:].decode() == '中文', 'retained publish, UTF-8 payload')
    t, f, body, _ = fb.mq_parse(fb.mq_connect('id', 30, 'p/state', 'offline'))
    check(t == 1 and body[:7] == b'\x00\x04MQTT\x04' and body[7] == 0x26 and body[8:10] == b'\x00\x1e', 'CONNECT: clean session + retained will, keep-alive 30')
    check(body.endswith(b'\x00\x07p/state\x00\x07offline'), 'CONNECT carries the will')
    t, f, body, _ = fb.mq_parse(fb.mq_subscribe(1, 'p/cmd'))
    check(t == 8 and f == 2 and body == b'\x00\x01\x00\x05p/cmd\x00', 'SUBSCRIBE QoS 0')


def test_script():
    b = fb.Board(seed=1)
    seen, eth, totals = [], {}, {}
    for t in range(0, 181):
        for wo in b.step(float(t), 1):
            j = json.loads(wo)
            seen.append((t, j['id'], j['key'], j['status']))
        eth[t], totals[t] = b.eth, b.total
    lap1 = [(t, k, s) for t, _, k, s in seen if t < 90]
    check(lap1 == [(15, 'light', 'open'), (45, 'light', 'recovered'), (60, 'belt', 'open'), (75, 'light', 'closed'), (75, 'belt', 'closed')],
          'order script of one lap: %r' % lap1)
    check(eth[29] == 1 and eth[30] == 0 and eth[44] == 0 and eth[45] == 1, 'cable out at 30 s, back at 45 s')
    check(totals[59] == 60 and totals[74] == 60 and totals[75] == 61, 'no parts while the fault stops the line (%d %d %d)' % (totals[59], totals[74], totals[75]))
    check(b.total == b.ok + b.ng + b.recheck and b.ok > b.ng > 0, 'counts add up')
    ids = sorted({i for _, i, _, _ in seen})
    check(ids == [1, 2, 3, 4], 'second lap opens new orders: %r' % ids)
    b2 = fb.Board(seed=2)
    b2.step(16.0, 3)
    s = json.loads(b2.state(16.4))
    check(s['online'] is True and s['sim'] is True and s['up_s'] == 16 and s['app'] is True and s['total'] == 3, 'state basics')
    check(s['wo'] == dict(active=1, unacked=1, level=2, id=1, title='画面亮度异常'), 'state: order summary %r' % s['wo'])
    check(set(s) >= {'session', 'ok', 'ng', 'recheck', 'eth', 'net', 'tx', 'last'} and set(s['net']) == {'csq', 'dbm', 'oper', 'act'} and
          set(s['last']) == {'label', 'disp', 'dest', 'conf', 'size_mm'}, 'state has every field the dashboard reads')
    b2.step(61.0, 3)
    s = json.loads(b2.state(61))
    check(s['wo']['active'] == 1 and s['wo']['unacked'] == 2 and s['wo']['level'] == 3 and s['wo']['id'] == 2, 'fault order on top, recovered one still unacknowledged')
    check(len(b2.state(1e7).encode()) < 1400, 'state fits one MQTT message of the module')


def test_loopback():
    ls = socket.socket()
    ls.bind(('127.0.0.1', 0))
    ls.listen(1)
    got = dict(connect=None, pubs=[], sub=None, disconnect=False)

    def broker():
        c, _ = ls.accept()
        buf, asked = bytearray(), False
        c.settimeout(5)
        try:
            while True:
                d = c.recv(4096)
                if not d:
                    return
                buf.extend(d)
                while True:
                    p = fb.mq_parse(buf)
                    if not p:
                        break
                    del buf[:p[3]]
                    t, f, body = p[0], p[1], p[2]
                    if t == 1:
                        got['connect'] = body
                        c.sendall(b'\x20\x02\x00\x00')
                    elif t == 8:
                        got['sub'] = body
                        c.sendall(b'\x90\x03' + body[:2] + b'\x00')
                    elif t == 3:
                        tl = body[0] << 8 | body[1]
                        got['pubs'].append((body[2:2 + tl].decode(), body[2 + tl:].decode(), bool(f & 1)))
                        if not asked:
                            asked = True
                            c.sendall(fb.mq_publish('p/x/cmd', '{"cmd":"ping","id":41}'))
                            c.sendall(fb.mq_publish('p/x/cmd', '{"cmd":"start","id":42}'))
                            c.sendall(fb.mq_publish('p/x/other', 'ignored'))
                    elif t == 14:
                        got['disconnect'] = True
                        return
        except OSError:
            pass
        finally:
            c.close()

    th = threading.Thread(target=broker)
    th.start()
    log = io.StringIO()
    fb.run('p/x', '127.0.0.1', ls.getsockname()[1], 0.2, 1.5, out=log)
    th.join(5)
    ls.close()
    pubs = got['pubs']
    check(got['connect'] is not None and got['connect'].endswith(b'\x00\x09p/x/state\x00\x07offline'), 'will = offline on <prefix>/state')
    check(got['sub'] is not None and b'p/x/cmd' in got['sub'], 'subscribes to <prefix>/cmd')
    states = [json.loads(m) for t, m, r in pubs if t == 'p/x/state' and m != 'offline']
    check(len(states) >= 5 and all(r for t, m, r in pubs if t == 'p/x/state'), 'state published about every 0.2 s, always retained (%d)' % len(states))
    check(states and states[-1]['total'] > states[0]['total'] and states[0]['sim'] is True, 'counts grow, marked as simulated')
    replies = [json.loads(m) for t, m, r in pubs if t == 'p/x/reply']
    check(len(replies) == 2 and replies[0]['id'] == 41 and replies[0]['ok'] is True and replies[1]['id'] == 42 and replies[1]['ok'] is False,
          'ping answered, a line command refused: %r' % replies)
    check(pubs and pubs[-1] == ('p/x/state', 'offline', True) and got['disconnect'], 'leaves with a retained offline and a DISCONNECT')
    check('cmd    ping -> ok' in log.getvalue(), 'log line for the command')


if __name__ == '__main__':
    test_codec()
    test_script()
    test_loopback()
    print('test_fake_board: %d passed, %d failed' % (passed, failed))
    raise SystemExit(failed)
