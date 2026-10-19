# ZzRkDevTool 各平台编译与打包指南

本文说明如何从源码编译、测试和打包 ZzRkDevTool。项目要求 Qt 6.8 或以上、C++20、CMake 3.25 或以上；本文命令以 Qt 6.12.0 为例。使用 Visual Studio 2026 生成器时，CMake 必须为 4.2 或以上。

USB 依赖统一由 `thirdParty/Zzlibusb` 子模块从源码静态构建（当前版本 1.0.30），不再查找预编译 libusb，也不要求安装额外的 libusb DLL/dylib/so。主项目自动启用 Windows hotplug 后端。运行时仍通过 `libusb_has_capability(LIBUSB_CAP_HAS_HOTPLUG)` 检查能力；能力缺失时显示错误并允许手动刷新，不退回定时轮询。旧的 `thirdParty/libusb` 二进制暂时保留，待 Linux 真机验证通过后再清理；`ZZRK_LIBUSB_ROOT` 已不再参与构建。

三平台的默认布局：

| 平台 | 编译方式 | 安装/解压后入口 |
| --- | --- | --- |
| Windows TGZ | Ninja + MSVC，或独立 VS 2022/2026 生成器 | 包根目录的 `ZzRkDevTool.exe` 和 `zzrkdevtool-cli.exe`；Qt DLL、`qt.conf` 同目录，插件在 `plugins/` |
| Linux DEB | Ninja + GCC/Clang | `/usr/bin`；私有 Qt 库和插件位于 `/usr/lib/zzrkdevtool` |
| Linux 便携包 `.tar.gz` | Ninja + GCC/Clang | 解压目录根部的 GUI/CLI；依赖在 `lib/`，插件在 `plugins/` |
| macOS DMG | Ninja + Apple Clang | `ZzRkDevTool.app`；GUI 和 CLI 均位于 `Contents/MacOS` |

Ninja 负责调度编译，不决定编译器和 ABI。Linux/macOS 使用它没有额外限制；Windows MSVC 必须在对应 VS 的 x64 开发者终端中配置。生成器不同的预设使用独立目录，不要在同一构建目录中切换 Ninja 和 Visual Studio。

Qt 6.10 及以上使用插件筛选参数；Qt 6.8/6.9 使用默认完整插件部署，避免传入旧 SDK 尚不支持的参数。实际平台验收仍以使用的 Qt 套件为准。

当前状态：

