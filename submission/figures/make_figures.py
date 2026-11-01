#!/usr/bin/env python3
"""make_figures.py — 生成作品报告的 14 张插图（8 张线稿 + 6 张报告内实物图占位图），写在本目录。

    python submission/figures/make_figures.py              # 重画全部 14 张
    python submission/figures/make_figures.py arch time    # 只重画名字里含 arch / time 的线稿图
    python submission/figures/make_figures.py ph           # 只重画 6 张报告内占位图
    python submission/figures/make_figures.py ui           # 只重画 id 含 ui 的占位图（图 13）
    python submission/figures/fig_timebase.py              # 单张调试（只跑这一张）

生成物（SVG / PNG / fig_meta.json）由本脚本重建、不入库（见本目录 .gitignore）；
submission/build/build.ps1 每次出报告前会自动先跑一遍本脚本。fig_meta.json 只登记报告实际
引用的 14 张（8 线稿 + 6 占位），已移出报告的旧记录由 figkit.write_meta(keep=...) 一并清掉。

其余 2 项实物照片（导流挡板 / 测试现场）不进报告版面（作品照片是单独必交材料，报告
不插这 2 张），只保留在 placeholder_manifest.json 的 in_report=false 项里，作为
「作品照片 + 演示视频」拍摄清单（见 README）；本脚本不为它们生成 PNG。整机全景（图 10，
一张两用兼作品照片主图）、机械臂（图 11）与开发板特写（图 12）按官方模板第三部分
「实物照片为主」的要求进报告版面，属 in_report=true 占位。

约定与图元都在 figkit.py：**画布按报告落版尺寸（15.8cm 宽，单位 pt）作图，字号即纸面字号**，
每张图保存前会核对文字越界与字号（见文件头注释）。单张线稿图的画法看 fig_architecture.py；
占位图的清单与画法在 placeholder_manifest.json / make_placeholders.py（关键字按占位图 id 过滤）。
每个 fig_*.py 要暴露 draw()，并能单独 `python submission/figures/fig_xxx.py` 跑。
"""
import importlib
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)

import figkit as fk                                   # noqa: E402

MODULE_NAMES = ['fig_design_flow', 'fig_architecture', 'fig_timebase', 'fig_pl_regs',
                'fig_sw_flow', 'fig_pick_sequence', 'fig_layout', 'fig_perclass']
PLACEHOLDER_MODULE = 'make_placeholders'              # 报告内 6 张待插入实物图占位（清单见 placeholder_manifest.json）


def main(argv):
    keys = [a for a in argv[1:] if not a.startswith('--')]
    skipped, n_line, n_ph, ph_errors = [], 0, 0, []
    keep = None
    print('输出目录：%s' % os.path.relpath(fk.OUT_DIR, ROOT).replace('\\', '/'))

    # ---- 8 张线稿图：关键字匹配模块名（design / arch / time / pl_regs / sw / pick / layout / perclass）----
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
        n_line += 1

    # ---- 报告内 6 张占位图：关键字匹配占位图 id（ph / overview / ui / roi / samples / arm / board）----
    try:
        ph = importlib.import_module(PLACEHOLDER_MODULE)
    except ModuleNotFoundError as e:
        print('跳过占位图：%s' % e)
        skipped.append(PLACEHOLDER_MODULE)
    else:
        n_ph, ph_errors = ph.run(keys)
        keep = MODULE_NAMES + ph.report_ids()          # fig_meta 只留报告实际引用的 10 张

    fk.write_meta(keep=keep)
    if skipped:
        print('缺少模块：%s —— 插图必须齐全，报告验收会拦' % ', '.join(skipped))
    for e in ph_errors:
        print('ERR %s' % e)
    if keys and n_line + n_ph == 0:
        print('关键字没有匹配到任何插图：%s（报告内占位图只认 overview / ui / roi / samples / '
              'arm / board，其余 2 项为报告外拍摄清单）' % ' '.join(keys))
        return 1
    if fk.failed() or skipped or ph_errors:
        print('有插图越界 / 字号不足 / 模块缺失 / 占位图未就绪 —— 见上面的 ERR 行')
        return 1
    head = '按关键字重画' if keys else '全部'
    print('%s %d 张插图完成（线稿 %d + 报告内占位 %d）' % (head, n_line + n_ph, n_line, n_ph))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
