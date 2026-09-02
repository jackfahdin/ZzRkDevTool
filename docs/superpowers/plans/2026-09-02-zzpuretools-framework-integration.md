# ZzPureTools 完整框架接入实现计划

> **面向 AI 代理的工作者：** 必需子技能：使用 superpowers:subagent-driven-development（推荐）或 superpowers:executing-plans 逐任务实现此计划。步骤使用复选框（`- [ ]`）语法来跟踪进度。

**目标：** 将 ZzRkDevTool GUI 迁移到完整的 ZzPureTools 应用、无边框窗口和 Fluent 顶部标签框架，同时保持现有设备、下载、升级和分区业务行为。

**架构：** `ZzWindowKitBootstrap` 在应用创建前完成窗口环境准备，`ZzPureApplication` 和 `ZzApplicationBuilder` 创建唯一的 `ZzApplicationWindow`。窗口 setup callback 按 ZzPureTools 标准流程创建 `ZzWorkspaceShell`、调用 `integrateApplicationNavigation()` 并挂载工作区；内置 `ZzPageHost` 作为第一个固定中央标签，另外三个业务页面直接加入同一个 Fluent `ZzTabWidget`。业务操作由 `MainWorkspaceController` 通过 `IGuiOperationBackend` 协调，页面只负责展示和用户意图。

**技术栈：** Qt 6.11 Widgets/Concurrent/Svg、C++20、CMake 3.25、ZzPureTools 固定 SSH submodule、ZzWindowKit、ZzFluentUI、libusb 1.0.30+、CTest、Xvfb。

---

## 文件结构

将创建或修改以下文件，职责在此固定：

- 创建：`cmake/ZzPureTools.cmake`，在隔离作用域接入完整 ZzPureTools 子工程及 Qt 私有模块。
- 修改：`.gitmodules`、`CMakeLists.txt`、`apps/gui/CMakeLists.txt`，登记递归 SSH submodule、GUI 框架目标和测试依赖。
- 创建：`apps/gui/GuiApplicationBootstrap.hpp/.cpp`，负责 `ZzPureApplication`、Builder、根路由、应用元数据和启动错误。
- 创建：`apps/gui/GuiWindowShell.hpp/.cpp`，作为窗口级 Shell 装配对象，拥有 WorkspaceShell 并观察初始页面 Controller；不作为 `ZzPageInstance` 的 Presenter。
- 创建：`apps/gui/MainWorkspaceViewModel.hpp/.cpp`，保存跨页面共享快照和取消状态，不引用 QWidget。
- 创建：`apps/gui/MainWorkspaceController.hpp/.cpp`，承接旧 `MainWindow` 的后台协调、全局工具栏、状态栏和页面装配。
- 创建：`apps/gui/pages/ImageDownloadPage.hpp/.cpp`，下载镜像控件、表格和进度展示。
- 创建：`apps/gui/pages/DevicePage.hpp/.cpp`，设备列表、详情和设备选择展示。
- 创建：`apps/gui/pages/FullUpgradePage.hpp/.cpp`，完整升级选项、双进度和历史报告展示。
- 创建：`apps/gui/pages/PartitionPage.hpp/.cpp`，GPT 表格、备份、写入和擦除操作展示。
- 删除：`apps/gui/MainWindow.hpp/.cpp`，旧顶层窗口和业务/界面混合实现在迁移完成后移除。
- 修改：`apps/gui/main.cpp`，在窗口装配完成后改为 WindowKit 准备、PureApplication 构建和事件循环。
- 创建：`tests/GuiTestHarness.hpp/.cpp`，为 GUI 测试创建真实 PureApplication、Builder、唯一窗口和可注入 backend。
- 创建：`tests/gui_framework_test.cpp`，覆盖框架对象、标题栏、导航隐藏、标签能力和主题。
- 修改：`tests/gui_upgrade_test.cpp`、`tests/gui_image_download_test.cpp`、`tests/CMakeLists.txt`，迁移到统一 harness 并保留原业务断言。
- 修改：`cmake/InstallAndPackage.cmake`、`packaging/linux/THIRD_PARTY_NOTICES.md`、`NOTICE.md`，加入框架许可证、固定 commit 和静态依赖说明。
- 创建：`tests/zzpuretools_cmake_scope.cmake`，验证框架选项不会污染父工程。

### 任务 1：接入递归 SSH submodule 和完整框架 CMake

**文件：**
- 修改：`.gitmodules`
- 创建：`cmake/ZzPureTools.cmake`
- 修改：`CMakeLists.txt:159-176`
- 修改：`apps/gui/CMakeLists.txt:1-34`
- 创建：`tests/zzpuretools_cmake_scope.cmake`

