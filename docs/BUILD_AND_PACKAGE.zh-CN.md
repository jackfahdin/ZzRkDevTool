# ZzRkDevTool 各平台编译与打包指南

本文说明如何从源码编译、测试和打包 ZzRkDevTool。当前发布基线为 Qt 6.11.1、C++20、CMake 3.25 或更高版本、Ninja 和 libusb 1.0.30 或更高版本。

libusb 1.0.30 是硬性最低版本，不兼容旧版本。运行库还必须通过 `libusb_has_capability(LIBUSB_CAP_HAS_HOTPLUG)` 报告当前平台具备热插拔能力；能力缺失时 GUI 会继续启动，显示明确错误并允许手动刷新，不会退回定时轮询。Windows 的 libusb 必须在编译时启用 `LIBUSB_WINDOWS_HOTPLUG`。

当前状态：

- Ubuntu 26.04 x86_64 的自包含 `.deb` 已在本地完成构建、24 项测试和临时安装验证。
- Windows x86_64 ZIP 与 macOS DMG 已建立 CMake/CPack 和 CI 配置，需在对应系统或 CI runner 上完成首次验证。
- Windows ZIP 尚未正式签名，也不包含 USB 驱动安装程序。
- macOS CI 产物只使用 ad-hoc 签名，尚未完成 Developer ID 签名和 Apple 公证。

所有命令都应在仓库根目录执行。不要使用 root 或管理员身份运行 ZzRkDevTool 本身；管理员权限只用于安装系统依赖、软件包或 USB 访问规则。

## 1. 通用准备

首次获取源码后递归初始化固定版本的所有子模块，包括 spdlog 和完整的 ZzPureTools 框架：

```bash
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

GUI 依赖 Qt 6.11.1 的 `CorePrivate`、`GuiPrivate` 和 `WidgetsPrivate` CMake 包，这是 ZzWindowKit 无边框平台实现的硬性要求。使用在线安装器时必须安装与编译器 ABI 完全一致的 Qt 6.11.1 桌面套件及私有开发头文件；不能混用其他 Qt 补丁版本、MSVC 与 MinGW 套件。Linux 可在配置前检查：

```bash
test -f "$HOME/Qt/6.11.1/gcc_64/lib/cmake/Qt6CorePrivate/Qt6CorePrivateConfig.cmake"
test -f "$HOME/Qt/6.11.1/gcc_64/lib/cmake/Qt6GuiPrivate/Qt6GuiPrivateConfig.cmake"
test -f "$HOME/Qt/6.11.1/gcc_64/lib/cmake/Qt6WidgetsPrivate/Qt6WidgetsPrivateConfig.cmake"
```

GUI 启动路径固定为：先调用 `ZzWindowKitBootstrap::prepare()`，再构造 `ZzPureApplication`，通过 `ZzApplicationBuilder` 创建唯一的 `ZzApplicationWindow`，最后由 `GuiWindowShell` 装配 `ZzWorkspaceShell` 和顶部 4 个固定业务标签。不要在应用侧另建 `QApplication`、`QMainWindow` 或 `QTabWidget`。

主要 CMake 选项：

| 选项                           | 默认值             | 说明                                                |
| ------------------------------ | ------------------ | --------------------------------------------------- |
| `ZZRK_BUILD_GUI`               | `ON`               | 构建 Qt Widgets GUI                                 |
| `ZZRK_BUILD_CLI`               | `ON`               | 构建命令行程序                                      |
| `ZZRK_BUILD_TRANSPORT`         | `ON`               | 构建 libusb transport                               |
| `ZZRK_BUILD_TESTS`             | `ON`               | 构建自动化测试                                      |
| `ZZRK_WARNINGS_AS_ERRORS`      | `OFF`              | 将项目警告视为错误；发布和 CI 应启用                |
| `ZZRK_BUNDLE_RUNTIME`          | 应用构建时 `ON`    | 安装/打包 Qt、插件和非系统运行库                    |
| `ZZRK_LIBUSB_ROOT`             | Linux 指向仓库副本 | 指定 libusb 头文件、库和版本元数据根目录            |
| `ZZRK_MACOS_CODESIGN_IDENTITY` | 空                 | macOS `macdeployqt` 使用的签名身份；`-` 表示 ad-hoc |

## 2. Ubuntu 26.04 x86_64

### 2.1 环境准备

安装编译和打包工具：

```bash
sudo apt update
sudo apt install build-essential cmake ninja-build pkg-config dpkg-dev udev
```

安装 Qt 6.11.1 GCC x86_64 套件。下面假设 Qt 位于：

```text
$HOME/Qt/6.11.1/gcc_64
```

Linux 预设默认使用仓库中的 `thirdParty/libusb`，无需另外安装开发版 libusb。

### 2.2 Debug 构建

```bash
cmake --preset linux-debug \
  -DCMAKE_PREFIX_PATH="$HOME/Qt/6.11.1/gcc_64"
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
  -DCMAKE_PREFIX_PATH="$HOME/Qt/6.11.1/gcc_64"
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
  -DCMAKE_PREFIX_PATH="$HOME/Qt/6.11.1/gcc_64"
