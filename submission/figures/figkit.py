#!/usr/bin/env python3
"""figkit — 作品报告插图的公共画布与图元（按落版尺寸作图，单位 pt）。

    import figkit as fk
    r = fk.new_fig(300)                       # 画布 447.9pt（= 报告版心 15.8cm）× 300pt
    fk.box(ax, 10, 200, 90, 24, '相机通路\nMIPI RX · ISP', 'pl')
    fk.save(fig, 'fig_architecture')

约定（作图与报告自检共用同一套坐标）：

1. **画布单位 = pt，画布宽 = ``FIG_W`` = 15.8cm，落版 1:1**；字号就是纸面字号。
2. 字号只取 ``FS`` 的档位，全图最小 7.5pt（报告正文 10.5pt 的七成，屏幕与打印都读得清）；
   框内文字分两级：第一行是名称（8.5pt 粗体墨色），其余行是限定（7.5pt 次级灰）。
3. 文字一律用 ``box()`` / ``text()`` / ``group()`` 画：``save()`` 会核对**是否越出盒子或画布**
   （误差 > 0.5pt 即报错退出），并打印每图的最小盒内余量与最小字号 —— 排版靠数据，不靠目测。
4. **不画图内标题**：标题由报告图注（``*图 2-1 …*``）承担。
5. 渲染为期刊线稿风：白底、方框细边、虚线分组框、黑色细箭头、Times 西文 + 宋体中文、无装饰色。
6. 导出：SVG（矢量，文字可编辑）+ PNG 宽 3200px；同时写本目录的 ``fig_meta.json``
   （画布尺寸、``px_per_pt``、最小字号），供 ``submission/build/verify_report.py`` 复核落版字号。
   本目录（``submission/figures/``）= 报告插图脚本 + 生成物，改了脚本重跑 ``make_figures.py``。
"""
import json
import os
import sys

try:                                   # Windows 控制台默认 GBK，中文/µ 会乱码甚至报错
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')
except Exception:                      # pragma: no cover
    pass

import matplotlib

matplotlib.use('Agg')
import matplotlib.pyplot as plt                                        # noqa: E402
from matplotlib.patches import Circle, FancyArrowPatch, FancyBboxPatch, Rectangle  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))          # 仓库根：submission/figures → submission → 根
OUT_DIR = HERE                                         # 脚本与生成物同处（提交件目录，入库）
META_PATH = os.path.join(OUT_DIR, 'fig_meta.json')

# 期刊线稿：西文 Times、中文宋体（matplotlib 逐字回退）
plt.rcParams['font.family'] = ['Times New Roman', 'SimSun', 'Microsoft YaHei']
# SimSun 没有粗体字面，标题里的中文按常规字重回退（西文仍是 Times Bold），不刷屏警告
import logging

logging.getLogger('matplotlib.font_manager').setLevel(logging.ERROR)
plt.rcParams['axes.unicode_minus'] = False
plt.rcParams['svg.fonttype'] = 'none'      # SVG 里的字保持可编辑文本
plt.rcParams['svg.hashsalt'] = 'dragoneye'  # 固定 clip-path id，重跑 SVG 逐字节一致（可复现）

# 报告版心 15.8cm = 447.92pt
FIG_W = 447.92
PNG_W = 3200                                # 导出 PNG 宽（px）：报告 ≈514dpi
TOL = 0.5                                   # 越界容差（pt）；超过即报错
TIGHT = 2.0                                 # 盒内余量小于此值就提示（还能读，但排版偏挤）
FS_MIN = 7.5                                # 全图最小字号（pt）

# 字号档位（落版真实 pt）。要改字号只改这里，别在画图代码里写数字。
FS = {'box': 8.5, 'sub': 7.5, 'group': 9.5, 'head': 9.5, 'note': 7.5}

