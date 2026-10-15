# 提示弹窗统一适配

**目标：** 按现有 Fluent 界面约定，修正文件删除确认及扫描发现的其他旧式提示弹窗。

**设计：** 新增 `GuiDialogs` 统一展示业务通知和确认，使用框架 `ZzContentDialog`。文件浏览器仍由 Qt 管理文件系统操作；对浏览器内部生成的 `QMessageBox` 设置透明，在事件循环中正常隐藏并展示 Fluent 对话框，将用户操作映射到原按钮。桥接只作用于当前文件浏览器的子对话框，随浏览器销毁，不使用 Qt 私有头文件，不修改子仓库。

应用初始化时设置 `AA_DontUseNativeDialogs`，先阻止 Qt 原生 helper 路径。独立审查确认 macOS 的原生路径发生在 Polish/Show 之前，单靠桥接显示事件不足以保证统一外观。

**行为约束：** 删除、覆盖和烧录的默认按钮为取消；Escape 和关闭均取消，确认才执行。普通错误及完成消息只需关闭。文案、文件名、详细错误保持可见，正文按纯文本显示。业务文案绑定即时翻译；Qt 内部文件提示沿用原消息及操作逻辑。

- [x] 在 `tests/gui_framework_test.cpp` 复现文件删除确认和业务下载确认仍是旧式窗口的失败；覆盖取消删除、确认删除、覆盖保存取消/确认。
- [x] 创建 `apps/gui_qt/GuiDialogs.hpp/.cpp`，提供通知和确认接口及文件消息桥接；接入 `FileSelectionDialog.cpp` 和构建清单。
- [x] 替换 `MainWorkspaceController.cpp` 中读取、分区、完整升级和下载的所有旧式提示；恢复默认设置复用统一接口。
- [x] 运行 Linux Release 构建与完整 CTest，检查弹窗截图，更新中文维护说明并提交。

**验证命令：** `cmake --build --preset local-linux-release`；`ctest --test-dir build/local-linux-release --output-on-failure`；`git diff --check`。

## 验证记录

2026-10-14：Linux Release 构建成功，完整 CTest 33/33 通过。测试使用真实 Qt 文件模型，只操作临时文件；覆盖 Escape 取消删除、确认删除、Enter 默认取消覆盖、确认保存路径及文件内容不变、业务确认的键盘/按钮行为、纯文本文件名及已打开通知的即时语言更新。移除文件消息桥接时删除和覆盖测试均失败，恢复后通过；中文 Qt 文件提示翻译测试也先失败再通过。

扫描后业务代码不再直接调用 `QMessageBox`，只在桥接模块读取 Qt 自己生成的实例。独立审查指出原生 helper 的显示时机问题，已在应用初始化封住原生路径，其余审查范围未发现确定问题。已检查 `/tmp/zzrk-fluent-dialogs/file-delete-confirmation.png` 的实际截图，子仓库未修改。Windows 和 macOS 的实际界面仍需对应环境检查。

## 2026-10-15：删除新建文件夹后无法点击的修复

复现路径：打开 parameter.txt 文件选择框，新建并重命名文件夹，右键删除并确认，随后文件选择框的按钮不再响应。

问题分为两处：

- `QFileDialog::setItemDelegate()` 会将同一个代理分配给列表、详细视图。编辑器提交、关闭信号会误发给另一个视图，产生 `editor that does not belong to this view` 警告。现在两个视图分别持有 `FileRows`，侧栏继续使用独立代理。
- `QMessageBoxPrivate::setVisible(false)` 会重置 `WA_DontShowOnScreen`。旧桥接将原提示框作为离屏模态窗口显示，隐藏时却走普通窗口路径，漏掉模态清理；调试确认文件选择窗口的 `blockedByModalWindow` 残留为 true。现在使用透明度隐藏原提示框的外观，保持正常的窗口显示、隐藏配对，再展示 Fluent 确认框。

新增 `gui_file_operations` 回归测试：使用真实文件模型和临时目录，新建、重命名文件夹，从右键菜单删除并用窗口鼠标事件确认，随后用鼠标取消文件选择、切换主页面，检查编辑器警告和残留模态窗口。直接调用按钮 `click()` 或对话框 `done()` 会绕过输入阻塞，旧测试因此未捕获本问题。测试须等待初始目录扫描完成，再操作新建目录，避免将扫描与重命名的测试时序竞争误判为生产问题。

验证：Linux Release 构建和完整 CTest 34/34 通过；新增测试分别使用 offscreen 和 Xvfb/xcb 输入后端通过，并覆盖取消、确认删除两个分支。独立审查未发现必须修复的问题。Windows、macOS 和真实 Wayland 桌面尚未验证；Wayland 不支持顶层窗口透明度、X11 透明度依赖合成器，需检查原窗口在排队隐藏前是否会短暂闪现，该外观限制与本次模态清理修复分别跟踪。
