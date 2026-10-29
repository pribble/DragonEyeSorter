#!/usr/bin/env python3
"""make_figures.py — 生成作品报告的 5 张插图（SVG + PNG + fig_meta.json），写在本目录。

    python submission/figures/make_figures.py              # 重画全部
    python submission/figures/make_figures.py arch time    # 只重画名字里含 arch / time 的
    python submission/figures/fig_timebase.py              # 单张调试（只跑这一张）

生成物（SVG / PNG / fig_meta.json）由本脚本重建、不入库（见本目录 .gitignore）；
submission/build/build.ps1 每次出报告前会自动先跑一遍本脚本。

约定与图元都在 figkit.py：**画布按报告落版尺寸（15.8cm 宽，单位 pt）作图，字号即纸面字号**，
每张图保存前会核对文字越界与字号（见文件头注释）。单张图的画法看 fig_architecture.py。
每个 fig_*.py 要暴露 draw()，并能单独 `python submission/figures/fig_xxx.py` 跑。
"""
import importlib
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)

import figkit as fk                                   # noqa: E402

MODULE_NAMES = ['fig_architecture', 'fig_timebase', 'fig_pl_regs', 'fig_pick_sequence', 'fig_layout']


def main(argv):
    keys = [a for a in argv[1:] if not a.startswith('--')]
    skipped, drawn = [], 0
    print('输出目录：%s' % os.path.relpath(fk.OUT_DIR, ROOT).replace('\\', '/'))
    for name in MODULE_NAMES:
        if keys and not any(k in name for k in keys):
            continue
        try:
            mod = importlib.import_module(name)
        except ModuleNotFoundError as e:
            print('跳过 %s：%s' % (name, e))
            skipped.append(name)
            continue
        print('%s：' % name)
        fig, _ = mod.draw()
        fk.save(fig, name)
        drawn += 1
    fk.write_meta()
    if skipped:
        print('缺少模块：%s —— 5 张图必须齐全，报告验收会拦' % ', '.join(skipped))
    if fk.failed() or skipped:
        print('有插图越界 / 字号不足 / 模块缺失 —— 见上面的 ERR 行')
        return 1
    print('全部 %d 张插图完成' % drawn)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
