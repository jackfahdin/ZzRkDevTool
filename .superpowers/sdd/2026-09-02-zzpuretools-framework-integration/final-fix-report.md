# 最终审查修复报告

## 已修复

- CLI-only 安装不再注册 ZzPureTools、ZzLog 和 QWindowKit 许可证安装规则；新增 `cli_only_install_smoke`，复制不含 `thirdParty/ZzPureTools` 的源码副本并执行 configure、build、install，验证 CLI 产物及 GUI 框架许可证缺失。
- `MainWorkspaceController` 在公开 ViewModel 同步 setter 前建立 `QPointer`，setter 返回后立即检查，覆盖 busy、device selection、progress、history 以及扫描、详情、分区和下载布局路径。监控启动失败也会恢复可重试状态。
- 第二轮审计补齐 refresh、selection/partition 清理、参数/设备布局清理、下载进度和 shutdown 的剩余 setter 返回边界；同步把 `removeHistoryReport()` 的 `historyChanged` 边界纳入保护。
- GUI 框架测试新增真实 `refreshDevices()`、`shutdown()`、`loadDownloadParameter()` 路径的隔离子进程 DirectConnection + `beginShutdown()` 回归。
- GUI 框架测试新增四类 ViewModel 信号的隔离进程 DirectConnection 同步关闭回归场景。
- macOS CI 增加 DMG 内 `libqcocoa`、GUI/CLI `otool -L` 和 offscreen GUI smoke 检查；增加固定 Qt 6.11.1 的 Windows MinGW GUI job，并要求显式提供匹配 MinGW/libusb 根目录，避免把缺少工具链的 runner 记录为通过。

## 验证

- `cmake --build build-debug --parallel 4`：通过。
- `ctest --test-dir build-debug -R 'gui_framework|cli_only_install_smoke' --output-on-failure`：2/2 通过。
- CLI-only smoke 在修复前于 ZzLog LICENSE 安装处失败，修复后 configure/build/install 及无框架许可证断言通过。
- TDD 红灯：在临时 detached worktree 仅应用新增测试、不应用 Controller 修复后，ASan `gui_framework_test --synchronous-shutdown refreshDevicesBusyChanged` 复现 `MainWorkspaceController::refreshDevices()` setter 返回后的 heap-use-after-free；`shutdownBusyChanged` 复现 `MainWorkspaceController::shutdown()` setter 返回后的同步销毁崩溃；`loadDownloadParameterLayoutChanged` 复现 `MainWorkspaceController::loadDownloadParameter()` setter 返回后的 heap-use-after-free。
- `cmake --build build-debug --target gui_framework_test gui_upgrade_test gui_image_download_test --parallel 4`：通过。
- `ctest --test-dir build-debug -R 'gui_framework|gui_upgrade|gui_image_download' --output-on-failure`：3/3 通过。
- `cmake --build build-asan --target gui_framework_test gui_upgrade_test gui_image_download_test --parallel 4`：通过。
- `ASAN_OPTIONS=detect_leaks=0 ctest --test-dir build-asan -R 'gui_framework|gui_upgrade|gui_image_download' --output-on-failure`：3/3 通过。
- `cmake --build build-debug --parallel 4`：通过。
- `ctest --test-dir build-debug --output-on-failure`：25/25 通过。
- `cmake --build build-asan --parallel 4`：通过。
- `ASAN_OPTIONS=detect_leaks=0 ctest --test-dir build-asan --output-on-failure`：25/25 通过。
- `git diff --check`：通过。

## 环境限制

当前 Linux 主机没有 Windows MSVC/MinGW 或 macOS runner，未宣称这些平台实际构建通过；对应 workflow 检查需在 GitHub Actions runner 上执行。ASan 验证沿用 `detect_leaks=0`，不宣称 LSan 泄漏检查通过。

## 延期事项

设备表排序/空 `portPath` 展示和 CMake cache `HELPSTRING`/`ADVANCED` 深度断言未在本轮修改，保留为低风险后续项。Windows/macOS 仍需对应 runner 实测。
