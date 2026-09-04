# 最终定向复审（50c9f93）

审查范围：`b629391..50c9f93`；工作树为 `zzpuretools-framework`。本次仅复核最终审查中的四项问题、修复 diff 是否引入新的 Critical/Important 问题，以及对应验证证据。除本报告外未修改源码，也未提交。Windows、macOS 和 Windows MinGW 结论仅来自 workflow/CMake 静态检查，未在当前 Linux 主机执行这些平台的构建或运行。

## 结论

**不可合并，修完再合。** CLI-only 安装隔离、macOS/MinGW 门禁和 CMake cache 元数据断言均已补齐；但公开 ViewModel 同步信号的生命周期问题仍未完整修复，而且 `shutdown()` 的新 guard 会在同步重入销毁时跳过 watcher 等待和生命周期完成标记。当前仍有两项 Important 阻断问题。

## 原问题复核

### 1. CLI-only 安装硬依赖 GUI 子模块

**ADDRESSED。** GUI 框架许可证安装现受 `ZZRK_BUILD_GUI AND TARGET ZzRkDevTool` 保护：[cmake/InstallAndPackage.cmake:79](/home/zz/Jackfahdin/qt/github/ZzRkDevTool/.worktrees/zzpuretools-framework/cmake/InstallAndPackage.cmake:79)。新增 smoke 创建独立 source/build/install 目录，只复制 `thirdParty/spdlog`（以及可选的已获取 fmt），不复制 `thirdParty/ZzPureTools`，再完成 CLI-only configure、build、install，并断言 CLI 存在且三个框架许可证目录均不存在：[tests/cli_only_install_smoke.cmake:9](/home/zz/Jackfahdin/qt/github/ZzRkDevTool/.worktrees/zzpuretools-framework/tests/cli_only_install_smoke.cmake:9)、[tests/cli_only_install_smoke.cmake:36](/home/zz/Jackfahdin/qt/github/ZzRkDevTool/.worktrees/zzpuretools-framework/tests/cli_only_install_smoke.cmake:36)、[tests/cli_only_install_smoke.cmake:83](/home/zz/Jackfahdin/qt/github/ZzRkDevTool/.worktrees/zzpuretools-framework/tests/cli_only_install_smoke.cmake:83)。该测试已注册为 Linux packaging smoke：[tests/CMakeLists.txt:264](/home/zz/Jackfahdin/qt/github/ZzRkDevTool/.worktrees/zzpuretools-framework/tests/CMakeLists.txt:264)。

### 2. 公开 ViewModel 同步 setter 导致 Controller UAF

**NOT ADDRESSED。** 直接调用 setter 的位置大多已经在返回后检查 `QPointer`，但调用这些位置的上层 helper/caller 没有形成完整的存活边界，仍存在同步销毁后继续执行的路径，详见 Important 1。

### 3. macOS 打包启动检查和 Windows MinGW 门禁

**ADDRESSED（静态检查）。** macOS job 挂载生成的 DMG，检查 GUI、CLI、`libqcocoa.dylib` 和 bundled libusb，执行 GUI/CLI 的 `otool` 检查，并从 DMG 以 offscreen 模式运行 GUI smoke 和 CLI `--help`：[.github/workflows/cross-platform-apps.yml:98](/home/zz/Jackfahdin/qt/github/ZzRkDevTool/.worktrees/zzpuretools-framework/.github/workflows/cross-platform-apps.yml:98)。部署规则继续包含 `qoffscreen`，且 `FixMacOSLibUSB.cmake.in` 对 GUI 和 CLI 都重写 libusb 引用。

Windows MinGW job 固定使用 Windows 2022、Qt 6.11.1 `win64_mingw` 和 `tools_mingw1310`，从 action 输出推导 Qt/Tools 路径，自举官方 libusb 1.0.30 shared build，然后用 Ninja 构建并运行 `gui_framework|gui_smoke`：[.github/workflows/cross-platform-apps.yml:125](/home/zz/Jackfahdin/qt/github/ZzRkDevTool/.worktrees/zzpuretools-framework/.github/workflows/cross-platform-apps.yml:125)、[.github/workflows/cross-platform-apps.yml:143](/home/zz/Jackfahdin/qt/github/ZzRkDevTool/.worktrees/zzpuretools-framework/.github/workflows/cross-platform-apps.yml:143)、[.github/workflows/cross-platform-apps.yml:168](/home/zz/Jackfahdin/qt/github/ZzRkDevTool/.worktrees/zzpuretools-framework/.github/workflows/cross-platform-apps.yml:168)、[.github/workflows/cross-platform-apps.yml:185](/home/zz/Jackfahdin/qt/github/ZzRkDevTool/.worktrees/zzpuretools-framework/.github/workflows/cross-platform-apps.yml:185)。workflow 可被 PyYAML 解析，但本机没有实际执行 macOS 或 Windows runner。

### 4. CMake cache `HELPSTRING` / `ADVANCED` 断言

