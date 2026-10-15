# 三平台安装布局与源码 USB 构建计划

目标：Windows TGZ 解压后可直接启动，Linux 使用标准安装目录或便携包目录，macOS 使用 `.app`；为下一步 Linux Zzlibusb 真机验证提供可构建的软件。

架构：保留 Ninja 单配置流程，增加独立 VS 多配置预设。主项目在局部作用域中静态构建 Zzlibusb，测试只继承其头文件，不链接真实 USB 实现。

- [x] 调整 `CMakeLists.txt`、`CMakePresets.json`：Windows 默认根目录；MSVC Ninja Debug/Release 一致，VS 2022/2026 各用独立目录；macOS 包名区分架构。
- [x] 调整 Zzlibusb 接入和 `tests/CMakeLists.txt`：隔离静态库选项，系统头文件消除第三方警告，替换已失效的头文件变量。
- [x] 更新安装检查和跨平台 CI：验证源码静态链接，不再要求单独的 libusb 动态库；保留旧二进制文件供真机验证后清理。
- [x] 更新中文构建文档：平台目录、编译器环境、VS 2026 的 CMake 4.2 要求、源码依赖和 Linux 验证命令。
- [x] 在新 Linux 构建目录启用严格警告，构建全部目标、运行 CTest、检查安装并生成 DEB；Windows/macOS 仅报告本机可验证的配置结果。

验证记录：Linux Qt 6.12.0 / Ninja Release 严格编译通过，37/37 CTest 通过，DEB 生成成功。动态依赖检查确认 GUI/CLI 不引用 libusb 共享库。修正了安装时从系统误收集 Qt 的问题，并按 Qt 6.10 分隔插件筛选选项。Windows 相对安装目录和预设格式已检查；Windows/macOS 原生构建、Qt 6.8/6.9 以及 USB 真机行为仍需对应环境验证。

不自动提交用户尚未完成的改动，不操作 USB 设备、不烧录固件。

## Linux 双格式发布补充

用户确认同时提供便携包和 DEB。DEB 继续使用 `linux-release` / `linux-deb`；新增 `linux-portable-release` / `linux-tgz`，独立构建目录，根目录程序、`lib/` 库、`plugins/` 插件和 `docs/` 文档。便携包仅携带 USB 规则，不自动安装系统文件。

- [x] 在实现前验证安装测试能够拒绝旧的 `bin/` 布局。
- [x] 调整 CMake 布局、归档生成器和预设，补充包内中文说明和编译指南。
- [x] 构建便携版本，通过移动到带空格目录后的启动检查和 CTest。
- [x] 生成并解压验证 `.tar.gz`，重新生成并检查 `.deb`，确认两种布局独立。

验证记录：便携版本 Ninja Release 严格编译通过，37/37 CTest 通过；DEB 安装检查重新通过。实际生成的 TGZ、DEB 均解压到带空格的临时路径，清除 Qt 插件路径与动态库路径环境变量后，GUI `--smoke-test` 和 CLI `--version` 均通过。TGZ 使用根目录程序及 `$ORIGIN/lib`，DEB 使用 `usr/bin` 及 `$ORIGIN/../lib/zzrkdevtool`。只读复审及 `git diff --check` 通过。

本机产物基于当前系统工具链，DEB 自动计算的依赖包括 glibc 2.43 和 libstdc++ 14；如需支持更旧的发行版，应在目标最低系统版本的构建环境中重新生成两种包。便携布局不消除系统运行库版本要求。USB 真机验证仍由用户后续执行，旧 libusb 二进制目录保留。

## 包名与 Windows 压缩格式调整

发布归档统一使用 `ZzRkDevTool-版本号-平台-架构` 前缀，Linux 便携包保留 `-portable` 后缀。Windows 改为 TGZ（`.tar.gz`），对应预设改为 `windows-msvc-tgz`、`windows-vs2022-tgz` 和 `windows-vs2026-tgz`；CI 与文档同步改用 TGZ 解压流程。DEB 的软件包标识和标准文件名继续使用小写 `zzrkdevtool`。