- [ ] **步骤 1：登记固定递归依赖**

在 `.gitmodules` 增加：

```ini
[submodule "thirdParty/ZzPureTools"]
    path = thirdParty/ZzPureTools
    url = git@github.com:jackfahdin/ZzPureTools.git
    branch = master
```

执行 `git submodule update --init --recursive thirdParty/ZzPureTools`，确认
`thirdParty/ZzPureTools` 的 HEAD 为 `d6b90f1d266b6aa97b0ffb3a75deaa4bc2314514`，并确认嵌套 `ZzThirdParty/qwindowkit` 存在。

- [ ] **步骤 2：编写隔离的框架接入函数**

在 `cmake/ZzPureTools.cmake` 提供 `zzrk_add_puretools()`，在函数内保存并设置：
`BUILD_SHARED_LIBS=OFF`、`ZZ_BUILD_TESTS=OFF`、`ZZ_BUILD_EXAMPLES=OFF`、
`ZZ_BUILD_BENCHMARKS=OFF`、`ZZ_BUILD_FLUENT_QUICK=OFF`、`ZZ_RELEASE_BUILD=OFF`、
`ZZLOG_BUILD_TESTS=OFF`、`ZZLOG_BUILD_EXAMPLES=OFF`、`ZZLOG_INSTALL=OFF`。
函数同时执行 `find_package(Qt6 6.11 EXACT REQUIRED COMPONENTS Core Gui Widgets Svg Concurrent)`、
`find_package(Qt6CorePrivate 6.11 EXACT REQUIRED)`、`find_package(Qt6GuiPrivate 6.11 EXACT REQUIRED)`
和 `find_package(Qt6WidgetsPrivate 6.11 EXACT REQUIRED)`，再以独立二进制目录执行：

```cmake
add_subdirectory(
    "${PROJECT_SOURCE_DIR}/thirdParty/ZzPureTools"
    "${PROJECT_BINARY_DIR}/thirdParty/ZzPureTools"
    EXCLUDE_FROM_ALL)
```

函数退出前恢复每个普通变量；对原先未定义的变量使用 `unset()`，不改变调用方的缓存值。

- [ ] **步骤 3：在 GUI 配置路径调用框架并链接目标**

在顶层 `CMakeLists.txt` 的 GUI 分支包含该模块并调用 `zzrk_add_puretools()`；在 `apps/gui/CMakeLists.txt` 将 `Zz::PureTools`、`Zz::WindowKit` 和 `Zz::FluentUI` 加入 `zzrk_gui_support` 的私有链接，同时保留现有 Qt、transport 和 workflow 公开链接。CLI 和非 GUI 目标不添加任何框架链接。

- [ ] **步骤 4：添加 CMake 作用域回归检查**

在顶层 `CMakeLists.txt` 调用 `zzrk_add_puretools()` 后立即 `include("${PROJECT_SOURCE_DIR}/tests/zzpuretools_cmake_scope.cmake")`；脚本用 `if(BUILD_SHARED_LIBS)`、`if(ZZ_BUILD_TESTS)`、`if(ZZ_BUILD_EXAMPLES)` 检查父工程值仍为调用前快照，并用 `if(NOT TARGET Zz::PureTools OR NOT TARGET Zz::WindowKit OR NOT TARGET Zz::FluentUI)` 检查完整目标。脚本只承担配置期断言，不通过独立 `cmake -P` 重新加载不存在的函数作用域。

- [ ] **步骤 5：验证配置、构建和目标隔离**

运行：

```bash
cmake -S . -B build-zzpuretools -G Ninja -DZZRK_BUILD_GUI=ON -DZZRK_BUILD_CLI=ON -DZZRK_BUILD_TESTS=ON
cmake --build build-zzpuretools --target zzrk_gui_support zzrkdevtool-cli
```

预期：Qt 私有模块缺失时配置在明确依赖信息处失败；依赖齐全时配置期作用域断言、GUI 支持库和 CLI 均构建成功。

- [ ] **步骤 6：Commit**

```bash
git add .gitmodules cmake/ZzPureTools.cmake CMakeLists.txt apps/gui/CMakeLists.txt tests/zzpuretools_cmake_scope.cmake
git commit -m "接入ZzPureTools完整框架构建"
```

### 任务 2：建立 PureApplication 基础生命周期和统一 GUI 测试 Harness

**文件：**
- 创建：`apps/gui/GuiApplicationBootstrap.hpp/.cpp`
- 创建：`tests/GuiTestHarness.hpp/.cpp`
- 修改：`apps/gui/CMakeLists.txt`、`tests/CMakeLists.txt`

