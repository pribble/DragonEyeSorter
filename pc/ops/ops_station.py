#!/usr/bin/env python3
"""ops_station.py — 远程运维站（赛题拓展 4「工单推送」、拓展 5「远程运维」）。只用 Python 标准库。

板子（des_app run，ps/src/app/ops.hpp）在本地检测设备故障并生成运维工单，经网口推给这里：
  · 端口 9500（des_app.json storage.feed_port）：每件检测记录 'R'、不良品图片 'I'、工单变化 'W'（JSON）、
    连上时的工单列表 'w'（JSON 数组）；
  · 端口 502（Modbus-TCP）：线体状态 / 计数 / 工单摘要（IR 32..37），确认工单（HR 8），启停（HR 0），带速（HR 1），
    对时（HR 5 / 6，板子没有 RTC，本站一连上就对时）。
本站把工单存进 SQLite（换机 / 重启不丢）、开一个网页看板（工单、设备状态、最近零件与不良图、远程确认 / 启停），
可导出运维报表 CSV；板子 5 秒没响应时本站自己开一张「设备离线」工单。

    python pc/ops/ops_station.py                          # 板子 = $BOARD_IP（仓库根 .env），看板 http://127.0.0.1:8090
    python pc/ops/ops_station.py --host 192.168.137.100 --src 192.168.137.1 --out D:/des_ops
    python pc/ops/ops_station.py --bind 0.0.0.0           # 局域网里其他电脑 / 手机也能打开看板
（--src：本机开着 FlClash / Clash 的 TUN 时，绑定有线网口的地址才连得上板子。）
"""
import argparse
import csv
import io
import json
import os
import queue
import socket
import sqlite3
import struct
import sys
import threading
import time
import urllib.parse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'modbus'))
import mb_client as MB  # noqa: E402

# Modbus map v1.2 additions (ps/src/hw/modbus_tcp.hpp)
HR_WO_ACK = 8
IR_WO_ACTIVE, IR_WO_UNACKED, IR_WO_LEVEL, IR_WO_LAST_ID, IR_WO_LAST_CODE, IR_WO_SEQ = range(32, 38)
N_IR_V12 = 40
STATES = {0: '停止', 1: '运行', 2: '暂停', 3: '急停', 4: '故障'}
LABELS = {'gear_ok': '良品', 'gear_short_l': '缺料·轻', 'gear_short_h': '缺料·重', 'gear_dirty_l': '脏污·轻',
          'gear_dirty_h': '脏污·重', 'gear_flash_l': '飞边·轻', 'gear_flash_h': '飞边·重', 'unclassified': '未分类'}
DISP = {'pick': '抓取', 'flap': '挡板', 'recheck': '复检'}
DEST = {'gear_box': '齿轮盒', 'scrap': '报废', 'rework': '返修', 'recheck': '复检', 'tail': '带尾'}
OFFLINE_CODE = 101   # station-side order: the board does not answer


def repo_env(key):
    """value from the repo's .env (KEY=VALUE lines), for the board address default"""
    p = os.path.join(HERE, '..', '..', '.env')
    try:
        for line in open(p, encoding='utf-8'):
            if line.strip().startswith(key + '='):
                return line.split('=', 1)[1].strip().strip('"\'')
    except OSError:
        pass
    return None


# ------------------------------------------------------------------ feed frames (ps/src/app/part_log.cpp NetFeed)
def read_exact(sock, n):
    buf = bytearray()
    while len(buf) < n:
        chunk = sock.recv(n - len(buf))
        if not chunk:
            raise ConnectionError('板子断开了连接')
        buf += chunk
    return bytes(buf)


def read_frame(sock):
    hdr = read_exact(sock, 10)
    if hdr[:2] != b'DF':
        raise ConnectionError('帧头不对 %r' % hdr[:2])
    ftype = chr(hdr[2])
    name_len, payload_len = struct.unpack('>HI', hdr[4:10])
    name = read_exact(sock, name_len).decode('utf-8', 'replace') if name_len else ''
    payload = read_exact(sock, payload_len) if payload_len else b''
    return ftype, name, payload


