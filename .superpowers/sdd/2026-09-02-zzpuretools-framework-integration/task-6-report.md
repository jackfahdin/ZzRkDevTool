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
