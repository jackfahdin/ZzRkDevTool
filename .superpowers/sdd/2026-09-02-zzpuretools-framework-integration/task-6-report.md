# 任务 6 报告：装配无边框窗口与 Fluent 顶部标签

## 状态

已完成。

## 红灯证据

先扩展 `gui_framework_test`，要求 Builder 创建并暴露唯一的框架应用窗口。
首次执行：

```text
$ QT_QPA_PLATFORM=offscreen ./build-task2/tests/gui_framework_test
FAIL: builder creates exactly one framework application window
```

失败原因符合预期：测试 harness 当时只创建 `ZzPureApplication`，尚未通过
`ZzApplicationBuilder` 装配 `ZzApplicationWindow`。

## 变更

- 新增 `GuiWindowShell`，按
  `create(&window, window.titleBar()) -> integrateApplicationNavigation(...) ->
  setCentralWidget(...)` 的框架顺序装配 `ZzWorkspaceShell`。
- Builder 注册唯一的“下载镜像”初始路由和 NavigationNode；初始页面实例独占
  `MainWorkspaceViewModel`、`MainWorkspaceController` 和下载页面，Controller 独占
  设备、完整升级、分区三个页面实例。
- 隐藏集成导航面板、左右 ActivityBar、左右 SidePane 和窗口 NavigationPane；
  配置窗口标题、图标、最小尺寸 `800x700` 和初始尺寸 `980x820`。
- 中央 `ZzTabWidget` 固定为“下载镜像”“设备”“完整升级”“分区”四个标签，
  禁止关闭、移动、撕出和跨窗口转移，并隐藏新建标签按钮。
- 测试 harness 改为构建真实 `ZzApplicationWindow`，继续支持后端注入、关闭
  modal 对话框以及三个文件/目录加载入口。
- 删除旧 `MainWindow` 实现及构建引用；入口程序和 GUI 业务测试均迁移到框架
  窗口与 Controller。
- smoke test 保留两次扫描契约：首次扫描后点击刷新按钮，第二次扫描完成后退出。

## 验证

```text
$ cmake --build build-task2 --target ZzRkDevTool gui_framework_test \
    gui_upgrade_test gui_image_download_test -j2
[1/3] Building CXX object tests/CMakeFiles/gui_framework_test.dir/gui_framework_test.cpp.o
[2/3] Linking CXX executable tests/gui_framework_test
```

```text
$ QT_QPA_PLATFORM=offscreen ctest --test-dir build-task2 \
    -R 'gui_(framework|upgrade|image_download|smoke)' --output-on-failure
1/4 Test #19: gui_smoke ................. Passed
2/4 Test #20: gui_framework ............. Passed
3/4 Test #22: gui_upgrade ............... Passed
4/4 Test #23: gui_image_download ........ Passed
100% tests passed, 0 tests failed out of 4
Total Test time (real) = 0.76 sec
```

```text
$ git diff --check
# exit 0, no output

$ rg -n 'MainWindow' apps/gui tests/CMakeLists.txt tests \
    --glob '!review-*' --glob '!task-*-*.md'
# exit 1, no matches
```

## 疑虑

- 当前测试环境不支持真实 USB hotplug。smoke test 不再将 hotplug 可用性作为
  启动成功的前置条件，但仍通过刷新按钮完成并验证第二次手动扫描回退路径。

## 第 1 轮审查修复

### 根因

`integrateApplicationNavigation()` 成功后，框架已把原 application body 中的
导航面板和 page host 迁入 workspace，并对原 body 排队 `DeferredDelete`。
`GuiWindowShell::initialize()` 随即把 `workspaceRoot` 设为 central widget，但
后续 `showPanel()` 和 surface 可用性检查仍可能失败。失败时局部
`ZzWorkspaceShell` 析构发现已集成的 root 仍由 host 持有，因而保留它；代码也
没有取消原 body 的延迟删除或把导航/page host 迁回原布局，留下半装配窗口。

### 红灯证据

在 `gui_framework_test` 中用真实 `ZzApplicationWindow` 装配，并通过 Qt 事件
过滤器连接左侧 `ZzSidePane::collapsedChanged`。导航面板已接管后，测试在
`showPanel(..., false)` 折叠时立即恢复展开，使框架真实状态审计返回失败。

修复前执行：

```text
$ QT_QPA_PLATFORM=offscreen ./build-task2/tests/gui_framework_test
FAIL: failed workspace attach restores the original central widget
FAIL: failed workspace attach removes the workspace root
FAIL: failed workspace attach restores the navigation pane
FAIL: failed workspace attach restores the page host and body layout
# exit 4
```

注入已触发、`attach()` 返回失败和临时 shell 被移除的断言均通过，失败集中在
审查指出的已提交工作区状态未回滚。

### 修复

- 集成前保存原 body、`QBoxLayout`、导航面板、page host，以及两者的布局位置、
  stretch 和 alignment。
- 集成后的后置步骤失败时，取消原 body 的 `DeferredDelete`，从 workspace
  取回导航面板、移出 page-host 固定标签，并分离 `workspaceRoot`。
- 按快照恢复原 body 布局与 central widget；临时 workspace 随局部 shell
  析构清理，同时原样返回触发失败的 `ZzError`。
- 回归测试在处理全部 `DeferredDelete` 后验证原 central 仍存活、workspace
  root 已消失、导航和 page host 回到两项原布局，且当前路由保持不变。

### 验证

```text
$ cmake --build build-task2 --target ZzRkDevTool gui_framework_test \
    gui_upgrade_test gui_image_download_test -j2
[1/4] Linking CXX executable apps/gui/ZzRkDevTool
[2/4] Linking CXX executable tests/gui_upgrade_test
[3/4] Linking CXX executable tests/gui_image_download_test
# exit 0
```

```text
$ QT_QPA_PLATFORM=offscreen ctest --test-dir build-task2 \
    -R 'gui_(framework|upgrade|image_download|smoke)' --output-on-failure
1/4 Test #19: gui_smoke ................. Passed
2/4 Test #20: gui_framework ............. Passed
3/4 Test #22: gui_upgrade ............... Passed
4/4 Test #23: gui_image_download ........ Passed
100% tests passed, 0 tests failed out of 4
Total Test time (real) = 0.79 sec
```

```text
$ git diff --check
# exit 0, no output
```

实现提交：`cc43ea8`（`修复工作区装配失败回滚`）。