# ---- 期刊线稿风：只用黑 / 灰，颜色不参与信息表达（分组靠标签与虚线框）----
INK = '#111111'
BODY = '#333333'
MUTED = '#4A4A4A'
FAINT = '#6A6A6A'
RULE = '#C8C8C8'
EDGE = '#333333'
WHITE = '#FFFFFF'
ARROW = '#333333'
# 卡片底色：白，NPU / 警示块用极浅灰
C = {
    'pl': ('#FFFFFF', '#111111'),
    'ps': ('#FFFFFF', '#111111'),
    'npu': ('#F2F2F2', '#111111'),
    'ext': ('#FFFFFF', '#111111'),
    'mem': ('#F0F0F0', '#111111'),
    'acc': ('#F2F2F2', '#111111'),
}
# 布局图专用底纹（皮带 / 灯光条）
BELT_FILL, BELT_EDGE, BELT_HATCH = '#F7F7F7', '#444444', '#E0E0E0'
LIGHT_FILL, LIGHT_EDGE = '#F2F2F2', '#444444'

_checks = []            # [(artist, rect|None, text, fs)]
_meta = {}
_report = []


# --------------------------------------------------------------------------------------------- 画布
def new_fig(h, w=None):
    """新画布：宽默认 FIG_W（15.8cm），高 h（pt）。返回 (fig, ax)，坐标单位 pt、原点左下。"""
    w = FIG_W if w is None else w
    fig = plt.figure(figsize=(w / 72.0, h / 72.0))
    ax = fig.add_axes([0, 0, 1, 1])
    ax.set_xlim(0, w)
    ax.set_ylim(0, h)
    ax.axis('off')
    fig._fk_size = (w, h)
    _checks.clear()
    return fig, ax


def _register(artist, rect, text, fs):
    _checks.append((artist, rect, text, fs))


def text(ax, x, y, s, fs=None, ha='left', va='center', color=INK, weight='normal', rect=None,
         linespacing=1.28, zorder=3, bg=False, family=None):
    """自由文本。rect 给了就核对是否落在 rect 内（用于组标题这类「必须在框里」的字）；
    bg=True 给白底（短线箭头上的标签压住线时用，避免读成线的一部分）。"""
    fs = FS['sub'] if fs is None else fs
    kw = {}
    if weight in ('bold', 'semibold'):
        # 宋体没有粗体字面；中文标题用黑体（中文期刊图的惯例），西文仍走 Times Bold
        kw['family'] = ['Times New Roman', 'SimHei']
    if bg:
        kw['bbox'] = dict(boxstyle='square,pad=0.10', fc=WHITE, ec='none')
    if family:
        kw['family'] = family
    t = ax.text(x, y, s, ha=ha, va=va, fontsize=fs, color=color, weight=weight,
                linespacing=linespacing, zorder=zorder, **kw)
    _register(t, rect, s, fs)
    return t


def _block(ax, cx, cy, title, rest, rect, *, align='center', fs=None, pad_x=4.0,
           title_color=INK, rest_color=MUTED, zorder=3):
    """框内文字块：第一行名称（粗体墨色）+ 其余行限定（次级灰），整块垂直居中。"""
    fs = FS['box'] if fs is None else fs
    n = len(rest)
    title_h = fs * 1.30
    line_h = FS['sub'] * 1.30
    total = title_h + n * line_h
    top = cy + total / 2.0
    tx = cx if align == 'center' else rect[0] + pad_x
    text(ax, tx, top - title_h / 2.0, title, fs=fs, ha=align, va='center', color=title_color,
         weight='bold', rect=rect, zorder=zorder)
    for i, line in enumerate(rest):
        y = top - title_h - line_h * (i + 0.5)
        text(ax, tx, y, line, fs=FS['sub'], ha=align, va='center', color=rest_color,
             rect=rect, zorder=zorder)


# --------------------------------------------------------------------------------------------- 图元
def box(ax, x, y, w, h, s='', kind='pl', fs=None, align='center', pad_x=4.0,
        lw=0.7, ls='-', fc=None):
    """卡片：白底（NPU / 警示块用极浅灰）+ 0.7pt 深灰方角细边，不放色条。

    ``s`` 里第一行是名称（8.5pt 粗体），其余行是限定（7.5pt 次级灰）。
    """
    face = C[kind][0] if fc is None else fc
    ax.add_patch(Rectangle((x, y), w, h, fc=face, ec=EDGE, lw=lw, ls=ls, zorder=2))
    if s:
        lines = s.split('\n')
        _block(ax, x + w / 2.0 if align == 'center' else x, y + h / 2.0, lines[0], lines[1:],
               (x, y, w, h), align=align, fs=fs, pad_x=pad_x)
    return (x, y, w, h)


