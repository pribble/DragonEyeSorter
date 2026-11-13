#!/usr/bin/env python3
"""fake_board.py: 冒充板上的 4G 上报程序 des_4g，往 MQTT 服务器发与它格式相同的状态和工单，
用来在没有 4G 模组时预演手机看板 des_4g_view.html。只用标准库。状态里带 "sim": true，看板会标「模拟数据」。

    python fake_board.py --prefix des/xxxxxxxx [--key 操作密钥] [--host broker.emqx.io] [--port 1883] [--period 2] [--seconds 0]

带 --key 时状态里会列出可用的远程命令（确认工单 / 暂停 / 停止 / 继续），看板出现「远程操作」一栏；
命令的签名规则与 des_4g 相同：sig = HMAC-SHA256(key, "<nonce>|<seq>|<cmd>|<arg>")，seq 必须比上一次大。

剧本（90 s 一圈，循环）：正常过件 → 15 s 警告工单 → 30 s 网线断开 → 45 s 网线恢复、警告恢复
→ 60 s 故障工单并停线 → 75 s 两张工单关闭、恢复过件。看板上的「测往返时间」会得到回答。
Ctrl+C 退出时发 offline；被强行杀掉时由服务器代发遗嘱 offline。
"""
import argparse
import hashlib
import hmac
import json
import random
import select
import socket
import sys
import time


# ---------------------------------------------------------------- MQTT 3.1.1, QoS 0 only
def mq_str(s):
    b = s.encode('utf-8')
    return bytes([len(b) >> 8, len(b) & 255]) + b


def mq_packet(head, body):
    n, out = len(body), bytearray([head])
    while True:
        d, n = n % 128, n // 128
        out.append(d | (128 if n else 0))
        if not n:
            return bytes(out) + bytes(body)


def mq_parse(buf):
    """One packet from the front of buf -> (type, flags, body, size), or None while incomplete."""
    if len(buf) < 2:
        return None
    n, mul, i = 0, 1, 1
    while True:
        if i >= len(buf):
            return None
        if i > 4:
            raise ValueError('bad remaining length')
        n += (buf[i] & 127) * mul
        mul *= 128
        if not buf[i] & 128:
            break
        i += 1
    start = i + 1
    if len(buf) < start + n:
        return None
    return buf[0] >> 4, buf[0] & 15, bytes(buf[start:start + n]), start + n


def mq_connect(client_id, keepalive, will_topic, will_msg):
    flags = 0x02 | 0x04 | 0x20   # clean session, will, will retained (will QoS 0)
    return mq_packet(0x10, mq_str('MQTT') + bytes([4, flags, keepalive >> 8, keepalive & 255]) +
                     mq_str(client_id) + mq_str(will_topic) + mq_str(will_msg))


def mq_publish(topic, text, retain=False):
    return mq_packet(0x31 if retain else 0x30, mq_str(topic) + text.encode('utf-8'))


def mq_subscribe(pid, topic):
    return mq_packet(0x82, bytes([pid >> 8, pid & 255]) + mq_str(topic) + b'\x00')


class Mqtt:
    def __init__(self, host, port, client_id, will_topic, keepalive=30):
        self.sock = socket.create_connection((host, port), timeout=10)
        self.buf = bytearray()
        self.keepalive = keepalive
        self.last_tx = time.monotonic()
        self.sock.sendall(mq_connect(client_id, keepalive, will_topic, 'offline'))
        ack = self._wait(2)
        if ack is None or ack[2][1:2] != b'\x00':
            raise OSError('broker refused the connection')
        self.sock.settimeout(None)

    def _wait(self, ptype, timeout=10):
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            p = mq_parse(self.buf)
            if p:
                del self.buf[:p[3]]
                if p[0] == ptype:
                    return p
                continue
            if select.select([self.sock], [], [], 0.2)[0]:
                d = self.sock.recv(4096)
                if not d:
                    return None
                self.buf += d
        return None

    def send(self, packet):
        self.sock.sendall(packet)
        self.last_tx = time.monotonic()

    def publish(self, topic, text, retain=False):
        self.send(mq_publish(topic, text, retain))

    def subscribe(self, topic):
        self.send(mq_subscribe(1, topic))

    def poll(self, timeout):
        """Wait up to timeout s; return the PUBLISH messages that arrived as [(topic, text)]."""
        out = []
        if select.select([self.sock], [], [], timeout)[0]:
            d = self.sock.recv(4096)
            if not d:
                raise OSError('broker closed the connection')
            self.buf += d
        while True:
            p = mq_parse(self.buf)
            if not p:
                break
            del self.buf[:p[3]]
            if p[0] == 3:
                tl = p[2][0] << 8 | p[2][1]
                off = 2 + tl + (2 if p[1] & 6 else 0)
                out.append((p[2][2:2 + tl].decode('utf-8', 'replace'), p[2][off:].decode('utf-8', 'replace')))
        if time.monotonic() - self.last_tx > self.keepalive / 2:
            self.send(b'\xc0\x00')
        return out

    def close(self):
        try:
            self.sock.sendall(b'\xe0\x00')
        finally:
            self.sock.close()