**ADDRESSED。** 作用域测试除 `VALUE`、`TYPE` 外，现已读取并比较 `HELPSTRING` 和 `ADVANCED`：[tests/zzpuretools_cmake_scope.cmake:55](/home/zz/Jackfahdin/qt/github/ZzRkDevTool/.worktrees/zzpuretools-framework/tests/zzpuretools_cmake_scope.cmake:55)、[tests/zzpuretools_cmake_scope.cmake:67](/home/zz/Jackfahdin/qt/github/ZzRkDevTool/.worktrees/zzpuretools-framework/tests/zzpuretools_cmake_scope.cmake:67)、[tests/zzpuretools_cmake_scope.cmake:76](/home/zz/Jackfahdin/qt/github/ZzRkDevTool/.worktrees/zzpuretools-framework/tests/zzpuretools_cmake_scope.cmake:76)。

## Critical

无。

## Important

### 1. 间接 ViewModel setter 返回后仍可沿上层调用栈访问已释放 Controller

- File: [apps/gui/MainWorkspaceController.cpp:838](/home/zz/Jackfahdin/qt/github/ZzRkDevTool/.worktrees/zzpuretools-framework/apps/gui/MainWorkspaceController.cpp:838)、[apps/gui/MainWorkspaceController.cpp:845](/home/zz/Jackfahdin/qt/github/ZzRkDevTool/.worktrees/zzpuretools-framework/apps/gui/MainWorkspaceController.cpp:845)、[apps/gui/MainWorkspaceController.cpp:692](/home/zz/Jackfahdin/qt/github/ZzRkDevTool/.worktrees/zzpuretools-framework/apps/gui/MainWorkspaceController.cpp:692)、[apps/gui/MainWorkspaceController.cpp:2213](/home/zz/Jackfahdin/qt/github/ZzRkDevTool/.worktrees/zzpuretools-framework/apps/gui/MainWorkspaceController.cpp:2213)
- `processPendingDeviceRefresh()` 尾调用 `refreshDevices()`；后者在第 786 行调用 `m_viewModel->setBusy(true)`，同步 `busyChanged` 接收器可以在这里执行 `beginShutdown()` 并销毁 Controller。`refreshDevices()` 自己会因 guard 返回，但上层完成槽仍继续执行。例如 `handleScanFinished()` 在第 861 行调用 helper 后，未经再次检查就在第 862 行 `emit scanCompleted()`。同一缺口还出现在详情完成（第 1024/1065 行）、分区表完成（第 1123/1168 行）、分区操作完成（第 1423 行）、固件预检（第 1500/1536 行）、升级完成（第 1723 行）以及镜像下载准备/完成（第 2347/2499 行）路径。
- 可达场景不要求外部直接调用 setter：USB operation 运行时收到 hotplug 会令 `m_hotplugRefreshPending=true`；operation 完成时接收器忽略 `busyChanged(false)`，随后待处理刷新发出的 `busyChanged(true)` 同步关闭应用。调用栈回到完成槽后便通过已释放的 `this` 发射终态信号，属于确定性的 UAF。
- 另有两个相同结构的公开入口：`attachPages()` 调用 `rebuildDownloadTable()`，后者经 `updateDownloadControls()` 在第 2509 行调用 `setBusy()`；若此处销毁 Controller，`attachPages()` 会从第 695 行继续访问页面成员。`clearSelectedDownloadImage()` 调用 `handleDownloadPlanChanged()` 走到同一 setter 后，会从第 2217 行继续访问 `m_downloadPage` 和映射。
- 现有隔离场景没有覆盖这些调用链。尤其 `viewModelProgressChanged` 和 `viewModelHistoryChanged` 只直接调用 ViewModel setter：[tests/gui_framework_test.cpp:464](/home/zz/Jackfahdin/qt/github/ZzRkDevTool/.worktrees/zzpuretools-framework/tests/gui_framework_test.cpp:464)。它们即使面对修复前的 Controller 也会通过，不能证明真实 Controller progress/history 栈安全；场景列表也没有 pending refresh、`attachPages()` 或清除下载镜像用例：[tests/gui_framework_test.cpp:1093](/home/zz/Jackfahdin/qt/github/ZzRkDevTool/.worktrees/zzpuretools-framework/tests/gui_framework_test.cpp:1093)。
- 修复建议：把“可能间接发射 ViewModel 信号”的 helper 视为同步销毁边界；每个调用者在调用前建立 guard、返回后立即检查，且补充真实 Controller 路径的隔离 ASan 回归。也可让 helper 显式返回存活/中止结果，避免继续遗漏上层边界。

### 2. `shutdown()` 的重入 guard 会跳过确定性清理和生命周期完成状态