cmake --build --preset linux-release --parallel
ctest --test-dir build/linux-release --output-on-failure
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

## 3. Windows 10/11 x86_64（MSVC）

### 3.1 环境准备

安装以下组件：

- Visual Studio 2022，并选择“使用 C++ 的桌面开发”和 MSVC x64 工具链。
- CMake 3.25 或更高版本与 Ninja，并加入 `PATH`。
- Qt 6.11.1 `msvc2022_64` 套件。
- vcpkg，以及动态库 triplet `x64-windows` 的 libusb。

在“x64 Native Tools Command Prompt for VS 2022”或已加载 MSVC 环境的 PowerShell 中执行：

```powershell
& "$env:VCPKG_ROOT\vcpkg.exe" install libusb:x64-windows `
  "--overlay-ports=$PWD/packaging/windows/vcpkg-overlay-ports"
```

必须使用仓库中的 overlay。vcpkg 的普通 Windows `Release` 配置不保证编译 libusb 1.0.30 新增的 Windows 热插拔后端；该 overlay 固定 1.0.30，并向上游 MSBuild 工程传入 `EnableWindowsHotplug=true`。

vcpkg 安装根目录应至少包含：

```text
include/libusb-1.0/libusb.h
lib/libusb-1.0.lib
bin/libusb-1.0.dll
lib/pkgconfig/libusb-1.0.pc
```

### 3.2 Release 构建和 ZIP 打包

根据本机路径设置变量：

```powershell
$qtRoot = "C:/Qt/6.11.1/msvc2022_64"
$libusbRoot = "$env:VCPKG_ROOT/installed/x64-windows"
```

配置、构建、测试并打包：

```powershell
cmake -S . -B build/windows-release -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_PREFIX_PATH="$qtRoot" `
  -DZZRK_BUILD_GUI=ON `
  -DZZRK_LIBUSB_ROOT="$libusbRoot" `
  -DZZRK_BUNDLE_RUNTIME=ON `
  -DZZRK_WARNINGS_AS_ERRORS=ON
cmake --build build/windows-release --parallel
ctest --test-dir build/windows-release --output-on-failure
ctest --test-dir build/windows-release --output-on-failure `
  -R "^(gui_framework|gui_smoke)$"
cpack --config build/windows-release/CPackConfig.cmake -G ZIP
```

当前版本的预期产物为：

```text
build/windows-release/zzrkdevtool-0.1.0-windows-x86_64.zip
```

### 3.3 解压后检查

```powershell
Expand-Archive `
  build/windows-release/zzrkdevtool-0.1.0-windows-x86_64.zip `
  -DestinationPath build/windows-package
$gui = Get-ChildItem build/windows-package -Recurse -Filter ZzRkDevTool.exe |
  Select-Object -First 1
$cli = Get-ChildItem build/windows-package -Recurse -Filter zzrkdevtool-cli.exe |
  Select-Object -First 1
$libusb = Get-ChildItem build/windows-package -Recurse -Filter libusb-1.0.dll |
  Select-Object -First 1
if (-not $gui -or -not $cli -or -not $libusb) {
  throw "打包后的 GUI、CLI 或 libusb DLL 缺失"
}
$env:QT_QPA_PLATFORM = "offscreen"
& $gui.FullName --smoke-test
& $cli.FullName --help
```