- [ ] **步骤 1：先写启动失败与对象契约测试**

在 `tests/gui_framework_test.cpp` 添加启动测试入口，调用 `ZzWindowKitBootstrap::prepare()`；创建 `ZzPureApplication` 后断言 `qApp` 可转换为 `ZzPureApplication`、`themeController()` 非空、`QApplication::style()` 非空，并断言应用退出前 `beginShutdown()` 可重复调用。窗口的 `titleBar()`、`windowAgent()` 和标签断言放到任务 6，避免在页面 factory 尚未接入时引入跨任务依赖。

- [ ] **步骤 2：实现应用启动装配接口**

让 `GuiApplicationBootstrap` 暴露应用元数据和错误转换接口；本任务不注册业务页面，也不调用 Builder：

```cpp
struct GuiApplicationOptions {
    std::shared_ptr<zzrk::gui::IGuiOperationBackend> backend;
    bool showModalDialogs = true;
    bool smokeTest = false;
};

void configureApplicationMetadata(
    ZzPureTools::ZzPureApplication &application);
void reportStartupFailure(
    const char *stage,
    const ZzCore::ZzError &error);
```

`GuiApplicationOptions` 保留 backend、对话框和 smoke 配置，供后续 Builder 装配复用；`reportStartupFailure()` 输出 stage、`technicalMessage()` 和 `context()`，不吞掉框架错误。

- [ ] **步骤 3：实现测试 Harness 的隔离生命周期**

`GuiTestHarness::create(backend)` 在 GUI 线程准备 bootstrap、创建 `ZzPureApplication`、调用 `configureApplicationMetadata()`，本任务返回应用观察值但不创建窗口；析构顺序为调用 `beginShutdown()`、销毁应用。测试使用 `QT_QPA_PLATFORM=offscreen`，不得启动真实设备。

- [ ] **步骤 4：运行基础生命周期测试**

运行：

```bash
cmake --build build-zzpuretools --target gui_framework_test
QT_QPA_PLATFORM=offscreen ctest --test-dir build-zzpuretools -R gui_framework --output-on-failure
```

预期：测试只覆盖 WindowKit 进程准备、PureApplication 对象、主题控制器、Fluent style 和幂等关闭；不依赖任何业务页面或窗口装配。

- [ ] **步骤 5：Commit**

```bash
git add apps/gui/GuiApplicationBootstrap.* tests/GuiTestHarness.* tests/gui_framework_test.cpp apps/gui/CMakeLists.txt tests/CMakeLists.txt
git commit -m "建立PureApplication启动与GUI测试框架"
```

### 任务 3：提取共享 ViewModel、业务 Controller 和窗口内容根

**文件：**
- 创建：`apps/gui/MainWorkspaceViewModel.hpp/.cpp`
- 创建：`apps/gui/MainWorkspaceController.hpp/.cpp`
- 临时修改：`apps/gui/MainWindow.hpp/.cpp`
- 修改：`apps/gui/CMakeLists.txt`

- [ ] **步骤 1：定义无 QWidget 的共享状态接口**

`MainWorkspaceViewModel` 继承 `QObject`，不保存 QWidget 指针；提供 `selectedPortPath`、`inspectedPort`、`partitionPort`、`downloadLayoutPort`、三个 cancellation token、当前 busy 状态、双进度值和历史报告快照的读写方法，并发出 `deviceSelectionChanged`、`busyChanged`、`progressChanged` 和 `historyChanged`。所有 setter 只在对象所属 GUI 线程调用。该对象由首个“下载镜像”页面的 `ZzPageInstance` 以无 parent unique_ptr 持有，Controller 和其他标签页只保存非拥有观察指针。

- [ ] **步骤 2：为 Controller 写可注入 backend 的构造与关闭测试**

在 `tests/gui_framework_test.cpp` 增加 fake backend，创建 `MainWorkspaceController`，断言它接受 backend、可以设置 `showModalDialogs=false`，并在 `shutdown()` 后 backend 的监控停止、三个取消原子值均为 true；用临时 `ZzFluentUI::ZzTabWidget` 和 `QStatusBar` 调用 `attachWorkspace()`，断言保存的 `mainTabs` 和公共状态控件非空。

- [ ] **步骤 3：迁移 MainWindow 的共享逻辑**

将 `MainWindow` 中的 `refreshDevices`、`handleDeviceChange`、`handleDeviceMonitoringError`、`processPendingDeviceRefresh`、`handleScanFinished`、`applyScanResult`、`selectedPortPath`、`downloadPortPath`、`usbOperationRunning`、析构清理和 watcher 连接迁入 `MainWorkspaceController`。Controller 保存 `QFutureWatcher` 和 backend，所有异步完成信号在 GUI 线程更新 ViewModel；迟到回调以 `m_shuttingDown` 直接忽略。