# ------------------------------------------------------------------ store
class Store:
    """work orders (one row per board session + id; station orders use session 'station') and station events"""
    COLS = ['uid', 'session', 'id', 'code', 'key', 'level', 'level_cn', 'device', 'title', 'detail', 'advice', 'stop',
            'status', 'status_cn', 'count', 'wall_first', 'wall_last', 'wall_clear', 'wall_ack', 'acked_by', 'source',
            'first_seen', 'last_seen']

    @classmethod
    def cols_sql(cls):
        return ', '.join('"%s"' % c for c in cls.COLS)   # quoted: KEY is an SQL keyword

    def __init__(self, path):
        self.lock = threading.Lock()
        self.db = sqlite3.connect(path, check_same_thread=False)
        self.db.execute('create table if not exists orders (%s, primary key (uid))' % self.cols_sql())
        self.db.execute('create table if not exists events (t real, text text)')
        self.db.commit()

    def upsert(self, o, source='board'):
        now = time.time()
        uid = '%s#%s' % (o.get('session', ''), o.get('id'))
        with self.lock:
            row = self.db.execute('select first_seen from orders where uid = ?', (uid,)).fetchone()
            first_seen = row[0] if row else now
            vals = dict(o, uid=uid, source=source, first_seen=first_seen, last_seen=now)
            vals['stop'] = 1 if o.get('stop') else 0
            self.db.execute('insert or replace into orders (%s) values (%s)' % (self.cols_sql(),
                                                                              ', '.join('?' * len(self.COLS))),
                            [vals.get(c) for c in self.COLS])
            self.db.commit()
        return row is None

    def orders(self, n=200):
        with self.lock:
            cur = self.db.execute('select %s from orders order by first_seen desc limit ?' % self.cols_sql(), (n,))
            return [dict(zip(self.COLS, r)) for r in cur.fetchall()]

    def get(self, uid):
        with self.lock:
            r = self.db.execute('select %s from orders where uid = ?' % self.cols_sql(), (uid,)).fetchone()
        return dict(zip(self.COLS, r)) if r else None

    def event(self, text):
        with self.lock:
            self.db.execute('insert into events values (?, ?)', (time.time(), text))
            self.db.commit()

    def events(self, n=30):
        with self.lock:
            return [{'t': t, 'text': x} for t, x in
                    self.db.execute('select t, text from events order by t desc limit ?', (n,)).fetchall()]

    def report_csv(self):
        """运维报表: every order, Excel-friendly (UTF-8 BOM)"""
        out = io.StringIO()
        w = csv.writer(out)
        w.writerow(['工单号', '会话', '级别', '设备', '故障', '详情', '处理建议', '状态', '次数', '首次发生', '最近发生',
                    '恢复时间', '确认时间', '确认人', '来源', '是否停机'])
        for o in sorted(self.orders(100000), key=lambda r: r['first_seen']):
            w.writerow([o['id'], o['session'], o['level_cn'], o['device'], o['title'], o['detail'], o['advice'],
                        o['status_cn'], o['count'], wall_text(o['wall_first'], o['first_seen']),
                        wall_text(o['wall_last'], o['last_seen']), wall_text(o['wall_clear']), wall_text(o['wall_ack']),
                        o['acked_by'], '板子' if o['source'] == 'board' else '运维站', '是' if o['stop'] else '否'])
        return ('\ufeff' + out.getvalue()).encode('utf-8')


def wall_text(ms, fallback_s=None):
    """board wall clock (ms, UTC) as local time; before any clock sync the board says 1970 -> the PC time instead"""
    if ms and ms > 1.7e12:
        return time.strftime('%Y-%m-%d %H:%M:%S', time.localtime(ms / 1000))
    if fallback_s:
        return time.strftime('%Y-%m-%d %H:%M:%S', time.localtime(fallback_s)) + '（本站收到）'
    return ''