ZIP 不安装 USB 驱动。RockUSB 真机访问要求设备绑定兼容 WinUSB 驱动；正式发布必须使用项目自有并可信签名的 INF/CAT。首次切换驱动前应记录原驱动，避免影响 Rockchip 厂商工具。详细策略见 `packaging/windows/README.md`。

正式发布还需要对所有 EXE/DLL 做 Authenticode SHA-256 签名和时间戳验证，然后重新生成 ZIP。证书、密码、token 和私钥不得写入仓库或 CI 日志。

### 3.4 Windows MinGW 验证

Qt 的 MinGW 套件必须与本机 MinGW-w64 编译器完全匹配，不能和 `msvc2022_64` 混用。仓库的 vcpkg overlay 调用 MSBuild，仅适用于上面的 MSVC 流程；MinGW 流程应使用 Qt 6.11.1 的 `win64_mingw` 套件和 `tools_mingw1310`，并从官方 libusb 1.0.30 tarball 本地构建动态库（必须启用 Windows hotplug）。下面的命令在 Git Bash 中执行，会把 libusb 安装到 `build/libusb-mingw`：

```bash
set -euo pipefail

qtRoot="/c/Qt/6.11.1/mingw_64"
mingwBin="/c/Qt/Tools/mingw1310_64/bin"
export PATH="$mingwBin:$PATH"
if command -v x86_64-w64-mingw32-gcc >/dev/null 2>&1; then
  export CC=x86_64-w64-mingw32-gcc
  export CXX=x86_64-w64-mingw32-g++
else
  export CC=gcc
  export CXX=g++
fi

mkdir -p build
curl --fail --location --retry 3 \
  --output build/libusb-1.0.30.tar.bz2 \
  https://github.com/libusb/libusb/releases/download/v1.0.30/libusb-1.0.30.tar.bz2
tar -xjf build/libusb-1.0.30.tar.bz2 -C build
pushd build/libusb-1.0.30
./configure \
  --host=x86_64-w64-mingw32 \
  --enable-windows-hotplug \
  --enable-shared \
  --disable-static \
  --prefix="$PWD/../libusb-mingw"
mingw32-make -j"$(nproc)"
mingw32-make install
popd

export PATH="$PWD/build/libusb-mingw/bin:$PATH"
cmake -S . -B build/windows-mingw-release -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$qtRoot" \
  -DZZRK_BUILD_GUI=ON \
  -DZZRK_BUILD_CLI=ON \
  -DZZRK_BUILD_TESTS=ON \
  -DZZRK_LIBUSB_ROOT="$(cygpath -m "$PWD/build/libusb-mingw")" \
  -DZZRK_BUNDLE_RUNTIME=OFF \
  -DZZRK_WARNINGS_AS_ERRORS=ON
cmake --build build/windows-mingw-release --parallel
ctest --test-dir build/windows-mingw-release --output-on-failure \
  -R "^(gui_framework|gui_smoke)$"
```

CI 中的 MinGW job 使用同一自举流程，不依赖仓库 secret 或预设 `MINGW_INSTALL_ROOT`。当前 Linux 主机未实际运行 Windows MinGW、MSVC 或 macOS 构建；这些平台仍需在 GitHub Actions runner 或对应系统上确认。在 Windows 桌面会话中还要手工检查无边框窗口拖动区、最小化/最大化/关闭系统按钮、125%/150% DPI 缩放以及最大化后工作区边界；这些项目无法由 Linux runner 的 offscreen 测试替代。

## 4. macOS

### 4.1 环境准备

安装 Xcode Command Line Tools、Homebrew、CMake、Ninja 和 libusb：

```bash
xcode-select --install
brew install cmake ninja libusb
```

安装 Qt 6.11.1 `clang_64` 套件。下面假设 Qt 位于：

```text
$HOME/Qt/6.11.1/macos
```

### 4.2 Release 构建和 DMG 打包

本地和 CI 验证建议先使用 ad-hoc 签名：

