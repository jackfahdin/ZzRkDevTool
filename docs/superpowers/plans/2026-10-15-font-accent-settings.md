# 字体与高亮颜色实现计划

> **面向 AI 代理的工作者：** 使用 executing-plans 按顺序实施，逐项验证。

**目标：** 设置页提供字体、字号与高亮颜色，修改即时生效，自动保存，支持单项及整体恢复默认。

**架构：** `GuiAppearanceSettings` 负责将现有配置应用到 Qt 应用字体与框架主题控制器，保留启动时的系统字体作为默认值。设置页只提供控件和操作入口；窗口壳负责恢复时机和自动保存。业务文字使用框架排版令牌，避免固定像素字体阻止即时更新。

**技术栈：** Qt 6、ZzPureTools、`ZzThemeController`、`ZzColorPickerButton`，沿用 INI 存储。

## 文件职责

- 新增 `apps/gui_qt/GuiAppearanceSettings.hpp/.cpp`：字体、字号及颜色应用，校验及恢复默认。
- 修改 `pages/SettingsPage.hpp/.cpp`：字体下拉、8–24 pt 字号、框架颜色按钮、局部恢复按钮；设置区域可滚动。
- 修改 `GuiWindowShell.hpp/.cpp`：外观对象生命周期、启动及重置恢复、保存触发。
- 修改 `GuiSettings.cpp`：新增默认配置项，兼容现有 schemaVersion=1。
- 修改 `WorkspaceUi.cpp`、`FileSelectionDialog.cpp`：文字跟随应用字体及框架排版，不固定像素字号；字体增大时表格行高随之增长。
- 修改翻译资源和 `docs/GUI_SETTINGS_ZH.md`：中英文文案与配置说明。
- 新增 `tests/gui_appearance_test.cpp`：验证设置入口对真实应用、控件与持久化的影响。

## 实施步骤

- [x] 先添加失败测试。通过 `settingsFontFamilyCombo`、`settingsFontSizeSpin` 和 `settingsAccentButton` 修改，断言 `QApplication::font().pointSizeF() == 14.0`、框架强调色为 `#a43bb7`，已有页面与表格同步；在子进程重启验证恢复。
- [x] 执行 `cmake --build --preset local-linux-release --target gui_appearance_test` 和对应 CTest，确认缺少设置入口时失败。
- [x] 实现外观控制器和设置入口。配置字段为 `appearance/fontFamily`（空值跟随系统）、`appearance/fontPointSize`（0 跟随系统）、`appearance/accentColor`（空值使用框架默认）。不存在字体、非法字号和颜色恢复默认；强调色不允许透明。
- [x] 接入窗口恢复和自动保存。字体更改使用 `QApplication::setFont(font)`；颜色使用 `theme->setAccentColor(color)`。局部恢复按钮只修改对应外观字段，整体恢复复用现有流程。
- [x] 去除业务固定像素字体，使用框架 Body/BodyStrong/Caption 令牌；在字体和主题快照变化时更新已有控件，表格根据字体度量调整行高。文件选择框继承当前字体。
- [x] 补齐中英文与框架颜色弹层翻译，记录配置规则。运行字体/颜色、即时语言、文件操作等相关测试，再执行 Linux Release 全量构建与 CTest。
- [x] 检查设置页截图、独立审查并修正问题，按已授权的中文提交格式提交。

## 验收边界

使用框架自身颜色选择器，不自行实现选色弹窗。设备任务、进度及草稿不因外观变化而重建。保留独立图标字体。旧配置无需迁移；找不到原机器的字体时安全回到当前系统字体。

## 验证与适配记录

- 新增 `gui_appearance`，覆盖即时应用、异步观察磁盘验证自动保存、真实子进程启动恢复、局部/整体重置、非法配置与缺失字体回退、透明色归一化、大字号行高、忙碌时修改、草稿保留、实时英文及文件弹窗字体。
- Linux Release 全量构建及 35 项 CTest；截图检查 14 pt 和 24 pt 设置布局，24 pt 下通过滚动访问全部配置。
- 独立审查补充新控件的 `accessibleName` 与标签 `setBuddy()`。
- 截图发现框架标题栏固定 32 px 且不处理 `FontChange` 重排；项目让标题栏保留启动时系统字体，避免裁切，不修改框架子仓库。限制与后续移除适配条件记录在 `docs/GUI_SETTINGS_ZH.md`。