# ---------------------------------------------------------------- the pretend line
NG = [('gear_short_l', 'scrap'), ('gear_short_h', 'scrap'), ('gear_dirty_l', 'scrap'), ('gear_dirty_h', 'scrap'),
      ('gear_flash_l', 'rework'), ('gear_flash_h', 'rework')]
ORDERS = {
    'light': dict(code=4, key='light', level=2, level_cn='警告', device='相机 / 光源', title='画面亮度异常', stop=False,
                  detail='画面平均亮度降到基准的 48%', advice='检查灯条是否点亮、镜头是否被遮挡'),
    'belt': dict(code=2, key='belt', level=3, level_cn='故障', device='传送带', title='皮带速度异常（卡滞 / 打滑）', stop=True,
                 detail='步进指令 60 mm/s，计米轮实测 3 mm/s', advice='检查皮带是否卡住、计米轮是否压紧，排除后复位报警'),
}
STATUS_CN = {'open': '待处理', 'acked': '处理中', 'recovered': '已恢复待确认', 'closed': '已关闭'}


class Board:
    """State of the pretend line; step(t) advances the 90 s script and returns the work-order changes to publish."""

    CMDS = ['ack', 'pause', 'stop', 'resume']

    def __init__(self, seed=None, key=''):
        self.rng = random.Random(seed)
        self.key = key
        self.nonce = '%016x' % random.getrandbits(64)
        self.last_seq = 0
        self.run_state = 1        # what the operator asked for: 1 run, 2 paused, 0 stopped
        self.session = time.strftime('%Y%m%d-%H%M%S')
        self.total = self.ok = self.ng = self.recheck = 0
        self.last = None
        self.eth = 1
        self.orders = {}          # id -> dict
        self.next_id = 1
        self.ids = {}             # 'light' / 'belt' -> id of this lap
        self.lap = -1
        self.done = set()
        self.stopped = False
        self.published = 0

    def _order(self, kind, status):
        if status == 'open':
            self.ids[kind] = self.next_id
            self.next_id += 1
        oid = self.ids[kind]
        now = int(time.time() * 1000)
        o = self.orders.get(oid) or dict(ORDERS[kind], id=oid, count=1, wall_first=now, wall_clear=0, wall_ack=0,
                                         t_first=0, acked_by='', session=self.session)
        o.update(status=status, status_cn=STATUS_CN[status], wall_last=now)
        if status in ('recovered', 'closed'):
            o['wall_clear'] = now
        if status == 'closed':
            o.update(wall_ack=now, acked_by='模拟')
        self.orders[oid] = o
        return json.dumps(o, ensure_ascii=False)

    def step(self, t, parts):
        lap, at = divmod(t, 90.0)
        if int(lap) != self.lap:
            self.lap, self.done = int(lap), set()
        out = []
        script = [(15, lambda: out.append(self._order('light', 'open'))),
                  (30, lambda: setattr(self, 'eth', 0)),
                  (45, lambda: (setattr(self, 'eth', 1), out.append(self._order('light', 'recovered')))),
                  (60, lambda: (setattr(self, 'stopped', True), out.append(self._order('belt', 'open')))),
                  (75, lambda: (setattr(self, 'stopped', False), out.append(self._order('light', 'closed')),
                                out.append(self._order('belt', 'closed'))))]
        for when, act in script:
            if at >= when and when not in self.done:
                self.done.add(when)
                act()
        for _ in range(0 if self.stopped or self.run_state != 1 else parts):
            r = self.rng.random()
            self.total += 1
            if r < 0.85:
                self.ok += 1
                self.last = dict(label='gear_ok', disp='pick', dest='gear_box', conf=round(self.rng.uniform(0.93, 0.999), 3))
            elif r < 0.95:
                self.ng += 1
                label, dest = self.rng.choice(NG)
                self.last = dict(label=label, disp='flap', dest=dest, conf=round(self.rng.uniform(0.75, 0.99), 3))
            else:
                self.recheck += 1
                self.last = dict(label=self.rng.choice(NG)[0], disp='recheck', dest='recheck', conf=round(self.rng.uniform(0.4, 0.6), 3))
            self.last['size_mm'] = round(self.rng.gauss(30.0, 0.04), 2)
        return out

    def command(self, j):
        """A message from <prefix>/cmd -> (reply dict, [work-order JSON to publish])."""
        cmd = j.get('cmd', '') if isinstance(j, dict) else ''
        reply = dict(id=j.get('id', 0) if isinstance(j, dict) else 0, cmd=cmd)
        if cmd == 'ping':
            reply['ok'] = True
            return reply, []

        def refuse(why):
            reply.update(ok=False, err=why)
            return reply, []
        if not self.key or cmd not in self.CMDS:
            return refuse('command not enabled')
        arg, seq = j.get('arg', 0), j.get('seq', 0)
        if not isinstance(arg, int) or not isinstance(seq, int):
            return refuse('bad signature')
        want = hmac.new(self.key.encode(), ('%s|%d|%s|%d' % (self.nonce, seq, cmd, arg)).encode(), hashlib.sha256).hexdigest()
        if not hmac.compare_digest(want, str(j.get('sig', ''))):
            return refuse('bad signature')
        if seq <= self.last_seq:
            return refuse('replayed')
        self.last_seq = seq
        out = []
        if cmd == 'ack':
            if not 1 <= arg <= 65535:
                return refuse('bad argument')
            now = int(time.time() * 1000)
            for oid, o in sorted(self.orders.items()):
                if (arg == 65535 or arg == oid) and o['status'] in ('open', 'recovered'):
                    st = 'acked' if o['status'] == 'open' else 'closed'
                    o.update(status=st, status_cn=STATUS_CN[st], wall_ack=now, wall_last=now, acked_by='4G')
                    out.append(json.dumps(o, ensure_ascii=False))
        else:
            self.run_state = {'pause': 2, 'stop': 0, 'resume': 1}[cmd]
        reply.update(ok=True, result=0)
        return reply, out

    def state(self, up_s):
        act = [o for o in self.orders.values() if o['status'] in ('open', 'acked')]
        top = max(act, key=lambda o: (o['level'], o['id'])) if act else None
        s = dict(online=True, sim=True, up_s=int(up_s), app=True, session=self.session, total=self.total, ok=self.ok,
                 ng=self.ng, recheck=self.recheck, eth=self.eth,
                 wo=dict(active=len(act), unacked=sum(o['status'] in ('open', 'recovered') for o in self.orders.values()),
                         level=top['level'] if top else 0, id=top['id'] if top else 0, title=top['title'] if top else ''),
                 net=dict(csq=22, dbm=-69, oper='模拟运营商', act='FDD LTE'),
                 tx=dict(ok=self.published, fail=0, conn=1), counts='app',
                 line=dict(state=4 if self.stopped else self.run_state, alarm=2 if self.stopped else 0, recipe=1,
                           speed=0.0 if self.stopped or self.run_state != 1 else 60.0))
        if self.key:
            s.update(cmds=self.CMDS, nonce=self.nonce)
        if self.last:
            s['last'] = self.last
        return json.dumps(s, ensure_ascii=False)


