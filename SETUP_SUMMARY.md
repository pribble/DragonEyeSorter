# 环境配置完成总结

## ✅ 已完成的工作

### 1. 创建环境配置系统

为了解决硬编码路径问题，已建立以下配置系统：

| 文件 | 用途 |
|------|------|
| `.env.example` | 配置模板（入库，不含具体路径） |
| `.env` | 你的本机配置（不入库，.gitignore已配置） |
| `.env.make` | 自动生成，供Makefile使用（不入库） |
| `scripts/env_loader.sh` | Bash环境变量加载器 |
| `scripts/env_loader.ps1` | PowerShell环境变量加载器 |
| `scripts/generate_env_make.sh` | 从.env生成.env.make |
| `scripts/test_env.ps1` | 验证环境配置是否正确 |
| `docs/13-环境配置指南.md` | 详细文档 |

### 2. 更新了核心脚本

以下脚本已改为自动从 `.env` 读取配置：

- `scripts/bootbin/make_bootbin.ps1` - 自动检测并加载环境变量
- `scripts/sd_image/make_sd_image.sh` - 自动检测 NPU_DEMO 路径
- `ps/Makefile` - 支持从 .env.make include 配置

### 3. 已生成的配置文件

✅ `.env` 文件已创建，当前配置：
- `FD_ROOT=C:/Anlogic/FD_2026.1_SP1` ✓ 检测到
- `TD_ROOT=C:/Anlogic/TD_Release_6.2.1` ⚠️ 需要 6.2.1（当前 2026.1 SP2）

## 📝 下一步操作

### 1. 补充 .env 配置（必做）

当前 `.env` 文件缺少以下关键路径，需要你手动补充：

```bash
# NPU Demo 路径（必需）
# 从应用说明中找到该目录的位置
NPU_DEMO=D:/path/to/demo

# PS 开发依赖（如果需要编译 PS 代码）
SDK_LIBS=D:/path/to/face_detection/libs
FREETYPE_INC=D:/path/to/freetype2_include
BOARD_LIBS=D:/path/to/board_libs
```

**编辑方法**：
```bash
# Windows
notepad .env

# WSL/Linux
nano .env
```

### 2. 下载和安装 TD 6.2.1（紧急）

当前安装的是 TD 2026.1 SP2，但项目要求 TD 6.2.1。

**下载**：
1. 访问 http://www.anlogic.com 官网
2. 进入下载中心 → 工具软件 → Tang Dynasty
3. 找到版本 **6.2.1 Release (Build 175876)** 并下载

**安装步骤**：
```powershell
# 1. 卸载旧版本
# 控制面板 → 程序和功能 → 卸载 "Anlogic TD 2026.1"

# 2. 安装新版本
# 双击下载的 TD 6.2.1 安装程序

# 3. 放置许可证
# 将许可证文件放到: C:\Anlogic\<TD安装目录>\license\Anlogic.lic

# 4. 更新 .env
# 编辑 .env，改为:
# TD_ROOT=C:/Anlogic/TD_Release_6.2.1
```

### 3. 验证环境配置（可选但推荐）

```powershell
# PowerShell 中验证所有路径
.\scripts\test_env.ps1

# 或加详细输出
.\scripts\test_env.ps1 -Verbose
```

### 4. 生成 Makefile 配置

```bash
# WSL 中运行
bash scripts/generate_env_make.sh

# 或 PowerShell
wsl bash scripts/generate_env_make.sh
```

## 🎯 配置系统的好处

现在当你换电脑或项目移到不同位置时：

1. **只需复制一个 `.env` 文件**（或按 `setup_env.cmd` 向导重新配置）
2. **所有脚本自动适配**，无需修改命令
3. **团队成员可各自配置**本机路径，不会互相干扰
4. **CI/CD 环境可通过环境变量覆盖**（不需要 .env 文件）

## 📌 关键文件说明

### `.env` 文件结构

```bash
# 工具链路径（必填）
FD_ROOT=...
TD_ROOT=...

# 厂家资源（必填）
NPU_DEMO=...

# PS 开发（可选，不编译PS代码可以不填）
SDK_LIBS=...
FREETYPE_INC=...
BOARD_LIBS=...

# 开发工作目录（WSL 用户填写）
DES_TRAIN_VENV=...
DES_WORK_DIR=...

# 板子连接信息
BOARD_IP=192.168.137.100
SSH_KEY=$HOME/.ssh/id_ed25519_anlu
```

### 环境变量优先级（从高到低）

1. **命令行参数** - `make CROSS=/path/to/cross-`
2. **Shell 环境变量** - `export FD_ROOT=/...`
3. **`.env` 文件** - 最方便的方式
4. **脚本默认值** - 向后兼容

## ❓ 常见问题

### Q: 为什么要用 `.env` 而不是直接改脚本？

A: `.env` 不入库，每个人可以有不同的本机路径，不会互相冲突。改脚本会产生大量 git diff，不适合团队协作。

### Q: 换电脑后怎么办？

A: 只需：
1. 克隆仓库
2. 运行 `setup_env.cmd`（或 `./run_setup.ps1`）
3. 编辑 `.env` 补充路径
4. 完成！

### Q: 在 CI/CD 中怎么用？

A: 设置环境变量，脚本会自动检测：
```bash
export FD_ROOT=/opt/anlogic/fd
export NPU_DEMO=/data/demo
make -C ps all
```

## 📖 详细文档

完整的使用说明和高级配置见 [docs/13-环境配置指南.md](docs/13-环境配置指南.md)

---

## 🚀 立即开始

1. ✅ `.env` 已生成（自动检测到部分路径）
2. 📝 编辑 `.env` 补充缺失的路径
3. ⬇️ 下载并安装 TD 6.2.1
4. ✓ 验证环境：`.\scripts\test_env.ps1`

有问题请参考 [docs/13-环境配置指南.md](docs/13-环境配置指南.md) 或提交 Issue。