# ------------------------------------------------------------------ station
class Station:
    def __init__(self, a):
        self.a = a
        os.makedirs(a.out, exist_ok=True)
        self.store = Store(os.path.join(a.out, 'ops.db'))
        self.lock = threading.Lock()
        self.stop = False
        self.cmds = queue.Queue()
        self.feed = {'connected': False, 'session': '', 'records': 0, 'images': 0, 'status': '', 'since': 0}
        self.board = {'connected': False, 'ir': None, 'hr': None, 'last_ok': 0, 'ever': False, 'time_synced': False}
        self.parts = []            # newest last, at most 40
        self.cols = []
        self.offline_id = 0
        self.new_orders = 0        # increments per new order (the page beeps when it moves)

    # -- helpers
    def connect(self, port, timeout=5):
        src = (self.a.src, 0) if self.a.src else None
        return socket.create_connection((self.a.host, port), timeout=timeout, source_address=src)

    def log(self, text):
        print(time.strftime('%H:%M:%S'), text, flush=True)
        self.store.event(text)

    def on_order(self, o, snapshot=False, source='board'):
        is_new = self.store.upsert(o, source)
        if is_new and o.get('status') != 'closed':
            with self.lock:
                self.new_orders += 1
            if not snapshot:
                self.log('新工单 #%s %s：%s（%s）' % (o.get('id'), o.get('level_cn'), o.get('title'), o.get('detail')))

    # -- network feed: records, images, work orders
    def feed_loop(self):
        while not self.stop:
            try:
                sock = self.connect(self.a.feed_port)
                sock.settimeout(10)   # the board sends a status frame every 2 s
                with self.lock:
                    self.feed.update(connected=True, since=time.time())
                self.log('网口记录通道已连接 %s:%d' % (self.a.host, self.a.feed_port))
                session = ''
                while not self.stop:
                    t, name, payload = read_frame(sock)
                    if t == 'H':
                        session = name
                        self.cols = payload.decode('utf-8').split(',')
                        with self.lock:
                            self.feed['session'] = session
                    elif t == 'R':
                        rec = dict(zip(self.cols, payload.decode('utf-8', 'replace').split(',')))
                        rec['session'] = session
                        rec['seen'] = time.time()
                        with self.lock:
                            self.feed['records'] += 1
                            self.parts.append(rec)
                            del self.parts[:-40]
                    elif t == 'I':
                        p = os.path.normpath(os.path.join(self.a.out, 'img', session, name))
                        if p.startswith(os.path.normpath(os.path.join(self.a.out, 'img'))):
                            os.makedirs(os.path.dirname(p), exist_ok=True)
                            with open(p, 'wb') as f:
                                f.write(payload)
                            with self.lock:
                                self.feed['images'] += 1
                    elif t == 'W':
                        self.on_order(json.loads(payload.decode('utf-8')))
                    elif t == 'w':
                        for o in json.loads(payload.decode('utf-8')):
                            self.on_order(o, snapshot=True)
                    elif t == 'S':
                        with self.lock:
                            self.feed['status'] = payload.decode('utf-8', 'replace')
            except (OSError, ConnectionError, ValueError) as e:
                with self.lock:
                    was = self.feed['connected']
                    self.feed['connected'] = False
                if was:
                    self.log('网口记录通道断开：%s' % e)
            time.sleep(2)

    # -- Modbus: status, clock, commands
    def mb_loop(self):
        while not self.stop:
            cl = None
            try:
                cl = MB.Client.__new__(MB.Client)   # mb_client.Client with our socket (source address)
                cl.s = self.connect(self.a.mb_port, timeout=2)
                cl.s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
                cl.tid, cl.unit = 0, 1
                now = int(time.time())
                r = cl.write_checked(MB.HR_TIME_HI, [now >> 16, now & 0xFFFF])
                with self.lock:
                    self.board.update(connected=True, time_synced=r == 0)
                self.log('Modbus 已连接，已给板子对时（%s）' % MB.ACK.get(r, r))
                while not self.stop:
                    self.run_commands(cl)
                    try:
                        ir = cl.read_input(0, N_IR_V12)
                    except MB.ModbusError:
                        ir = MB.read_status(cl) + [0] * (N_IR_V12 - MB.N_IR)   # board without work orders (map < 1.2)
                    hr = cl.read_holding(0, MB.N_HR)
                    with self.lock:
                        self.board.update(connected=True, ir=ir, hr=hr, last_ok=time.time(), ever=True)
                    self.check_offline()
                    time.sleep(0.5)
            except (OSError, MB.ModbusError) as e:
                with self.lock:
                    was = self.board['connected']
                    self.board['connected'] = False
                if was:
                    self.log('Modbus 断开：%s' % e)
            finally:
                if cl is not None and getattr(cl, 's', None):
                    cl.s.close()
            self.check_offline()
            time.sleep(2)

    def run_commands(self, cl):
        while True:
            try:
                kind, arg, reply = self.cmds.get_nowait()
            except queue.Empty:
                return
            try:
                if kind == 'cmd':
                    r = cl.write_checked(MB.HR_CMD, [MB.CMDS[arg]])
                    text = '远程命令 %s → %s' % (arg, MB.ACK.get(r, r))
                elif kind == 'speed':
                    r = cl.write_checked(MB.HR_SPEED, [int(round(float(arg) * 10))])
                    text = '远程设带速 %.1f mm/s → %s' % (float(arg), MB.ACK.get(r, r))
                else:   # ack
                    r = cl.write_checked(HR_WO_ACK, [int(arg)])
                    text = '远程确认工单 %s → %s' % ('全部' if int(arg) == 0xFFFF else '#%d' % int(arg), MB.ACK.get(r, r))
                self.log(text)
                reply.put((r == 0, text))
            except (KeyError, ValueError) as e:
                reply.put((False, '参数不对：%s' % e))
            except (MB.ModbusError, OSError) as e:
                reply.put((False, '失败：%s' % e))
                raise

    def command(self, kind, arg, timeout=3):
        reply = queue.Queue()
        self.cmds.put((kind, arg, reply))
        try:
            return reply.get(timeout=timeout)
        except queue.Empty:
            return False, 'Modbus 没连上或没有响应'

    def check_offline(self):
        """station work order: the board stopped answering (after it answered once)"""
        with self.lock:
            b = dict(self.board)
        silent = b['ever'] and time.time() - b['last_ok'] > 5
        uid = 'station#%d' % self.offline_id
        cur = self.store.get(uid) if self.offline_id else None
        if silent and cur is not None and cur['status'] == 'recovered':   # offline again before anyone acknowledged
            cur.update(status='open', status_cn='待处理', count=cur['count'] + 1, wall_last=int(time.time() * 1000))
            self.store.upsert({k: cur[k] for k in Store.COLS if k not in ('uid', 'source', 'first_seen', 'last_seen')},
                              source='station')
            self.log('板子又离线了')
        elif silent and (cur is None or cur['status'] == 'closed'):
            self.offline_id = int(time.time())
            self.on_order(dict(session='station', id=self.offline_id, code=OFFLINE_CODE, key='offline', level=3,
                               level_cn='故障', device='主控板 / 网络', title='设备离线（网口无响应）',
                               detail='Modbus 超过 5 s 没有应答，最后一次 %s' % time.strftime(
                                   '%H:%M:%S', time.localtime(b['last_ok'])),
                               advice='检查板子供电、网线和 des_app 服务（板上 des_service.sh status）', stop=False,
                               status='open', status_cn='待处理', count=1, wall_first=int(time.time() * 1000),
                               wall_last=int(time.time() * 1000), wall_clear=0, wall_ack=0, acked_by=''),
                          source='station')
        elif cur and not silent and cur['status'] in ('open', 'acked'):
            cur.update(status='closed' if cur['status'] == 'acked' else 'recovered',
                       status_cn='已关闭' if cur['status'] == 'acked' else '已恢复待确认', wall_clear=int(time.time() * 1000))
            self.store.upsert({k: cur[k] for k in Store.COLS if k not in ('uid', 'source', 'first_seen', 'last_seen')},
                              source='station')
            self.log('板子恢复在线')

    def ack(self, uid):
        o = self.store.get(uid)
        if not o:
            return False, '没有这张工单'
        if o['source'] == 'station':   # the station's own order: acknowledged here
            closed = o['status'] in ('recovered', 'closed')
            o.update(status='closed' if closed else 'acked', status_cn='已关闭' if closed else '处理中',
                     acked_by='远程运维站', wall_ack=int(time.time() * 1000))
            self.store.upsert({k: o[k] for k in Store.COLS if k not in ('uid', 'source', 'first_seen', 'last_seen')},
                              source='station')
            return True, '已确认 #%s' % o['id']
        with self.lock:
            session = self.feed['session']
        if o['session'] != session:
            return False, '这张工单属于板子之前的运行（%s），板上已经没有了' % o['session']
        return self.command('ack', o['id'])

    # -- the page's data
    def state(self):
        with self.lock:
            b, f, parts, new = dict(self.board), dict(self.feed), list(self.parts), self.new_orders
        board = {'connected': b['connected'], 'time_synced': b['time_synced']}
        ir, hr = b['ir'], b['hr']
        if ir:
            board.update(state=STATES.get(ir[MB.IR_STATE], str(ir[MB.IR_STATE])), state_code=ir[MB.IR_STATE],
                         speed=ir[MB.IR_SPEED] / 10, set_speed=hr[MB.HR_SPEED] / 10 if hr else 0,
                         total=MB.u32(ir, MB.IR_TOTAL), ok=MB.u32(ir, MB.IR_OK), ng=MB.u32(ir, MB.IR_NG),
                         recheck=MB.u32(ir, MB.IR_RECHECK), recipe=ir[MB.IR_RECIPE], uptime=ir[MB.IR_UPTIME],
                         alarm=ir[MB.IR_ALARM], flags=[n for bit, n in MB.FLAGS if ir[MB.IR_FLAGS] & bit],
                         wo_active=ir[IR_WO_ACTIVE], wo_unacked=ir[IR_WO_UNACKED], wo_level=ir[IR_WO_LEVEL],
                         map=ir[MB.IR_VERSION])
        rows = []
        for p in reversed(parts[-20:]):
            img = p.get('image', '')
            rows.append({'time': time.strftime('%H:%M:%S', time.localtime(p['seen'])), 'track': p.get('track'),
                         'label': LABELS.get(p.get('label'), p.get('label')), 'conf': p.get('conf'),
                         'disp': DISP.get(p.get('disp'), p.get('disp')), 'dest': DEST.get(p.get('dest'), p.get('dest')),
                         'why': p.get('why'), 'od': p.get('size_mm') if p.get('meas_valid') == '1' else '',
                         'img': ('/img/%s/%s' % (urllib.parse.quote(p['session']), urllib.parse.quote(img))) if img else ''})
        orders = self.store.orders(80)
        for o in orders:
            o['first_text'] = wall_text(o['wall_first'], o['first_seen'])
            o['last_text'] = wall_text(o['wall_last'], o['last_seen'])
        return {'board': board, 'feed': f, 'orders': orders, 'parts': rows, 'events': self.store.events(25),
                'new_orders': new, 'host': self.a.host, 'now': time.strftime('%Y-%m-%d %H:%M:%S')}


