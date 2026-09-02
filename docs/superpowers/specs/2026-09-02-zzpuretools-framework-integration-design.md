# ZzPureTools 完整框架接入设计

## 目标

将 ZzRkDevTool GUI 从独立的 Qt `QApplication`、`QMainWindow` 和
`QTabWidget` 应用迁移到完整的 ZzPureTools 桌面应用框架。接入范围包括：

- `ZzWindowKitBootstrap` 的进程级窗口初始化；
- `ZzPureApplication` 的应用生命周期、Fluent 样式和主题管理；
- `ZzApplicationBuilder` 的应用装配；
- `ZzApplicationWindow`、`ZzWindowAgent` 和 `ZzFluentTitleBar` 的无边框窗口；
- `ZzWorkspaceShell` 的工作区生命周期和后续扩展能力；
- `ZzTabWidget` 提供的顶部功能标签。

GUI 保留四个固定业务入口，顺序不变：

`下载镜像 | 设备 | 完整升级 | 分区`

当前不展示左侧导航、侧栏、底部工具面板、命令面板或多窗口入口，但不通过裁剪源码
移除这些框架能力。它们随完整框架一起构建，后续可以在应用装配层按需启用。

## 设计原则

1. 应用只有一个顶层窗口。旧 `MainWindow` 不作为子窗口嵌入
   `ZzApplicationWindow`，避免重复的标题栏、状态栏和窗口生命周期。
2. 框架负责应用、主题、窗口和工作区；ZzRkDevTool 负责设备、镜像和烧录业务。
3. GUI 只能依赖业务层的公开接口，transport 和 workflow 不反向依赖框架。
4. 本次迁移不改变刷写顺序、USB 热插拔、取消语义、报告内容和设备协议。
5. 框架接入失败时明确终止启动，不回退到旧窗口或旧标签控件。

## 依赖与版本

- 依赖仓库：`git@github.com:jackfahdin/ZzPureTools.git`
- 固定基线 commit：`d6b90f1d266b6aa97b0ffb3a75deaa4bc2314514`
- 接入路径：`thirdParty/ZzPureTools`
- 接入方式：Git submodule，初始化命令必须使用
  `git submodule update --init --recursive`，以同时取得 ZzPureTools 的
  QWindowKit 等嵌套依赖。
- 应用使用完整的 `Zz::PureTools` 目标，并按公开头文件需求显式链接
  `Zz::WindowKit` 和 `Zz::FluentUI`。不再只挑选 `ZzCore`、
  `ZzFluentFoundation` 和 `ZzFluentUI` 三个组件。
- 框架示例、框架测试、Benchmark、Qt Quick 和发布检查默认关闭；
  ZzRkDevTool 自己的测试继续由 `ZZRK_BUILD_TESTS` 控制。
- ZzWindowKit 依赖与当前 Qt 版本完全匹配的 `Qt6CorePrivate`、
  `Qt6GuiPrivate` 和 `Qt6WidgetsPrivate`。缺失时 CMake 配置必须失败并给出
  安装匹配 Qt 私有开发文件的提示。

## 运行时架构

```text
main()
  -> ZzWindowKitBootstrap::prepare()
  -> ZzPureApplication
  -> GuiApplicationBootstrap
       -> ZzApplicationBuilder
       -> 单一 persistent 工作区根路由
       -> ZzApplicationWindow
            -> ZzFluentTitleBar
            -> ZzWindowAgent
            -> ZzPageHost
                 -> ZzWorkspaceShell
                      -> ZzSplitWorkspace
                           -> ZzTabWidget
                                -> ImageDownloadPage
                                -> DevicePage
                                -> FullUpgradePage
                                -> PartitionPage
```

`ZzApplicationBuilder` 需要有效的初始页面路由。本应用注册一个名为
`workspace` 的 `Persistent` 根路由，它代表整个工具工作区，而不是伪造一个不可见
业务页面。`ZzApplicationWindow` 自带的 `ZzPageHost` 激活该路由并托管
`ZzWorkspaceShell::workspaceWidget()`。

