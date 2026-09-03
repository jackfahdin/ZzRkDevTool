# 最终审查修复报告

## 已修复

- CLI-only 安装不再注册 ZzPureTools、ZzLog 和 QWindowKit 许可证安装规则；新增 `cli_only_install_smoke`，复制不含 `thirdParty/ZzPureTools` 的源码副本并执行 configure、build、install，验证 CLI 产物及 GUI 框架许可证缺失。
- `MainWorkspaceController` 在公开 ViewModel 同步 setter 前建立 `QPointer`，setter 返回后立即检查，覆盖 busy、device selection、progress、history 以及扫描、详情、分区和下载布局路径。监控启动失败也会恢复可重试状态。
- 第二轮审计补齐 refresh、selection/partition 清理、参数/设备布局清理、下载进度和 shutdown 的剩余 setter 返回边界；同步把 `removeHistoryReport()` 的 `historyChanged` 边界纳入保护。
- 最终审计补齐 `handleDeviceSelectionChanged()` 中连续调用 `clearDeviceInspection()`、`setDevicePartitionTable()`、`clearPartitionTable()` 后的逐点返回检查；`handlePartitionTableFinished()` 也改为在 `setDevicePartitionTable(&partitions)` 前创建 guard，并在返回后立即停止。
- 继续审计会间接调用 ViewModel setter 的 helper；在 `setPagesBusy()` 内部和其关键调用者、`beginHistoryEntry()`、`finishHistoryEntry()`、`rebuildDownloadTable()` / `matchDownloadDirectory()` 相关上层路径补齐同步销毁后的返回检查。
- GUI 框架测试新增真实 `refreshDevices()`、`shutdown()`、`loadDownloadParameter()` 路径的隔离子进程 DirectConnection + `beginShutdown()` 回归。
- GUI 框架测试新增四类 ViewModel 信号的隔离进程 DirectConnection 同步关闭回归场景。
- GUI 框架测试新增真实 Controller 路径的隔离场景，覆盖 selection 清理 inspection、selection 清理设备 GPT、selection 清理 partition 表，以及 partition table 完成后应用设备 GPT 布局。
- macOS CI 增加 DMG 内 `libqcocoa`、GUI/CLI `otool -L` 和 offscreen GUI smoke 检查；Windows MinGW GUI job 改为固定 Qt 6.11.1 `win64_mingw` + `tools_mingw1310`，在 Git Bash 中下载官方 libusb 1.0.30 tarball 并以 `--enable-windows-hotplug --enable-shared --disable-static` 安装到 `build/libusb-mingw`，不再依赖 `MINGW_INSTALL_ROOT` 或仓库 secret/env。
- 中文构建文档同步 MinGW 可复现自举流程，并修正 hotplug 能力缺失行为：GUI 继续启动、显示错误并允许手动刷新，不启用轮询。
- `tests/zzpuretools_cmake_scope.cmake` 现在同时断言已保存 cache 条目的 `HELPSTRING` 和 `ADVANCED` 元数据。

## 验证

- `cmake --build build-debug --parallel 4`：通过。
- `QT_QPA_PLATFORM=offscreen ctest --test-dir build-debug --output-on-failure -R '^(gui_framework|gui_smoke|cli_only_install_smoke)$'`：3/3 通过。
- CLI-only smoke 在修复前于 ZzLog LICENSE 安装处失败，修复后 configure/build/install 及无框架许可证断言通过。
- TDD 红灯：在临时 detached worktree 仅应用新增测试、不应用 Controller 修复后，ASan `gui_framework_test --synchronous-shutdown refreshDevicesBusyChanged` 复现 `MainWorkspaceController::refreshDevices()` setter 返回后的 heap-use-after-free；`shutdownBusyChanged` 复现 `MainWorkspaceController::shutdown()` setter 返回后的同步销毁崩溃；`loadDownloadParameterLayoutChanged` 复现 `MainWorkspaceController::loadDownloadParameter()` setter 返回后的 heap-use-after-free。
- 本轮 TDD 红灯：仅添加新增测试时，ASan `gui_framework_test --synchronous-shutdown selectionClearsInspection` 在 `handleDeviceSelectionChanged()` 第 919 行复现 heap-use-after-free；`selectionClearsDeviceGpt` 在同函数清理 GPT 后复现 heap-use-after-free；`partitionTableAppliesDeviceGpt` 在 `handlePartitionTableFinished()` 第 1092 行复现 heap-use-after-free。修复后新增场景全部 exit 0。
- `cmake --build build-debug --target gui_framework_test gui_upgrade_test gui_image_download_test --parallel 4`：通过。
- `ctest --test-dir build-debug -R 'gui_framework|gui_upgrade|gui_image_download' --output-on-failure`：3/3 通过。
- `cmake --build build-asan --target gui_framework_test gui_upgrade_test gui_image_download_test --parallel 4`：通过。
- `ASAN_OPTIONS=detect_leaks=0 ctest --test-dir build-asan -R 'gui_framework|gui_upgrade|gui_image_download' --output-on-failure`：3/3 通过。
- `cmake --preset linux-debug -DCMAKE_PREFIX_PATH="$HOME/Qt/6.11.1/gcc_64"`：通过，并执行 CMake scope 断言。
- `cmake --preset linux-sanitize -DCMAKE_PREFIX_PATH="$HOME/Qt/6.11.1/gcc_64"`：通过，并执行 CMake scope 断言。
- `cmake --build build-debug --parallel 4`：通过。
- `QT_QPA_PLATFORM=offscreen ctest --test-dir build-debug --output-on-failure`：25/25 通过。
- `cmake --build build-asan --parallel 4`：通过。
- `ASAN_OPTIONS=detect_leaks=0 QT_QPA_PLATFORM=offscreen ctest --test-dir build-asan --output-on-failure -R '^(gui_framework|gui_smoke)$'`：2/2 通过。
- `ASAN_OPTIONS=detect_leaks=0 QT_QPA_PLATFORM=offscreen ctest --test-dir build-asan --output-on-failure`：25/25 通过。
- `python3` + PyYAML 解析 `.github/workflows/cross-platform-apps.yml` 并断言 MinGW job 关键字段：通过。
- `curl --fail --location --head https://github.com/libusb/libusb/releases/download/v1.0.30/libusb-1.0.30.tar.bz2`：通过，最终响应含 `content-disposition: attachment; filename=libusb-1.0.30.tar.bz2`。
- `git diff --check`：通过。

## 环境限制

当前 Linux 主机没有 Windows MSVC/MinGW 或 macOS runner，未宣称这些平台实际构建通过；对应 workflow 检查需在 GitHub Actions runner 上执行。ASan 验证沿用 `detect_leaks=0`，不宣称 LSan 泄漏检查通过。

## 延期事项

设备表排序/空 `portPath` 展示继续延期，避免扩大交互范围。Windows MSVC/MinGW 与 macOS 仍需对应 runner 实测。
