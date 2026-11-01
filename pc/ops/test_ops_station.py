#!/usr/bin/env python3
"""test_ops_station.py — 远程运维站自检（无板）：假板子 = 记录通道（发 H / w / W / R / I 帧）+ Modbus 从站，
检查：工单入库与去重、对时、远程确认经 Modbus HR 8 发到板子、零件记录与图片、看板接口、运维报表、
本站「设备离线」工单的开 / 恢复 / 确认。退出码 = 失败项数（与 ps/tests 同一口约）。

    python pc/ops/test_ops_station.py
"""
import json
import os
import socket
import struct
import sys
import tempfile
import threading
import time
import urllib.request
from types import SimpleNamespace

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import ops_station as S  # noqa: E402

fail = 0


def check(cond, what):
    global fail
    print(('ok   ' if cond else 'FAIL ') + what)
    if not cond:
        fail += 1


def frame(ftype, name=b'', payload=b''):
    return b'DF' + ftype.encode() + b'\x00' + struct.pack('>HI', len(name), len(payload)) + name + payload


def order(oid, status, title='皮带速度异常（卡滞 / 打滑）', level=3):
    return {'id': oid, 'code': 2, 'key': 'belt_speed', 'level': level, 'level_cn': '故障', 'device': '传送带',
            'title': title, 'detail': '指令带速 50.0 mm/s，计米轮实测 2.0 mm/s', 'advice': '检查皮带', 'stop': True,
            'status': status, 'status_cn': {'open': '待处理', 'closed': '已关闭'}[status], 'count': 1,
            'wall_first': 1762000000000, 'wall_last': 1762000000000, 'wall_clear': 0, 'wall_ack': 0, 'acked_by': '',
            't_first': 12.5, 'session': 'S1'}


class FakeBoard:
    """records channel + Modbus-TCP slave of one board session"""

    def __init__(self):
        self.feed = socket.socket()
        self.feed.bind(('127.0.0.1', 0))
        self.feed.listen(2)
        self.mb = socket.socket()
        self.mb.bind(('127.0.0.1', 0))
        self.mb.listen(2)
        self.hr = {3: 0, 4: 7}
        self.ir = {0: 1, 2: 0, 3: 12, 4: 0, 5: 10, 17: 500, 21: 1, 22: 0x0102, 32: 1, 33: 1, 34: 3, 35: 2, 36: 2, 37: 9}
        self.writes = []
        self.mb_up = True
        threading.Thread(target=self.serve_feed, daemon=True).start()
        threading.Thread(target=self.serve_mb, daemon=True).start()

    def serve_feed(self):
        c, _ = self.feed.accept()
        cols = b'wall_ms,t_s,track,cls,label,conf,disp,dest,why,meas_valid,size_mm,image'
        c.sendall(frame('H', b'S1', cols))
        c.sendall(frame('w', b'', json.dumps([order(1, 'closed')], ensure_ascii=False).encode()))
        c.sendall(frame('W', b'', json.dumps(order(2, 'open'), ensure_ascii=False).encode()))
        c.sendall(frame('W', b'', json.dumps(dict(order(2, 'open'), count=2), ensure_ascii=False).encode()))
        c.sendall(frame('I', b'img/S1_5_gear_flash_h.jpg', b'\xff\xd8fakejpeg'))
        c.sendall(frame('R', b'', b'0,1.0,5,6,gear_flash_h,0.97,flap,rework,gear_flash_h,1,31.950,img/S1_5_gear_flash_h.jpg'))
        while True:
            time.sleep(0.5)
            try:
                c.sendall(frame('S', b'', b'records=1 images=1 dropped=0'))
            except OSError:
                return

    def serve_mb(self):
        while True:
            conn, _ = self.mb.accept()
            try:
                while self.mb_up:
                    req = conn.recv(256)
                    if not req:
                        break
                    tid = struct.unpack('>H', req[:2])[0]
                    fc, (addr, qty) = req[7], struct.unpack('>HH', req[8:12])
                    if fc in (3, 4):
                        t = self.hr if fc == 3 else self.ir
                        pdu = bytes([fc, qty * 2]) + b''.join(struct.pack('>H', t.get(addr + i, 0)) for i in range(qty))
                    elif fc == 6:
                        v = struct.unpack('>H', req[10:12])[0]
                        self.writes.append((addr, [v]))
                        pdu = req[7:12]
                        self.hr[3], self.hr[4] = 0, self.hr[4] + 1
                    elif fc == 16:
                        vals = list(struct.unpack('>%dH' % qty, req[13:13 + 2 * qty]))
                        self.writes.append((addr, vals))
                        pdu = req[7:12]
                        self.hr[3], self.hr[4] = 0, self.hr[4] + 1
                    else:
                        pdu = bytes([fc | 0x80, 1])
                    conn.sendall(struct.pack('>HHHB', tid, 0, len(pdu) + 1, 1) + pdu)
            except OSError:
                pass
            conn.close()


def wait(cond, s=5.0):
    end = time.time() + s
    while time.time() < end:
        if cond():
            return True
        time.sleep(0.05)
    return False


