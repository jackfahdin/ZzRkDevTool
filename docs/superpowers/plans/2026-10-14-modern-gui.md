# GUI 现代工具界面实现计划

**目标：** 按已确认设计更新四个 GUI 页面的视觉表现，提供中文修改说明。

**架构：** 在 `WorkspaceUi` 集中管理视觉尺寸、Segoe 图标、卡片、标题、按钮和表格配置。页面保留原有信号、对象名称和后端接口，布局代码负责展示。

**技术栈：** Qt 6.11.1 Widgets、项目已有 ZzPureTools。

## 任务一：共用视觉与下载页

- [x] 新建 `apps/gui_qt/WorkspaceUi.hpp/.cpp`：中文注释的尺寸常量、语义图标映射、标题和卡片构建方法。
- [x] 在 `apps/gui_qt/CMakeLists.txt` 注册文件，使用框架 `ZzPushButton::setAppearance(ZzButtonAppearance::Accent)` 设置主操作。
- [x] 更新 `ImageDownloadPage`：配置/清单/任务分区、文件名及完整路径提示、空表引导、明确的双百分比。
- [x] `GuiWindowShell::configureFixedTabs()` 设置 `ZzTabBarAppearance::PivotSlide`；移除标签混用图标。

## 任务二：推广到其他页面

- [x] `DevicePage` 复用标题、卡片与表格规范，去掉每行默认勾选图标。
- [x] `FullUpgradePage` 分离固件配置、计划、任务与历史，保留所有原始计划信息和导出功能。
- [x] `PartitionPage` 复用统一表格、任务布局；写入强调、擦除单独放置。
- [x] `GuiApplicationBootstrap` 与 `MainWorkspaceController` 替换默认应用/刷新图标。
- [x] 新增中文维护文档，标明改间距、图标、标签风格和页布局的文件位置。

## 任务三：验证与交付

- [x] 构建：`cmake --build --preset local-linux-release --parallel 12`。
- [x] 回归：`ctest --test-dir build/local-linux-release --output-on-failure --parallel 6`，27/27 通过。
- [x] 视觉：`ZZRK_GUI_SCREENSHOT_DIR=/tmp/zzrk-ui-modern QT_QPA_PLATFORM=offscreen ./build/local-linux-release/tests/gui_framework_test`。
- [x] 逐张检查截图，修正表格主题缓存、通知高度及任务按钮裁切，并验证长路径与纯文本显示。
- [x] `git diff --check` 并复核业务接口、对象名称、信号及选择状态。

本次为可逆的展示调整，复用现有交互回归和截图检查，不为尺寸常量建立镜像式测试。用户已确认设计并要求实施，直接在当前会话完成，不再增加审批步骤。
