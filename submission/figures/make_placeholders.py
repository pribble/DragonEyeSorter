#!/usr/bin/env python3
"""make_placeholders.py — 生成报告 7 张「待插入实物照片」占位图（PNG 宽 3200 px）。

    python submission/figures/make_placeholders.py                # 画全部 7 张，写 fig_meta.json
    python submission/figures/make_placeholders.py overview test  # 只画 id 里含 overview / test 的
    python submission/figures/make_figures.py                     # 报告生成链：5 线稿 + 7 占位
    python submission/figures/make_figures.py ph                  # 只画 7 张占位

清单与拍摄要求都在同目录的 ``placeholder_manifest.json``（7 项：id / file / h_pt / 图注 /
机位 / 光线 / 拍摄要求）。画布按报告落版尺寸 1:1（宽 447.92pt = 15.8cm，高 = 447.92 × 比例），
观感与线稿图一致：白底、灰色虚线边框、极浅灰填充，居中三行字 ——
9.5pt 黑体「【待插入实物照片】」+ 8.5pt 说明（标题 / 机位 / 建议分辨率）+ 7.5pt 拍摄要求
（图内不写「图 N」编号——图号由 PDF 里图下方的正式图注承担，避免重复编号）。
字号取 ``figkit.FS`` 档位（head / box / sub），全图最小 7.5pt；``fk.save()`` 会核对越界与字号，
另打印三行字的实测宽度与左右余量。每张图的信息并入 ``fig_meta.json``（与 5 张线稿同一结构，
供 ``submission/build/verify_report.py`` 复核落版字号与 dpi）。

拍了实物后：按 manifest 的 ``resolution`` 把照片存成同名 ``ph_xxx.png`` 覆盖占位图，并把该项
``replaced`` 改为 true —— 本脚本 / ``make_figures.py`` / ``build.ps1`` 之后就不再重画这一项，
只读实际像素尺寸登记 meta；正文（``技术文档.md``）与图注一字不动。替换流程见 README「占位图」。
"""
import json
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import figkit as fk                                   # noqa: E402

MANIFEST_PATH = os.path.join(HERE, 'placeholder_manifest.json')

# 占位图版式（pt）。不是信息图，只画一个「待贴照片」的框 + 三行字，不画任何装饰。
FRAME_INSET = 6.0                                     # 虚线框离画布边（pt）
FRAME_FILL = '#FAFAFA'                                # 极浅灰填充
FRAME_EDGE = '#7F7F7F'                                # 灰色虚线（与 figkit.group 同色）
FRAME_DASH = (0, (5.0, 3.0))
LINE_YS = (15.0, 1.5, -10.0)                          # 三行字相对画布垂直中线的偏移（pt）


def load_manifest():
    """读清单并校验（比例 ↔ h_pt、file ↔ id、7 项齐全）；清单错就是脚本错，直接抛 ValueError。"""
    with open(MANIFEST_PATH, encoding='utf-8') as f:
        man = json.load(f)
    items = man.get('placeholders', [])
    if len(items) != 7:
        raise ValueError('placeholder_manifest.json 应有 7 项，实际 %d 项' % len(items))
    ids = set()
    for it in items:
        for k in ('id', 'file', 'h_pt', 'aspect', 'title', 'caption', 'desc', 'shoot'):
            if not it.get(k):
                raise ValueError('清单项缺字段 %s：%r' % (k, it))
        if it['id'] in ids:
            raise ValueError('清单 id 重复：%s' % it['id'])
        ids.add(it['id'])
        if it['file'] != it['id'] + '.png':
            raise ValueError('%s 的 file 必须是 %s.png（报告按这个文件名引用）'
                             % (it['id'], it['id']))
        num, den = [float(x) for x in it['aspect'].split(':')]
        want = fk.FIG_W * den / num
        if abs(float(it['h_pt']) - want) > 0.02:
            raise ValueError('%s 的 h_pt=%.2f 与 aspect %s 不符（应为 %.2f）'
                             % (it['id'], it['h_pt'], it['aspect'], want))
    return man


def _png_size(path):
    """只读 PNG 头取像素宽高（不依赖 Pillow）。"""
    with open(path, 'rb') as f:
        head = f.read(24)
    if len(head) < 24 or head[:8] != b'\x89PNG\r\n\x1a\n' or head[12:16] != b'IHDR':
        raise ValueError('不是 PNG 文件：%s' % path)
    return struct.unpack('>II', head[16:24])


