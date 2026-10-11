# W1 步进电机通路验证工程

## 这是什么

20 行 Verilog，把 100MHz 分频成 381Hz 方波，从 CEP 第 3 脚（Y12）输出，
直接驱动 TB6600 的光耦输入。**只用两根线，不需要 5V、不需要电平转换、不需要万用表。**

一次验证五件事：12V 供电、电机相序、拨码电流档、光耦输入、TB6600 本体。

## 接线（共阴极，测试专用）

| FPGA CEP | TB6600 |
|---|---|
| PIN 2（GND） | `PUL −` |
| PIN 3（Y12） | `PUL +` |

`DIR+ / DIR− / ENA+ / ENA−` **全部悬空**。TB6600 的 12V 和 FPGA 之间**不共地**，
光耦本身提供隔离（已由 D3~D6 实测证实是真光耦：三路各自单向导通、三个 `+` 端互不导通）。

### 与最终设计的接法（2026-10-10 更新）

这里就是**共阴极 / 高有效**；最终接法（[docs/05-硬件与接线.md](../../../docs/05-硬件与接线.md)「步进驱动器」10-10 更正）
也改为**共阴、FPGA 3.3V 直驱、取消 74HCT245**——顶层 `STEP_COMMON_CATHODE=1` 与本工程相同，无需改回。
FPGA 3.3V 直接给光耦供电流，对 BANK11 零倒灌风险。仅当实测丢步时，才按 docs/05 加「74AHCT125 电平转换模块」改回共阳（顶层参数置 0）。

### 电流余量

`(3.3V − 1.3V) / 330Ω ≈ 6mA`。TB6600 输入设计值是 5V 下 ~11mA，
所以 3.3V 属于**偏低但通常可用**（大量 3.3V MCU 直驱 TB6600 的实践）。
不动 → 把 `DRIVESTRENGTH` 从 8 提到 12/16 再试；还不动就按 docs/05 加 74AHCT125 电平转换并改回共阳（顶层参数置 0）。

## 命令行编译（不开图形界面）

```bat
cd pl\test\step_test
D:\ANLU\TD\bin\td_commands_prompt.exe build.tcl < NUL
```

几十秒出 `step_test.bit`（2026-10-09 用 TD 6.2.1 实测通过）。

## TD 操作步骤（图形界面）

1. TD 新建工程，器件选 **DR1M90GEG400**（和 `02_fpga_start` 同款）
2. 加入 `step_test.v`，设为 Top
3. 加入 `step_test.adc`（管脚）和 `step_test.sdc`（时序）
4. Synthesize → Place & Route → Generate Bitstream
5. BitWriter 下载，模式 **PROGRAM_SRAM**

### ⚠ 下载前必读

- **BitWriter 的 File Selection 里只留这一个 bit**，其余 Delete 掉
- **不要在 Linux 运行期间下载**。Linux 用的 MIPI/ISP/VDMA/HDMI/NPU soft IP 全在 PL 里，
  会被覆盖（见 [docs/09-开发环境与构建.md](../../../docs/09-开发环境与构建.md)「操作禁忌」）。测完断电重启，BOOT.bin 会恢复正确 bitstream

## 上电顺序

1. **先接好电机四根线**（带电拔电机线 = 感性反冲打死驱动器）
2. 接 CEP 那两根线
3. TB6600 上 12V → 应该立刻有保持力矩
4. FPGA 上电 + 下载 bit → 电机开始转

**bit 一加载电机就转**，先确认带上没东西、手离开。

## 调速

改 `step_test.v` 里的 `parameter TAP`：

| TAP | 频率 | 16细分 | 1细分 |
|---|---|---|---|
| 16 | 763 Hz | 14.3 RPM | 229 RPM |
| **17** | **381 Hz** | **7.1 RPM** | **114 RPM** |
| 18 | 191 Hz | 3.6 RPM | 57 RPM |
| 19 | 95 Hz | 1.8 RPM | 29 RPM |

TAP 越大越慢。丢步/啸叫就调大。