不注册 `ZzNavigationNode`，窗口装配时隐藏空的 `ZzNavigationPane`。因此用户看不到
导航页，`ZzPageHost` 仍按框架契约管理唯一根页面的创建、取消和销毁。

## 应用启动与关闭

`main()` 必须在构造任何 `QCoreApplication` 或 `QApplication` 前调用
`ZzWindowKit::ZzWindowKitBootstrap::prepare()`。失败时输出包含 stage、
technical message 和 context 的启动错误，并返回失败退出码。

准备成功后构造 `ZzPureTools::ZzPureApplication`，设置组织名、应用名、版本和图标，
再初始化现有日志。应用不得额外调用 `QApplication::setStyle()`；
`ZzPureApplication` 独占 `ZzThemeController` 和 `ZzFluentStyle` 生命周期。

`GuiApplicationBootstrap` 配置 `ZzApplicationBuilder`，注册根页面 factory 和窗口装配
回调，然后执行 `build()`。应用退出由 `ZzPureApplication::beginShutdown()` 统一停止
页面任务、销毁窗口和卸载框架资源。现有升级、下载和分区取消令牌在页面销毁前触发，
后台 watcher 完成后才释放业务对象。

## 窗口与标题栏

`ZzApplicationWindow` 是唯一顶层 `QMainWindow`。框架负责：

- 创建并安装 `ZzFluentTitleBar`；
- 通过 `ZzWindowAgent` 接入平台无边框窗口；
- 配置标题栏拖动区、窗口图标和系统按钮命中；
- 处理最小化、最大化、还原和关闭意图；
- 同步浅色、深色和系统主题；
- 通过 `ZzWorkspaceShell` 处理置顶状态和标题同步。

应用装配层只设置产品名称、图标、最小尺寸和初始尺寸，并放置全局设备状态工具栏与
底部状态栏。标题栏不复制窗口控制逻辑，也不直接访问业务 backend。

无边框能力是启动必需项。WindowKit 初始化、窗口绑定或 chrome 配置失败时，
Builder 返回失败，应用记录具体阶段并退出，不能静默创建普通有边框窗口。

## 工作区与顶部标签

根页面 factory 在 GUI 线程中取得所属 `ZzApplicationWindow`，创建
`ZzWorkspaceShell`，将 `workspaceWidget()` 挂到传入的 `pageParent`，再通过
`ZzPageInstance` 提交 View、ViewModel 和 Presenter 的所有权。

工作区不调用 `integrateApplicationNavigation()`，也不注册任何 Side Panel、
Bottom Panel 或 Dock Panel。空的 Activity Bar 和 Side Pane 保持隐藏，中央区域只显示
框架的 `ZzTabWidget`。

四个标签是固定工具页面：

- 禁止关闭；
- 禁止新建；
- 禁止拖出；
- 禁止跨容器转移；
- 禁止用户重排；
- 不使用 pinned、modified 和 attention 状态。

实现时对 `ZzTabWidget::fluentTabBar()` 显式调用对应的关闭、移动、tear-off、transfer
和新建按钮配置，不能仅依赖框架默认值。当前页变化仍由 Qt GUI 线程处理，不参与 USB
后台线程或烧录 workflow。

## 源码职责

框架接入后按以下边界组织 GUI：

- `apps/gui/GuiApplicationBootstrap.hpp/.cpp`
  负责 WindowKit 准备后的应用元数据、Builder 配置、启动错误转换和 smoke 模式。
- `apps/gui/GuiWindowShell.hpp/.cpp`
  作为根 `ZzPageInstance` 的 Presenter，拥有 `ZzWorkspaceShell`，配置标题栏、全局设备
  工具栏、状态栏和四个固定标签，并协调确定性关闭。
- `apps/gui/MainWorkspaceViewModel.hpp/.cpp`
  保存跨页面共享的设备选择、忙碌状态、准备结果、进度和报告状态，不访问 QWidget。