def _text_width(fig, ax, artists):
    """实测一组文字的宽度（pt，取最宽的一行）。"""
    fig.canvas.draw()
    inv = ax.transData.inverted()
    widest = 0.0
    for a in artists:
        bb = a.get_window_extent()
        p0, p1 = inv.transform((bb.x0, bb.y0)), inv.transform((bb.x1, bb.y1))
        widest = max(widest, abs(p1[0] - p0[0]))
    return widest


def draw_one(item):
    """按清单项画一张占位图：白底 + 灰色虚线框 + 极浅灰底 + 居中三行字。"""
    w, h = fk.FIG_W, float(item['h_pt'])
    fig, ax = fk.new_fig(h)
    m = FRAME_INSET
    fk.rect(ax, m, m, w - 2 * m, h - 2 * m, fc=FRAME_FILL, ec=FRAME_EDGE,
            lw=0.9, ls=FRAME_DASH)
    cx, cy = w / 2.0, h / 2.0
    l1, l2, l3 = fk.FS['head'], fk.FS['box'], fk.FS['sub']
    y1, y2, y3 = [cy + dy for dy in LINE_YS]
    artists = [
        fk.text(ax, cx, y1, '【待插入实物照片】', fs=l1, ha='center', va='center',
                color=fk.INK, weight='bold'),
        fk.text(ax, cx, y2, item['desc'], fs=l2, ha='center', va='center',
                color=fk.BODY),
        fk.text(ax, cx, y3, item['shoot'], fs=l3, ha='center', va='center',
                color=fk.MUTED),
    ]
    widest = _text_width(fig, ax, artists)
    info = '三行字 %.1f / %.1f / %.1f pt，最宽 %.1fpt，左右余量 %.1fpt' % (
        l1, l2, l3, widest, (w - widest) / 2.0)
    return fig, ax, info


def _register_replaced(item):
    """已替换成实物图：不重画，只读实际像素尺寸登记 meta。返回错误串（None = 成功）。"""
    path = os.path.join(HERE, item['file'])
    if not os.path.isfile(path):
        return ('清单标了 replaced=true，但找不到实物图 %s —— '
                '拍完请按 resolution 存成该文件，或把 replaced 改回 false' % item['file'])
    pw, ph = _png_size(path)
    w_pt = fk.FIG_W
    h_pt = w_pt * ph / pw
    fk.register_meta(item['id'], w_pt, h_pt, pw, ph)
    print('  OK   %-16s 已替换实物图 %s  %d × %d px → %.1f × %.1f pt'
          '（落版 1:1，等效 %.0f dpi）'
          % (item['id'], item['file'], pw, ph, w_pt, h_pt, pw / (w_pt / 72.0)))
    return None


def run(keys=None):
    """画占位图（keys 为 id 子串过滤，空 / None = 全部）。返回 (处理项数, 错误清单)。"""
    man = load_manifest()
    keys = [k for k in (keys or []) if k]
    todo = [it for it in man['placeholders']
            if not keys or any(k in it['id'] for k in keys)]
    if todo:
        print('占位图（待拍实物照片）：')
    n, errors = 0, []
    for it in todo:
        if it.get('replaced'):
            err = _register_replaced(it)
            if err:
                errors.append('%s：%s' % (it['id'], err))
            else:
                n += 1
            continue
        fig, _, info = draw_one(it)
        bad = fk.save(fig, it['id'])
        print('       %s' % info)
        if bad:
            errors.append('%s：%d 处越界 / 字号不足' % (it['id'], len(bad)))
        n += 1
    return n, errors


def main(argv):
    keys = [a for a in argv[1:] if not a.startswith('--')]
    n, errors = run(keys)
    fk.write_meta()
    if keys and n == 0 and not errors:
        print('关键字没有匹配到占位图：%s（清单 id：%s）'
              % (' '.join(keys), ', '.join(i['id'] for i in load_manifest()['placeholders'])))
        return 1
    if errors or fk.failed():
        for e in errors:
            print('ERR %s' % e)
        print('占位图有问题 —— 见上面 ERR 行')
        return 1
    print('占位图完成：本次 %d 项' % n)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
