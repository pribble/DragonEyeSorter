#!/usr/bin/env python3
"""part_stats.py — test-report numbers from the board's part records (des_app part_log CSV, 40 columns).

Copies the CSVs off the board first, e.g.
    scp -r -i ~/.ssh/id_ed25519_anlu root@192.168.137.100:/root/des/records ./records

Usage
    python part_stats.py records/2026-10-20/parts_*.csv                      # whole run: throughput, dispositions, AI time
    python part_stats.py run_gear_ok.csv=gear_ok run_gear_flash.csv=gear_flash   # one known class per file -> accuracy
    python part_stats.py --truth gear_ok a.csv b.csv                         # same, one class for all files
    python part_stats.py --fed 200 run.csv                                   # 200 parts were put on the belt -> miss rate
    python part_stats.py --same-part --true-size 31.98 loop.csv              # one sample passed again and again:
                                                                             # repeatability + error vs the caliper
    python part_stats.py --md report.md --json report.json ...               # also write the tables to files

Only the standard library; Python 3.8+. Labels / verdicts come from models/label_list.json (the single source).
"""
import argparse
import csv
import glob
import json
import math
import os
import statistics
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
LABELS = os.path.join(HERE, '..', '..', 'models', 'label_list.json')


def load_labels(path):
    try:
        with open(path, encoding='utf-8') as f:
            return {l['label']: l for l in json.load(f)['labels']}
    except OSError:
        return {}


def num(row, key, default=None):
    try:
        v = row.get(key, '')
        return float(v) if v not in ('', None) else default
    except ValueError:
        return default


def pct(values, q):
    """q-th percentile (0..100), linear interpolation."""
    if not values:
        return float('nan')
    s = sorted(values)
    k = (len(s) - 1) * q / 100.0
    lo, hi = int(math.floor(k)), int(math.ceil(k))
    return s[lo] + (s[hi] - s[lo]) * (k - lo)


def dist(values, unit='', digits=2):
    if not values:
        return {'n': 0}
    d = {'n': len(values), 'mean': statistics.fmean(values), 'p50': pct(values, 50), 'p95': pct(values, 95),
         'max': max(values), 'min': min(values)}
    if len(values) > 1:
        d['std'] = statistics.stdev(values)
    d['unit'] = unit
    d['digits'] = digits
    return d


def fmt_dist(d):
    if not d.get('n'):
        return '—'
    g = '%.' + str(d['digits']) + 'f'
    s = ('均值 ' + g + ' / p50 ' + g + ' / p95 ' + g + ' / 最大 ' + g) % (d['mean'], d['p50'], d['p95'], d['max'])
    if 'std' in d:
        s += (' / σ ' + g) % d['std']
    return s + ' ' + d['unit'] + '（n=%d）' % d['n']


def cpk(values, nominal, tol):
    if len(values) < 2 or tol <= 0:
        return None
    m, s = statistics.fmean(values), statistics.stdev(values)
    if s == 0:
        return float('inf')
    return min(nominal + tol - m, m - (nominal - tol)) / (3 * s)


