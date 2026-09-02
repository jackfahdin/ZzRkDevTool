# 任务 3 完成报告：提取工作区状态与业务控制器

## 变更

- 新增 `MainWorkspaceViewModel`，集中保存四个设备端口状态、升级/下载/分区取消标志、忙碌状态、双进度和升级历史快照；不持有 QWidget，并通过 Qt signals 通知页面。
- 新增 `MainWorkspaceController`，接管可注入 backend 的设备扫描、热插拔回调去抖、异步扫描 watcher、共享连接状态控件、刷新按钮、最后扫描时间和关闭取消协议。
- 增加 `attachWorkspace()`、`createInitialDownloadPage()`、`shutdown()` 及共享状态访问接口。下载页面当前为轻量占位 View，后续页面任务可直接替换 factory 内容。
- `MainWindow` 暂时保留原有业务页面和测试行为，同时持有工作区 Controller，析构时先触发 Controller 的幂等关闭，为后续完整页面迁移提供适配边界。
- GUI 支持库加入两个新类；`gui_framework_test` 增加完整 fake backend、Controller backend/modal 配置、共享控件挂载、监控停止、取消标志和幂等 shutdown 断言。

## 验证命令与实际结果

```text
cmake --build build-task2 --target gui_framework_test -j2
```

结果：目标成功编译并链接。

```text
cmake --build build-task2 --target gui_upgrade_test gui_image_download_test gui_framework_test -j2
```

结果：三个目标成功编译并链接。

```text
QT_QPA_PLATFORM=offscreen ctest --test-dir build-task2 -R 'gui_(upgrade|image_download|framework)' --output-on-failure
```

实际结果：

```text
Test project .../build-task2
    Start 20: gui_framework
1/3 Test #20: gui_framework ....................   Passed    0.03 sec
    Start 22: gui_upgrade
2/3 Test #22: gui_upgrade ......................   Passed    0.43 sec
    Start 23: gui_image_download
3/3 Test #23: gui_image_download ...............   Passed    0.27 sec

100% tests passed, 0 tests failed out of 3
Total Test time = 0.72 sec
```

## 疑虑

- 任务 3 的页面实例所有权契约已通过 `ZzPageInstance::create()` 验证，但下载 View 仍是占位页面；真实下载镜像页面和四标签完整装配留给后续任务 4-6。
- 旧 `MainWindow` 业务逻辑暂未全部删除或改为 Controller 转发，以保持现有升级/下载 GUI 回归测试稳定；后续页面迁移完成后应移除重复 watcher 和旧顶层窗口实现。
- 本工作树的旧 `build-zzpuretools` 目录会触发任务 1 的安装规则作用域检查，因此验证使用全新的 `build-task2` 目录。

## 审查修复

- 移除 `MainWindow` 的扫描 watcher、热插拔 debounce timer 和 backend 监控启动/停止；Controller 现在是唯一的设备扫描与热插拔协调者。旧窗口仅接收 `devicesChanged` 更新设备表，保留兼容业务信号和页面操作。
- Controller 支持外部忙碌回调，避免升级、镜像下载或分区 watcher 运行时由热插拔触发并发扫描；MainWindow 的共享 cancellation token 改为使用 Controller ViewModel 中的唯一原子值。
- Controller 将 ViewModel 所有权改为可转移的 `unique_ptr`。创建首个下载页面时转移该唯一 ViewModel 给 `ZzPageInstance`，Controller 仅以 `QPointer` 观察；页面销毁回调使用 Controller 的 guarded pointer，避免迟到销毁回调访问悬空对象。
- MainWindow 的设备选中端口同步写入共享 ViewModel，后续页面可观察同一状态。

修复后重新执行：

```text
cmake --build build-task2 --target gui_upgrade_test gui_image_download_test gui_framework_test -j2
QT_QPA_PLATFORM=offscreen ctest --test-dir build-task2 -R 'gui_(upgrade|image_download|framework)' --output-on-failure
```

实际结果：三个目标成功构建，`gui_framework`、`gui_upgrade`、`gui_image_download` 全部通过（3/3，0.44 秒）。
