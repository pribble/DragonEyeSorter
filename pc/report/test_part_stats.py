#!/usr/bin/env python3
"""test_part_stats.py — part_stats.py 的报告链路冒烟（无板、纯标准库）：45 列真实格式（part_log.cpp 的
parts.csv 表头）的三条记录，实跑出 Markdown + JSON，断言关键数字；再用坏 label_list.json 跑一遍，
断言优雅降级（类别表读不出来时按无类别出报告，不裸栈）。退出码 = 失败项数。

    python pc/report/test_part_stats.py
"""
import json
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
STATS = os.path.join(HERE, 'part_stats.py')
LABELS = os.path.join(HERE, '..', '..', 'scripts', 'deploy', 'out', 'label_list.json')

fail = 0


def check(cond, what):
    global fail
    print(('ok   ' if cond else 'FAIL ') + what)
    if not cond:
        fail += 1


COLS = ("wall_ms,t_s,track,frame_first,frame_last,cls,label,conf,disp,dest,why,"
        "meas_valid,kind,size_mm,bore_mm,ecc_mm,pitch_mm,teeth,short,missing,flash,teeth_judged,smear_mm,"
        "axis_dist_mm,meas_frame,meas_count,s_mm,y_mm,axis_deg,len_mm,wid_mm,pl_area_px,pl_d_mm,pl_w_mm,pl_h_mm,"
        "image,votes,npu_ms,lat_ms,recipe,hue_deg,sat,color,beam,beam_err_mm").split(',')
# part_log 恒写 %.3f 数值：未测量件是 0，不是空串
BASE = {c: '0.000' for c in COLS}
BASE.update({'kind': '-', 'teeth_judged': '0', 'meas_valid': '0', 'axis_dist_mm': '-1.0', 'meas_count': '0', 'votes': '0'})
ROWS = [
    dict(track=1, cls=0, label='gear_ok', conf=0.93, disp=0, dest=0, why='picked', meas_valid=1, kind='g',
         size_mm=32.01, bore_mm=6.02, ecc_mm=0.05, teeth=30, recipe=1, beam='ok'),
    dict(track=2, cls=1, label='gear_short_l', conf=0.88, disp=1, dest=1, why='class', meas_valid=1, kind='g',
         size_mm=31.98, teeth=30, short=1, recipe=1, beam='ok'),
    dict(track=3, cls=-1, label='', conf=0, disp=2, dest=0, why='unclassified', recipe=1),
]


def write_csv(path):
    with open(path, 'w', encoding='utf-8', newline='') as f:
        f.write(','.join(COLS) + '\n')
        for r in ROWS:
            base = dict(BASE)
            base.update({k: str(v) for k, v in r.items()})
            f.write(','.join(base.get(c, '') for c in COLS) + '\n')


def run(args):
    return subprocess.run([sys.executable, STATS] + args, capture_output=True, text=True).returncode


def main():
    d = tempfile.mkdtemp(prefix='part_stats_test_')
    csv = os.path.join(d, 'parts.csv')
    write_csv(csv)
    md = os.path.join(d, 'report.md')
    js = os.path.join(d, 'report.json')

    rc = run([csv, '--labels', LABELS, '--md', md, '--json', js])
    check(rc == 0, 'report run exits 0 (rc %s%s)' % (rc, ': ' + _tail(md) if rc else ''))
    ok_md = ok_js = False
    if rc == 0:
        text = open(md, encoding='utf-8').read()
        ok_md = '记录件数|3' in text.replace(' ', '').replace(chr(10), '|') and '运行概况' in text
        data = json.load(open(js, encoding='utf-8'))
        ok_js = data.get('records') == 3
    check(ok_md, 'markdown has the summary tables')
    check(ok_js, 'json records = 3')

    bad = os.path.join(d, 'bad_labels.json')
    with open(bad, 'w', encoding='utf-8') as f:
        f.write('{not json at all')
    rc2 = run([csv, '--labels', bad, '--md', os.path.join(d, 'report2.md')])
    check(rc2 == 0, 'a corrupt label_list degrades gracefully (rc %s)' % rc2)
    return 1 if fail else 0


if __name__ == '__main__':
    sys.exit(main())