# ------------------------------------------------------------------ HTTP
PAGE = r'''<!doctype html><html lang="zh-CN"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1"><title>远程运维站</title>
<style>
:root{--bg:#fafafa;--panel:#fff;--line:#e2e2e2;--text:#222;--dim:#777;--red:#c62828;--orange:#e07000;--green:#2e7d32;--blue:#1565c0}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--text);font:14px/1.5 "Microsoft YaHei","PingFang SC",sans-serif}
header{display:flex;flex-wrap:wrap;gap:12px;align-items:center;padding:10px 16px;background:var(--panel);border-bottom:1px solid var(--line)}
h1{font-size:18px;margin:0 12px 0 0}.chip{padding:2px 10px;border-radius:12px;background:#eee;color:var(--dim);white-space:nowrap}
.on{background:#e6f4ea;color:var(--green)}.off{background:#fde8e8;color:var(--red)}
main{display:grid;grid-template-columns:minmax(0,3fr) minmax(0,2fr);gap:14px;padding:14px 16px}
@media(max-width:1000px){main{grid-template-columns:1fr}}
section{background:var(--panel);border:1px solid var(--line);border-radius:8px;padding:12px 14px;min-width:0}
h2{font-size:15px;margin:0 0 8px}table{width:100%;border-collapse:collapse}th,td{text-align:left;padding:5px 6px;border-bottom:1px solid var(--line);vertical-align:top}
th{color:var(--dim);font-weight:normal;white-space:nowrap}.wrap{overflow-x:auto}
.L3{color:var(--red);font-weight:bold}.L2{color:var(--orange);font-weight:bold}.L1{color:var(--dim)}
.st-open{color:var(--red)}.st-acked{color:var(--blue)}.st-recovered{color:var(--orange)}.st-closed{color:var(--dim)}
tr.closed td{color:#999}.adv{color:var(--blue);font-size:12px}button{font:inherit;padding:4px 12px;border:1px solid #bbb;border-radius:5px;background:#f4f4f4;cursor:pointer}
button:hover{background:#e8e8e8}button.pri{background:var(--blue);color:#fff;border-color:var(--blue)}
.kpi{display:flex;flex-wrap:wrap;gap:18px;margin:6px 0 10px}.kpi div{min-width:70px}.kpi b{display:block;font-size:24px}
#banner{display:none;padding:10px 16px;color:#fff;font-weight:bold}.ctl{display:flex;flex-wrap:wrap;gap:6px;align-items:center}
img.th{width:56px;height:56px;object-fit:cover;border:1px solid var(--line);border-radius:4px}.msg{color:var(--dim);margin-left:8px}
</style></head><body>
<div id="banner"></div>
<header><h1>DragonEyeSorter 远程运维站</h1><span id="c-feed" class="chip">记录通道</span><span id="c-mb" class="chip">Modbus</span>
<span id="c-state" class="chip">线体</span><span id="c-time" class="chip">时钟</span><span class="chip" id="c-host"></span>
<span style="flex:1"></span><a href="/api/report.csv">导出运维报表（CSV）</a></header>
<main><div>
<section><h2>运维工单 <span id="wo-sum" class="msg"></span></h2>
<div class="ctl" style="margin-bottom:8px"><label><input type="checkbox" id="only-open" checked> 只看未关闭</label>
<button onclick="ackAll()">全部确认</button><span id="ack-msg" class="msg"></span></div>
<div class="wrap"><table><thead><tr><th>编号</th><th>首次发生</th><th>级别</th><th>设备</th><th>故障 / 详情</th><th>次数</th><th>状态</th><th></th></tr></thead>
<tbody id="orders"></tbody></table></div></section>
<section style="margin-top:14px"><h2>最近零件（不良品带图）</h2><div class="wrap"><table><thead><tr><th>时间</th><th>件号</th><th>类别</th><th>置信度</th><th>处置</th><th>原因</th><th>外径</th><th>图</th></tr></thead>
<tbody id="parts"></tbody></table></div></section></div>
<div><section><h2>线体</h2><div class="kpi" id="kpi"></div>
<div class="ctl"><button class="pri" onclick="cmd('start')">启动</button><button onclick="cmd('stop')">停止</button>
<button onclick="cmd('pause')">暂停</button><button onclick="cmd('resume')">恢复</button><button onclick="cmd('reset')">复位报警</button></div>
<div class="ctl" style="margin-top:8px">带速 <input id="spd" type="number" min="5" max="200" step="5" style="width:80px"> mm/s
<button onclick="speed()">设定</button><span id="cmd-msg" class="msg"></span></div>
<div class="msg" id="flags" style="margin:8px 0 0"></div></section>
<section style="margin-top:14px"><h2>运维站记录</h2><table><tbody id="events"></tbody></table></section></div></main>
<script>
let lastNew=-1, audio=null;
document.addEventListener('click',()=>{if(!audio){try{audio=new AudioContext()}catch(e){}}});
function beep(){if(!audio)return;const o=audio.createOscillator(),g=audio.createGain();o.frequency.value=880;o.connect(g);g.connect(audio.destination);g.gain.value=0.15;o.start();o.stop(audio.currentTime+0.25)}
function esc(s){return String(s==null?'':s).replace(/[&<>"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]))}
async function post(url,body){const r=await fetch(url,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)});return r.json()}
async function cmd(c){if(c=='start'&&!confirm('远程启动产线？请确认现场无人在皮带和机械臂旁。'))return;const r=await post('/api/cmd',{cmd:c});document.getElementById('cmd-msg').textContent=r.text}
async function speed(){const r=await post('/api/speed',{mm_s:document.getElementById('spd').value});document.getElementById('cmd-msg').textContent=r.text}
async function ack(uid){const r=await post('/api/ack',{uid:uid});document.getElementById('ack-msg').textContent=r.text;refresh()}
async function ackAll(){const r=await post('/api/ack_all',{});document.getElementById('ack-msg').textContent=r.text;refresh()}
function chip(id,ok,text){const e=document.getElementById(id);e.className='chip '+(ok?'on':'off');e.textContent=text}
async function refresh(){let s;try{s=await (await fetch('/api/state')).json()}catch(e){chip('c-feed',false,'运维站程序没在运行');return}
 const b=s.board,f=s.feed;chip('c-feed',f.connected,'记录通道 '+(f.connected?'在线 · '+f.records+' 件':'离线'));
 chip('c-mb',b.connected,'Modbus '+(b.connected?'在线':'离线'));document.getElementById('c-host').textContent='板子 '+s.host;
 const st=document.getElementById('c-state');st.textContent='线体 '+(b.state||'—');st.className='chip '+(b.state_code==1?'on':b.state_code>=3?'off':'');
 chip('c-time',b.time_synced,b.time_synced?'板子已对时':'板子未对时');
 const y=b.total?(100*b.ok/b.total).toFixed(1)+'%':'—';
 document.getElementById('kpi').innerHTML=b.connected?[['带速',b.speed+' mm/s'],['总数',b.total],['良品',b.ok],['不良',b.ng],['复检',b.recheck],['良率',y],['配方',b.recipe],['运行',Math.floor(b.uptime/60)+' 分']].map(k=>'<div><span class="msg" style="margin:0">'+k[0]+'</span><b>'+esc(k[1])+'</b></div>').join(''):'<span class="msg">板子没连上</span>';
 document.getElementById('flags').textContent=b.flags?('标志：'+b.flags.join('、')+(b.alarm?' · 报警代码 '+b.alarm:'')):'';
 const only=document.getElementById('only-open').checked;let open=0,faults=0;
 document.getElementById('orders').innerHTML=s.orders.filter(o=>!only||o.status!='closed').map(o=>{if(o.status!='closed'){open++;if(o.level==3&&o.status!='recovered')faults++}
  return '<tr class="'+(o.status=='closed'?'closed':'')+'"><td>'+esc(o.id)+'</td><td>'+esc(o.first_text)+'</td><td class="L'+o.level+'">'+esc(o.level_cn)+'</td><td>'+esc(o.device)+'</td><td><b>'+esc(o.title)+'</b><br>'+esc(o.detail)+'<div class="adv">处理建议：'+esc(o.advice)+'</div></td><td>'+esc(o.count)+'</td><td class="st-'+o.status+'">'+esc(o.status_cn)+(o.acked_by?'<br><span class="msg" style="margin:0">'+esc(o.acked_by)+'</span>':'')+'</td><td>'+(o.status=='open'||o.status=='recovered'?'<button onclick="ack(\''+o.uid+'\')">确认</button>':'')+'</td></tr>'}).join('')||'<tr><td colspan="8" class="msg">没有工单：设备运行正常</td></tr>';
 document.getElementById('wo-sum').textContent='未关闭 '+open+(b.connected?' · 板上待确认 '+b.wo_unacked:'');
 document.getElementById('parts').innerHTML=s.parts.map(p=>'<tr><td>'+esc(p.time)+'</td><td>'+esc(p.track)+'</td><td>'+esc(p.label)+'</td><td>'+esc(p.conf)+'</td><td>'+esc(p.disp)+' → '+esc(p.dest)+'</td><td>'+esc(p.why)+'</td><td>'+esc(p.od)+'</td><td>'+(p.img?'<a href="'+p.img+'" target="_blank"><img class="th" src="'+p.img+'"></a>':'')+'</td></tr>').join('')||'<tr><td colspan="8" class="msg">还没收到零件记录</td></tr>';
 document.getElementById('events').innerHTML=s.events.map(e=>'<tr><td class="msg" style="white-space:nowrap;margin:0">'+new Date(e.t*1000).toLocaleTimeString()+'</td><td>'+esc(e.text)+'</td></tr>').join('');
 const bn=document.getElementById('banner'),top=s.orders.find(o=>o.status=='open');
 if(top){bn.style.display='block';bn.style.background=top.level==3?'#c62828':'#e07000';bn.textContent='待处理工单 #'+top.id+'：'+top.title+' —— '+top.detail}else bn.style.display='none';
 document.title=(open?'('+open+') ':'')+'远程运维站';if(lastNew>=0&&s.new_orders>lastNew)beep();lastNew=s.new_orders}
refresh();setInterval(refresh,1000);
</script></body></html>'''


