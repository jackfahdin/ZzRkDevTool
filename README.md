<div align="center">

<h1>⚡ ZzRkDevTool</h1>

<p>面向 Rockchip 设备的跨平台固件烧录工具<br>共享 C++ 核心 · Qt 桌面界面 · Rust GPUI 实验前端 · 命令行自动化</p>

![C++20](https://img.shields.io/badge/C%2B%2B-20-00599C?style=flat-square&logo=cplusplus&logoColor=white)
![Qt ≥ 6.8](https://img.shields.io/badge/Qt-%E2%89%A5%206.8-41CD52?style=flat-square&logo=qt&logoColor=white)
![Rust GPUI](https://img.shields.io/badge/Rust-GPUI_Experimental-CE422B?style=flat-square&logo=rust&logoColor=white)
[![MIT License](https://img.shields.io/badge/License-MIT-7C3AED?style=flat-square)](LICENSE)

[功能一览](#features) · [快速开始](#quick-start) · [命令行使用](#cli) · [平台与打包](#platforms) · [文档导航](#docs)

</div>

---

ZzRkDevTool 提供设备发现、独立镜像下载、完整固件升级和分区管理。CLI、Qt GUI 与 Rust GPUI 前端复用同一套 C++20 协议、镜像解析和烧录工作流，统一执行写前检查、进度回报和回读校验。

当前真机验证基线是 **Ubuntu 26.04 x86_64 + RK3588/RK3588S + eMMC**，固件样本来自 `pudu_rk3588s`。Windows 和 macOS 已有构建、测试与打包配置，真机验证及正式签名仍待完成；Rust GPUI 当前以 Linux 开发构建为主。

<a id="features"></a>

## ✨ 功能一览

### 四个业务入口

Qt GUI 使用完整的 [ZzPureTools](https://github.com/jackfahdin/ZzPureTools) 框架，提供无边框标题栏、顶部标签页和明暗主题。“下载镜像”位于首标签页。

| 入口 | 当前能力 |
| --- | --- |
| 📥 下载镜像 | 从设备 GPT 或文本 `parameter.txt` 获取布局；选择独立 `MiniLoaderAll.bin`、Raw、Android Sparse 镜像；目录自动匹配 A/B 分区；逐项选择与回读校验 |
| 🔌 设备 | 发现 Rockchip USB 设备；按物理端口选择目标；查看 ChipInfo、Capability、FlashInfo、FlashID、eMMC 容量和 USB 接口信息 |
| 🚀 完整升级 | 解析 `update.img`、验证 MD5、预览烧录计划；准备 RAM Loader；烧录 Loader、GPT 和固件实际携带的分区文件；选择校验模式与完成后重启 |
| 🧩 分区 | 校验 GPT 后列出分区名称和 LBA 范围；备份、容量受限写入、回读校验、擦除和取消 |
| ⚙️ 设置 | 配置目录、默认备份目录、主题及语言；重启恢复镜像草稿与操作选项 |

首次启动可选择用户配置目录、应用所在目录或自定义目录。主题与语言即时生效，语言支持简体中文、英文和跟随系统。详见 [GUI 配置说明](docs/GUI_SETTINGS_ZH.md)。

镜像目录优先匹配 `boot_a.img`、`boot_b.img` 等精确分区名；缺少精确文件时，通用的 `boot.img` 可同时匹配 A/B 两个槽位。匹配项及其回读校验默认开启，可分别调整。

### 烧录与任务体验

- **双进度显示**：同时展示当前分区/操作百分比和完整计划的累计百分比。
- **事件式设备发现**：Qt GUI 使用 libusb 热插拔事件，保留启动扫描与手动刷新；不使用定时轮询。
- **全局设备状态栏**：底部按物理端口切换目标，所有业务页面共用当前设备；任务期间锁定选择，设备断开后需重新选择，保留刷新和更新时间。
- **分块取消**：在 RockUSB 传输块边界停止后续操作，取消后不自动重启设备。
- **升级诊断**：CLI 可导出 JSON 诊断报告，包含烧录计划、各阶段耗时和最终结果。
- **自动化接口**：主要 CLI 命令提供版本化 JSON；完整升级可用 JSON Lines 连续输出进度和最终结果。

### 🦀 Rust GPUI 实验前端

`rust/gui_gpui` 使用 `gpui-kit`，已接入设备、完整升级、下载镜像和分区四个页面，通过 `rust/zzrk-sys` 调用 C++ 核心的 C ABI。支持文件选择、任务进度、取消和明暗主题。

当前 GPUI 前端尚未接入设备热插拔事件、升级报告导出及 DEB/TGZ/DMG 打包。其 Linux 构建依赖构建树中的共享库，不能直接拷贝可执行文件作为独立安装包。Qt GUI + CLI 是现有安装打包流程的目标。

<a id="quick-start"></a>

## 🛠️ 快速开始

### 环境要求

| 组件 | 要求 | 使用范围 |
| --- | --- | --- |
| C++ 编译器 | 支持 C++20 | C++ 核心、CLI、Qt GUI 与 FFI |
| CMake / Ninja | CMake ≥ 3.25；Ninja | C++ 构建 |
| Zzlibusb | 固定子模块，基于 libusb 1.0.30 | 从源码静态构建，Windows 热插拔已启用 |
| Qt | ≥ 6.8，含同版本 `CorePrivate`、`GuiPrivate`、`WidgetsPrivate` 及 `LinguistTools` | GUI 与 CLI；可移植核心目标无需 Qt |
| Rust / libclang | Rust 稳定版工具链；bindgen 可加载的 libclang | GPUI 前端与 Rust 绑定 |

三个平台均使用 `thirdParty/Zzlibusb` 从源码构建，不需要另外准备 libusb 二进制或 vcpkg overlay。旧 `thirdParty/libusb` 暂时保留，等待真机验证后清理。详见[中文编译与打包指南](docs/BUILD_AND_PACKAGE.zh-CN.md)。

### 1. 获取源码与依赖

```bash
git clone git@gitcode.com:jackfahdin/ZzRkDevTool.git
cd ZzRkDevTool
git submodule update --init --recursive
```

ZzPureTools 和 Zzlibusb 子模块分别使用 GitHub、GitCode SSH 地址，需要配置对应的 SSH 访问。子模块提交由仓库固定；首次配置应用目标时，spdlog 会下载 SHA-256 固定的 fmt 12.1.0。

### 2. 构建 Qt GUI 与 CLI（Linux）

以下示例假设 Qt 安装在 `$HOME/Qt/6.11.1/gcc_64`：

```bash
cmake --preset linux-debug \
  -DCMAKE_PREFIX_PATH="$HOME/Qt/6.11.1/gcc_64"
cmake --build --preset linux-debug --parallel
ctest --preset linux-debug
```

启动应用或查看设备：

```bash
./build/linux-debug/apps/gui_qt/ZzRkDevTool
./build/linux-debug/apps/cli/zzrkdevtool-cli list
```

`linux-release` 用于优化构建，`linux-sanitize` 启用 ASan/UBSan。机器专用路径可写入已被 Git 忽略的 `CMakeUserPresets.json`。

<details>
<summary>📦 仅构建 CLI，或验证不依赖 Qt/libusb 的核心目标</summary>

仅构建 CLI，保留 USB 与镜像烧录能力：

```bash
cmake -S . -B build/cli-only -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DZZRK_BUILD_GUI=OFF
cmake --build build/cli-only --parallel
ctest --test-dir build/cli-only --output-on-failure
./build/cli-only/apps/cli/zzrkdevtool-cli
```

不安装 Qt 或 libusb，验证可移植核心、协议和当前平台基础代码：

```bash
cmake --preset portable-debug
cmake --build --preset portable-debug --parallel
ctest --preset portable-debug
```

portable 构建不生成 GUI、CLI 或 USB 传输目标。

</details>

### 3. 运行 GPUI 实验前端（Linux，可选）

准备 Rust、libclang、CMake/Ninja，以及 Linux 窗口与图形开发依赖：

```bash
sudo apt install libclang-dev libxcb1-dev libxkbcommon-dev \
  libxkbcommon-x11-dev libwayland-dev mesa-vulkan-drivers

cd rust
cargo test -p zzrk-sys
cargo run -p gui_gpui
# 发布模式开发构建
cargo build --release -p gui_gpui
```

Cargo 会自动通过 CMake 构建 C++ FFI 共享库，无需事先编译 Qt GUI 或 CLI。产物位于 `rust/target/debug/gui_gpui` 或 `rust/target/release/gui_gpui`。文件选择依赖 `xdg-desktop-portal`；缺少该服务时可手动输入路径。软件 Vulkan 和依赖排查见[GPUI 编译说明](docs/BUILD_AND_PACKAGE.zh-CN.md#5-rust-gpui-前端实验性)。

<a id="cli"></a>

## 💻 命令行使用

下面以 `build/linux-debug` 构建为例。`1-5` 是示例物理 USB 端口，执行设备命令前请用 `list` 获取实际端口路径。

### 查看设备与固件

```bash
./build/linux-debug/apps/cli/zzrkdevtool-cli list
./build/linux-debug/apps/cli/zzrkdevtool-cli info 1-5 --json
./build/linux-debug/apps/cli/zzrkdevtool-cli partitions 1-5 --json

./build/linux-debug/apps/cli/zzrkdevtool-cli inspect-image update.img
./build/linux-debug/apps/cli/zzrkdevtool-cli plan-image update.img --json
```

`inspect-image`、`plan-image` 和 `flash-image` 均在接受固件前验证 RKFW 尾部 MD5。校验使用共享 C++ 核心内建的 RFC 1321 实现，无需 Qt 或额外 MD5 库。

### 完整升级

先检查 RAM Loader、芯片和存储准备流程：

```bash
./build/linux-debug/apps/cli/zzrkdevtool-cli \
  flash-image 1-5 update.img --yes --prepare-only
```

`--prepare-only` 不写入 eMMC，但会在需要时复位到 Maskrom、下载 DDR/usbplug 到 RAM 并等待 USB 重枚举，因此会改变设备运行状态。

确认目标设备与固件后，执行完整烧录并保存报告：

```bash
./build/linux-debug/apps/cli/zzrkdevtool-cli \
  flash-image 1-5 update.img --yes --verify critical \
  --report upgrade-report.json
```

> **操作提示：** 完整升级、分区写入和擦除会覆盖设备数据。请核对物理端口、镜像与分区，并提前备份需要保留的内容。

完整升级依次准备 RAM Loader，将固件内的 `MiniLoaderAll.bin` 转换为 New IDBlock 写入 eMMC LBA 64，再写入 GPT 和固件实际携带的全部分区 payload。仅存在于 `parameter`、没有对应 payload 的分区保持原有内容。

| 选项 | 行为 |
| --- | --- |
| `--verify critical` | 默认；回读 GPT、Loader 和启动链分区 |
| `--verify all` | 回读全部已写入分区；Sparse 校验 RAW/FILL，跳过 DONT_CARE 区域 |
| `--verify none` | 关闭写后回读校验，仍需通过固件 MD5 验证 |
| `--prepare-only` | 仅完成 RAM Loader、芯片、eMMC 身份与容量检查 |
| `--no-reboot` | 成功后保留 RAM Loader，不自动重启 |
| `--report <file>` | 原子写入 JSON 诊断报告，默认只记录固件文件名 |
| `--report-include-firmware-path` | 在报告中额外记录固件完整路径 |
| `--json` | stdout 输出 JSON Lines；日志保留在 stderr |

成功升级后默认重启设备。`Ctrl+C` 在传输块边界取消，停止新存储命令、禁止自动重启，CLI 返回状态码 `130`。

### 分区备份、写入与擦除

```bash
# 备份；输出文件已存在时拒绝覆盖
./build/linux-debug/apps/cli/zzrkdevtool-cli \
  read-partition 1-5 boot_a --output boot_a.img

# 写入；默认开启回读校验
./build/linux-debug/apps/cli/zzrkdevtool-cli \
  write-partition 1-5 boot_a --input boot.img --yes

# 擦除指定分区
./build/linux-debug/apps/cli/zzrkdevtool-cli \
  erase-partition 1-5 cache --yes
```

分区名称必须来自经过 CRC 和范围校验的设备主 GPT，并且精确、唯一匹配。写入镜像必须小于或等于分区容量，只对最后一个不完整扇区补零；`--no-verify` 可显式关闭回读。

<details>
<summary>🔎 更多命令与结构化输出</summary>

| 命令 | 用途 |
| --- | --- |
| `probe <port-path>` | 仅打开、声明和释放接口，不发送 RockUSB 命令 |
| `read-chip-info <port-path>` | 读取 16 字节 ChipInfo |
| `read-capability <port-path>` | 读取 Loader 能力信息 |
| `read-flash-info <port-path>` | 读取 FlashInfo，按扇区数报告容量 |
| `read-flash-id <port-path>` | 读取 FlashID；识别已验证的 `EMMC ` 标记 |
| `read-lba <port-path> --start <lba> --count <sectors> --output <file>` | 按范围只读导出数据 |
| `reset <port-path>` | 复位明确选择的设备 |
| `--version` | 查看版本 |
| 不带参数运行 `zzrkdevtool-cli` | 查看完整用法 |

`list`、`info`、`inspect-image`、`plan-image`、`partitions`、按名称的分区操作和 `flash-image` 支持 `--json`，当前 schema version 为 `1`。完整升级输出零条或多条 `progress` 记录，最后输出一条 `result` 记录。

升级报告包含设备身份、固件 MD5、不可变烧录计划、校验策略、逐步骤耗时和最终结果或类型化错误。报告默认省略固件完整路径，便于分享诊断信息。

</details>

<a id="platforms"></a>

## 🖥️ 平台与打包

| 平台 | 构建与产物 | 当前验证范围 |
| --- | --- | --- |
| 🐧 Linux | GCC/Clang；Ubuntu 26.04 x86_64 `.deb` | 已有本地构建、安装冒烟及 RK3588/eMMC CLI 真机验证；Qt 页面验收事项见待办清单 |
| 🪟 Windows | MSVC x64 TGZ；另有 MinGW GUI 验证配置 | 构建、测试和打包工作流已配置；真机、WinUSB 驱动与正式签名待验证 |
| 🍎 macOS | Clang；包含 Qt GUI 与 CLI 的 DMG | 构建、测试和打包工作流已配置；真机、Developer ID 签名与公证待验证 |

Linux Release 测试与打包：

```bash
cmake --preset linux-release \
  -DCMAKE_PREFIX_PATH="$HOME/Qt/6.11.1/gcc_64"
cmake --build --preset linux-release --parallel
ctest --test-dir build/linux-release --output-on-failure
cpack --preset linux-deb

sudo apt install ./build/linux-release/zzrkdevtool_0.1.0_amd64.deb
```

Linux DEB 包含 Qt GUI、CLI、Qt/ICU 运行库、许可证和 SPDX 组件清单，以及桌面入口与限定 Rockchip 设备的 udev 规则；Zzlibusb 已静态链接。另提供解压即用的 TGZ 便携包。桌面 `uaccess` 或专用 `zzrkdevtool` 组提供 USB 权限，应用以普通用户运行。

Windows TGZ 不安装或替换 USB 驱动，真机访问需要兼容 WinUSB 驱动。macOS CI 配置采用 ad-hoc 签名；正式发布还需 Developer ID 与 Apple 公证。各平台完整命令见[编译、测试与打包指南](docs/BUILD_AND_PACKAGE.zh-CN.md)。

## 📌 当前支持边界

- 写入目标为已验证 RK3588/RK3588S 的 **eMMC user area**，不操作 boot0、boot1 或 RPMB。
- 支持 RKFW/RKAF 完整固件、文本 `parameter.txt`、独立 Loader、Raw 和 Android Sparse；GUI 独立镜像下载不解析二进制 `config.cfg`，不开放任意写入 LBA。
- 所有设备操作都显式选择物理 USB 端口；烧录前复核设备身份、接口布局、芯片、存储标记、容量和镜像计划。
- 当前 Loader 对盘尾备份 GPT 的读取返回不透明数据：主 GPT 严格验证，备份 GPT 写入报告“已提交”，读取时可报告 `unavailable`。
- 第二块板卡、更多 Loader revision 和 Windows/macOS 真机兼容矩阵仍在待办清单中。
- SSH/SFTP 远程固件选择目前处于方案讨论阶段，尚未实现。

<details>
<summary>🔬 已验证的 USB 身份与设备信息解释</summary>

| 模式 | VID:PID | bcdUSB | bcdDevice |
| --- | --- | --- | --- |
| Loader | `2207:350b` | `0x0201` | `0x0223` |
| Maskrom | `2207:350b` | `0x0200` | `0x0100` |

设备详情、GPT 和按名称分区操作要求已验证的 Loader 身份；完整升级还接受上述 Maskrom 身份。仅匹配 VID/PID 不足以通过检查，还需满足对应接口与端点布局。

FlashInfo 中的容量按扇区数计算，其他字段保留兼容性代码含义，不据此推断厂商、物理几何或介质类型。FlashID 的五字节 `EMMC ` 标记用于识别 eMMC，不代表 CID、厂商、容量或硬件分区；其他响应保留为不透明数据。

热插拔运行库能力不足时，Qt GUI 显示错误并保留手动刷新，不回退到周期轮询。

</details>

## 🏗️ 项目结构

```text
apps/
├── cli/                 命令行入口与结构化输出
└── gui_qt/              ZzPureTools 窗口、页面、控制器与进度展示
src/
├── core/                基础类型与强类型存储单位
├── protocol/            RockUSB 编解码
├── session/             会话状态与类型化设备命令
├── image/               固件、Loader、Raw/Sparse 解析与写入
├── storage/             GPT 校验与分区布局
├── transport/           libusb 传输、扫描与热插拔
├── workflow/            设备管理、MD5、升级、镜像下载与诊断报告
├── ffi/                 C ABI，供 Rust 等前端调用
├── platform/            平台服务
└── logging/             spdlog 日志封装
rust/
├── zzrk-sys/            Rust 绑定与自动构建 C++ FFI
└── gui_gpui/            gpui-kit 实验界面
tests/                   协议、工作流、GUI 与安装测试
packaging/               Linux / Windows / macOS 打包与许可证
docs/                    构建指南、开发计划、硬件与协议记录
```

无硬件测试覆盖协议编解码、会话故障、镜像和 GPT 校验、热插拔、工作流、JSON/报告契约以及 Qt 模拟后端页面操作。安装冒烟验证打包后程序的启动与运行库部署；真机能力以硬件验证记录为准。

<a id="docs"></a>

## 📚 文档导航

| 文档 | 内容 |
| --- | --- |
| [🛠️ 中文编译与打包指南](docs/BUILD_AND_PACKAGE.zh-CN.md) | Linux、Windows、macOS、GPUI 的环境与构建命令，签名和常见问题 |
| [🗺️ 项目计划与待办](docs/PROJECT_PLAN.zh-CN.md#18-待验证与发布清单) | 真机验收、第二块板卡、兼容矩阵与发布事项 |
| [🐧 Linux 安装与 USB 权限](packaging/linux/README.md) | DEB、udev、用户组、安装与卸载 |
| [🪟 Windows 打包与驱动](packaging/windows/README.md) | TGZ、libusb、WinUSB 与签名策略 |
| [🍎 macOS 打包与签名](packaging/macos/README.md) | DMG、运行库部署、Developer ID 与公证 |
| [🔬 RK3588S 硬件观察](docs/hardware/rk3588s-pudu-usb-observations.md) | Loader/Maskrom USB 身份与接口记录 |
| [📡 USB 协议采集工具](tools/usbtrace/README.md) | libusb trace 的采集与对比方式 |

## 📄 许可证

项目代码采用 [MIT License](LICENSE)。第三方依赖保留各自许可证，分发说明见 [NOTICE.md](NOTICE.md) 和[第三方声明](packaging/linux/THIRD_PARTY_NOTICES.md)；安装包附带许可证正文与 SPDX 组件清单。