- [ ] **步骤 4：抽出工作区根构建 API**

定义：

```cpp
void MainWorkspaceController::attachWorkspace(
    ZzFluentUI::ZzTabWidget *tabs,
    QStatusBar *statusBar);

[[nodiscard]] ZzCore::ZzResult<std::unique_ptr<ZzPureTools::ZzPageInstance>>
MainWorkspaceController::createInitialDownloadPage(QWidget *pageParent);
```

`attachWorkspace()` 只创建全局设备状态工具栏、刷新按钮、公共状态栏连接，并保存框架提供的 `ZzTabWidget`；不创建顶层窗口。`createInitialDownloadPage()` 创建初始“下载镜像”页面及其 `ZzPageInstance`，要求 `pageParent` 为框架 `ZzPageHost`；页面 View 由 page host 接管，`MainWorkspaceViewModel` 和 `MainWorkspaceController` 分别作为该实例的 ViewModel/Presenter 由实例独占，其他页面通过 Controller 观察同一 ViewModel。保留对象名 `connectionStatusText`、`refreshButton`、`mainTabs` 和 `lastScanText`，使 smoke 与业务测试可以继续通过 `findChild()` 查找。设备、完整升级和分区页面在任务 6 由 Controller 直接加入同一个 Fluent 标签组，并由 Controller 保存它们的页面实例和取消回调。

- [ ] **步骤 5：让旧 MainWindow 变成短期测试适配器并运行原测试**

将 `MainWindow` 改为仅拥有 `MainWorkspaceController` 的 `QWidget` 适配器，提供原有信号、`loadFirmwareImage`、`loadDownloadParameter` 和 `loadDownloadDirectory` 转发；暂时使用 `QTabWidget` 作为 controller 的测试容器，确保现有 `gui_upgrade` 和 `gui_image_download` 先保持通过后再删除适配器。

- [ ] **步骤 6：验证共享逻辑迁移**

运行：

```bash
cmake --build build-zzpuretools --target gui_upgrade_test gui_image_download_test gui_framework_test
QT_QPA_PLATFORM=offscreen ctest --test-dir build-zzpuretools -R 'gui_(upgrade|image_download|framework)' --output-on-failure
```

预期：旧业务断言、热插拔忙碌协调和 Controller 关闭断言通过。

- [ ] **步骤 7：Commit**

```bash
git add apps/gui/MainWorkspaceViewModel.* apps/gui/MainWorkspaceController.* apps/gui/MainWindow.* apps/gui/CMakeLists.txt tests/gui_framework_test.cpp
git commit -m "抽取GUI工作区状态与业务控制器"
```

### 任务 4：迁移下载镜像和设备页面

**文件：**
- 创建：`apps/gui/pages/ImageDownloadPage.hpp/.cpp`
- 创建：`apps/gui/pages/DevicePage.hpp/.cpp`
- 修改：`apps/gui/MainWorkspaceController.*`
- 修改：`apps/gui/MainWindow.*`
- 修改：`tests/gui_image_download_test.cpp`、`tests/gui_upgrade_test.cpp`

- [ ] **步骤 1：迁移下载页面控件和状态**

把 `MainWindow` 中下载布局构建（`downloadDeviceCombo`、`downloadLayoutCombo`、`downloadLayoutPath`、`downloadImageTable`、两个下载进度条和操作按钮）移动到 `ImageDownloadPage::buildUi(QWidget *)`；初始页由框架 `ZzPageHost` 直接拥有，其他页面由 Controller 再加入中央 `ZzTabWidget`。页面只发出设备改变、布局改变、选择镜像、清除、开始和取消信号，保留全部对象名和四列/九列表格设置。

- [ ] **步骤 2：迁移下载 workflow 方法**

将 `handleDownloadDeviceChanged`、`handleDownloadLayoutChanged`、`chooseDownloadParameter`、`chooseDownloadDirectory`、`loadDownloadParameter`、`loadDownloadDirectory`、`matchDownloadDirectory`、`assignDownloadImage`、`rebuildDownloadTable`、`chooseSelectedDownloadImage`、`clearSelectedDownloadImage`、`imageDownloadPrepareRequest`、`startImageDownload`、`handleDownloadPrepareFinished`、`cancelImageDownload`、`handleDownloadProgress`、`handleImageDownloadFinished`、`updateDownloadControls` 放入 Controller；页面通过 `setPlan()`、`setProgress()`、`setStatus()`、`setBusy()` 更新展示。

- [ ] **步骤 3：迁移设备页面控件和详情 workflow**