```bash
cmake -S . -B build/macos-release -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$HOME/Qt/6.11.1/macos" \
  -DZZRK_BUILD_GUI=ON \
  -DZZRK_LIBUSB_ROOT="$(brew --prefix libusb)" \
  -DZZRK_BUNDLE_RUNTIME=ON \
  -DZZRK_MACOS_CODESIGN_IDENTITY=- \
  -DZZRK_WARNINGS_AS_ERRORS=ON
cmake --build build/macos-release --parallel
ctest --test-dir build/macos-release --output-on-failure
ctest --test-dir build/macos-release --output-on-failure \
  -R '^(gui_framework|gui_smoke)$'
cpack --config build/macos-release/CPackConfig.cmake -G DragNDrop
```

当前版本的预期产物为：

```text
build/macos-release/zzrkdevtool-0.1.0-macos.dmg
```

### 4.3 DMG 检查

挂载 DMG 后，将下方 `<挂载目录>` 替换成 `hdiutil attach` 输出的 `/Volumes/...` 路径：

```bash
hdiutil attach -nobrowse -readonly \
  build/macos-release/zzrkdevtool-0.1.0-macos.dmg
test -x "<挂载目录>/ZzRkDevTool.app/Contents/MacOS/ZzRkDevTool"
test -x "<挂载目录>/ZzRkDevTool.app/Contents/MacOS/zzrkdevtool-cli"
test -f "<挂载目录>/ZzRkDevTool.app/Contents/PlugIns/platforms/libqcocoa.dylib"
otool -L "<挂载目录>/ZzRkDevTool.app/Contents/MacOS/ZzRkDevTool"
otool -L "<挂载目录>/ZzRkDevTool.app/Contents/MacOS/zzrkdevtool-cli"
"<挂载目录>/ZzRkDevTool.app/Contents/MacOS/zzrkdevtool-cli" --help
codesign --verify --deep --strict "<挂载目录>/ZzRkDevTool.app"
hdiutil detach "<挂载目录>"
```

`otool -L` 的 GUI 和 CLI 的 libusb 条目应使用 `@rpath/libusb...dylib`；Qt 框架也应解析到 bundle 内的 `Contents/Frameworks`。任何指向 `/opt/homebrew`、`/usr/local` 或构建机的绝对路径都表示打包不完整。安装的许可证位于 `ZzRkDevTool.app/Contents/Resources/doc/zzrkdevtool/licenses`（其中包含 Qt、libusb、ZzPureTools、ZzLog 和 QWindowKit 子目录）。

### 4.4 正式签名和公证

正式发布时把 ad-hoc 的 `-` 替换为真实 Developer ID Application 身份：

```bash
cmake -S . -B build/macos-release \
  -DZZRK_MACOS_CODESIGN_IDENTITY="Developer ID Application: Example (TEAMID)"
```

重新构建 DMG 后执行公证和 staple：

```bash
xcrun notarytool submit \
  build/macos-release/zzrkdevtool-0.1.0-macos.dmg \
  --keychain-profile ZZRK_NOTARY --wait
xcrun stapler staple \
  build/macos-release/zzrkdevtool-0.1.0-macos.dmg
spctl --assess --type open --context context:primary-signature -v \
  build/macos-release/zzrkdevtool-0.1.0-macos.dmg
```

签名身份、App Store Connect 密钥、密码和公证 profile 必须存放在开发者 Keychain 或 CI secret store 中。详细策略见 `packaging/macos/README.md`。

## 5. 安装许可证目录和硬件限制

打包后必须保留 `LICENSE`、`NOTICE.md` 和各依赖许可证。默认安装位置如下：

| 平台 | 文档根目录 | 第三方许可证目录 |
| --- | --- | --- |
| Linux | `/usr/share/doc/zzrkdevtool` | `/usr/share/doc/zzrkdevtool/licenses` |
| Windows ZIP | `share/doc/zzrkdevtool` | `share/doc/zzrkdevtool/licenses` |
| macOS bundle | `ZzRkDevTool.app/Contents/Resources/doc/zzrkdevtool` | `ZzRkDevTool.app/Contents/Resources/doc/zzrkdevtool/licenses` |