- `apps/gui/MainWorkspaceController.hpp/.cpp`
  协调四个页面与 `IGuiOperationBackend`，承接旧 `MainWindow` 中的异步操作、取消和
  跨页面同步逻辑，不再承担顶层窗口职责。
- `apps/gui/pages/ImageDownloadPage.hpp/.cpp`
  只负责下载镜像页面的控件、输入和展示状态。
- `apps/gui/pages/DevicePage.hpp/.cpp`
  只负责设备列表和设备详情展示。
- `apps/gui/pages/FullUpgradePage.hpp/.cpp`
  只负责完整升级、双进度和历史报告展示。
- `apps/gui/pages/PartitionPage.hpp/.cpp`
  只负责 GPT 分区列表及备份、写入、擦除操作界面。

页面类不直接创建 transport 或 workflow，不阻塞等待后台任务。控制器通过页面公开信号
接收用户意图，通过明确的更新方法写入展示状态。`IGuiOperationBackend` 仍是 GUI 与
现有业务层之间的唯一操作边界。

## 所有权与生命周期

`ZzPureApplication` 独占 `ZzApplicationWindow`。窗口的 `ZzPageHost` 独占根
`ZzPageInstance`，根页面实例按框架约定管理：

- View：`ZzWorkspaceShell::workspaceWidget()`，由 `pageParent` 的 Qt 对象树持有；
- ViewModel：无 QObject parent 的 `MainWorkspaceViewModel`；
- Presenter：无 QObject parent 的 `GuiWindowShell`；
- `GuiWindowShell` 独占 `ZzWorkspaceShell` 和 `MainWorkspaceController`；
- 四个业务页面由工作区 `ZzTabWidget` 按 Qt 父子关系持有。

根页面为 `Persistent`，正常运行期间不会因标签切换重建。页面实例的 cancellation
callback 首先通知控制器停止设备监控并设置全部取消令牌，然后等待现有 watcher 按当前
策略收尾；View 销毁后 Presenter 和 ViewModel 才释放。业务对象不得保存框架私有对象
或已销毁页面的裸指针，长期观察值使用 `QPointer`。

## 数据流与线程

USB 热插拔回调继续从 libusb 事件线程进入，通过 queued invocation 回到 GUI 线程。
设备扫描、镜像预检、升级、独立镜像下载和分区操作继续使用现有异步 backend。

`MainWorkspaceViewModel` 保存共享状态，`MainWorkspaceController` 负责以下协调：

- 设备页选择同步到下载镜像页；
- USB 操作互斥状态同步到四个页面；
- 当前分区进度和完整计划累计进度同时更新；
- 热插拔期间保留现有 debounce 和忙碌延迟刷新语义；
- 页面销毁期间忽略迟到回调并完成 watcher 清理。

所有 QWidget、主题控制器、WindowKit agent、标签切换和展示更新只在 GUI 线程执行。

## CMake 与打包

顶层工程通过独立的 `cmake/ZzPureTools.cmake` 接入完整子工程。该文件在独立函数作用域
设置框架构建选项，避免 `BUILD_SHARED_LIBS`、`ZZ_BUILD_TESTS`、
`ZZ_BUILD_EXAMPLES`、`ZZ_BUILD_BENCHMARKS`、`ZZ_RELEASE_BUILD` 和 ZzLog 选项污染
ZzRkDevTool 的父作用域。框架以静态库方式随 GUI 构建，减少新增运行时动态库；
QWindowKit 仍按 ZzWindowKit 的固定静态后端配置构建。

只有 GUI 目标链接完整框架。CLI、transport、workflow、storage 和非 GUI core 不链接
ZzPureTools、ZzFluentUI 或 Qt Widgets。现有 spdlog 与框架内命名隔离的 ZzLog backend
并存，不能用其中一个替换另一个而扩大本次迁移范围。

安装与打包需要：

- 收集 WindowKit 所需的 Qt 平台插件和平台运行库；
- 保留现有 libusb 打包规则；
- 加入 ZzPureTools、ZzLog、QWindowKit 及其传递依赖的许可证通知；
- 在版本记录中保存 ZzPureTools 固定 commit；
- Windows、macOS 和 Linux 包均验证无边框窗口可启动，而不只验证链接成功。