把设备表格和“所选设备详情”组装到 `DevicePage`，保留 `deviceTable`、`inspectDeviceButton`、`deviceDetailStatus`、`chipInfoValue`、`capabilityValue`、`flashIdValue`、`capacityValue`、`flashInfoValue`、`usbInterfaceValue` 对象名。将 `handleDeviceSelectionChanged`、`clearDeviceInspection`、`inspectSelectedDevice`、`handleInspectionFinished`、`syncDownloadDeviceFromTable` 和 `syncDeviceTableFromDownload` 放入 Controller。

- [ ] **步骤 4：运行下载/设备回归测试**

将 `tests/gui_image_download_test.cpp` 的窗口创建改为 `GuiTestHarness`，仍通过 `findChild` 检查 `mainTabs`、下载表格、设备 combo、双进度和 USB busy；在 `tests/gui_upgrade_test.cpp` 保留设备检查和分区读取前置断言。运行两个测试，预期所有原有计数、文件匹配和进度单调性断言不变。

- [ ] **步骤 5：删除重复实现并 Commit**

删除 MainWindow 中已迁出的下载/设备字段和方法，只保留适配器转发直到任务 6 完成；提交：

```bash
git add apps/gui/pages apps/gui/MainWorkspaceController.* apps/gui/MainWindow.* tests/gui_image_download_test.cpp tests/gui_upgrade_test.cpp
git commit -m "迁移下载镜像与设备页面"
```

### 任务 5：迁移完整升级和分区页面

**文件：**
- 创建：`apps/gui/pages/FullUpgradePage.hpp/.cpp`
- 创建：`apps/gui/pages/PartitionPage.hpp/.cpp`
- 修改：`apps/gui/MainWorkspaceController.*`
- 修改：`apps/gui/MainWindow.*`
- 修改：`tests/gui_upgrade_test.cpp`

- [ ] **步骤 1：迁移完整升级页面**

将固件路径、计划预览、校验模式、重启选项、`overallUpgradeProgress`、`operationUpgradeProgress`、`upgradeStatus`、历史表和导出按钮移入 `FullUpgradePage`，保留对象名、进度格式和三种校验选项。页面发出选择固件、开始、取消和导出报告意图。

- [ ] **步骤 2：迁移升级 workflow 和报告**

将 `chooseFirmwareImage`、`loadFirmwareImage`、`handlePrepareFinished`、`startUpgrade`、`cancelUpgrade`、`handleUpgradeProgress`、`handleUpgradeFinished`、`beginHistoryEntry`、`finishHistoryEntry`、`updateReportExportControls`、`exportSelectedReport` 迁入 Controller；`UpgradeProgressTracker` 仍由 Controller 独占，ViewModel 同时保存累计计划进度和当前操作进度。

- [ ] **步骤 3：迁移分区页面**

将 GPT 表格、校验复选框、备份/写入/擦除/取消按钮和分区进度移入 `PartitionPage`，保留 `readPartitionsButton`、`partitionTable`、`verifyPartitionWriteCheckBox`、`backupPartitionButton`、`writePartitionButton`、`erasePartitionButton`、`cancelPartitionButton`、`partitionProgress`、`partitionStatus` 对象名。将 `clearPartitionTable`、`readSelectedDevicePartitions`、`handlePartitionTableFinished`、`backupSelectedPartition`、`writeSelectedPartition`、`eraseSelectedPartition`、`startPartitionOperation`、`handlePartitionOperationProgress`、`cancelPartitionOperation`、`handlePartitionOperationFinished` 迁入 Controller。

- [ ] **步骤 4：保留跨页面互斥和进度契约**

Controller 在任一升级、下载或分区 operation 开始时通过 ViewModel 广播 busy，四页按钮按原规则禁用；每个 progress event 同时调用 `FullUpgradePage::setOverallProgress()` 和 `setOperationProgress()`，下载页也保持“完整计划累计百分比”和“当前分区百分比”并存。

- [ ] **步骤 5：运行完整 GUI 业务测试**

运行：

```bash
cmake --build build-zzpuretools --target gui_upgrade_test gui_image_download_test
QT_QPA_PLATFORM=offscreen ctest --test-dir build-zzpuretools -R 'gui_(upgrade|image_download)' --output-on-failure
```

预期：固件准备、升级、设备检查、GPT 读取、分区 operation 计数、双进度独立性、历史报告和取消清理全部通过。

- [ ] **步骤 6：Commit**

```bash
git add apps/gui/pages/FullUpgradePage.* apps/gui/pages/PartitionPage.* apps/gui/MainWorkspaceController.* apps/gui/MainWindow.* tests/gui_upgrade_test.cpp
git commit -m "迁移完整升级与分区页面"
```

