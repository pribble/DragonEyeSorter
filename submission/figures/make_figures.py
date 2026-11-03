#!/usr/bin/env python3
"""make_figures.py — 生成 / 登记作品报告的 20 张插图（9 张线稿 + 11 张报告内实物 / 截图图项），写在本目录。

    python submission/figures/make_figures.py              # 重画 / 登记全部 20 张
    python submission/figures/make_figures.py arch time    # 只重画名字里含 arch / time 的线稿图
    python submission/figures/make_figures.py ph           # 只处理 11 张报告内图项
    python submission/figures/make_figures.py ui           # 只处理 id 含 ui 的界面图项（图 16~19）
    python submission/figures/make_figures.py pick         # 只重画图 10 时序
    python submission/figures/fig_timebase.py              # 单张调试（只跑这一张）

生成物（SVG / PNG / fig_meta.json）由本脚本重建、不入库（见本目录 .gitignore）；
submission/build/build.ps1 每次出报告前会自动先跑一遍本脚本。fig_meta.json 只登记报告实际
引用的 20 张（9 线稿 + 11 图项），已移出报告的旧记录由 figkit.write_meta(keep=...) 一并清掉。

报告内 11 个 ph_* 图项（清单见 placeholder_manifest.json，11-03 起全部 replaced: true）：
图 2 系统实景、图 7 电控盘、图 11 整幅 vs ROI、图 12 七类样件、图 13 整机、图 14 机械臂、
图 15 电控盘接线、图 16~19 界面四组。replaced 项不重画，只按实际像素登记 meta。
其余 2 项实物照片（导流挡板 / 测试现场）不进报告版面（in_report=false），只作
「作品照片 + 演示视频」拍摄清单；本脚本不为它们生成 PNG。

约定与图元都在 figkit.py：画布按报告落版尺寸（15.8cm 宽，单位 pt）作图，字号即纸面字号，
每张图保存前会核对文字越界与字号。单张线稿图的画法看 fig_architecture.py；图项清单与画法
在 placeholder_manifest.json / make_placeholders.py。
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
                'fig_wiring', 'fig_sw_flow', 'fig_pick_sequence', 'fig_layout', 'fig_perclass']
PLACEHOLDER_MODULE = 'make_placeholders'              # 报告内 11 张图项（清单见 placeholder_manifest.json）


def main(argv):
    keys = [a for a in argv[1:] if not a.startswith('--')]
    skipped, n_line, n_ph, ph_errors = [], 0, 0, []
    keep = None
    print('输出目录：%s' % os.path.relpath(fk.OUT_DIR, ROOT).replace('\\', '/'))

    # ---- 9 张线稿图：关键字匹配模块名（design / arch / time / pl_regs / wiring / sw / pick / layout / perclass）----
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

    # ---- 报告内 11 张图项：关键字匹配 id（ph / overview / ui / roi / samples / arm / board / machine / elec）----
    try:
        ph = importlib.import_module(PLACEHOLDER_MODULE)
    except ModuleNotFoundError as e:
        print('跳过占位图：%s' % e)
        skipped.append(PLACEHOLDER_MODULE)
    else:
        n_ph, ph_errors = ph.run(keys)
        keep = MODULE_NAMES + ph.report_ids()          # fig_meta 只留报告实际引用的 20 张

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
