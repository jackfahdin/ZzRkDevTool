# GUI 外观修改指南

界面使用项目已有的 ZzPureTools，顶部四个功能标签和业务控制器保持原来的职责划分。修改布局通常不需要修改 USB、镜像解析或烧录代码。

## 先找这些文件

| 修改内容 | 入口 |
| --- | --- |
| 页面边距、区间距、行高、按钮高度 | `apps/gui_qt/WorkspaceUi.hpp` 中的视觉常量 |
| 字号、卡片圆角、表格边线、按钮强调样式 | `apps/gui_qt/WorkspaceUi.cpp` |
| 全部业务图标 | `WorkspaceUi.cpp` 的 `icon()` |
| 顶部标签样式 | `GuiWindowShell.cpp` 的 `configureFixedTabs()` |
| 标题栏的应用与页面名称 | `GuiWindowShell.cpp` 的 `updateWindowTitle()` |
| 文件、目录选择与备份保存弹窗 | `FileSelectionDialog.cpp` |
| 下载页配置字段、清单、任务区的排列 | `pages/ImageDownloadPage.cpp` 构造函数 |
| 设备列表及可展开原始信息 | `pages/DevicePage.cpp` 构造函数 |
| 完整升级的固件、计划和任务分区 | `pages/FullUpgradePage.cpp` 构造函数 |
| 分区表与操作按钮位置 | `pages/PartitionPage.cpp` 构造函数 |

表中 `pages/` 路径均位于 `apps/gui_qt/` 下。

## 调整间距与密度

所有尺寸都是 Qt 逻辑像素，高 DPI 由 Qt 和框架处理，无须自行乘缩放比例。默认页面边距 20、卡片内边距 16、区间距 16、表格行高 38、按钮高度 34。

页面名称统一显示在标题栏，格式为“ZzRkDevTool-页面名字”，页面内不再显示重复大标题和副说明。小窗口会通过 `WorkspaceUi.cpp` 中的 `PageDensity` 自动缩小边距，优先保证工作区和任务操作可见。修改紧凑模式的宽高阈值也在这里。

例如将 `TableRowHeight` 改为 34，可增加表格信息密度。页面内的具体网格间距和任务卡片内边距位于各页构造函数；中文注释标明了配置区、工作区和任务区的分界。

## 修改图标

业务页通过 `ui::Icon` 指定操作含义，共用映射调用 `ZzSegoeIconFont::icon()`。框架内嵌 Segoe Fluent 字体，无须 Windows 安装字体，也无须额外图标库。默认图标跟随框架前景色，适配浅色、深色及缩放。

当前框架命名枚举尚未覆盖 Refresh、Download、Delete、Save、Devices。它们使用字体的标准码点，集中记录在 `icon()` 中。框架增加命名枚举后只需修改这里。不要在页面里混入 Qt 默认彩色图标。

## 修改顶部标签

`configureFixedTabs()` 中修改这一行即可：

```cpp
bar->setAppearance(ZzFluentUI::ZzTabBarAppearance::PivotSlide);
```

例如换成 `SegmentedSlide`。四个标签采用纯文字，避免与标题栏混用图标样式。主题、置顶、最小化、最大化和关闭继续交给 ZzPureTools 管理。

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