## 错误处理

启动错误统一包含阶段名称和框架 `ZzError` 的技术信息。至少区分：

- WindowKit 进程准备失败；
- Qt 私有模块或 QWindowKit 构建依赖缺失；
- Builder 注册失败；
- 根页面或 WorkspaceShell 创建失败；
- ApplicationWindow、WindowAgent 或 chrome 配置失败；
- 应用 build 失败。

运行中的设备和烧录错误继续显示在对应页面及底部状态栏。框架错误不转换成成功状态，
业务错误也不通过销毁窗口来恢复。

## 迁移顺序

1. 接入固定 commit 的完整 ZzPureTools submodule，并验证纯 GUI 配置和链接。
2. 增加 WindowKit bootstrap、`ZzPureApplication` 和 Builder 启动路径。
3. 建立单一工作区根路由和 `ZzApplicationWindow`，验证无边框标题栏。
4. 创建 `GuiWindowShell` 和 `ZzWorkspaceShell`，隐藏导航和空侧栏。
5. 将旧 `MainWindow` 的顶层窗口职责移除，迁移共享状态与控制器。
6. 将四个页面迁入框架 `ZzTabWidget`，保持行为和对象名称兼容。
7. 迁移自动化测试、smoke 测试、安装规则和许可证清单。
8. 完成 Linux 实机视觉检查以及 Windows、macOS 构建和窗口行为验证。

每一步保持可编译、可测试并单独提交。旧 `QApplication`、旧 `MainWindow` 顶层窗口和旧
`QTabWidget` 只在新路径通过测试后删除，不保留运行时双实现开关。

## 测试与验收

自动化测试至少覆盖：

1. WindowKit bootstrap 在应用创建前成功；应用创建后首次调用返回
   `ZzErrorCode::InvalidState`，应用创建前的重复调用保持成功且无额外状态。
2. 实际应用对象是 `ZzPureApplication`，主题控制器和 Fluent style 已安装。
3. Builder 创建一个 `ZzApplicationWindow`，其 `titleBar()` 和 `windowAgent()` 非空。
4. 默认导航面板不可见，工作区左右 Activity Bar 和 Side Pane 在无注册项时不可见。
5. `mainTabs` 实际类型为 `ZzFluentUI::ZzTabWidget`，四个标签标题、顺序和页面切换正确。
6. 四个标签均不可关闭、不可移动、不可拖出、不可跨容器转移，新建按钮不可见。
7. 浅色、深色和系统主题切换能刷新标题栏、标签、表格和状态栏。
8. 最小化、最大化、还原、关闭和置顶意图能到达真实顶层窗口。
9. 现有设备扫描、热插拔、下载镜像、完整升级、分区操作、取消和报告测试保持通过。
10. 应用关闭时设备监控停止、取消令牌置位、watcher 完成，ASan/UBSan 不报告释放问题。

Linux 使用 Xvfb 或真实桌面分别检查 980x820、最小窗口和高 DPI，保存浅色与深色截图，
确认标题栏、全局设备工具栏、四个标签、表格、双进度条和状态栏没有重叠。Windows
MSVC、Windows MinGW 和 macOS 除构建测试外，还要人工验证拖动、缩放、DPI、系统按钮、
最大化边界和关闭行为。

## 非目标

- 本次不显示或配置左侧导航页。
- 本次不启用标签新建、关闭、重排、拖出、分屏或跨窗口转移。
- 本次不增加 Side Panel、Bottom Panel、Dock Panel 或命令面板业务功能。
- 本次不改变固件解析、MD5、USB 协议、Loader、GPT 或分区刷写实现。
- 本次不升级 Qt、libusb 或现有业务依赖版本。
- 本次不为框架缺失能力维护应用侧私有 fork；发现框架公共 API 不足时，先记录明确的
  API 需求并由 ZzPureTools 仓库补充，再更新固定 submodule commit。