- Linux 的自动化测试覆盖源码 USB 构建、GUI/CLI 和临时安装；真实 USB 通信需要按下文单独验收。
- Windows x86_64 TGZ 与 macOS DMG 已建立 CMake/CPack 和 CI 配置，需在对应系统或 CI runner 上完成首次验证。
- Windows TGZ 尚未正式签名，也不包含 USB 驱动安装程序。
- macOS CI 产物只使用 ad-hoc 签名，尚未完成 Developer ID 签名和 Apple 公证。
- 使用 Qt 6.12 官方预编译库时，macOS GUI 的最低系统版本为 14.4；macOS 预设和 CI 均显式设置部署目标。不能只把应用目标降低到框架的 13.3：依赖的 Qt 库也必须支持该版本。参见 [Qt 官方 macOS 支持说明](https://doc.qt.io/qt-6/macos.html#supported-configurations)。

所有命令都应在仓库根目录执行。不要使用 root 或管理员身份运行 ZzRkDevTool 本身；管理员权限只用于安装系统依赖、软件包或 USB 访问规则。

GitHub 自动构建、`continuous-build` 预发布、版本标签触发的正式版草稿与 Gitee 准备项见 [CI 与发布指南](CI_AND_RELEASE.zh-CN.md)。

## 1. 通用准备

首次获取源码后递归初始化固定版本的所有子模块，包括 spdlog 和完整的 ZzPureTools 框架：

三个子模块的来源统一为 GitCode HTTPS，无需子模块 SSH 密钥。已有 checkout 修改地址后，先执行 `git submodule sync --recursive` 同步本地记录；该命令只更新地址，不升级固定的子模块提交。GitHub CI 按仓库映射到公开的 GitHub 镜像。

```bash
git submodule sync --recursive
git submodule update --init --recursive
git submodule status --recursive
```

`git submodule status --recursive` 的每一行都必须以空格开头；`-` 表示尚未初始化，`+` 表示工作区提交偏离仓库记录的固定版本。发布构建不得使用这两种状态。

确认基础工具版本：

```bash
cmake --version
ninja --version
```

首次配置应用目标时，spdlog 的 CMake 配置会按固定 SHA-256 下载 fmt 12.1.0，因此需要网络。portable 构建不依赖 Qt 或 libusb，可先用于检查编译器和基础代码：

```bash
cmake --preset portable-debug
cmake --build --preset portable-debug --parallel
ctest --preset portable-debug
```

GUI 依赖所选 Qt 套件的 `CorePrivate`、`GuiPrivate` 和 `WidgetsPrivate` CMake 包，这是 ZzWindowKit 无边框平台实现的要求。必须安装与 Qt 版本和编译器 ABI 一致的私有开发头文件；构建和运行不能混用不同 Qt 版本或 MSVC/MinGW 套件。声明最低 Qt 版本不代表每个中间版本均已完成验证。Linux 可在配置前检查：

```bash
test -f "$HOME/Qt/6.12.0/gcc_64/lib/cmake/Qt6CorePrivate/Qt6CorePrivateConfig.cmake"
test -f "$HOME/Qt/6.12.0/gcc_64/lib/cmake/Qt6GuiPrivate/Qt6GuiPrivateConfig.cmake"
test -f "$HOME/Qt/6.12.0/gcc_64/lib/cmake/Qt6WidgetsPrivate/Qt6WidgetsPrivateConfig.cmake"
```

GUI 还需要 Qt `LinguistTools`（包含 `lrelease`）。英文 TS 在构建时生成 QM 并内嵌，安装包无需单独部署翻译文件。

GUI 启动路径固定为：先调用 `ZzWindowKitBootstrap::prepare()`，再构造 `ZzPureApplication`，定位配置并安装翻译，通过 `ZzApplicationBuilder` 创建唯一的 `ZzApplicationWindow`，最后由 `GuiWindowShell` 装配工作区、4 个业务标签和设置标签。首次启动选择目录的行为见 [GUI 配置说明](GUI_SETTINGS_ZH.md)。不要在应用侧另建 `QApplication`、`QMainWindow` 或 `QTabWidget`。

主要 CMake 选项：

| 选项                           | 默认值             | 说明                                                |
| ------------------------------ | ------------------ | --------------------------------------------------- |
| `ZZRK_BUILD_GUI`               | `ON`               | 构建 Qt Widgets GUI                                 |
| `ZZRK_BUILD_CLI`               | `ON`               | 构建命令行程序                                      |
| `ZZRK_BUILD_TRANSPORT`         | `ON`               | 构建 libusb transport                               |
| `ZZRK_BUILD_TESTS`             | `ON`               | 构建自动化测试                                      |
| `ZZRK_WARNINGS_AS_ERRORS`      | `OFF`              | 将项目警告视为错误；发布和 CI 应启用                |
| `ZZRK_BUNDLE_RUNTIME`          | 应用构建时 `ON`    | 安装/打包 Qt、插件和非系统运行库                    |
| `ZZRK_LINUX_PORTABLE`          | `OFF`              | 直接安装时使用 Linux 便携布局；`linux-portable-release` 自动启用。常规发包可直接从 `linux-release` 生成两种包 |
| `ZZRK_FFI_SHARED`              | `OFF`              | 将 `src/ffi` 构建为共享库；Rust 前端构建时自动启用  |
| `CMAKE_INSTALL_BINDIR`         | Windows 为 `.`，Linux 为 `bin` | 程序安装位置；macOS GUI 构建固定使用 `.app` 布局 |
| `ZZRK_MACOS_CODESIGN_IDENTITY` | macOS 预设为 ad-hoc `-` | macOS `macdeployqt` 使用的签名身份；`-` 表示 ad-hoc |

## 2. Ubuntu 26.04 x86_64

### 2.1 环境准备

安装编译和打包工具：

```bash
sudo apt update
sudo apt install build-essential cmake ninja-build pkg-config dpkg-dev udev libudev-dev
```

安装 Qt GCC x86_64 套件。下面假设 Qt 位于：

```text
$HOME/Qt/6.12.0/gcc_64
```

Linux 预设使用源码 `thirdParty/Zzlibusb`，无需安装开发版 libusb。安装 `libudev-dev` 后优先使用 udev；未找到时 Zzlibusb 可使用 netlink 后端。

### 2.2 Debug 构建

```bash
cmake --preset linux-debug \
  -DCMAKE_PREFIX_PATH="$HOME/Qt/6.12.0/gcc_64"
cmake --build --preset linux-debug --parallel
ctest --preset linux-debug
```

构建产物：

```text
build/linux-debug/apps/gui_qt/ZzRkDevTool
build/linux-debug/apps/cli/zzrkdevtool-cli
```

### 2.3 Sanitizer 构建

```bash
cmake --preset linux-sanitize \
  -DCMAKE_PREFIX_PATH="$HOME/Qt/6.12.0/gcc_64"
cmake --build --preset linux-sanitize --parallel
ctest --preset linux-sanitize
```

该预设启用 ASan/UBSan，并把编译警告视为错误。

GUI 重点检查可以单独运行：

```bash
ctest --test-dir build/linux-sanitize --output-on-failure \
  -R '^(gui_framework|gui_upgrade|gui_image_download|gui_smoke)$'
```

### 2.4 Release 和 DEB 打包

```bash
cmake --preset linux-release \
  -DCMAKE_PREFIX_PATH="$HOME/Qt/6.12.0/gcc_64"
cmake --build --preset linux-release --parallel
ctest --preset linux-release
cpack --preset linux-deb
```

当前版本的产物为：

```text
build/linux-release/zzrkdevtool_0.1.0_amd64.deb
```

检查控制信息、文件清单和哈希：

```bash
dpkg-deb --info build/linux-release/zzrkdevtool_0.1.0_amd64.deb
dpkg-deb --contents build/linux-release/zzrkdevtool_0.1.0_amd64.deb
sha256sum build/linux-release/zzrkdevtool_0.1.0_amd64.deb
```

安装并做无硬件启动检查：

```bash
sudo apt install ./build/linux-release/zzrkdevtool_0.1.0_amd64.deb
zzrkdevtool-cli --version
QT_QPA_PLATFORM=offscreen ZzRkDevTool --smoke-test
```

源码构建的无硬件 smoke 命令为：

```bash
QT_QPA_PLATFORM=offscreen \
  build/linux-release/apps/gui_qt/ZzRkDevTool --smoke-test
```

offscreen smoke 验证启动、异步扫描和确定性关闭，不用于视觉结论。视觉验收必须使用 Xvfb 的真实 xcb 路径，并把截图放到构建产物目录：

```bash
mkdir -p build/linux-debug/visual
Xvfb :99 -screen 0 1920x1080x24 \
  >build/linux-debug/visual/xvfb.log 2>&1 &
xvfb_pid=$!
trap 'kill "$xvfb_pid" 2>/dev/null || true' EXIT
DISPLAY=:99 QT_QPA_PLATFORM=xcb \
  ZZRK_GUI_SCREENSHOT_DIR="$PWD/build/linux-debug/visual" \
  build/linux-debug/tests/gui_framework_test
```

该命令生成 980×820 浅色、800×700 最小窗口和 980×820 深色截图。逐张检查标题栏拖动区与系统按钮、顶部 4 个标签、全局设备状态、表格、两级进度条和底部状态栏没有重叠或截断。截图是验收产物，不得提交到源代码目录。

桌面会话通常通过 udev `uaccess` 获得 USB 权限。无桌面或远程会话可把当前用户加入专用组，随后注销并重新登录：

```bash
sudo usermod -aG zzrkdevtool "$USER"
```

更详细的 udev 安装和卸载说明见 `packaging/linux/README.md`。

### 2.5 Linux 便携包 `.tar.gz`

便携包与 DEB 共用 `linux-release` 的一次编译、一次完整测试。执行上一节的构建后，直接生成 TGZ，无需再编译一遍：

```bash
cpack --preset linux-tgz
```

x86_64 当前版本产物为：

```text
build/linux-release/ZzRkDevTool-0.1.0-linux-x86_64-portable.tar.gz
```

解压后，程序直接位于顶层软件目录：

```text
ZzRkDevTool-0.1.0-linux-x86_64-portable/
├── ZzRkDevTool
├── zzrkdevtool-cli
├── qt.conf
├── README.zh-CN.md
├── lib/
├── plugins/
├── docs/
└── usb/60-zzrkdevtool.rules
```

例如解压到当前目录并运行：

```bash
tar -xzf build/linux-release/ZzRkDevTool-0.1.0-linux-x86_64-portable.tar.gz
cd ZzRkDevTool-0.1.0-linux-x86_64-portable
./zzrkdevtool-cli --version
./ZzRkDevTool
```

整个软件目录可以移动或重命名，无需安装 Qt。CPack 为两种包创建独立暂存目录，TGZ 在自己的目录内整理布局、调整程序及插件的相对库路径，不修改原始构建产物或 DEB。`patchelf` 是打包依赖。Qt 配置、可执行文件和插件均通过相对路径寻找包内依赖，不能只复制单个程序。

两种包生成后，可执行与 CI 相同的解包启动验证（含空格路径、清除 Qt/动态库环境变量、检查插件使用包内 Qt），不安装软件，也不访问 USB 硬件：

```bash
bash .github/scripts/verify-linux-packages.sh
```

仍保留 `linux-portable-release` 配置，供需要 `cmake --install` 直接获得便携布局的开发场景使用；该独立配置可在构建后用 `cpack --config build/linux-portable-release/CPackConfig.cmake -G TGZ` 打包。常规 CI 和发包使用前面的单次构建流程。

便携包不自动修改系统 udev 配置；USB 权限不足时按包根目录 `README.zh-CN.md` 手动安装 `usb/` 中的规则。DEB 则由安装脚本处理。便携包仍要求目标系统提供兼容的 glibc、C++ 运行库和图形系统，不表示能跨所有 Linux 发行版运行。

### 2.6 Zzlibusb 真机验收

先使用本次源码构建的程序，不要混用旧 `build/apps` 目录中的程序：

```bash
build/linux-release/apps/cli/zzrkdevtool-cli --version
build/linux-release/apps/cli/zzrkdevtool-cli list
build/linux-release/apps/gui_qt/ZzRkDevTool
```

在 GUI 中验证设备插入、拔出、Loader/Maskrom 模式变化和端口选择，无需手动刷新。关闭 GUI 后，按 `list` 输出替换下方 `1-5`（以下为只读检查）：

```bash
build/linux-release/apps/cli/zzrkdevtool-cli info 1-5
build/linux-release/apps/cli/zzrkdevtool-cli read-chip-info 1-5
build/linux-release/apps/cli/zzrkdevtool-cli read-flash-info 1-5
build/linux-release/apps/cli/zzrkdevtool-cli partitions 1-5
```

`read-*` 和分区操作要求设备处于 Loader 模式。自动化检查通过不等于 USB 真机验证通过；旧二进制目录将在真机验证通过后单独删除。

## 3. Windows 10/11 x86_64（MSVC）

### 3.1 环境准备

安装以下组件：

- Visual Studio 2022 或 2026，并选择“使用 C++ 的桌面开发”和 MSVC x64 工具链。
- CMake 3.25 或更高版本与 Ninja，并加入 `PATH`。
- 与 MSVC 工具链 ABI 匹配的 Qt 套件，例如 Qt 6.12.0 `msvc2022_64`。
- Zzlibusb 子模块；不再需要 vcpkg libusb 或手工复制 USB DLL。

在对应版本的 x64 开发者 PowerShell 中设置 Qt 路径：

```powershell
$env:QT_PREFIX_PATH = "D:/SoftWare/Qt/6.12.0/msvc2022_64"
```

Ninja 使用当前开发者环境中的编译器。首次配置前可运行 `where.exe cl` 和 `where.exe ninja` 确认工具可用。更换 VS 工具链时建议使用新的构建目录。

### 3.2 Debug / Release 构建和 TGZ 打包

`windows-msvc-debug`、`windows-msvc-release` 均使用 Ninja，构建类型由预设固定；本地调试使用 Debug，发布使用 Release。Windows 预设显式设置 `CMAKE_INSTALL_BINDIR=.`，已有缓存中的 `bin` 也会被更新。

在“x64 Native Tools Command Prompt for VS 2022”或已加载 MSVC 环境的 PowerShell 中执行：

```powershell
cmake --preset windows-msvc-release
cmake --build --preset windows-msvc-release --parallel
ctest --preset windows-msvc-release
ctest --preset windows-msvc-release -R "^(gui_framework|gui_smoke)$"
cpack --preset windows-msvc-tgz
```

需要 Visual Studio 工程文件时，使用独立预设（不与 Ninja 目录混用）：

```powershell
cmake --preset windows-vs2022-release
cmake --build --preset windows-vs2022-release --parallel
ctest --preset windows-vs2022-release
cpack --preset windows-vs2022-tgz
```

VS 2026 将名称中的 `vs2022` 换成 `vs2026`，并使用 CMake 4.2 或以上。对应 Debug 预设为 `windows-vs2022-debug`、`windows-vs2026-debug`。这些预设在构建、测试和打包时明确指定配置，不依赖对 Visual Studio 生成器无效的 `CMAKE_BUILD_TYPE`。若旧 `windows-msvc-debug` 目录由 VS 生成器创建，改用新目录或先归档旧目录，不能直接在其中运行 Ninja。

当前版本的预期产物为：

```text
build/windows-msvc-release/ZzRkDevTool-0.1.0-windows-x86_64.tar.gz
```

### 3.3 解压后检查

```powershell
$archive = (Resolve-Path `
  build/windows-msvc-release/ZzRkDevTool-0.1.0-windows-x86_64.tar.gz).Path