def read_rows(spec_list, truth_all):
    rows = []
    for spec in spec_list:
        path, truth = spec, truth_all
        if '=' in spec and not os.path.exists(spec):
            path, truth = spec.rsplit('=', 1)
        files = sorted(glob.glob(path)) or [path]
        for fp in files:
            with open(fp, encoding='utf-8', newline='') as f:
                for r in csv.DictReader(f):
                    r['_file'] = os.path.basename(fp)
                    r['_truth'] = truth
                    rows.append(r)
    return rows


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('csv', nargs='+', help='parts_*.csv (glob ok); FILE=LABEL gives the true class of every part in it')
    ap.add_argument('--truth', help='true class of every part in all files (label from label_list.json)')
    ap.add_argument('--fed', type=int, help='parts actually put on the belt (miss rate = 1 - records / fed)')
    ap.add_argument('--same-part', action='store_true', help='every record is the same physical sample (repeatability)')
    ap.add_argument('--true-size', type=float, help='caliper value of that sample (gear OD / rack length, mm)')
    ap.add_argument('--gear-od', type=float, default=32.0)
    ap.add_argument('--gear-tol', type=float, default=0.15)
    ap.add_argument('--rack-len', type=float, default=50.0)
    ap.add_argument('--rack-tol', type=float, default=0.5)
    ap.add_argument('--labels', default=LABELS, help='label_list.json')
    ap.add_argument('--md', help='write the Markdown tables here')
    ap.add_argument('--json', help='write the numbers here')
    a = ap.parse_args()
    try:
        sys.stdout.reconfigure(encoding="utf-8")   # Windows console: GBK by default
    except (AttributeError, ValueError):
        pass

    labels = load_labels(a.labels)
    rows = read_rows(a.csv, a.truth)
    if not rows:
        sys.exit('no records')
    out = {'files': sorted({r['_file'] for r in rows}), 'records': len(rows)}
    md = []

    # ---- run overview
    span = {}
    for r in rows:
        f = r['_file']
        v = num(r, 't_s', 0.0)
        lo, hi = span.get(f, (v, v))
        span[f] = (min(lo, v), max(hi, v))
    run_s = sum(hi - lo for lo, hi in span.values())
    out['run_s'] = run_s
    out['parts_per_min'] = len(rows) / run_s * 60 if run_s > 0 else None
    disp = {}
    dest = {}
    for r in rows:
        disp[r['disp']] = disp.get(r['disp'], 0) + 1
        dest[r['dest']] = dest.get(r['dest'], 0) + 1
    out['disp'] = disp
    out['dest'] = dest
    md.append('## 运行概况\n')
    md.append('| 项 | 值 |\n|---|---|')
    md.append('| 记录文件 | %s |' % ', '.join(out['files']))
    md.append('| 记录件数 | %d |' % len(rows))
    md.append('| 记录时间跨度 | %.1f s（首件到末件，各文件相加） |' % run_s)
    if out['parts_per_min']:
        md.append('| 节拍 | %.1f 件/min |' % out['parts_per_min'])
    md.append('| 处置 | %s |' % '，'.join('%s %d' % kv for kv in sorted(disp.items())))
    md.append('| 去向 | %s |' % '，'.join('%s %d' % kv for kv in sorted(dest.items())))
    if a.fed:
        miss = 1 - len(rows) / a.fed
        out['fed'] = a.fed
        out['miss_rate'] = miss
        md.append('| 投放 / 记录 | %d / %d → 漏检率 %.2f%%%s |' % (a.fed, len(rows), max(miss, 0) * 100,
                                                          '（记录多于投放：有重复计数）' if miss < 0 else ''))
    whys = {}
    for r in rows:
        if r['disp'] != 'pick':
            whys[r['why']] = whys.get(r['why'], 0) + 1
    if whys:
        md.append('| 非良品原因 | %s |' % '，'.join('%s %d' % kv for kv in sorted(whys.items(), key=lambda kv: -kv[1])[:8]))
    md.append('')

    # ---- AI time / latency (records with NPU votes)
    voted = [r for r in rows if num(r, 'votes', 0) > 0]
    out['npu_ms'] = dist([num(r, 'npu_ms') for r in voted], 'ms')
    out['lat_ms'] = dist([num(r, 'lat_ms') for r in voted], 'ms', 1)
    out['votes'] = dist([num(r, 'votes') for r in voted], '次', 1)
    md.append('## AI 推理\n')
    md.append('| 指标 | 值 |\n|---|---|')
    md.append('| 有 NPU 结果的件 | %d / %d |' % (len(voted), len(rows)))
    md.append('| NPU 单次推理（每件平均） | %s |' % fmt_dist(out['npu_ms']))
    md.append('| 帧采集完成 → 分类结果可用 | %s |' % fmt_dist(out['lat_ms']))
    md.append('| 每件投票次数 | %s |' % fmt_dist(out['votes']))
    md.append('')

    # ---- accuracy against the known class
    truth_rows = [r for r in rows if r['_truth']]
    if truth_rows:
        conf = {}
        ok = ok_bin = judged = 0
        for r in truth_rows:
            pred = r['label']
            conf.setdefault(r['_truth'], {})
            conf[r['_truth']][pred] = conf[r['_truth']].get(pred, 0) + 1
            if pred == 'unclassified':
                continue
            judged += 1
            ok += pred == r['_truth']
            tv = labels.get(r['_truth'], {}).get('verdict')
            pv = labels.get(pred, {}).get('verdict')
            ok_bin += tv is not None and tv == pv
        out['accuracy'] = ok / judged if judged else None
        out['accuracy_okng'] = ok_bin / judged if judged else None
        out['unclassified'] = len(truth_rows) - judged
        out['confusion'] = conf
        md.append('## 分类准确率（已知类别）\n')
        md.append('| 指标 | 值 |\n|---|---|')
        if judged:
            md.append('| 多类准确率 | **%.2f%%**（%d / %d） |' % (ok / judged * 100, ok, judged))
            md.append('| OK / NG 准确率 | **%.2f%%** |' % (ok_bin / judged * 100))
        md.append('| 未分类（无 NPU 结果 / 置信度低 → 复检） | %d |' % (len(truth_rows) - judged))
        preds = sorted({p for c in conf.values() for p in c})
        md.append('\n| 真值 \\ 预测 | ' + ' | '.join(preds) + ' |')
        md.append('|---' * (len(preds) + 1) + '|')
        for tr in sorted(conf):
            md.append('| %s | ' % tr + ' | '.join(str(conf[tr].get(p, '')) for p in preds) + ' |')
        md.append('')

    # ---- geometric measurement
    md.append('## 几何测量\n')
    md.append('| 品种 | 量 | 统计 | Cpk |\n|---|---|---|---|')
    for kind, name, nom, tol in (('g', '齿轮外径', a.gear_od, a.gear_tol), ('r', '齿条长度', a.rack_len, a.rack_tol)):
        vals = [num(r, 'size_mm') for r in rows if r.get('kind') == kind and r.get('meas_valid') == '1']
        if not vals:
            continue
        d = dist(vals, 'mm', 3)
        c = cpk(vals, nom, tol)
        out['size_' + kind] = d
        out['cpk_' + kind] = c
        md.append('| %s | 尺寸（规格 %.2f±%.2f） | %s | %s |' % (name, nom, tol, fmt_dist(d), '%.2f' % c if c else '—'))
        if kind == 'g':
            bores = [num(r, 'bore_mm') for r in rows if r.get('kind') == 'g' and num(r, 'bore_mm', 0) > 0]
            if bores:
                md.append('| %s | 孔径 | %s | |' % ('齿轮', fmt_dist(dist(bores, 'mm', 3))))
            ecc = [num(r, 'ecc_mm') for r in rows if r.get('kind') == 'g' and r.get('meas_valid') == '1']
            md.append('| 齿轮 | 偏心 | %s | |' % fmt_dist(dist(ecc, 'mm', 3)))
        judged = [r for r in rows if r.get('kind') == kind and r.get('meas_valid') == '1']
        tj = sum(r.get('teeth_judged') == '1' for r in judged)
        md.append('| %s | 判齿（离光轴近、视差拖尾小） | %d / %d | |' % (name[:2], tj, len(judged)))
    md.append('')
    if a.same_part:
        vals = [num(r, 'size_mm') for r in rows if r.get('meas_valid') == '1']
        if len(vals) > 1:
            out['repeat_std'] = statistics.stdev(vals)
            line = '**同一件重复测量**：n=%d，σ = %.4f mm，极差 %.3f mm' % (len(vals), out['repeat_std'],
                                                                        max(vals) - min(vals))
            if a.true_size:
                err = [v - a.true_size for v in vals]
                out['bias'] = statistics.fmean(err)
                out['max_abs_err'] = max(abs(e) for e in err)
                line += '；卡尺 %.3f mm → 平均误差 %+.3f mm，最大 |误差| %.3f mm（赛题中级要求 < 0.3 mm）' % (
                    a.true_size, out['bias'], out['max_abs_err'])
            md.append(line + '\n')

    # ---- PL coarse measurement next to the PS sub-pixel one
    pl = [r for r in rows if num(r, 'pl_area_px', 0) > 0 and r.get('meas_valid') == '1']
    if pl:
        md.append('## PL 硬件粗测 vs PS 亚像素测量\n')
        md.append('| 品种 | PL 外接框长边 - PS 尺寸 | PL 面积等效直径 |\n|---|---|---|')
        for kind, name in (('g', '齿轮'), ('r', '齿条')):
            k = [r for r in pl if r.get('kind') == kind]
            if not k:
                continue
            diff = [max(num(r, 'pl_w_mm', 0), num(r, 'pl_h_mm', 0)) - num(r, 'size_mm') for r in k]
            out['pl_diff_' + kind] = dist(diff, 'mm', 3)
            md.append('| %s | %s | %s |' % (name, fmt_dist(out['pl_diff_' + kind]),
                                            fmt_dist(dist([num(r, 'pl_d_mm') for r in k], 'mm', 2))))
        md.append('')

    text = '\n'.join(md)
    print(text)
    if a.md:
        with open(a.md, 'w', encoding='utf-8') as f:
            f.write(text + '\n')
    if a.json:
        with open(a.json, 'w', encoding='utf-8') as f:
            json.dump(out, f, ensure_ascii=False, indent=1, default=str)


if __name__ == '__main__':
    main()
