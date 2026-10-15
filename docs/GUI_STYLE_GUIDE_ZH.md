# GUI 外观修改指南

界面使用项目已有的 ZzPureTools，顶部四个业务标签及设置标签和业务控制器保持明确的职责划分。修改布局通常不需要修改 USB、镜像解析或烧录代码。

## 先找这些文件

| 修改内容 | 入口 |
| --- | --- |
| 页面边距、区间距、行高、按钮高度 | `apps/gui_qt/WorkspaceUi.hpp` 中的视觉常量 |
| 字号、卡片圆角、表格边线、按钮强调样式 | `apps/gui_qt/WorkspaceUi.cpp` |
| 全部业务图标 | `WorkspaceUi.cpp` 的 `icon()` |
| 顶部标签样式 | `GuiWindowShell.cpp` 的 `configureFixedTabs()` |
| 标题栏的应用与页面名称 | `GuiWindowShell.cpp` 的 `updateWindowTitle()` |
| 文件、目录选择与备份保存弹窗 | `FileSelectionDialog.cpp` |
| 业务通知、确认及 Qt 文件提示桥接 | `GuiDialogs.cpp` |
| 下载页配置字段、清单、任务区的排列 | `pages/ImageDownloadPage.cpp` 构造函数 |
| 全局设备选择、连接状态、底部提示与刷新 | `GuiDeviceStatusBar.hpp/.cpp` |
| 所选设备详情及可展开原始信息 | `pages/DevicePage.cpp` 构造函数 |
| 完整升级的固件、计划和任务分区 | `pages/FullUpgradePage.cpp` 构造函数 |
| 分区表与操作按钮位置 | `pages/PartitionPage.cpp` 构造函数 |
| 设置页面、首次启动及配置持久化 | `pages/SettingsPage.cpp`，详见 [配置说明](GUI_SETTINGS_ZH.md) |

表中 `pages/` 路径均位于 `apps/gui_qt/` 下。

## 调整间距与密度

所有尺寸都是 Qt 逻辑像素，高 DPI 由 Qt 和框架处理，无须自行乘缩放比例。默认页面边距 20、卡片内边距 16、区间距 16、表格行高 38、按钮高度 34。

页面名称统一显示在标题栏，格式为“ZzRkDevTool-页面名字”，页面内不再显示重复大标题和副说明。小窗口会通过 `WorkspaceUi.cpp` 中的 `PageDensity` 自动缩小边距，优先保证工作区和任务操作可见。修改紧凑模式的宽高阈值也在这里。

例如将 `TableRowHeight` 改为 34，可增加表格信息密度。页面内的具体网格间距和任务卡片内边距位于各页构造函数；中文注释标明了配置区、工作区和任务区的分界。

## 底部设备状态栏

底部统一显示“设备 / 端口选择 / 连接状态 / 操作提示 / 刷新 / 更新时间”。设备、下载镜像、完整升级和分区页面共用同一个当前端口，设备页只保留详情与读取操作，下载页只配置镜像和布局。

- 初次发现一台设备时自动选择；发现多台设备时必须明确选择。刷新保留当前端口。
- 已选设备拔出后清空目标并提示断开，即使另一台仍在线或原设备重新连接，也需要手动选择，避免误烧其他板卡。
- 同端口的模式、USB 地址或协议身份变化会清除设备详情、GPT 和预检结果。本地 parameter、镜像映射与勾选作为草稿保留；读取新 GPT 后按新容量重新校验。
- 任务和扫描期间锁定选择与刷新，控制器同时拒绝程序化切换。所有操作从 `MainWorkspaceViewModel::selectedPortPath()` 获取目标，不从下拉框显示文字获取。
- 不支持烧录的设备仍可查看端口及 USB 元数据；烧录能力继续由现有设备策略限制。
- 使用现有 libusb 热插拔事件及 150 ms 去抖，不增加轮询。热插拔失败时显示提示，保留手动刷新。

`GuiDeviceStatusBar` 只展示扫描快照并发出选择请求，选择策略与缓存失效位于 `MainWorkspaceController`。完整组件加入 `QStatusBar` 永久区域，通过 `messageChanged` 将现有 `setGuiStatus()` 消息同步到独立提示标签，因此临时提示不会遮住设备选择。状态文字随语言即时更新，长文字显示省略号，完整信息可通过悬浮提示查看。

## 修改图标

业务页通过 `ui::Icon` 指定操作含义，共用映射调用 `ZzSegoeIconFont::icon()`。框架内嵌 Segoe Fluent 字体，无须 Windows 安装字体，也无须额外图标库。默认图标跟随框架前景色，适配浅色、深色及缩放。