New-Item -ItemType Directory -Force build/windows-package | Out-Null
Push-Location build/windows-package
try {
  cmake -E tar xzf $archive
  if ($LASTEXITCODE -ne 0) { throw "TGZ 解压失败" }
} finally {
  Pop-Location
}
$gui = Get-ChildItem build/windows-package -Recurse -Filter ZzRkDevTool.exe |
  Select-Object -First 1
$cli = Get-ChildItem build/windows-package -Recurse -Filter zzrkdevtool-cli.exe |
  Select-Object -First 1
if (-not $gui -or -not $cli) {
  throw "打包后的 GUI 或 CLI 缺失"
}
$env:QT_QPA_PLATFORM = "offscreen"
& $gui.FullName --smoke-test
& $cli.FullName --help
```

Windows 和 Linux 便携包统一使用 CPack TGZ 格式（扩展名 `.tar.gz`）。可使用上述 CMake 命令或支持该格式的解压工具打开。程序应直接位于压缩包内的顶层软件目录，Qt DLL、`qt.conf` 与 exe 同级，插件位于 `plugins/`；没有额外的 `bin/`。不要单独拷贝 exe，需保留其 Qt 依赖。libusb 已静态链接，因此不需要 `libusb-1.0.dll`。

TGZ 不安装 USB 驱动。RockUSB 真机访问要求设备绑定兼容 WinUSB 驱动；正式发布必须使用项目自有并可信签名的 INF/CAT。首次切换驱动前应记录原驱动，避免影响 Rockchip 厂商工具。详细策略见 `packaging/windows/README.md`。

正式发布还需要对所有 EXE/DLL 做 Authenticode SHA-256 签名和时间戳验证，然后重新生成 TGZ。证书、密码、token 和私钥不得写入仓库或 CI 日志。

### 3.4 Windows MinGW 验证

Qt MinGW 套件必须与 MinGW-w64 编译器匹配，不能与 MSVC Qt 混用。预设不再硬编码 Qt 安装位置或编译器版本；请先将所用 Qt 的 MinGW 工具链加入 `PATH`，并将 `QT_PREFIX_PATH` 指向对应的 `mingw_64` 目录。

```powershell
$env:QT_PREFIX_PATH = "C:/Qt/6.12.0/mingw_64"
# 按实际安装位置修改 MinGW 工具链目录。
$env:PATH = "C:/Qt/Tools/mingw1310_64/bin;$env:PATH"
cmake --preset windows-mingw-release
cmake --build --preset windows-mingw-release --parallel
ctest --preset windows-mingw-release -R "^(gui_framework|gui_smoke)$"
```

MinGW 同样使用源码 Zzlibusb，当前预设只验证构建，不启用运行库打包。Windows MinGW、MSVC 和 macOS 需在对应系统或 CI 验证；Linux 上的配置检查不能替代原生编译。在 Windows 桌面会话中还需验证标题栏、系统按钮和 DPI 缩放。

## 4. macOS

### 4.1 环境准备

安装 Xcode Command Line Tools、Homebrew、CMake 和 Ninja：

```bash
xcode-select --install
brew install cmake ninja
```

安装支持目标架构的 Qt macOS 套件。下面假设 Qt 位于：

```text
$HOME/Qt/6.12.0/macos
```

### 4.2 Release 构建和 DMG 打包

macOS 预设按架构分为 `macos-arm64-*` 与 `macos-x86_64-*`，均使用 Ninja，默认 ad-hoc 签名（`ZZRK_MACOS_CODESIGN_IDENTITY=-`）。Zzlibusb 随目标架构一起静态构建，无需准备 Homebrew libusb：

```bash
cmake --preset macos-arm64-release \
  -DCMAKE_PREFIX_PATH="$HOME/Qt/6.12.0/macos"
