# 最终定向复审（51c3fda）

审查范围：`50c9f93..51c3fda`；工作树为 `zzpuretools-framework`。本次对照集成计划及上一轮 [final-rereview-50c9f93.md](/home/zz/Jackfahdin/qt/github/ZzRkDevTool/.worktrees/zzpuretools-framework/.superpowers/sdd/2026-09-02-zzpuretools-framework-integration/final-rereview-50c9f93.md)，复核修复 diff、相关调用链、回归测试和 macOS 门禁。除本报告外未修改产品代码，也未提交。Windows、Windows MinGW 和 macOS 未在当前 Linux 主机执行；这些平台的结论明确仅为静态检查。

## 结论

**可以合并。** 上轮两项 Important 均已得到对应修复：所有待处理刷新及页面/下载间接 setter 调用链在同步销毁后停止，`shutdown()` 现在在最后一次 `busyChanged(false)` 之前完成监控停止、取消、watcher 断开/等待和生命周期标记。新增隔离场景与完整 ASan 回归均通过，未发现本轮新增 Critical/Important 问题。

## 重点复核

### 1. `processPendingDeviceRefresh()` 及间接 setter 调用链

**ADDRESSED。**

- `processPendingDeviceRefresh()` 仍只负责调用 `refreshDevices()`：[apps/gui/MainWorkspaceController.cpp:847](/home/zz/Jackfahdin/qt/github/ZzRkDevTool/.worktrees/zzpuretools-framework/apps/gui/MainWorkspaceController.cpp:847)。所有调用它的完成槽现在都在返回后立即复查同一个 `QPointer`，再发出任何后续信号：扫描第 870-873 行、详情第 1035-1038/1078-1081 行、分区表第 1138-1143/1185-1190 行、分区操作第 1442-1445 行、固件预检第 1521-1524/1559-1562 行、升级第 1748-1751 行、下载准备第 2379-2382 行和下载完成第 2533-2536 行。
- `refreshDevices()` 在 `setBusy(true)` 前建立 guard，并在 setter 返回后第 796-797 行停止；因此待处理刷新即使触发同步 `busyChanged` 关闭，也不会在该 helper 内继续操作：[apps/gui/MainWorkspaceController.cpp:794](/home/zz/Jackfahdin/qt/github/ZzRkDevTool/.worktrees/zzpuretools-framework/apps/gui/MainWorkspaceController.cpp:794)。上层完成槽也已在 helper 返回后复查，封闭了上一轮指出的“helper 自身安全、caller 继续执行”缺口。
- `attachPages()` 在进入时建立 guard，在 `rebuildDownloadTable()` 返回后第 698-699 行以及后续 `updateUpgradeControls()` 后第 751-753 行检查；`attachWorkspace()` 还在调用 `attachPages()` 后第 609-613 行检查，因此页面装配过程中同步销毁不会继续访问成员：[apps/gui/MainWorkspaceController.cpp:667](/home/zz/Jackfahdin/qt/github/ZzRkDevTool/.worktrees/zzpuretools-framework/apps/gui/MainWorkspaceController.cpp:667)。
- `clearSelectedDownloadImage()` 在调用 `handleDownloadPlanChanged()` 前建立 guard、返回后第 2245-2246 行检查，并在 `rebuildDownloadTable()` 后第 2257-2258 行再次检查：[apps/gui/MainWorkspaceController.cpp:2240](/home/zz/Jackfahdin/qt/github/ZzRkDevTool/.worktrees/zzpuretools-framework/apps/gui/MainWorkspaceController.cpp:2240)。
- 相关嵌套 helper 也已覆盖：`updateDownloadControls()` 在 `setBusy()` 后检查（第 2543-2547 行）；`setPagesBusy()` 在 `updateDownloadControls()`、`updateUpgradeControls()` 后逐点检查（第 2708-2718 行）；`setDevicePartitionTable()` 在 layout setter、`rebuildDownloadTable()`、`matchDownloadDirectory()` 后检查（第 2600-2616 行）；`handleDownloadDeviceChanged()`、`loadDownloadParameter()`、`matchDownloadDirectory()` 的同类返回边界也均存在。
- 对所有 `m_viewModel->set*`/`removeHistoryReport` 调用的静态枚举未发现 setter 返回后继续访问未受保护的路径：正常操作中的信号 setter 都有立即 guard 检查，`shutdown()` 的末尾 setter 则是函数最后动作。worker 进度回调只通过 `Qt::QueuedConnection` 投递到 Controller，未发现后台线程直接调用 setter。

### 2. `shutdown()` 重入、等待和死锁风险