def make_handler(station):
    class H(BaseHTTPRequestHandler):
        def log_message(self, *args):   # quiet: the page polls every second
            pass

        def send(self, code, body, ctype):
            self.send_response(code)
            self.send_header('Content-Type', ctype)
            self.send_header('Content-Length', str(len(body)))
            self.send_header('Cache-Control', 'no-store')
            self.end_headers()
            self.wfile.write(body)

        def js(self, obj, code=200):
            self.send(code, json.dumps(obj, ensure_ascii=False).encode('utf-8'), 'application/json; charset=utf-8')

        def do_GET(self):
            path = urllib.parse.unquote(urllib.parse.urlparse(self.path).path)
            if path == '/':
                self.send(200, PAGE.encode('utf-8'), 'text/html; charset=utf-8')
            elif path == '/api/state':
                self.js(station.state())
            elif path == '/api/report.csv':
                body = station.store.report_csv()
                self.send_response(200)
                self.send_header('Content-Type', 'text/csv; charset=utf-8')
                self.send_header('Content-Disposition', 'attachment; filename="ops_report_%s.csv"' % time.strftime('%Y%m%d_%H%M'))
                self.send_header('Content-Length', str(len(body)))
                self.end_headers()
                self.wfile.write(body)
            elif path.startswith('/img/'):
                root = os.path.normpath(os.path.join(station.a.out, 'img'))
                p = os.path.normpath(os.path.join(root, path[5:]))
                if p.startswith(root + os.sep) and os.path.isfile(p):
                    with open(p, 'rb') as f:
                        self.send(200, f.read(), 'image/jpeg')
                else:
                    self.send(404, b'not found', 'text/plain')
            else:
                self.send(404, b'not found', 'text/plain')

        def do_POST(self):
            try:
                n = int(self.headers.get('Content-Length', 0))
                body = json.loads(self.rfile.read(n) or b'{}')
            except ValueError:
                return self.js({'ok': False, 'text': '请求格式不对'}, 400)
            path = urllib.parse.urlparse(self.path).path
            if path == '/api/cmd' and body.get('cmd') in MB.CMDS:
                ok, text = station.command('cmd', body['cmd'])
            elif path == '/api/speed':
                ok, text = station.command('speed', body.get('mm_s', 0))
            elif path == '/api/ack':
                ok, text = station.ack(str(body.get('uid', '')))
            elif path == '/api/ack_all':
                ok, text = station.command('ack', 0xFFFF)
                for o in station.store.orders(500):   # the station's own orders too
                    if o['source'] == 'station' and o['status'] in ('open', 'recovered'):
                        station.ack(o['uid'])
            else:
                return self.js({'ok': False, 'text': '未知请求'}, 404)
            self.js({'ok': ok, 'text': text})
    return H


