#!/usr/bin/env python3
"""make_figures.py — 生成技术文档 / 作品报告 / PPT 的 5 张插图（SVG + PNG + fig_meta.json）。

    python docs/figures/make_figures.py              # 重画全部
    python docs/figures/make_figures.py arch time    # 只重画名字里含 arch / time 的
    python docs/figures/fig_timebase.py              # 单张调试（等价于上面 + 只跑这一张）

约定与图元都在 figkit.py：**画布按报告落版尺寸（15.8cm 宽，单位 pt）作图，字号即纸面字号**，
每张图保存前会核对文字越界与字号（见文件头注释）。单张图的画法看 fig_architecture.py。
每个 fig_*.py 要暴露 draw() 与 REGIONS，并能单独 `python docs/figures/fig_xxx.py` 跑。
"""
import importlib
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import figkit as fk                                  # noqa: E402

MODULE_NAMES = ['fig_architecture', 'fig_timebase', 'fig_pl_regs', 'fig_pick_sequence', 'fig_layout']


def main(argv):
    keys = argv[1:]
    skipped, drawn = [], 0
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
        fk.save(fig, name, getattr(mod, 'REGIONS', None))
        drawn += 1
    fk.write_meta()
    if skipped:
        print('缺少模块：%s —— 5 张图必须齐全，报告与 PPT 的验收会拦' % ', '.join(skipped))
    if fk.failed() or skipped:
        print('有插图越界 / 字号不足 / 模块缺失 —— 见上面的 ERR 行')
        return 1
    print('全部 %d 张插图完成' % drawn)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
