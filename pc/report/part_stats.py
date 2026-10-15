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
    python part_stats.py --plots figs ...                                    # + report charts (PNG, needs matplotlib)

Tables: only the standard library; Python 3.8+. Charts (--plots): matplotlib. Labels / verdicts come from
models/label_list.json (the single source).
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
RECIPES = os.path.join(HERE, '..', '..', 'ps', 'src', 'recipes.json')
DEFAULT_SPEC = {'g': (32.0, 0.15), 'r': (50.0, 0.5)}   # recipe 1, used when a record has no recipe column / id


def load_labels(path):
    try:
        with open(path, encoding='utf-8') as f:
            return {l['label']: l for l in json.load(f)['labels']}
    except OSError:
        return {}


def load_recipes(path):
    try:
        with open(path, encoding='utf-8') as f:
            return {str(r['id']): r for r in json.load(f)['recipes']}
    except (OSError, KeyError, ValueError):
        return {}


def size_groups(rows, a, recipes):
    """(kind, name, nominal, tol, tag, rows) per part kind and recipe: each record is judged against the spec of the
    recipe it was produced under (recipe column + recipes.json); --gear-od etc. override for every record."""
    groups = []
    rids = sorted({r.get('recipe', '') for r in rows})
    for kind, name in (('g', '齿轮外径'), ('r', '齿条长度')):
        for rid in rids:
            k = [r for r in rows if r.get('kind') == kind and r.get('meas_valid') == '1' and r.get('recipe', '') == rid]
            if not k:
                continue
            rc = recipes.get(rid, {})
            nom, tol = DEFAULT_SPEC[kind]
            if kind == 'g':
                nom, tol = rc.get('gear_od', nom), rc.get('gear_od_tol', tol)
                nom = a.gear_od if a.gear_od is not None else nom
                tol = a.gear_tol if a.gear_tol is not None else tol
            else:
                nom, tol = rc.get('rack_len', nom), rc.get('rack_len_tol', tol)
                nom = a.rack_len if a.rack_len is not None else nom
                tol = a.rack_tol if a.rack_tol is not None else tol
            tag = '配方 %s' % rid if rid and len(rids) > 1 else ''
            groups.append((kind, name, nom, tol, tag, rid, k))
    return groups


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
    ap.add_argument('--gear-od', type=float, help='spec for all records (default: their recipe, else 32.0)')
    ap.add_argument('--gear-tol', type=float, help='default: recipe, else 0.15')
    ap.add_argument('--rack-len', type=float, help='default: recipe, else 50.0')
    ap.add_argument('--rack-tol', type=float, help='default: recipe, else 0.5')
    ap.add_argument('--recipes', default=RECIPES, help='recipes.json (spec per recipe id of the records)')
    ap.add_argument('--labels', default=LABELS, help='label_list.json')
    ap.add_argument('--md', help='write the Markdown tables here')
    ap.add_argument('--json', help='write the numbers here')
    ap.add_argument('--plots', help='write report charts (PNG) into this folder')
    a = ap.parse_args()
    try:
        sys.stdout.reconfigure(encoding="utf-8")   # Windows console: GBK by default
    except (AttributeError, ValueError):
        pass

    labels = load_labels(a.labels)
    recipes = load_recipes(a.recipes)
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
    per_recipe = {}
    for r in rows:
        if r.get('recipe'):   # column added with recipes (40-column CSV)
            per_recipe[r['recipe']] = per_recipe.get(r['recipe'], 0) + 1
    if per_recipe:
        out['recipes'] = per_recipe
        md.append('| 配方 | %s |' % '，'.join('%s 号 %d 件' % kv for kv in sorted(per_recipe.items())))
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
    for kind, name, nom, tol, tag, rid, judged in size_groups(rows, a, recipes):
        vals = [num(r, 'size_mm') for r in judged]
        d = dist(vals, 'mm', 3)
        c = cpk(vals, nom, tol)
        key = kind + ('_r' + rid if tag else '')
        out['size_' + key] = d
        out['cpk_' + key] = c
        label = name[:2] + ('（%s）' % tag if tag else '')
        md.append('| %s | 尺寸（规格 %.2f±%.2f） | %s | %s |' % (label, nom, tol, fmt_dist(d), '%.2f' % c if c else '—'))
        if kind == 'g':
            bores = [num(r, 'bore_mm') for r in judged if num(r, 'bore_mm', 0) > 0]
            if bores:
                md.append('| %s | 孔径 | %s | |' % (label, fmt_dist(dist(bores, 'mm', 3))))
            md.append('| %s | 偏心 | %s | |' % (label, fmt_dist(dist([num(r, 'ecc_mm') for r in judged], 'mm', 3))))
        tj = sum(r.get('teeth_judged') == '1' for r in judged)
        md.append('| %s | 判齿（离光轴近、视差拖尾小） | %d / %d | |' % (label, tj, len(judged)))
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
    if a.plots:
        for p in plots(rows, a, labels, out, recipes):
            print('chart:', p)


