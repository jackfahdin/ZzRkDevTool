# 任务 4 完成报告：迁移下载镜像与设备页面

## 变更

- 新增 `ImageDownloadPage`，承载设备与布局选择、九列镜像计划表、镜像操作按钮、双进度条和状态展示，保留原有对象名。
- 新增 `DevicePage`，承载六列设备表、设备详情状态和 ChipInfo、Capability、FlashID、容量、FlashInfo、USB 接口展示，保留原有对象名。
- `MainWorkspaceController` 接管下载准备与执行 watcher、设备检查 watcher、下载目录匹配、镜像选择状态、设备/下载选择同步、GPT 布局接收和热插拔待刷新排空。
- `MainWindow` 删除旧下载/设备 UI、字段、watcher 和 workflow 实现，仅保留任务 6 前所需的路径加载薄转发与分区页面适配。
- `gui_image_download_test`、`gui_upgrade_test` 改由 `GuiTestHarness` 管理 `ZzPureApplication`；`gui_framework_test` 覆盖真实框架页面、九列表格、双进度、设备详情和异步失败收尾。

## 审查修复

- 仅在设备端口实际变化时发送 `selectedDeviceChanged`，避免同端口刷新清空已读取分区表和设备 GPT 下载布局。
- 扫描与设备检查完成后排空 USB busy 期间积压的热插拔刷新；回归测试在检查期间注入事件并验证只补扫一次。
- 页面复选变化同步回 Controller 的下载/校验映射，避免选择另一个镜像导致表格重建后覆盖用户状态。
- 下载执行失败时恢复双进度条确定范围，并按 `itemIndex` 标记对应目标为“失败”或“已取消”。
- 恢复下载与设备管理错误码的细分中文文案，以及破坏性下载确认框中的布局、目标和重启摘要。

## 验证命令与实际结果

```text
cmake --build build-task2 --target gui_image_download_test gui_upgrade_test gui_framework_test -j2
```

结果：三个目标成功编译并链接。

```text
QT_QPA_PLATFORM=offscreen ctest --test-dir build-task2 -R 'gui_(image_download|upgrade|framework)' --output-on-failure
```

实际结果：

```text
1/3 Test #20: gui_framework ....................   Passed    0.07 sec
2/3 Test #22: gui_upgrade ......................   Passed    0.29 sec
3/3 Test #23: gui_image_download ...............   Passed    0.13 sec

100% tests passed, 0 tests failed out of 3
Total Test time (real) = 0.49 sec
```

```text
git diff --check
```

结果：无输出，通过。

## 疑虑

- `MainWorkspaceController` 的 tabs、status bar 和共享状态控件仍为裸 QObject 指针；当前 MainWindow 与框架宿主保证其生命周期更长，后续若支持动态重挂工作区，应改为 guarded pointer 或明确生命周期契约。
- `DevicePage` 仍包含设备可管理/可刷写判定，并且端口为空时未恢复基线的 `portNumbers` 降级显示；设备规则后续应收敛到 Controller，避免页面与 Controller 两处规则漂移。
- 设备表未恢复基线的固定列宽、默认总线排序和总线数值 `DisplayRole`，总线号达到两位数时手动排序仍按文本顺序。
- `MainWindow.cpp` 顶部仍有已迁移设备/下载列与 role 的无引用常量，可在任务 6 删除旧窗口适配器时一并清理。

## 正式审查修复

- 将设备 GPT 读取 workflow 和 `QFutureWatcher` 完整迁入 `MainWorkspaceController`，并将 watcher 纳入 USB busy 判定、关闭等待和 pending hotplug 排空。`MainWindow` 仅通过 Controller 发起读取并刷新旧分区页适配 UI。
- 新增 Controller-only 回归测试：不构造 `MainWindow`，从下载页点击“读取设备 GPT”，验证后端读取一次、路径切换到“1-5 / 设备 GPT”，以及 Loader 和两个真实 GPT 分区组成的三行计划。
- `ImageDownloadPage` 通过 `PlanRow`、`PlanChoice`、`PlanTarget` 和状态更新方法封装下载行模型；Controller 不再引用 `QTableWidget`、下载列枚举或 item role。
- 将设备可刷写/可管理规则移入 Controller，`DevicePage` 只接收展示行和准入结果。原“DevicePage 仍包含规则”的疑虑已解决；固定列宽、默认总线排序、数值 `DisplayRole` 及旧窗口无引用常量等 Minor 保留，未扩大本次修复范围。

### 修复后验证

```text
cmake --build build-task2 --target gui_framework_test gui_image_download_test gui_upgrade_test -j2
```

结果：三个目标成功编译并链接。

```text
QT_QPA_PLATFORM=offscreen ctest --test-dir build-task2 -R 'gui_(image_download|upgrade|framework)' --output-on-failure
```

实际结果：

```text
1/3 Test #20: gui_framework ....................   Passed    0.17 sec
2/3 Test #22: gui_upgrade ......................   Passed    0.28 sec
3/3 Test #23: gui_image_download ...............   Passed    0.13 sec

100% tests passed, 0 tests failed out of 3
Total Test time (real) = 0.58 sec
```

```text
git diff --check
```

结果：无输出，通过。