def main():
    ap = argparse.ArgumentParser(description='远程运维站：接收板子的运维工单与检测记录，网页看板 + 远程确认 / 启停')
    ap.add_argument('--host', default=os.environ.get('BOARD_IP') or repo_env('BOARD_IP') or '192.168.137.100')
    ap.add_argument('--src', default=None, help='本机源地址（开着 Clash TUN 时填有线网口地址，如 192.168.137.1）')
    ap.add_argument('--feed-port', type=int, default=9500)
    ap.add_argument('--mb-port', type=int, default=502)
    ap.add_argument('--bind', default='127.0.0.1', help='看板监听地址（0.0.0.0 = 局域网可访问）')
    ap.add_argument('--http', type=int, default=8090)
    ap.add_argument('--out', default=os.path.join(HERE, 'ops_data'), help='工单库 ops.db 与收到的图片')
    a = ap.parse_args()
    st = Station(a)
    threading.Thread(target=st.feed_loop, daemon=True).start()
    threading.Thread(target=st.mb_loop, daemon=True).start()
    srv = ThreadingHTTPServer((a.bind, a.http), make_handler(st))
    st.log('远程运维站启动：板子 %s，看板 http://%s:%d/' % (a.host, '127.0.0.1' if a.bind == '0.0.0.0' else a.bind, a.http))
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        st.stop = True
        srv.server_close()


if __name__ == '__main__':
    main()