def group(ax, x, y, w, h, label, kind, fs=None, note=None):
    """分组：灰色虚线圆角框 + 黑色标题。

    ``note`` 给右侧一行灰色小字（域说明 / 数量），与标题同基线。
    内容区从 ``y + h - 26`` 往下排（标题占顶部 26pt）。
    """
    fs = FS['group'] if fs is None else fs
    ax.add_patch(FancyBboxPatch((x, y), w, h, boxstyle='round,pad=0,rounding_size=3.5',
                                fc='none', ec='#7F7F7F', lw=0.7, ls=(0, (4.0, 2.4)),
                                zorder=1))
    ty = y + h - 13.5
    text(ax, x + 8, ty, label, fs=fs, ha='left', va='center', weight='bold', color=INK,
         rect=(x, y, w, h))
    if note:
        text(ax, x + w - 8, ty, note, fs=FS['sub'], ha='right', va='center', color=FAINT,
             rect=(x, y, w, h))
    return (x, y, w, h)


def arrow(ax, p, q, s=None, color=None, lw=0.9, style='-|>', fs=None, off=(0, 3.2),
          both=False, ls='-', ms=6.5, bg=True):
    color = ARROW if color is None else color
    a = FancyArrowPatch(p, q, arrowstyle=('<|-|>' if both else style), mutation_scale=ms, lw=lw,
                        color=color, linestyle=ls, shrinkA=0, shrinkB=0, zorder=2)
    ax.add_patch(a)
    if s:
        text(ax, (p[0] + q[0]) / 2.0 + off[0], (p[1] + q[1]) / 2.0 + off[1], s, fs=fs,
             ha='center', va='bottom', color=color, bg=bg)


def hline(ax, x0, x1, y, color=RULE, lw=0.7, ls='-', zorder=1.5):
    ax.plot([x0, x1], [y, y], color=color, lw=lw, ls=ls, solid_capstyle='butt', zorder=zorder)


def rect(ax, x, y, w, h, fc=WHITE, ec=EDGE, lw=0.7, ls='-', alpha=1.0):
    """矩形（返回 patch，可拿来当 set_clip_path 的裁剪区）。"""
    p = Rectangle((x, y), w, h, fc=fc, ec=ec, lw=lw, ls=ls, alpha=alpha, zorder=1.6)
    ax.add_patch(p)
    return p


def circle(ax, x, y, r, color=None):
    ax.add_patch(Circle((x, y), r, color=ARROW if color is None else color, zorder=5))


def tick(ax, x, y0, y1, color=None, lw=0.9, zorder=2):
    ax.plot([x, x], [y0, y1], color=ARROW if color is None else color, lw=lw,
            solid_capstyle='butt', zorder=zorder)


# --------------------------------------------------------------------------------------------- 校验
def _rect_over(r, b):
    """矩形 r=(x,y,w,h) 与文本 bbox=(x0,y0,x1,y1) 的越界量（pt）：max(超出四边的量, 0)。"""
    x, y, w, h = r
    return max(x - b[0], y - b[1], b[2] - (x + w), b[3] - (y + h), 0.0)


def _check(fig, name):
    fig.canvas.draw()
    inv = fig.axes[0].transData.inverted()
    w, h = fig._fk_size
    worst_gap, bad, min_fs, tight = 1e9, [], 99.0, []
    for artist, r, s, fs in _checks:
        min_fs = min(min_fs, fs)
        bb = artist.get_window_extent()
        pa = inv.transform((bb.x0, bb.y0))
        pb = inv.transform((bb.x1, bb.y1))
        b = (min(pa[0], pb[0]), min(pa[1], pb[1]), max(pa[0], pb[0]), max(pa[1], pb[1]))
        if r is not None:
            over = _rect_over(r, b)
            gap = min(b[0] - r[0], b[1] - r[1], (r[0] + r[2]) - b[2], (r[1] + r[3]) - b[3])
            worst_gap = min(worst_gap, gap)
            if over <= TOL and gap < TIGHT:
                tight.append((s, gap, r))
        else:
            over = _rect_over((0, 0, w, h), b)
        if over > TOL:
            bad.append((s, over, r))
    return dict(name=name, n=len(_checks), min_fs=min_fs,
                min_gap=None if worst_gap > 1e8 else worst_gap, bad=bad, tight=tight, w=w, h=h)