def run(prefix, host, port, period, seconds, out=sys.stdout, key=''):
    mq = Mqtt(host, port, 'des-sim-%06x' % random.getrandbits(24), prefix + '/state')
    mq.subscribe(prefix + '/cmd')
    board = Board(key=key)
    t0 = time.monotonic()
    print('fake board -> %s:%d  topics %s/...  (Ctrl+C to stop)' % (host, port, prefix), file=out)
    try:
        nxt = 0.0
        while not seconds or time.monotonic() - t0 < seconds:
            t = time.monotonic() - t0
            if t >= nxt:
                nxt += period
                for wo in board.step(t, max(1, round(period * 0.8))):
                    mq.publish(prefix + '/wo', wo)
                    board.published += 1
                    print('%6.1f  wo     %s' % (t, wo[:100]), file=out)
                board.published += 1
                mq.publish(prefix + '/state', board.state(t), retain=True)
                print('%6.1f  state  total %d  eth %d' % (t, board.total, board.eth), file=out)
            for topic, text in mq.poll(max(0.0, min(0.2, nxt - (time.monotonic() - t0)))):
                if topic != prefix + '/cmd':
                    continue
                try:
                    j = json.loads(text)
                except ValueError:
                    continue
                reply, changed = board.command(j)
                mq.publish(prefix + '/reply', json.dumps(reply))
                for wo in changed:
                    mq.publish(prefix + '/wo', wo)
                if reply['ok'] and reply['cmd'] != 'ping':
                    mq.publish(prefix + '/state', board.state(time.monotonic() - t0), retain=True)   # show the effect at once
                print('%6.1f  cmd    %s -> %s' % (time.monotonic() - t0, reply['cmd'], 'ok' if reply['ok'] else 'refused (%s)' % reply.get('err')), file=out)
    except KeyboardInterrupt:
        pass
    mq.publish(prefix + '/state', 'offline', retain=True)
    mq.close()
    print('stopped (offline published)', file=out)


def main():
    ap = argparse.ArgumentParser(description='冒充板上的 des_4g，给手机看板发模拟数据')
    ap.add_argument('--prefix', required=True, help='主题前缀，与看板里填的一致，例如 des/ab12cd34')
    ap.add_argument('--host', default='broker.emqx.io')
    ap.add_argument('--port', type=int, default=1883)
    ap.add_argument('--period', type=float, default=2.0, help='多少秒发一次状态')
    ap.add_argument('--seconds', type=float, default=0, help='运行多久后退出，0 = 一直跑')
    ap.add_argument('--key', default='', help='操作密钥：给了才开放远程命令（至少 8 个字符）')
    a = ap.parse_args()
    if a.key and len(a.key) < 8:
        ap.error('--key 至少 8 个字符')
    run(a.prefix.rstrip('/'), a.host, a.port, a.period, a.seconds, key=a.key)


if __name__ == '__main__':
    main()