**ADDRESSED。** `shutdown()` 现在的顺序为：置 `m_shuttingDown`、停止 debounce/设备监控、置三个 cancellation token、断开八个 watcher、逐个 `waitForFinished()`、提交 `monitoringStopped`/`allWatchersStopped`/`shutdownComplete`，最后才调用 `m_viewModel->setBusy(false)`：[apps/gui/MainWorkspaceController.cpp:2628](/home/zz/Jackfahdin/qt/github/ZzRkDevTool/.worktrees/zzpuretools-framework/apps/gui/MainWorkspaceController.cpp:2628)、[apps/gui/MainWorkspaceController.cpp:2637](/home/zz/Jackfahdin/qt/github/ZzRkDevTool/.worktrees/zzpuretools-framework/apps/gui/MainWorkspaceController.cpp:2637)、[apps/gui/MainWorkspaceController.cpp:2652](/home/zz/Jackfahdin/qt/github/ZzRkDevTool/.worktrees/zzpuretools-framework/apps/gui/MainWorkspaceController.cpp:2652)、[apps/gui/MainWorkspaceController.cpp:2660](/home/zz/Jackfahdin/qt/github/ZzRkDevTool/.worktrees/zzpuretools-framework/apps/gui/MainWorkspaceController.cpp:2660)、[apps/gui/MainWorkspaceController.cpp:2668](/home/zz/Jackfahdin/qt/github/ZzRkDevTool/.worktrees/zzpuretools-framework/apps/gui/MainWorkspaceController.cpp:2668)、[apps/gui/MainWorkspaceController.cpp:2673](/home/zz/Jackfahdin/qt/github/ZzRkDevTool/.worktrees/zzpuretools-framework/apps/gui/MainWorkspaceController.cpp:2673)。这样同步 `busyChanged(false)` 销毁 Controller 时，析构重入只遇到已完成的 cleanup，不会跳过 watcher 等待或 lifecycle 标记。
- 所有 watcher 的 future 工作运行在 QtConcurrent 线程池；其进度通知使用 queued lambda，且 shutdown 先断开完成信号再等待。没有 `BlockingQueuedConnection` 或 worker 等待 GUI 的反向依赖，静态调用图未见死锁环。backend cancellation 回调只读取原子 token，停止监控在 GUI 线程同步完成。
- 最后的 `setBusy(false)` 后立即 return，不再访问 Controller 成员；即使 DirectConnection 接收器同步触发 `beginShutdown()` 并销毁对象，也没有返回路径上的成员解引用。析构函数仍调用幂等 `shutdown()`，第二次调用在第 2629-2631 行返回是安全的，因为第一次已完成全部清理。
- `QFutureWatcher` 析构本身不承担等待职责，但此实现已在析构前显式等待全部 watcher，满足计划中“所有 watcher 停止后销毁”的要求。

### 3. 新增真实 Controller 隔离场景

**有效且通过。** 新增场景不是只直接调用 ViewModel setter，而是进入真实 Controller 栈，并用 `Qt::DirectConnection` 调用框架 `beginShutdown()`：

- `pendingRefreshAfterInspection` 先完成真实扫描、启动 `inspectSelectedDevice()`，再注入 hotplug pending，busy 变为 true 时同步关闭：[tests/gui_framework_test.cpp:510](/home/zz/Jackfahdin/qt/github/ZzRkDevTool/.worktrees/zzpuretools-framework/tests/gui_framework_test.cpp:510)。覆盖 pending refresh -> `refreshDevices()` -> `setBusy(true)` 链。
- `attachPagesRebuildBusyChanged` 获取四个真实页面，先解绑再重新绑定，触发 `rebuildDownloadTable()`：[tests/gui_framework_test.cpp:526](/home/zz/Jackfahdin/qt/github/ZzRkDevTool/.worktrees/zzpuretools-framework/tests/gui_framework_test.cpp:526)。覆盖 `attachPages()` 上层继续执行风险。
- `clearImagePlanBusyChanged` 真实加载 parameter 文件、定位下载表行，再通过 `QMetaObject::invokeMethod` 调用 Controller 的 `clearSelectedDownloadImage()`：[tests/gui_framework_test.cpp:547](/home/zz/Jackfahdin/qt/github/ZzRkDevTool/.worktrees/zzpuretools-framework/tests/gui_framework_test.cpp:547)。覆盖 plan changed -> controls -> busy setter 链。
- `shutdownBusyChanged` 启动一个有 250ms 延迟的真实 scan watcher，保留 `MainWorkspaceLifecycle`，在 shutdown 的 `busyChanged(false)` 上同步销毁，并在窗口消失后断言 scan 已完成及三个 lifecycle 标记均为 true：[tests/gui_framework_test.cpp:495](/home/zz/Jackfahdin/qt/github/ZzRkDevTool/.worktrees/zzpuretools-framework/tests/gui_framework_test.cpp:495)、[tests/gui_framework_test.cpp:700](/home/zz/Jackfahdin/qt/github/ZzRkDevTool/.worktrees/zzpuretools-framework/tests/gui_framework_test.cpp:700)。这直接验证了上一轮提前返回缺陷的 cleanup contract。
- 每个场景通过独立 `QProcess` 执行，父测试断言 Controller 与框架窗口均销毁；分区表双终态场景仍断言第二个信号不会发出：[tests/gui_framework_test.cpp:693](/home/zz/Jackfahdin/qt/github/ZzRkDevTool/.worktrees/zzpuretools-framework/tests/gui_framework_test.cpp:693)、[tests/gui_framework_test.cpp:697](/home/zz/Jackfahdin/qt/github/ZzRkDevTool/.worktrees/zzpuretools-framework/tests/gui_framework_test.cpp:697)。