当前框架命名枚举尚未覆盖 Refresh、Download、Delete、Save、Devices。它们使用字体的标准码点，集中记录在 `icon()` 中。框架增加命名枚举后只需修改这里。不要在页面里混入 Qt 默认彩色图标。

## 修改顶部标签

`configureFixedTabs()` 中修改这一行即可：

```cpp
bar->setAppearance(ZzFluentUI::ZzTabBarAppearance::PivotSlide);
```

例如换成 `SegmentedSlide`。标签采用纯文字，避免与标题栏混用图标样式。主题、置顶、最小化、最大化和关闭继续交给 ZzPureTools 管理。

标题栏使用框架 `Custom` 标题模式，在标签切换时读取标签名称并同步系统窗口和框架标题栏。修改分隔符或显示顺序只需修改 `updateWindowTitle()`，无需修改框架子仓库。

## 维护时注意

- 保留控件 `objectName`：自动化测试及部分界面装配通过它们查找控件。
- 保留原信号连接和 `setBusy()` 限制：烧录过程中不能允许修改设备、镜像或分区选择。
- 清单只显示文件名，但 `PlanRow::file` 始终保存完整路径，提示显示完整路径；不要用显示文字作为烧录路径。
- 百分比文字独立于细线进度条。修改进度范围后调用 `ui::refreshProgressText()`，未知总量显示“进行中”。
- 错误通知使用框架 `ZzInfoBar`，通过明确的 `setError()` 调用呈现失败；用户取消属于普通状态，不猜测消息中是否包含“失败”。
- `NoticeSizing` 随窗口宽度更新换行高度。长错误需要更多空间时，下载清单自动缩小，保证错误文字和操作按钮完整可见。
- 颜色从框架调色板获取，不在页面中维护第二套主题。

## 文件选择弹窗

业务入口统一调用 `FileSelectionDialog`，包括固件、独立镜像、parameter.txt、镜像目录及备份保存位置。窗口由框架 `ZzContentDialog` 承载，目录浏览仍使用 Qt 的非原生 `QFileDialog`，保持文件筛选、导航、路径补全和打开/保存校验行为。

按钮位于底部，表头与常用位置使用中文；图标通过 `WorkspaceUi.cpp` 统一映射为 Segoe 线性字形。字体、行高和按钮高度沿用公共视觉参数，不设置第二套主题颜色。`BrowserWidth` 为文件浏览提供比普通提示框更宽的范围，并处理框架主题刷新时的宽度约束。

Qt 工具按钮和输入字段的控件名只在 `configureBrowser()` 中适配。升级 Qt 后如内部布局变化，应检查工具图标及底部按钮排列；不要在各业务页面直接查找这些控件。目录模式仅返回目录，保存模式仅选择路径，实际备份仍由业务流程执行，并保持输出文件不覆盖规则。

文件浏览器的删除、覆盖保存、文件或目录不存在等提示由 Qt 内部生成。应用初始化时设置 `AA_DontUseNativeDialogs`，避免 macOS 在普通显示事件前打开系统提示。`dialogs::adaptFileMessages()` 为每个浏览器安装独立桥接，只处理它的 QObject 子链中的 `QMessageBox`：原窗口不显示到屏幕，改由框架 `ZzContentDialog` 展示，用户操作通过点击原按钮传回 Qt。不要只调用 `done(1)` 或自行删除文件，否则会丢失 Qt 的 `clickedButton` 和校验逻辑。内部提示的中文源翻译在 `translations/zzrkdevtool_zh_CN.ts`，不需要额外安装系统 Qt 翻译包。

## 通知和操作确认

业务提示统一调用 `dialogs::notify(parent, title, text)`；需要用户确认的操作调用 `dialogs::confirm(parent, title, text, action)`，例如写入、擦除和开始升级。它们复用框架的无边框对话框、主题和遮罩，源文案通过 `GuiText` 保留即时翻译信息。不要在页面中新增 `QMessageBox` 或重复拼装框架提示窗口。

危险操作默认选择取消，Enter、Escape 和关闭都不会执行操作；只有用户点击操作按钮才返回确认。正文和标题使用纯文本，文件名中的尖括号不被解释为 HTML。弹窗使用 `QPointer` 管理，父窗口关闭后不会继续访问已经销毁的窗口。

## 编译和视觉检查

```bash
cmake --build --preset local-linux-release --parallel 12
ctest --test-dir build/local-linux-release --output-on-failure --parallel 6
ZZRK_GUI_SCREENSHOT_DIR=/tmp/zzrk-ui-modern QT_QPA_PLATFORM=offscreen \
  ./build/local-linux-release/tests/gui_framework_test
```

最后一条命令使用假后端，输出四个页面的浅色、深色和最小窗口截图，不烧录真实板卡。应用可直接运行：

```bash
./build/local-linux-release/apps/gui_qt/ZzRkDevTool
```