# ---- report charts (static PNG for the technical document / PPT; light surface, print-friendly)
INK, INK2, MUTED, GRID, AXIS = '#0b0b0b', '#52514e', '#898781', '#e1e0d9', '#c3c2b7'
SURFACE, SERIES1, CRITICAL = '#fcfcfb', '#2a78d6', '#d03b3b'   # categorical slot 1, status "critical"
SEQ_BLUE = ['#fcfcfb', '#cde2fb', '#9ec5f4', '#6da7ec', '#3987e5', '#256abf', '#184f95', '#0d366b']


def plots(rows, a, labels, out, recipes):
    try:
        import matplotlib
        matplotlib.use('Agg')
        import matplotlib.pyplot as plt
        from matplotlib.colors import LinearSegmentedColormap
    except ImportError:
        sys.exit('--plots needs matplotlib (pip install matplotlib)')
    plt.rcParams.update({
        'font.family': ['Microsoft YaHei', 'Noto Sans CJK SC', 'WenQuanYi Zen Hei', 'SimHei', 'DejaVu Sans'],
        'axes.unicode_minus': False, 'figure.facecolor': SURFACE, 'axes.facecolor': SURFACE,
        'axes.edgecolor': AXIS, 'axes.labelcolor': INK2, 'xtick.color': MUTED, 'ytick.color': MUTED,
        'axes.titlecolor': INK, 'axes.titlesize': 12, 'axes.labelsize': 10, 'xtick.labelsize': 9,
        'ytick.labelsize': 9, 'axes.spines.top': False, 'axes.spines.right': False, 'axes.grid': True,
        'grid.color': GRID, 'grid.linewidth': 0.8, 'axes.axisbelow': True, 'legend.frameon': False,
        'legend.fontsize': 9})
    os.makedirs(a.plots, exist_ok=True)
    written = []

    def save(fig, name):
        p = os.path.join(a.plots, name + '.png')
        fig.savefig(p, dpi=150, bbox_inches='tight')
        plt.close(fig)
        written.append(p)

    def hist(ax, vals, title, unit):
        # the CSV rounds (npu_ms to 0.01): bins on that grid, else empty bins between the steps look like gaps
        u = sorted(set(vals))
        res = min((b - c for b, c in zip(u[1:], u)), default=0)
        n = int(round((u[-1] - u[0]) / res)) + 1 if res > 0 else 1
        if 1 < n <= 60:
            bins = [u[0] - res / 2 + k * res for k in range(n + 1)]
        else:
            bins = min(40, max(8, len(vals) // 5))
        ax.hist(vals, bins=bins, color=SERIES1, edgecolor=SURFACE, linewidth=1)
        for q, name in ((50, 'p50'), (95, 'p95')):
            v = pct(vals, q)
            ax.axvline(v, color=INK2, linewidth=1, linestyle=(0, (4, 3)))
            ax.annotate('%s %.2f %s' % (name, v, unit), (v, 1), xycoords=('data', 'axes fraction'),
                        xytext=(4, -12 if q == 50 else -26), textcoords='offset points', fontsize=9, color=INK2,
                        bbox=dict(boxstyle='square,pad=0.15', facecolor=SURFACE, edgecolor='none'))
        ax.set_title('%s（n=%d）' % (title, len(vals)), loc='left')
        ax.set_xlabel(unit)
        ax.set_ylabel('件数')
        ax.yaxis.get_major_locator().set_params(integer=True)
        ax.grid(axis='x', visible=False)

    # AI time: per-part NPU inference, and camera frame complete -> probabilities usable (two charts, no dual axis)
    voted = [r for r in rows if num(r, 'votes', 0) > 0]
    if voted:
        fig, axs = plt.subplots(1, 2, figsize=(10, 3.4))
        hist(axs[0], [num(r, 'npu_ms') for r in voted], 'NPU 单次推理（每件平均）', 'ms')
        hist(axs[1], [num(r, 'lat_ms') for r in voted], '帧采集完成 → 分类结果可用', 'ms')
        fig.tight_layout(w_pad=3)
        save(fig, 'ai_time')

    # size trends: every measured part in record order, spec limits, out-of-spec marked (shape + color + legend)
    for kind, name, nom, tol, tag, rid, judged in size_groups(rows, a, recipes):
        vals = [num(r, 'size_mm') for r in judged]
        fig, ax = plt.subplots(figsize=(10, 3.4))
        x = list(range(1, len(vals) + 1))
        ax.plot(x, vals, color=SERIES1, linewidth=2, zorder=2)
        inside = [(i, v) for i, v in zip(x, vals) if abs(v - nom) <= tol]
        outside = [(i, v) for i, v in zip(x, vals) if abs(v - nom) > tol]
        if inside:
            ax.scatter(*zip(*inside), s=30, color=SERIES1, edgecolor=SURFACE, linewidth=1.5, zorder=3, label='合格')
        if outside:
            ax.scatter(*zip(*outside), s=48, marker='X', color=CRITICAL, edgecolor=SURFACE, linewidth=1, zorder=4,
                       label='超差（%d 件）' % len(outside))
        for v, txt in ((nom + tol, '上限 %.2f' % (nom + tol)), (nom, '名义 %.2f' % nom), (nom - tol, '下限 %.2f' % (nom - tol))):
            ax.axhline(v, color=INK2 if v != nom else MUTED, linewidth=1, linestyle=(0, (4, 3)) if v != nom else '-',
                       zorder=1)
            ax.annotate(txt, (1, v), xycoords=('axes fraction', 'data'), xytext=(4, -3), textcoords='offset points',
                        fontsize=9, color=INK2)
        c = cpk(vals, nom, tol)
        ax.set_title('%s趋势%s（规格 %.2f±%.2f mm，n=%d，均值 %.3f，σ %.4f，Cpk %s）' % (
            name, '·' + tag if tag else '', nom, tol, len(vals), statistics.fmean(vals), statistics.stdev(vals) if len(vals) > 1 else 0,
            '%.2f' % c if c is not None else '—'), loc='left')
        ax.set_xlabel('件序（按记录顺序）')
        ax.set_ylabel('mm')
        ax.grid(axis='x', visible=False)
        ax.legend(loc='upper left', ncol=2)
        save(fig, 'size_' + ('gear' if kind == 'g' else 'rack') + ('_r' + rid if tag else ''))

    # confusion matrix (known class per file): row-normalised colour, counts written in the cells
    conf = out.get('confusion')
    if conf:
        cn = {k: v.get('cn', k) for k, v in labels.items()}
        truths = sorted(conf)
        preds = sorted({p for c in conf.values() for p in c} | set(truths), key=lambda p: (p == 'unclassified', p))
        m = [[conf[t].get(p, 0) for p in preds] for t in truths]
        norm = [[v / max(1, sum(row)) for v in row] for row in m]
        fig, ax = plt.subplots(figsize=(1.1 * len(preds) + 2.5, 0.6 * len(truths) + 1.8))
        cmap = LinearSegmentedColormap.from_list('seq_blue', SEQ_BLUE)
        ax.imshow(norm, cmap=cmap, vmin=0, vmax=1, aspect='auto')
        for i, row in enumerate(m):
            for j, v in enumerate(row):
                if v:
                    ax.text(j, i, str(v), ha='center', va='center', fontsize=10,
                            color='#ffffff' if norm[i][j] > 0.55 else INK)
        ax.set_xticks(range(len(preds)), [cn.get(p, '未分类' if p == 'unclassified' else p) for p in preds],
                      rotation=30, ha='right')
        ax.set_yticks(range(len(truths)), [cn.get(t, t) for t in truths])
        ax.set_xlabel('预测')
        ax.set_ylabel('真值')
        ax.grid(False)
        ax.spines[:].set_visible(False)
        acc = out.get('accuracy')
        ax.set_title('分类混淆矩阵' + ('（多类准确率 %.2f%%）' % (acc * 100) if acc is not None else ''), loc='left')
        save(fig, 'confusion')

    # throughput: cumulative parts over time, one line per file (runs are separate sessions)
    files = sorted({r['_file'] for r in rows})
    if len(files) == 1:
        ts = sorted(num(r, 't_s', 0.0) for r in rows)
        if len(ts) > 1:
            fig, ax = plt.subplots(figsize=(10, 3.2))
            t0 = ts[0]
            ax.step([t - t0 for t in ts], range(1, len(ts) + 1), where='post', color=SERIES1, linewidth=2)
            span = ts[-1] - t0
            ax.set_title('累计件数（%d 件 / %.0f s = %.1f 件/min）' % (len(ts), span, len(ts) / span * 60 if span else 0),
                         loc='left')
            ax.set_xlabel('s（首件起）')
            ax.set_ylabel('件')
            ax.grid(axis='x', visible=False)
            save(fig, 'throughput')
    return written


if __name__ == '__main__':
    main()