### 任务 6：装配无边框 ApplicationWindow、WorkspaceShell 和 Fluent 顶部标签

**文件：**
- 创建/修改：`apps/gui/GuiWindowShell.hpp/.cpp`
- 修改：`apps/gui/GuiApplicationBootstrap.cpp`
- 修改：`apps/gui/MainWorkspaceController.*`
- 修改：`apps/gui/MainWindow.*`
- 修改：`tests/gui_framework_test.cpp`、`tests/CMakeLists.txt`

- [ ] **步骤 1：编写失败的窗口装配断言**

在 `gui_framework_test` 断言：`window->isWindow()`、`window->titleBar()` 可转换为 `ZzFluentTitleBar`、`window->windowAgent()` 非空、`window->navigationPane()->isVisible()==false`；查找 `mainTabs` 并断言其类型为 `ZzFluentUI::ZzTabWidget`，标签文本依次为“下载镜像”“设备”“完整升级”“分区”。

- [ ] **步骤 2：实现标准 WorkspaceShell 装配和初始页面 factory**

`GuiWindowShell::attach()` 在 GUI 线程通过 `ZzWorkspaceShell::create(&window, window.titleBar())` 创建 Shell，调用 `integrateApplicationNavigation()` 将框架内置 `ZzPageHost` 迁移为中央工作区的第一个固定标签，并随后执行 `window.setCentralWidget(workspace->workspaceWidget())`（这是框架标准迁移顺序）。Shell 由窗口直接拥有的 `GuiWindowShell` 独占；初始路由 factory 再调用 `GuiWindowShell::createInitialDownloadPage(pageParent)`，创建 `MainWorkspaceViewModel` 和 `MainWorkspaceController`，并用 `ZzPageInstance::create()` 让页面实例独占二者及“下载镜像” View。`GuiWindowShell` 仅以 `QPointer` 观察该 Controller，Controller 保存中央 `ZzTabWidget`，加入“设备”“完整升级”“分区”三个页面实例；页面 View 必须是 `pageParent` 的直接子对象。任何一步失败都回滚 central widget、Shell 面板和已创建页面，并向 Builder 返回原始 `ZzError`。

- [ ] **步骤 3：隐藏导航表面并配置窗口尺寸**

在 `GuiApplicationBootstrap` 的 window setup callback 中设置窗口标题、图标、最小尺寸 `800x700`、初始尺寸 `980x820`；调用 `integrateApplicationNavigation()` 时使用稳定的内部导航面板 ID 和合法图标，集成完成后立即通过 `workspace->showPanel(panelId, false)`、`workspace->activityBar(edge)->hide()`、`workspace->sidePane(edge)->hide()` 和 `window.navigationPane()->hide()` 隐藏全部导航表面，不注册其他 Activity/Side/Bottom/Dock 面板。将 `window.statusBar()` 交给 Controller，显示“准备就绪”和最后扫描时间。Builder 注册一个有效的初始路由“下载镜像”和对应 NavigationNode；该节点只满足框架路由契约，不作为用户可见导航入口，顶部四标签承担实际页面切换。

- [ ] **步骤 4：固定四个标签的行为**

在初始页面 factory 成功、Controller 加入另外三个页面后，对包含固定 page-host 标签的中央标签组执行：

```cpp
tabs->setTabsClosable(false);
tabs->setMovable(false);
auto *bar = tabs->fluentTabBar();
bar->setTearOffEnabled(false);
bar->setTabTransferEnabled(false);
if (auto *button = bar->newTabButton(); button != nullptr) {
    button->hide();
}
```

同时断开或不连接 `newTabRequested`、`tearOffRequested`、`tabsCloseRequested`，不设置 pinned、modified、attention 状态。

- [ ] **步骤 5：清理 MainWindow 双窗口路径**

删除 `MainWindow` 的顶层构造、`setCentralWidget`、窗口尺寸和 `QTabWidget` 创建；将 `MainWindow.cpp/.hpp` 从 `zzrk_gui_support` 源文件列表和测试 include 中移除，四个页面和 Controller 只由 `GuiWindowShell` 创建。业务测试全部改从 harness 的 `ZzApplicationWindow` 查找对象，确保运行时不存在旧窗口实现。

- [ ] **步骤 6：运行框架和业务联合测试**

运行：

```bash
cmake --build build-zzpuretools --target ZzRkDevTool gui_framework_test gui_upgrade_test gui_image_download_test
QT_QPA_PLATFORM=offscreen ctest --test-dir build-zzpuretools -R 'gui_(framework|upgrade|image_download|smoke)' --output-on-failure
```