cmake --build --preset macos-arm64-release --parallel
ctest --preset macos-arm64-release
ctest --preset macos-arm64-release -R '^(gui_framework|gui_smoke)$'
cpack --preset macos-arm64-dmg
```

x86_64 构建把预设换为 `macos-x86_64-release` 与 `macos-x86_64-dmg`；在 Apple Silicon 上运行 x86_64 测试还需要 Rosetta。

当前版本的预期产物为：

```text
build/macos-arm64-release/ZzRkDevTool-0.1.0-macos-arm64.dmg
```

### 4.3 DMG 检查

挂载 DMG 后，将下方 `<挂载目录>` 替换成 `hdiutil attach` 输出的 `/Volumes/...` 路径：

```bash
hdiutil attach -nobrowse -readonly \
  build/macos-arm64-release/ZzRkDevTool-0.1.0-macos-arm64.dmg
test -x "<挂载目录>/ZzRkDevTool.app/Contents/MacOS/ZzRkDevTool"
test -x "<挂载目录>/ZzRkDevTool.app/Contents/MacOS/zzrkdevtool-cli"
test -f "<挂载目录>/ZzRkDevTool.app/Contents/PlugIns/platforms/libqcocoa.dylib"
otool -L "<挂载目录>/ZzRkDevTool.app/Contents/MacOS/ZzRkDevTool"
otool -L "<挂载目录>/ZzRkDevTool.app/Contents/MacOS/zzrkdevtool-cli"
"<挂载目录>/ZzRkDevTool.app/Contents/MacOS/zzrkdevtool-cli" --help
codesign --verify --deep --strict "<挂载目录>/ZzRkDevTool.app"
hdiutil detach "<挂载目录>"
```

`otool -L` 不应出现 libusb 动态库依赖；Qt 框架应解析到 bundle 内的 `Contents/Frameworks`。任何指向 `/opt/homebrew`、`/usr/local` 或构建机的依赖路径都表示打包不完整。许可证位于 `ZzRkDevTool.app/Contents/Resources/doc/zzrkdevtool/licenses`。

### 4.4 正式签名和公证

正式发布时把预设默认的 ad-hoc `-` 替换为真实 Developer ID Application 身份：

```bash
cmake --preset macos-arm64-release \
  -DZZRK_MACOS_CODESIGN_IDENTITY="Developer ID Application: Example (TEAMID)"
