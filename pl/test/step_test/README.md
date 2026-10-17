# W1 步进电机通路验证工程

20 行 Verilog：100MHz 分频成 381Hz 方波，CEP PIN3（Y12）输出，直驱 TB6600 光耦输入。只用两根线（不要 5V / 电平转换 / 万用表），一次验证五件事：测试电源 12V、电机相序、拨码电流档、光耦输入、TB6600 本体。测试电源用 12V；正式系统为 24V 主电源，见 [硬件与接线](../../../docs/硬件与接线.md) 供电树。

## 接线（共阴，与最终接法相同）

| FPGA CEP | TB6600 |
|---|---|
| PIN2（GND） | `PUL −` |
| PIN3（Y12） | `PUL +` |

- `DIR+ / DIR− / ENA+ / ENA−` 全部悬空；TB6600 的 12V 与 FPGA 不共地，靠光耦隔离（D3~D6 实测：真光耦，三路各自单向导通、三个 `+` 端互不导通）。
- 与最终设计一致：共阴、FPGA 3.3V 直驱、不用 74HCT245（顶层 `STEP_COMMON_CATHODE=1`），见 [硬件与接线](../../../docs/硬件与接线.md)「步进驱动器」；3.3V 直接给光耦供电流，对 BANK11 零倒灌风险。
- 电流余量：`(3.3V − 1.3V) / 330Ω ≈ 6mA`；TB6600 输入设计值是 5V 下 ~11mA，3.3V 偏低但通常可用。实测丢步 → 先把 `DRIVESTRENGTH` 从 8 提到 12/16；仍丢步则加 74AHCT125 电平转换模块并改回共阳（顶层参数置 0）。

## 编译与下载

```bat
cd pl\test\step_test
<TD 安装目录>\bin\td_commands_prompt.exe build.tcl < NUL
```

几十秒出 `step_test.bit`（TD 6.2.1 实测通过）。图形界面：器件选 **DR1M90GEG400** → 加 `step_test.v`（Top）/ `step_test.adc` / `step_test.sdc` → 综合 → 布局布线 → 出比特流 → BitWriter 下载（**PROGRAM_SRAM**，File Selection 只放这一个 bit）。

**下载只在不跑 Linux 时进行**：Linux 用的 MIPI / ISP / VDMA / HDMI / NPU soft IP 全在 PL 里，运行时下载会覆盖它们；测完断电重启由 BOOT.bin 恢复（见 [开发环境与构建](../../../docs/开发环境与构建.md)「操作禁忌」）。

## 上电与调速

上电顺序：先接电机四根线（带电拔线 = 感性反冲打死驱动器）→ 接 CEP 两根线 → TB6600 上 12V（应立刻有保持力矩）→ FPGA 上电并下载 bit（**一加载就转**，先清空带上物件、手离开）。

改 `step_test.v` 里的 `parameter TAP` 调速，TAP 越大越慢；丢步 / 啸叫就调大。

| TAP | 频率 | 16 细分 | 1 细分 |
|---|---|---|---|
| 16 | 763 Hz | 14.3 RPM | 229 RPM |
| **17** | **381 Hz** | **7.1 RPM** | **114 RPM** |
| 18 | 191 Hz | 3.6 RPM | 57 RPM |
| 19 | 95 Hz | 1.8 RPM | 29 RPM |