# --------------------------------------------------------------------------------------------- 导出
def _out_rel(path):
    """仓库根为基准的显示路径（统一用 /，Windows 下也好看）。"""
    return os.path.relpath(path, ROOT).replace('\\', '/')


def save(fig, name):
    """核对越界 → 写 <name>.svg / <name>.png → 登记 meta，并打印该图一行结论。"""
    w, h = fig._fk_size
    dpi = PNG_W * 72.0 / w
    rep = _check(fig, name)
    _report_line(rep)
    os.makedirs(OUT_DIR, exist_ok=True)
    fig.savefig(os.path.join(OUT_DIR, name + '.svg'), facecolor='white',
                metadata={'Date': None})
    fig.savefig(os.path.join(OUT_DIR, name + '.png'), dpi=dpi, facecolor='white')
    plt.close(fig)

    png = (PNG_W, int(round(h * PNG_W / w)))
    k = PNG_W / w
    _meta[name] = dict(w_pt=round(w, 2), h_pt=round(h, 2), png_w=png[0], png_h=png[1],
                       px_per_pt=round(k, 4), min_fs_pt=rep['min_fs'])
    print('       → %s/%s.svg / %s.png  %.1f x %.1f pt  PNG %d x %d（%.0f dpi @15.8cm）' %
          (_out_rel(OUT_DIR), name, name, w, h, png[0], png[1], png[0] / (w / 72.0)))
    return rep['bad']


def _report_line(r):
    """打印一图结论，返回越界清单（含字号不合格）。"""
    _report.append(r)
    tag = 'OK ' if not r['bad'] else 'ERR'
    gap = '—' if r['min_gap'] is None else '%.1fpt' % r['min_gap']
    print('  %s %-20s 文本 %2d  最小字号 %.1fpt  最小盒内余量 %s' %
          (tag, r['name'], r['n'], r['min_fs'], gap))
    for s, over, rc in list(r['bad']):
        print('      越界 %.1fpt: %r  rect=%r' % (over, s, rc))
    for s, gap, rc in r.get('tight', []):
        print('      偏紧 %.1fpt（< %.1f）: %r' % (gap, TIGHT, s[:28]))
    if r['min_fs'] < FS_MIN - 1e-6:
        print('      字号 %.1fpt < 最小要求 %.1fpt' % (r['min_fs'], FS_MIN))
        r['bad'].append(('<字号> %.1fpt' % r['min_fs'], FS_MIN - r['min_fs'], None))
    return r['bad']


def write_meta():
    """写 fig_meta.json。与已有文件合并：只重画其中几张时，其余图的记录保持不变。"""
    old = {}
    if os.path.isfile(META_PATH):
        try:
            with open(META_PATH, encoding='utf-8') as f:
                old = json.load(f).get('figures', {})
        except (ValueError, OSError):
            old = {}
    merged = dict(old)
    merged.update(_meta)
    for v in merged.values():          # 旧记录里已废弃的重点框字段 regions 一并清掉
        if isinstance(v, dict):
            v.pop('regions', None)
    with open(META_PATH, 'w', encoding='utf-8') as f:
        json.dump(dict(figure_width_pt=FIG_W, png_width=PNG_W, figures=merged), f,
                  ensure_ascii=False, indent=1)
        f.write('\n')
    print('wrote %s（本次 %d 图，合并后 %d 图）' % (_out_rel(META_PATH), len(_meta), len(merged)))


def failed():
    return any(r['bad'] for r in _report)
