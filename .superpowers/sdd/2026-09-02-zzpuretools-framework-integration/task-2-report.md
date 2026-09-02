# 任务 2 完成报告：PureApplication 基础生命周期和 GUI 测试 Harness

## 变更

- 新增 `apps/gui/GuiApplicationBootstrap.hpp/.cpp`。
  - 定义 `GuiApplicationOptions`，保留 backend、对话框和 smoke-test 配置。
  - 集中设置组织名、组织域名、应用名和版本元数据。
  - 将 `ZzCore::ZzError` 转换为带阶段、技术信息和上下文的日志记录。
- 新增 `tests/GuiTestHarness.hpp/.cpp`。
  - 在 GUI 线程调用 `ZzWindowKitBootstrap::prepare()`。
  - 创建并拥有唯一 `ZzPureApplication`，不创建窗口、不注册页面、不调用 Builder。
  - 析构时幂等调用 `beginShutdown()` 后释放应用。
- 新增 `tests/gui_framework_test.cpp`。
  - 使用 offscreen 平台验证 `qApp`、`ZzPureApplication`、主题控制器和应用样式。
  - 验证 `beginShutdown()` 可重复调用。
- 更新 GUI 支持库的框架公开依赖和测试目标配置。

## 验证命令与实际结果

配置并构建了全新的任务专用目录，避免复用旧生成目录中的安装脚本：

```text
cmake -S . -B build-task2 -G Ninja -DZZRK_BUILD_GUI=ON -DZZRK_BUILD_CLI=ON -DZZRK_BUILD_TESTS=ON
cmake --build build-task2 --target gui_framework_test -j2
```

结果：配置成功，目标成功编译并链接。

```text
Test project .../build-task2
    Start 20: gui_framework
1/1 Test #20: gui_framework ....................   Passed    0.01 sec

100% tests passed, 0 tests failed out of 1
```

测试命令：

```text
QT_QPA_PLATFORM=offscreen ctest --test-dir build-task2 -R gui_framework --output-on-failure
```

结果：`gui_framework` 通过，1/1 测试通过。

## 疑虑

- 工作树中原有的 `build-zzpuretools` 生成目录带有旧的 `thirdParty/ZzPureTools/cmake_install.cmake`，直接重新生成会触发任务 1 的安装规则作用域检查。因此本报告使用全新的 `build-task2` 验证；源代码和测试不依赖该旧目录。
- 本任务没有创建窗口、注册业务页面或调用 `ZzApplicationBuilder`，标题栏、WindowKit agent 和顶部标签页契约留给任务 6。
