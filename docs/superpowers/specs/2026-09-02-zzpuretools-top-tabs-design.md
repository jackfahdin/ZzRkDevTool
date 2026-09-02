# ZzPureTools 顶部标签页接入设计

## 目标

将 ZzRkDevTool 当前基于 Qt `QTabWidget` 的四个业务页面替换为
`ZzPureTools` 仓库中的 `ZzFluentUI::ZzTabWidget`，统一使用 Fluent 风格的
顶部标签、控件绘制和主题调色板。页面顺序和业务行为保持不变：

`下载镜像 | 设备 | 完整升级 | 分区`

本阶段不接入 `ZzNavigationView`、`ZzWorkspaceShell` 或完整的
`ZzPureTools` 应用装配层，不改变烧录 workflow、USB 监控、异步任务和
`IGuiOperationBackend` 接口。

## 依赖与版本

- 依赖仓库：`git@github.com:jackfahdin/ZzPureTools.git`
- 固定来源：`master` 当前基线 commit
  `d6b90f1d266b6aa97b0ffb3a75deaa4bc2314514`
- 接入方式：新增 Git submodule，路径为 `thirdParty/ZzPureTools`；构建时
  以源码子目录方式加入，不依赖开发机全局安装的框架包。
- 当前阶段使用的目标：`Zz::Core`、`Zz::FluentFoundation`、`Zz::FluentUI`。
  不启用框架示例、测试、Benchmark 或 Qt Quick；不链接 `Zz::PureTools`
  和 `Zz::WindowKit`，避免引入当前不需要的导航和无边框窗口能力。
- 不直接 `add_subdirectory(thirdParty/ZzPureTools)`。父项目通过独立的
  `cmake/ZzPureToolsFluent.cmake` 依次加入 `ZzThirdParty/ZzLog`、`ZzCore`
  和 `ZzFluentUI`，并加载框架的 `ZzFirstPartyTarget.cmake` 与
  `ZzLibraryTarget.cmake`。这样不会配置 `ZzWindowKit`、Qt 私有窗口模块或
  `ZzPureTools` 导航/工作区目标。
- 框架的测试、示例、Benchmark、Qt Quick 和发布检查不进入主工程；
  ZzRkDevTool 自己的测试仍保持开启。

## 架构

应用入口在创建 `MainWindow` 前构造一个由 `QApplication` 拥有的
`ZzFluentUI::ZzThemeController` 和 `ZzFluentUI::ZzFluentStyle`。样式使用
Fusion 作为基础样式，并通过 `QApplication::setStyle()` 设置为应用级样式，
确保标准 Qt 控件和 `ZzTabWidget` 使用同一套 Fluent 颜色、尺寸和焦点视觉。
主题控制器只在 GUI 线程使用，生命周期长于样式和主窗口。

`MainWindow` 的成员类型由 `QTabWidget*` 改为 `ZzFluentUI::ZzTabWidget*`，
保留现有 `addTab()`、`setCurrentIndex()`、`currentChanged` 和
`findChild<QTabWidget*>()` 兼容语义。创建四个页面时继续以标签容器为父对象，
不改变页面所有权。标签栏的扩展能力按固定工具页策略关闭：不允许拖出、跨容器
转移、新建标签和关闭标签；不启用固定、脏状态或注意提示。这样用户得到统一的
顶部标签视觉，但不会改变四个固定业务入口的行为。

## 数据流与状态

业务页面内部的控件和状态维持现状。主题变化由 `ZzThemeController` 发出
快照变更，`ZzFluentStyle` 负责刷新应用调色板和受影响控件；主窗口不读取或
修改框架私有快照。USB 热插拔回调、QtConcurrent watcher 和进度信号继续沿用
现有线程边界，样式和标签切换只发生在 GUI 线程。

若框架构建或运行时初始化失败，应用在启动阶段记录明确错误并退出，不回退到
旧 `QTabWidget`，避免同一安装包出现两套视觉契约。业务 workflow 的错误仍由
现有页面状态栏、消息框和表格状态展示。

## CMake 与打包

顶层 CMake 在 `thirdParty/ZzPureTools` 可用时先找到 Qt Concurrent、Svg 和
Widgets，再包含最小组件集。`Zz::FluentUI` 和
`Zz::FluentFoundation` 作为 `zzrk_gui_support` 的私有链接依赖，`Zz::Core`
仅作为它们的传递实现依赖。框架库不暴露给 CLI、core、transport、workflow
或 storage 目标，保持业务层不依赖 Qt Widgets。安装/打包阶段将随 GUI 一起
收集框架动态库及其 Qt 依赖；现有应用名称、桌面入口、libusb 规则和许可证
清单继续有效，并新增 ZzPureTools 的 MIT 许可证通知和固定 commit 记录。

## 测试与验收

1. 扩展 GUI 离屏 smoke 测试，确认 `mainTabs` 的实际类型是
   `ZzFluentUI::ZzTabWidget`，四个标签顺序、标题和页面切换均正确。
2. 增加主题初始化测试：创建 `ZzThemeController` 和 `ZzFluentStyle` 后，
   浅色主题可见，显式深色主题能刷新 palette；测试结束恢复原应用样式。
3. 保持现有下载镜像、完整升级、设备详情、分区管理和 USB 忙碌协调测试，
   验证替换标签容器不改变业务信号和异步生命周期。
4. Linux Debug、Release、ASan/UBSan 至少运行现有测试全集；Windows MSVC、
   Windows MinGW 和 macOS 执行无硬件配置/编译检查，确认公共头、链接目标和
   打包运行库可解析。
5. 在 Linux Xvfb 下分别截图 980x820 和窄窗口，检查顶部标签、页面表格、
   双进度条、状态栏无重叠；验证暗色主题截图中的文字和焦点状态可读。

## 非目标

- 不实现左侧导航、工作区拆分、标签拖出、多窗口、停靠面板或命令面板。
- 不重构 `MainWindow` 的业务页面为独立 Presenter/ViewModel；仅在标签容器和
  应用级样式边界内做必要调整。
- 不升级 Qt、libusb、spdlog 或现有烧录协议；若框架需要的组件与当前 Qt
  安装不匹配，应记录缺失组件并停止在依赖接入阶段，由用户补充框架能力或
  开发环境后再继续。