- File: [apps/gui/MainWorkspaceController.cpp:2592](/home/zz/Jackfahdin/qt/github/ZzRkDevTool/.worktrees/zzpuretools-framework/apps/gui/MainWorkspaceController.cpp:2592)、[apps/gui/MainWorkspaceController.cpp:537](/home/zz/Jackfahdin/qt/github/ZzRkDevTool/.worktrees/zzpuretools-framework/apps/gui/MainWorkspaceController.cpp:537)、[tests/gui_framework_test.cpp:484](/home/zz/Jackfahdin/qt/github/ZzRkDevTool/.worktrees/zzpuretools-framework/tests/gui_framework_test.cpp:484)
- 外层 `shutdown()` 先在第 2596 行置 `m_shuttingDown=true`，再于第 2616 行调用可能同步发射 `busyChanged` 的 `setBusy(false)`。DirectConnection 接收器调用框架 `beginShutdown()` 后会销毁 Controller；析构函数再次调用 `shutdown()`，但内层调用因 `m_shuttingDown` 已为 true 在第 2593 行立即返回。外层随后看到 guard 为空并在第 2618 行返回。
- 因此 watcher 的 disconnect/wait（第 2620-2635 行）以及 `monitoringStopped`、`allWatchersStopped`、`shutdownComplete` 三个标记（第 2636-2639 行）全部被跳过。这违反实现计划“关闭后所有 `QFutureWatcher` 均已停止”的确定性关闭要求：[集成计划:367](/home/zz/Jackfahdin/qt/github/ZzRkDevTool/.worktrees/zzpuretools-framework/docs/superpowers/plans/2026-09-02-zzpuretools-framework-integration.md:367)。Qt 6.11.1 的 `QFutureWatcher` 析构函数只调用 `disconnectOutputInterface()`，不会替代这里明确要求的 `waitForFinished()`。
- 只读 GDB 核验在第 2618 行命中断点，`guard.isNull()==true`，调用来自 `runSynchronousShutdownScenario()` 的 `shutdownBusyChanged` 分支，确认当前测试实际走了这个提前返回分支。
- 测试仍会通过，因为该场景只调用 `controller->shutdown()`，公共后置条件只断言 Controller 和窗口已销毁：[tests/gui_framework_test.cpp:491](/home/zz/Jackfahdin/qt/github/ZzRkDevTool/.worktrees/zzpuretools-framework/tests/gui_framework_test.cpp:491)、[tests/gui_framework_test.cpp:613](/home/zz/Jackfahdin/qt/github/ZzRkDevTool/.worktrees/zzpuretools-framework/tests/gui_framework_test.cpp:613)。它没有保留并检查 lifecycle，也没有让一个可控的 watcher 运行到同步关闭边界；常规窗口关闭的 lifecycle 断言不覆盖这一重入场景：[tests/gui_framework_test.cpp:1085](/home/zz/Jackfahdin/qt/github/ZzRkDevTool/.worktrees/zzpuretools-framework/tests/gui_framework_test.cpp:1085)。
- 修复建议：在任何可能同步销毁 Controller 的通知之前完成 watcher disconnect/wait 和 lifecycle 提交，将 `setBusy(false)` 作为无后续成员访问的最后动作；并让 `shutdownBusyChanged` 保留 lifecycle、持有一个可控的未完成 future，断言返回前 watcher 已收敛且三个状态均为 true。

## Minor

1. **macOS GUI 的 `otool` 断言过宽。** [.github/workflows/cross-platform-apps.yml:108](/home/zz/Jackfahdin/qt/github/ZzRkDevTool/.worktrees/zzpuretools-framework/.github/workflows/cross-platform-apps.yml:108) 会确认 Frameworks 中存在 bundled libusb，CLI 明确匹配 `@rpath/libusb`，但 GUI 只要求任意一行含 `@rpath/`（第 112-113 行）。因此 GUI 若仍引用 Homebrew 的绝对 libusb，同时通过某个 Qt `@rpath` 依赖满足 grep，门禁也会通过；同一 runner 已安装 Homebrew libusb，offscreen 启动同样无法排除该情况。当前安装脚本静态看来会正确重写 GUI libusb，所以这不是现有产物失败证据；建议把 GUI 断言也收紧为 `@rpath/libusb`，或显式拒绝 Homebrew/构建机绝对路径。

## 修复 diff 新增问题

- 新增 Important：`shutdown()` 为避免 setter 返回后的 UAF 所加的提前返回会跳过原有确定性清理，见 Important 2。
- 原 ViewModel UAF 属于未完整修复而非新问题，见 Important 1。
- 未发现其他新增 Critical/Important 问题。

## 验证证据

- `ASAN_OPTIONS=detect_leaks=0 QT_QPA_PLATFORM=offscreen ctest --test-dir build-asan --output-on-failure -R '^(gui_framework|cli_only_install_smoke)$'`：2/2 通过，0 失败（16.27s）。这证明现有场景通过，不覆盖上述缺失场景；因 `detect_leaks=0` 不宣称 LSan 通过。
- `git diff --check b629391..50c9f93`：无输出。
- 对两个 revision 的 `MainWorkspaceController.cpp` 分别经 `clang-format` 后比较，确认大量格式化之外的 Controller 语义变化集中在生命周期 guard 和监控重试；未发现格式化掩盖的其他行为改动。
- PyYAML 可解析 workflow，并识别 `macos`、`windows`、`windows-mingw` jobs。
- Windows、Windows MinGW 和 macOS 未在当前 Linux 主机实际执行；对应结论必须以 GitHub Actions runner 结果为最终运行证据。
