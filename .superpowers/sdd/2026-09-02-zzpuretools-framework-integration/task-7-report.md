# 任务 7：框架主题与关闭生命周期测试报告

基线提交：`1120ff7a1161bd21ca18888889cf8f23e7bd2712`

## 实现范围

- `GuiWindowShell` 在主题快照变化时，将应用 palette 同步到标题栏、主标签页和状态栏；标题栏的主题切换意图会在 `Light` 与 `Dark` 模式间切换 `ZzThemeController`。
- `MainWorkspaceController` 新增只读的 `MainWorkspaceLifecycle` 观察值。关闭时，只有既有设备监控停止、3 个 cancellation token 写入、8 个 `QFutureWatcher::waitForFinished()` 都返回后，才依次记录监控停止、watcher 收敛和关闭完成。
- `gui_framework_test` 通过公开框架控件执行主题切换、最小化、最大化/还原和关闭；测试还覆盖固定业务标签的不可移动、不可关闭、不可 tear-off、不可 transfer 和隐藏新建标签按钮契约。
- 已确认 `gui_upgrade_test`、`gui_image_download_test` 当前均使用 `GuiTestHarness`；该迁移由任务 6 的基线代码提供，任务 7 无需重复修改。

## 简报核对

| 简报步骤 | 当前状态 | 证据 |
| --- | --- | --- |
| 主题和标题栏交互 | 已实现 | 测试保存原 mode/style，等待实际 `snapshotChanged`，核对三个表面的 `QPalette::Window`，并实际点击主题、最小化、最大化/还原按钮。 |
| 固定标签能力 | 已实现 | 分别断言 `isMovable`、`tabsClosable`、`isTearOffEnabled`、`isTabTransferEnabled` 和 `newTabButton()->isVisible()`；三类装饰状态不改变标签数和当前导航。 |
| 确定性关闭 | 已实现 | 实际点击公开关闭按钮，等待 `windowCount() == 0` 和窗口销毁，断言后端停止一次、3 个 token 已取消，以及只读生命周期观察值全部为真。 |
| 原 GUI 测试迁移 | 已具备 | `gui_upgrade_test.cpp` 和 `gui_image_download_test.cpp` 均调用 `GuiTestHarness::create()`，未再创建局部 `QApplication` 或旧 `MainWindow`。 |

关闭测试通过 `allWatchersStopped()` 观察值间接验证所有 watcher 已停止：该值只会在 8 次既有 `waitForFinished()` 返回之后置位。若后续要求逐个直接读取 `isRunning()`，需要额外公开测试诊断 API；当前实现避免扩大生产 API。

## TDD 红绿证据

### 生命周期

- RED：关闭测试首先因没有可在 controller 销毁后继续读取的观察 API 而无法编译。加入观察骨架后，真实关闭路径仍因三个 lifecycle 标志均为 false 而失败。
- GREEN：在既有停止监控、取消和等待逻辑全部完成之后置位 lifecycle 标志；最终关闭测试转绿。

### 主题

- RED：`ZzThemeController` 已改变应用 palette，但标题栏、主标签页和状态栏仍为 `#f9f9f9`，且标题栏主题按钮不改变应用模式。
- GREEN：壳层订阅真实主题快照并同步三个表面的 palette，同时处理 `themeToggleRequested`；Dark 模式下三个表面与应用 palette 一致（`#202020`），主题按钮可回切 Light。

### ASan 回滚注入器

- RED：`WorkspaceCollapseFailureInjector` 的 application event filter 在 `ZzSidePanePrivate` 构造期间枚举到半构造的 `ZzSidePane` 并读取 `edge()`，ASan 报告 `d_ptr` 未初始化导致的 SEGV。移除 `edge()` 后，递归 `findChildren()` 仍会遍历构造或析构中的 Qt 子对象并触发 SEGV。
- GREEN：event filter 只检查当前 `watched` 对象，并仅连接其 `collapsedChanged` 信号；连接阶段不读取 `edge()`、`pageCount()` 或递归遍历对象树。信号回调时对象已完成构造，使用 `pageCount()==1` 限定目标导航 pane 并注入回滚失败。

### 聚焦回归

前序执行记录：

```bash
cmake --build build-task2 --target gui_framework_test gui_upgrade_test gui_image_download_test -j2
QT_QPA_PLATFORM=offscreen ctest --test-dir build-task2 --output-on-failure -R '^(gui_framework|gui_upgrade|gui_image_download)$'
```

结果：3/3 通过。

## 自审结果

- `git diff --check` 已通过，当前 diff 无空白错误。
- 代码变更仅涉及 `GuiWindowShell.cpp`、`MainWorkspaceController.hpp`、`MainWorkspaceController.cpp` 和 `tests/gui_framework_test.cpp`；本报告为任务产物，与任务范围一致。
- 未发现阻塞性代码问题。主题同步 lambda 使用 `QPointer`，窗口销毁后不会访问已销毁表面；连接以窗口为 context，会随窗口断开。
- 当前工作树仍未提交，符合任务要求。

## 最终验证

```bash
cmake --build build-task2 --target all -j2
ASAN_OPTIONS=detect_leaks=0 QT_QPA_PLATFORM=offscreen ctest --test-dir build-task7-asan -R '^gui_framework$' --output-on-failure
QT_QPA_PLATFORM=offscreen ctest --test-dir build-task2 -LE packaging --output-on-failure
cmake -DBUILD_DIR="$PWD/build-task2" -DSOURCE_DIR="$PWD" -DINSTALL_CONFIG=Debug -P tests/linux_install_smoke.cmake
git diff --check
```

结果：

- `build-task2` 全量构建退出码为 `0`。
- ASan/UBSan `gui_framework` 为 1/1 通过。当前受 ptrace 的环境使 LSan 无法运行，因此设置 `ASAN_OPTIONS=detect_leaks=0`；double-free、UAF 和越界检查仍有效。
- 排除 `packaging` 标签后的 `build-task2` CTest 为 23/23 通过。
- `linux_install_smoke.cmake` 以 `INSTALL_CONFIG=Debug` 手工执行退出码为 `0`。
- `git diff --check` 退出码为 `0`。

## 已知 Concern

- 裸完整 CTest 的 24 项中有 23 项通过。唯一失败的 `linux_install_smoke` 使用既有 `INSTALL_CONFIG=$<CONFIG>`，该值在单配置 Ninja 构建中为空。
- `build-zzpuretools` 仍含任务 1 修复前的 stale `thirdParty/ZzPureTools/cmake_install.cmake`，因此不能作为当前源码回归的判断依据。
- 两项均属于任务 8 的安装 smoke/CMake 接入范围，本任务不越界修改；任务 7 的 GUI、生命周期和 sanitizer 验证以上述 `build-task2` 证据为准。