自动化和 offscreen 测试使用模拟后端，只能验证无硬件启动、页面工作流和确定性关闭；不得据此宣称真实 RK3588/eMMC 读写成功。连接目标设备前，应先确认供电、USB 线材、设备模式、权限或驱动，并在专用测试设备上依次执行只读扫描、分区读取和 `--prepare-only`。完整刷写会改写存储介质，必须另行确认镜像、分区目标、备份与恢复路径。

## 6. CI 构建

`.github/workflows/cross-platform-apps.yml` 定义了 Windows 2022 与 macOS 14 的构建、23 项无硬件测试、打包后启动检查和产物上传。Linux 另有第 24 项安装 smoke test。该 workflow 由 `push`、`pull_request` 或手工 `workflow_dispatch` 触发。

当前仓库的 `origin` 指向 GitCode，且远端 `main` 与本地历史没有共同祖先。启用 GitHub Actions 前必须先确认正确的 GitHub 仓库或新分支，禁止未经备份和明确确认直接 force-push 覆盖远端。

CI 通过只说明对应 runner 上能够编译、测试、部署和启动，不能替代 USB 真机、Windows 驱动、正式签名或 macOS 公证验证。

## 7. 常见问题

### CMake 找不到 Qt

确认 `CMAKE_PREFIX_PATH` 指向包含 `lib/cmake/Qt6/Qt6Config.cmake` 的 Qt 套件根目录，并确保 Qt 编译器 ABI 与当前工具链一致。例如 MSVC 不能使用 MinGW Qt。

### CMake 找不到 libusb 1.0.30

检查 `ZZRK_LIBUSB_ROOT` 下是否存在头文件、链接库和 `lib/pkgconfig/libusb-1.0.pc`。Windows 还必须存在动态 DLL；macOS 应使用动态 `.dylib`，不能只提供静态库。

### GUI 报告热插拔检测不可用

确认实际加载的运行库版本不低于 1.0.30，并且 `libusb_has_capability(LIBUSB_CAP_HAS_HOTPLUG)` 返回非零。Windows 必须用仓库内 vcpkg overlay 重新构建 libusb；仅替换头文件或使用未启用 `LIBUSB_WINDOWS_HOTPLUG` 的 DLL 无效。本项目不提供旧版兼容或轮询回退。

### 修改依赖路径后仍使用旧缓存

CMake 3.25 或更高版本可以重新配置并清理缓存：

```bash
cmake --fresh --preset linux-release \
  -DCMAKE_PREFIX_PATH="$HOME/Qt/6.11.1/gcc_64"
```

Windows/macOS 使用对应的 `cmake -S . -B ... --fresh` 配置命令和原有参数。

### 打包成功但目标机器无法启动

- Linux：检查 `dpkg-deb --info` 的系统依赖和 `/usr/lib/zzrkdevtool`，不要删除包内 `qt.conf`。
- Windows：确认 EXE 同目录存在 Qt、MSVC runtime、平台插件和 `libusb-1.0.dll`。
- macOS：检查 `codesign --verify`、`otool -L`、bundle 的 `Contents/Frameworks` 和 `Contents/PlugIns`。

## 8. 发布前统一检查

- [ ] Release 构建启用 `ZZRK_WARNINGS_AS_ERRORS=ON`。
- [ ] 当前平台全部无硬件测试通过；Linux 额外通过安装 smoke test。
- [ ] 打包后的 GUI、CLI 和运行库在干净系统能够启动。
- [ ] 许可证正文、第三方声明和 SPDX SBOM 随包发布。
- [ ] 计算并保存最终包的 SHA-256。
- [ ] Windows 驱动和应用签名、macOS Developer ID 与公证门禁已完成。
- [ ] 对目标系统和 RK3588/eMMC 真机执行只读检查、`--prepare-only`、完整升级和报告导出。
- [ ] 在 GUI“下载镜像”页分别验证设备 GPT 与文本 `parameter.txt`，以及独立 Loader、Raw/Sparse、A/B 自动匹配、逐行校验、双进度、取消和完成后复位；首版不使用二进制 `config.cfg`，也不接受任意 LBA。
