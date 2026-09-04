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
- 最终复审第二轮补齐所有 `processPendingDeviceRefresh()` 上层调用者、`attachPages()` / `attachWorkspace()` 和 `clearSelectedDownloadImage()` 的间接 setter 返回边界，避免下层 helper 已停止而外层调用栈继续访问被同步销毁的 Controller。
- `shutdown()` 现在先停止监控、设置取消标志、断开并等待全部 watcher、提交 lifecycle 状态，最后才发出 `busyChanged(false)`；同步重入销毁不会再跳过确定性清理。
- GUI 框架测试新增 pending refresh、页面重绑、清除下载镜像和带运行中 scan watcher 的重入 shutdown 四个真实 Controller 隔离场景；macOS DMG 检查同时精确要求 GUI 和 CLI 使用 `@rpath/libusb`。
- macOS CI 增加 DMG 内 `libqcocoa`、GUI/CLI `otool -L` 和 offscreen GUI smoke 检查；Windows MinGW GUI job 改为固定 Qt 6.11.1 `win64_mingw` + `tools_mingw1310`，在 Git Bash 中下载官方 libusb 1.0.30 tarball 并以 `--enable-windows-hotplug --enable-shared --disable-static` 安装到 `build/libusb-mingw`，不再依赖 `MINGW_INSTALL_ROOT` 或仓库 secret/env。
- 中文构建文档同步 MinGW 可复现自举流程，并修正 hotplug 能力缺失行为：GUI 继续启动、显示错误并允许手动刷新，不启用轮询。
- `tests/zzpuretools_cmake_scope.cmake` 现在同时断言已保存 cache 条目的 `HELPSTRING` 和 `ADVANCED` 元数据。

## 验证

- `cmake --build build-debug --parallel 4`：通过。
- `QT_QPA_PLATFORM=offscreen ctest --test-dir build-debug --output-on-failure -R '^(gui_framework|gui_smoke|cli_only_install_smoke)$'`：3/3 通过。
- CLI-only smoke 在修复前于 ZzLog LICENSE 安装处失败，修复后 configure/build/install 及无框架许可证断言通过。
- TDD 红灯：在临时 detached worktree 仅应用新增测试、不应用 Controller 修复后，ASan `gui_framework_test --synchronous-shutdown refreshDevicesBusyChanged` 复现 `MainWorkspaceController::refreshDevices()` setter 返回后的 heap-use-after-free；`shutdownBusyChanged` 复现 `MainWorkspaceController::shutdown()` setter 返回后的同步销毁崩溃；`loadDownloadParameterLayoutChanged` 复现 `MainWorkspaceController::loadDownloadParameter()` setter 返回后的 heap-use-after-free。
- 本轮 TDD 红灯：仅添加新增测试时，ASan `gui_framework_test --synchronous-shutdown selectionClearsInspection` 在 `handleDeviceSelectionChanged()` 第 919 行复现 heap-use-after-free；`selectionClearsDeviceGpt` 在同函数清理 GPT 后复现 heap-use-after-free；`partitionTableAppliesDeviceGpt` 在 `handlePartitionTableFinished()` 第 1092 行复现 heap-use-after-free。修复后新增场景全部 exit 0。
- 最终复审第二轮 TDD 红灯：修复前，ASan `pendingRefreshAfterInspection`、`attachPagesRebuildBusyChanged`、`clearImagePlanBusyChanged` 分别在完成槽、页面重绑和下载镜像清理路径复现 heap-use-after-free；`shutdownBusyChanged` 稳定失败 watcher 收敛与三个 lifecycle 后置断言。提交 `51c3fda` 后四个场景均 exit 0。
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
- `QT_QPA_PLATFORM=offscreen ctest --test-dir build-release --output-on-failure`：25/25 通过。
- `go run github.com/rhysd/actionlint/cmd/actionlint@v1.7.7 .github/workflows/cross-platform-apps.yml`：通过，无 workflow 语法错误。
- `cpack --config build-release/CPackConfig.cmake`：生成 `zzrkdevtool_0.1.0_amd64.deb`；包内容包含 GUI、CLI、`libqxcb`、`libqoffscreen` 及 ZzPureTools、ZzLog、QWindowKit 许可证。
- `python3` + PyYAML 解析 `.github/workflows/cross-platform-apps.yml` 并断言 MinGW job 关键字段：通过。
- `curl --fail --location --head https://github.com/libusb/libusb/releases/download/v1.0.30/libusb-1.0.30.tar.bz2`：通过，最终响应含 `content-disposition: attachment; filename=libusb-1.0.30.tar.bz2`。
- `git diff --check`：通过。

## 环境限制

当前 Linux 主机没有 Windows MSVC/MinGW 或 macOS runner，未宣称这些平台实际构建通过；对应 workflow 检查需在 GitHub Actions runner 上执行。ASan 验证沿用 `detect_leaks=0`，不宣称 LSan 泄漏检查通过。

## 延期事项

设备表排序/空 `portPath` 展示继续延期，避免扩大交互范围。progress/history 的真实 Controller 操作栈同步销毁测试可继续加深；现有 setter 调用点已静态确认带存活守卫，因此不阻断本次集成。Windows MSVC/MinGW 与 macOS 仍需对应 runner 实测。

## 最终复审

- `50c9f93` 定向复审：0 个 Critical、2 个 Important，结论为修完再合；报告见 `final-rereview-50c9f93.md`。
- 修复提交：`51c3fda`，关闭间接 setter 调用栈和重入 shutdown 两项 Important。
- `51c3fda` 定向复审：0 个 Critical、0 个 Important、1 个不阻断 Minor，结论为可以合并；报告见 `final-rereview-51c3fda.md`。