预期：窗口为唯一顶层窗口，标题栏和 WindowKit agent 非空，导航和空侧栏隐藏，四标签固定，原业务测试通过。

- [ ] **步骤 7：Commit**

```bash
git add apps/gui/GuiWindowShell.* apps/gui/GuiApplicationBootstrap.* apps/gui/MainWorkspaceController.* apps/gui/MainWindow.* apps/gui/pages tests/gui_framework_test.cpp tests/CMakeLists.txt
git commit -m "装配无边框窗口与Fluent顶部标签"
```

### 任务 7：迁移测试、关闭生命周期和主题契约

**文件：**
- 修改：`tests/GuiTestHarness.*`
- 修改：`tests/gui_framework_test.cpp`
- 修改：`tests/gui_upgrade_test.cpp`
- 修改：`tests/gui_image_download_test.cpp`
- 修改：`apps/gui/GuiWindowShell.*`、`apps/gui/MainWorkspaceController.*`

- [ ] **步骤 1：增加主题和标题栏交互测试**

保存原应用 style 指针，断言 `ZzPureApplication::themeController()->mode()` 初始可读；调用 `setMode(ZzThemeMode::Dark)` 后等待 `snapshotChanged`，断言标题栏、`mainTabs` 和状态栏 palette 已刷新；恢复原模式后再继续其他断言。点击标题栏主题、最小化、最大化/还原和关闭意图时，断言窗口状态或 `aboutToQuit` 变化符合 API。

- [ ] **步骤 2：增加固定标签能力测试**

逐个断言 `tabs->tabBar()->isMovable()==false`、`tabs->tabsClosable()==false`、`bar->isTearOffEnabled()==false`、`bar->isTabTransferEnabled()==false` 和 `newTabButton()->isVisible()==false`；调用 `setTabPinned`, `setTabModified`, `setTabAttention` 不作为业务状态来源。

- [ ] **步骤 3：验证确定性关闭**

关闭窗口后等待 `windowCount()==0`，断言 fake backend 的 `stopDeviceMonitoring()` 已调用、所有 cancellation token 已置位、所有 `QFutureWatcher` `isRunning()==false`，并在 ASan 构建中运行相同测试防止 View/Presenter 双重释放。

- [ ] **步骤 4：迁移原有两个 GUI 测试到 Harness**

删除 `MainWindow window{backend,false}` 和 `QApplication` 局部对象，统一使用 `GuiTestHarness`；原有 `findChild`、进度、设备和报告断言保持不变，只把窗口观察值换成 `harness.window()`。

- [ ] **步骤 5：运行测试全集**

运行：

```bash
cmake --build build-zzpuretools --target all
QT_QPA_PLATFORM=offscreen ctest --test-dir build-zzpuretools --output-on-failure
```

预期：框架、GUI、workflow、transport、storage 和 CLI 测试全部通过；GUI 测试不需要硬件。

- [ ] **步骤 6：Commit**

```bash
git add tests apps/gui/GuiWindowShell.* apps/gui/MainWorkspaceController.*
git commit -m "补齐框架主题与关闭生命周期测试"
```

### 任务 8：安装、许可证和跨平台打包接入

**文件：**
- 修改：`cmake/InstallAndPackage.cmake`
- 修改：`packaging/linux/THIRD_PARTY_NOTICES.md`
- 修改：`NOTICE.md`
- 创建：`packaging/licenses/ZzPureTools-MIT.txt`、`packaging/licenses/QWindowKit-MIT.txt`
- 修改：`tests/linux_install_smoke.cmake`

- [ ] **步骤 1：登记框架许可证和版本来源**

将 ZzPureTools MIT LICENSE、`ZzThirdParty/ZzLog/LICENSE` 和 `ZzThirdParty/qwindowkit/LICENSE` 复制到安装文档目录，`NOTICE.md` 增加固定 commit、仓库 SSH 来源和静态链接说明；Linux `THIRD_PARTY_NOTICES.md` 增加三项名称、版本和许可证路径。

- [ ] **步骤 2：扩展安装和 Qt 部署规则**

在 `InstallAndPackage.cmake` 安装 GUI 时保留现有 Qt/libusb 规则，并把框架许可证安装到 `${ZZRK_INSTALL_DOCDIR}/licenses/ZzPureTools`、`ZzLog`、`QWindowKit`；静态框架目标不写入虚假的动态库路径。Linux、Windows、macOS 的 deploy script 都以 `ZzRkDevTool` 为主目标，继续包含 `qoffscreen` 插件。

- [ ] **步骤 3：增加安装 smoke 断言**