def main():
    board = FakeBoard()
    out = tempfile.mkdtemp(prefix='ops_station_test_')
    a = SimpleNamespace(host='127.0.0.1', src=None, feed_port=board.feed.getsockname()[1],
                        mb_port=board.mb.getsockname()[1], out=out)
    st = S.Station(a)
    threading.Thread(target=st.feed_loop, daemon=True).start()
    threading.Thread(target=st.mb_loop, daemon=True).start()
    srv = S.ThreadingHTTPServer(('127.0.0.1', 0), S.make_handler(st))
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    base = 'http://127.0.0.1:%d' % srv.server_address[1]

    check(wait(lambda: len(st.store.orders()) == 2), 'snapshot + new order stored (2 rows)')
    o2 = st.store.get('S1#2')
    check(o2 is not None and o2['count'] == 2 and o2['status'] == 'open', 'repeat of #2 updates the same row (count 2)')
    check(wait(lambda: st.new_orders == 1), 'one new open order counted (the closed snapshot one is not): %d' % st.new_orders)
    check(wait(lambda: board.writes and board.writes[0][0] == 5 and len(board.writes[0][1]) == 2),
          'board clock synced first (FC 16 at HR 5, two words)')
    if board.writes:
        t = (board.writes[0][1][0] << 16) | board.writes[0][1][1]
        check(abs(t - time.time()) < 5, 'clock value = this PC time')
    check(wait(lambda: st.feed['records'] == 1 and st.feed['images'] == 1), 'record + image received')
    img = os.path.join(out, 'img', 'S1', 'img', 'S1_5_gear_flash_h.jpg')
    check(os.path.isfile(img), 'image saved under img/<session>/')
    check(wait(lambda: st.board['ir'] is not None), 'Modbus status read')

    s = json.loads(urllib.request.urlopen(base + '/api/state').read())
    check(s['board']['state'] == '运行' and s['board']['wo_unacked'] == 1 and s['board']['map'] == 0x0102,
          'state: line running, 1 unacked order, map v1.2')
    check(s['parts'] and s['parts'][0]['label'] == '飞边·重' and s['parts'][0]['dest'] == '返修', 'part row in Chinese')
    check(s['parts'][0]['img'].startswith('/img/S1/'), 'part row links its image')
    body = urllib.request.urlopen(base + s['parts'][0]['img']).read()
    check(body == b'\xff\xd8fakejpeg', 'image served by the dashboard')
    check(any(o['uid'] == 'S1#2' and o['first_text'].startswith('2025-11-01') for o in s['orders']),
          'board wall clock shown as local time')
    html = urllib.request.urlopen(base + '/').read().decode('utf-8')
    check('远程运维站' in html and '/api/state' in html, 'dashboard page')

    # acknowledge #2 from the page: goes to the board as Modbus HR 8 = 2
    req = urllib.request.Request(base + '/api/ack', data=json.dumps({'uid': 'S1#2'}).encode(),
                                 headers={'Content-Type': 'application/json'})
    r = json.loads(urllib.request.urlopen(req).read())
    check(r['ok'] and (8, [2]) in board.writes, 'ack -> HR 8 = 2 on the board (%s)' % r['text'])
    req = urllib.request.Request(base + '/api/cmd', data=json.dumps({'cmd': 'stop'}).encode(),
                                 headers={'Content-Type': 'application/json'})
    r = json.loads(urllib.request.urlopen(req).read())
    check(r['ok'] and (0, [2]) in board.writes, 'remote stop -> HR 0 = 2')
    req = urllib.request.Request(base + '/api/speed', data=json.dumps({'mm_s': 'abc'}).encode(),
                                 headers={'Content-Type': 'application/json'})
    r = json.loads(urllib.request.urlopen(req).read())
    check(not r['ok'], 'bad speed rejected, the Modbus thread keeps running')
    req = urllib.request.Request(base + '/api/speed', data=json.dumps({'mm_s': 45}).encode(),
                                 headers={'Content-Type': 'application/json'})
    r = json.loads(urllib.request.urlopen(req).read())
    check(r['ok'] and (1, [450]) in board.writes, 'speed 45 -> HR 1 = 450')

    # station order: the board goes quiet after it answered (stop the polling threads first: they refresh last_ok)
    st.stop = True
    time.sleep(3)
    with st.lock:
        st.board['last_ok'] = time.time() - 10
    st.check_offline()
    off = [o for o in st.store.orders() if o['source'] == 'station']
    check(len(off) == 1 and off[0]['status'] == 'open' and off[0]['code'] == S.OFFLINE_CODE, 'offline order opened')
    st.check_offline()
    check(len([o for o in st.store.orders() if o['source'] == 'station']) == 1, 'still one offline order')
    with st.lock:
        st.board['last_ok'] = time.time()
    st.check_offline()
    off = st.store.get(off[0]['uid'])
    check(off['status'] == 'recovered', 'board back: offline order recovered')
    ok, text = st.ack(off['uid'])
    check(ok and st.store.get(off['uid'])['status'] == 'closed', 'station order acknowledged locally -> closed')

    rep = urllib.request.urlopen(base + '/api/report.csv').read().decode('utf-8-sig')
    check(rep.startswith('工单号,') and '皮带速度异常' in rep and '设备离线' in rep, 'report CSV has every order')
    srv.shutdown()
    print('test_ops_station: %d failed' % fail)
    return fail


if __name__ == '__main__':
    sys.exit(main())
