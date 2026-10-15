# 全局设备状态栏实现计划

> 使用 executing-plans 内联实施；用户已批准设计与开始实现，继续在现有功能分支工作。

**目标：** 状态栏统一展示和切换全局目标设备，页面移除重复选择，确保缓存与操作目标一致。

**架构：** `GuiDeviceStatusBar` 负责展示和选择请求；`MainWorkspaceController` 负责选择验证与状态失效，ViewModel 保存唯一目标。完整组件放入框架状态栏永久区域，监听 `QStatusBar::messageChanged` 更新独立提示标签，沿用现有 `setGuiStatus` 的翻译绑定。

**技术栈：** Qt 6、ZzPureTools、现有 libusb 热插拔与 GUI 后端，沿用当前测试注入边界。

## 文件职责

- 新增 `apps/gui_qt/GuiDeviceStatusBar.hpp/.cpp`：端口选择、连接信息、提示与刷新/时间；不启动后台任务。
- 修改 `MainWorkspaceController.hpp/.cpp`：移除双向控件同步，统一选择与身份检测，缓存失效，忙碌保护。
- 修改 `pages/DevicePage.hpp/.cpp`、`pages/ImageDownloadPage.hpp/.cpp`：去掉旧列表/选择器，页面只接收当前设备能力和状态。
- 沿用 `GuiTranslations.cpp` 的消息绑定，由组件监听消息变化，无须修改业务消息调用点。
- 修改构建、英文翻译和 GUI 文档；新增 `tests/gui_device_status_test.cpp` 并迁移既有 GUI 测试的选择入口。

## 任务与验收

- [x] 添加失败集成测试。通过 `statusDeviceCombo` 选择 `1-6`，断言 ViewModel 目标和真实设备读取请求均为 `1-6`；旧实现缺少全局入口时失败。
- [x] 实现状态栏组件。`setDevices(result, port)` 只更新展示且阻塞选择信号；`portSelected(port)` 请求控制器选择；`setBusy` 禁止控件交互。刷新/时间沿用现有对象名。
- [x] 统一当前设备。自动选择仅限未要求明确选择且无错误的一台设备；失去已选设备后保留断开提示。相同端口身份变化也调用清除详情、GPT、设备布局与预检结果，再刷新各页能力。
- [x] 删除页面重复入口，下载请求使用 `selectedPortPath()`；下载页接收 canReadGpt，启动条件检查现有支持设备策略。
- [x] 将状态消息放到独立区域，补齐实时翻译、提示和大字号/窄窗口布局，记录使用规则。
- [x] 扩展测试覆盖扫描/断开/重连/模式/身份/错误/忙碌与草稿；迁移旧测试后全量构建并运行 `ctest --test-dir build/local-linux-release --output-on-failure -j 4`。
- [x] 检查截图、独立审查并修正问题，完成中文说明与提交。

验证命令先使用 `cmake --build --preset local-linux-release --target gui_device_status_test` 和 `ctest --test-dir build/local-linux-release -R '^gui_device_status$' --output-on-failure`，随后运行全量 Release 构建与 CTest。截图使用临时目录，测试不读写真实设备和用户配置。

## 实施结果（2026-10-15）

- Release 全量构建通过，CTest 37/37 通过，包含新增多设备和单设备启动场景。
- 集成测试覆盖实际后端请求端口、忙碌拒绝切换、断开重连、同端口地址/模式变化、错误扫描、热插拔失败后手动刷新、实时英文和普通消息不遮住选择器。
- 独立审查发现并修复扫描忙碌信号先于控制器锁定，以及 GPT 重读覆盖当前会话手工草稿两处问题；补充真实文件选择弹窗回归，复核无剩余阻断问题。
- 检查了中文、英文及 24 磅字体截图，长状态与操作提示省略，选择器、刷新、扫描时间保持可见。
- 说明同步更新到 README 和 `docs/GUI_STYLE_GUIDE_ZH.md`。自动化使用注入后端，真实板卡热插拔和多板切换仍待用户现场验收。