```

重新构建 DMG 后执行公证和 staple：

```bash
xcrun notarytool submit \
  build/macos-arm64-release/ZzRkDevTool-0.1.0-macos-arm64.dmg \
  --keychain-profile ZZRK_NOTARY --wait
xcrun stapler staple \
  build/macos-arm64-release/ZzRkDevTool-0.1.0-macos-arm64.dmg
spctl --assess --type open --context context:primary-signature -v \
  build/macos-arm64-release/ZzRkDevTool-0.1.0-macos-arm64.dmg
```

签名身份、App Store Connect 密钥、密码和公证 profile 必须存放在开发者 Keychain 或 CI secret store 中。详细策略见 `packaging/macos/README.md`。

## 5. Rust GPUI 前端（实验性）

`rust/` 是一个独立的 Cargo workspace，包含第二个 GUI 前端（gpui-kit 组件库），与 Qt GUI 并行维护、共享同一套 C++ 核心：

- `rust/zzrk-sys`：`src/ffi` C ABI 的 Rust 绑定（bindgen 生成）。其 `build.rs` 自动调用 CMake，以 `ZZRK_FFI_SHARED=ON` + PIC 在 `rust/target/` 下的专用构建树编译核心库并链接 `libzzrk_ffi` 共享库，**无需先手动执行 CMake 构建**
- `rust/gui_gpui`：GPUI 桌面应用本体（设备、完整升级、下载镜像、分区四页，样式对齐 Qt 版）

### 5.1 环境准备

Rust 稳定版工具链（开发验证版本 1.97.1）加上 C++ 侧的 CMake/Ninja/libusb 准备（见第 1、2 节）。Linux 额外需要窗口与图形系统开发包：

```bash
sudo apt install libxcb1-dev libxkbcommon-dev libxkbcommon-x11-dev libwayland-dev mesa-vulkan-drivers
```

说明：

- GPUI 走 Vulkan 渲染；无 GPU 的环境可装 mesa-vulkan-drivers 后用软件 Vulkan（lavapipe）运行：`VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json cargo run -p gui_gpui`
- 缺 `libxkbcommon-x11-dev` 时 `gui_gpui/build.rs` 会自动建私有符号链接兜底；装上 dev 包后该兜底自动失效
- 文件选择对话框依赖 xdg-desktop-portal（多数桌面默认自带）；无 portal 的环境下浏览按钮不可用，可直接在输入框手填路径

### 5.2 构建与运行

```bash
cd rust
cargo test                    # zzrk-sys 单元测试（无需硬件）
cargo run -p gui_gpui         # 开发模式运行 GPUI 界面
cargo build --release -p gui_gpui   # 发布构建
```

首次构建需下载并编译 gpui 依赖树（约 700 个 crate，10–20 分钟），之后增量构建为秒级。产物位于 `rust/target/<profile>/gui_gpui`。

### 5.3 打包现状

GPUI 前端尚未接入 CPack/DEB/TGZ/DMG 打包链路。`gui_gpui` 二进制经 rpath 引用 `rust/target/` 构建树内的 `libzzrk_ffi.so`，直接拷贝到其他机器无法运行；发布前需要补齐安装规则（核心共享库 + 可执行文件的相对 RPATH）。该工作列入后续迭代，当前仅以 Qt GUI + CLI 作为正式发布产物。

## 6. 安装许可证目录和硬件限制

打包后必须保留 `LICENSE`、`NOTICE.md` 和各依赖许可证。默认安装位置如下：

| 平台 | 文档根目录 | 第三方许可证目录 |
| --- | --- | --- |
| Linux | `/usr/share/doc/zzrkdevtool` | `/usr/share/doc/zzrkdevtool/licenses` |
| Linux 便携包 | `docs` | `docs/licenses` |
| Windows TGZ | `share/doc/zzrkdevtool` | `share/doc/zzrkdevtool/licenses` |
| macOS bundle | `ZzRkDevTool.app/Contents/Resources/doc/zzrkdevtool` | `ZzRkDevTool.app/Contents/Resources/doc/zzrkdevtool/licenses` |

自动化和 offscreen 测试使用模拟后端，只能验证无硬件启动、页面工作流和确定性关闭；不得据此宣称真实 RK3588/eMMC 读写成功。连接目标设备前，应先确认供电、USB 线材、设备模式、权限或驱动，并在专用测试设备上依次执行只读扫描、分区读取和 `--prepare-only`。完整刷写会改写存储介质，必须另行确认镜像、分区目标、备份与恢复路径。

## 7. CI 构建

`.github/workflows/ci-linux.yml`、`ci-windows.yml` 和 `ci-macos.yml` 分别定义三个平台的构建、测试、解包启动检查和产物上传；Windows 还保留 MinGW GUI 验证。各 job 均直接构建 Zzlibusb，不再自举 vcpkg/Homebrew/autotools libusb。Linux CTest 额外验证 GUI/CLI 安装和不依赖 UI 框架的 CLI 单独构建。测试数量以当前 `ctest -N` 输出为准。工作流由主分支 `push`、`pull_request`、`vX.Y.Z` 标签 push 或手工触发，共用 `publish-artifacts.yml` 处理预发布和正式版草稿，详见 [CI 与发布指南](CI_AND_RELEASE.zh-CN.md)。

CI 通过只说明对应 runner 上能够编译、测试、部署和启动，不能替代 USB 真机、Windows 驱动、正式签名或 macOS 公证验证。

## 8. 常见问题

### CMake 找不到 Qt

确认 `CMAKE_PREFIX_PATH` 指向包含 `lib/cmake/Qt6/Qt6Config.cmake` 的 Qt 套件根目录，并确保 Qt 编译器 ABI 与当前工具链一致。例如 MSVC 不能使用 MinGW Qt。

### CMake 找不到 Zzlibusb

执行 `git submodule update --init --recursive`，确认 `thirdParty/Zzlibusb/CMakeLists.txt` 存在。不要再设置 `ZZRK_LIBUSB_ROOT`；旧目录可以暂时保留，但不会参与构建。

### GUI 报告热插拔检测不可用

确认正在运行新的源码构建产物，且 `libusb_has_capability(LIBUSB_CAP_HAS_HOTPLUG)` 返回非零。Windows 热插拔由主项目在源码构建时启用。本项目不提供旧版兼容或轮询回退。

### 修改依赖路径后仍使用旧缓存

CMake 3.25 或更高版本可以重新配置并清理缓存：

```bash
cmake --fresh --preset linux-release \
  -DCMAKE_PREFIX_PATH="$HOME/Qt/6.12.0/gcc_64"