### 4. macOS GUI `@rpath/libusb` 门禁

**ADDRESSED（静态检查）。** workflow 现在对 CLI 和 GUI 两个可执行文件都精确要求 `otool -L` 输出包含 `@rpath/libusb`：[.github/workflows/cross-platform-apps.yml:110](/home/zz/Jackfahdin/qt/github/ZzRkDevTool/.worktrees/zzpuretools-framework/.github/workflows/cross-platform-apps.yml:110)、[.github/workflows/cross-platform-apps.yml:112](/home/zz/Jackfahdin/qt/github/ZzRkDevTool/.worktrees/zzpuretools-framework/.github/workflows/cross-platform-apps.yml:112)。同时 workflow 检查 DMG 内 `libqcocoa.dylib` 和 bundled libusb，并以 offscreen 模式运行 GUI smoke。当前 Linux 主机无法运行 `otool`、DMG 或 macOS bundle，因此未将静态检查表述为平台实测。

## Critical

无。

## Important

无。

## Minor

1. **progress/history 缺少真实 Controller 操作栈的同步销毁回归。** `viewModelProgressChanged` 和 `viewModelHistoryChanged` 仍直接调用 ViewModel setter：[tests/gui_framework_test.cpp:475](/home/zz/Jackfahdin/qt/github/ZzRkDevTool/.worktrees/zzpuretools-framework/tests/gui_framework_test.cpp:475)。它们不能单独证明真实升级 progress/history handler 在 setter 返回后安全；不过当前源码对所有 progress/history setter 调用点均有立即 guard，且完整 ASan 回归通过，因此这是测试纵深建议，不构成合并阻断。后续可增加真实 `handleUpgradeProgress()`、`finishHistoryEntry()` 场景。

## 修复 diff 新增问题

未发现 Critical 或 Important 级别的新问题。`shutdown()` 将最后的 `setBusy(false)` 放在 cleanup 之后并立即 return，保留了 busy 状态通知，同时避免了上一轮的 cleanup 提前返回；页面装配和下载清理新增 guard 只在同步销毁后中止当前调用，不改变正常路径行为。

## 验证证据

- `cmake --build build-asan --target gui_framework_test --parallel 4`：exit 0（目标已是最新）。
- 四个新增隔离场景分别执行：
  - `shutdownBusyChanged`：exit 0；
  - `pendingRefreshAfterInspection`：exit 0；
  - `attachPagesRebuildBusyChanged`：exit 0；
  - `clearImagePlanBusyChanged`：exit 0。
- `ASAN_OPTIONS=detect_leaks=0 QT_QPA_PLATFORM=offscreen ctest --test-dir build-asan --output-on-failure -R '^(gui_framework|gui_upgrade|gui_image_download|gui_smoke)$'`：4/4 通过，0 失败（3.27s）。
- `ASAN_OPTIONS=detect_leaks=0 QT_QPA_PLATFORM=offscreen ctest --test-dir build-asan --output-on-failure`：25/25 通过，0 失败（18.19s）；packaging smoke（`linux_install_smoke`、`cli_only_install_smoke`）也通过。`detect_leaks=0` 下不宣称 LSan 泄漏检查通过。
- `python3` + PyYAML 解析 workflow，并断言存在 `macos`、`windows-mingw` jobs 及 GUI/CLI 两处 `@rpath/libusb` 匹配：通过。
- `git diff --check 50c9f93..51c3fda`：无输出。
- 当前 worktree 在报告写入前无产品代码变更；Windows/Windows MinGW/macOS 未本地执行，需以对应 GitHub Actions runner 结果作为最终平台证据。

## 最终判断

**可以合并。** 合并前的剩余事项仅为 Minor 测试纵深和跨平台 runner 实测，不阻断本次修复提交。