在 `tests/linux_install_smoke.cmake` 检查安装目录存在 `ZzRkDevTool`、GUI Qt 插件、libusb 运行库和三个框架许可证文件；执行安装后的 `ZzRkDevTool --smoke-test`，设置 `QT_QPA_PLATFORM=offscreen`，断言退出码为 0。

- [ ] **步骤 4：验证 Linux 安装包**

运行：

```bash
cmake --install build-zzpuretools --prefix /tmp/zzrkdevtool-install
ctest --test-dir build-zzpuretools -R linux_install_smoke --output-on-failure
cpack --config build-zzpuretools/CPackConfig.cmake
```

预期：安装 smoke、DEB 生成和许可证文件检查通过。

- [ ] **步骤 5：Commit**

```bash
git add cmake/InstallAndPackage.cmake packaging/linux/THIRD_PARTY_NOTICES.md NOTICE.md packaging/licenses tests/linux_install_smoke.cmake
git commit -m "完善PureTools安装许可证与打包"
```

### 任务 9：多平台构建、视觉验收和最终清理

**文件：**
- 修改：`docs/BUILD_AND_PACKAGE.zh-CN.md`
- 修改：`tests/CMakeLists.txt`
- 删除确认：`apps/gui/MainWindow.hpp/.cpp`

- [ ] **步骤 1：Linux Debug/Release 和 Sanitizer 验证**

分别配置 `build-debug`、`build-release` 和 `build-asan`，运行 `cmake --build` 与 `ctest --output-on-failure`；ASan/UBSan 使用 `-DZZRK_ENABLE_SANITIZERS=ON`，重点执行 `gui_framework`、`gui_upgrade`、`gui_image_download`、`gui_smoke`。

- [ ] **步骤 2：执行 Xvfb 视觉验收**

在 `Xvfb :99 -screen 0 1920x1080x24` 下运行 smoke 截图路径，分别检查 980x820、最小窗口和深色主题。记录标题栏拖动区、系统按钮、顶部四标签、全局设备状态栏、表格、双进度和状态栏没有重叠，保存截图到构建产物而不是源代码。

- [ ] **步骤 3：Windows 和 macOS 无硬件构建检查**

Windows MSVC、Windows MinGW 和 macOS 使用固定 submodule 和 Qt 6.11 配置 `ZZRK_BUILD_GUI=ON`，执行构建及 `gui_framework`/`gui_smoke`；检查 Windows 无边框系统按钮、DPI 和最大化边界，macOS 检查 bundle 内 Qt 平台插件和 `@rpath` 解析。

- [ ] **步骤 4：更新中文构建文档**

在 `docs/BUILD_AND_PACKAGE.zh-CN.md` 增加：递归 submodule 初始化、Qt 私有模块要求、PureApplication 启动路径、Linux/Windows/macOS 构建命令、安装许可证目录、offscreen smoke 命令和硬件测试前的限制。

- [ ] **步骤 5：删除旧实现并做仓库级检查**

确认 `rg -n "class MainWindow|QApplication application|new QTabWidget|QApplication::setStyle" apps/gui tests` 不再命中运行时实现（测试说明文本除外）；确认 `git diff --check`、`git status --short` 和 `ctest --test-dir build-release --output-on-failure` 结果干净。

- [ ] **步骤 6：最终 Commit**

```bash
git add apps/gui docs/BUILD_AND_PACKAGE.zh-CN.md tests/CMakeLists.txt
git commit -m "完成ZzPureTools跨平台GUI迁移验收"
```

## 规格覆盖自检

- 完整 `ZzWindowKit`、`ZzPureApplication`、`ZzApplicationBuilder`、`ZzApplicationWindow`、`ZzFluentTitleBar` 和 `ZzWorkspaceShell`：任务 1、2、6、7。
- 唯一无边框顶层窗口与隐藏导航：任务 6、7、9。
- 四个固定 `ZzTabWidget` 顶部标签和不可关闭/移动/拖出/转移/新建：任务 6、7。
- 现有下载、设备、升级、分区、双进度、热插拔、取消和报告行为：任务 3、4、5、7。
- ViewModel/Controller/Page 分层、所有权和 GUI 线程：任务 3、4、5、6、7。
- Qt 私有模块、CMake 选项隔离、静态框架构建和 GUI-only 链接：任务 1、8。
- 启动错误、WindowKit/Builder/页面失败和确定性关闭：任务 2、6、7。
- 许可证、安装、Linux/Windows/macOS 构建与视觉验收：任务 8、9。

计划中没有 `TODO`、`TBD`、`待定` 或未定义的“适当处理”占位步骤；接口名称、对象名、目标名和测试命令在各任务之间保持一致。