```

Windows/macOS 使用对应的 `cmake --fresh --preset windows-msvc-release`（或 `windows-mingw-release`、`macos-arm64-release`、`macos-x86_64-release`）加原有参数。

### 打包成功但目标机器无法启动

- Linux：检查 `dpkg-deb --info` 的系统依赖和 `/usr/lib/zzrkdevtool`，不要删除包内 `qt.conf`。
- Windows：确认包根目录包含 EXE、Qt DLL、MSVC runtime、`qt.conf` 和 `plugins/`；不需要单独的 libusb DLL。
- macOS：检查 `codesign --verify`、`otool -L`、bundle 的 `Contents/Frameworks` 和 `Contents/PlugIns`。

## 9. 发布前统一检查

- [ ] Release 构建启用 `ZZRK_WARNINGS_AS_ERRORS=ON`。
- [ ] 当前平台全部无硬件测试通过；Linux 额外通过安装 smoke test。
- [ ] 打包后的 GUI、CLI 和运行库在干净系统能够启动。
- [ ] 许可证正文、第三方声明和 SPDX SBOM 随包发布。
- [ ] 计算并保存最终包的 SHA-256。
- [ ] Windows 驱动和应用签名、macOS Developer ID 与公证门禁已完成。
- [ ] 对目标系统和 RK3588/eMMC 真机执行只读检查、`--prepare-only`、完整升级和报告导出。
- [ ] 在 GUI“下载镜像”页分别验证设备 GPT 与文本 `parameter.txt`，以及独立 Loader、Raw/Sparse、A/B 自动匹配、逐行校验、双进度、取消和完成后复位；首版不使用二进制 `config.cfg`，也不接受任意 LBA。
