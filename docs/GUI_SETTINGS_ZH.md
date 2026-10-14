# GUI 配置与设置使用说明

GUI 使用 ZzPureTools 的 `ZzQtSettingsStore` 保存 INI 配置，无需额外数据库。CLI 参数与运行方式不受 GUI 配置影响。

## 首次启动

没有找到配置时，先显示目录选择弹窗。确认时检查目录是否可写，成功保存后才进入主窗口；取消则退出，不创建配置。

| 位置 | 适用场景 |
| --- | --- |
| 用户配置目录 | 常规安装，默认推荐，由 Qt `QStandardPaths::AppConfigLocation` 定位 |
| 应用所在目录 | 便携使用，需要目录可写，软件目录移动后仍能定位配置 |
| 自定义目录 | 指定绝对路径，便于单独管理 |

弹窗与设置页显示实际路径。Windows 安装到 Program Files 时通常没有写权限，应选择用户或自定义目录。macOS 应用目录模式使用 `.app` 同级目录，不向签名包内写入。

## 设置页面

第五个顶部标签为“设置”，包含：

- 外观与语言：跟随系统、浅色、深色；简体中文、英文或跟随系统。主题和语言即时生效，未支持的系统语言使用中文。
- 路径与存储：查看、打开及修改配置目录，单独设置分区备份的默认保存目录。
- 配置管理：恢复默认偏好与镜像草稿，保留配置目录。设备忙碌时不能迁移或恢复默认。

迁移时先保存目标配置，再切换定位信息，保留旧文件。目标已有 `settings.ini` 时不会覆盖；首次启动选择已有目录则会读取有效配置。保存失败显示错误，继续使用原位置。

语言切换原地刷新标签、标题栏、按钮、表头、状态及文件弹窗文案，不重建页面，也不重新扫描设备、解析镜像或开始任务。正在进行的任务、文件路径、选择、勾选与进度保留；已有历史日志保留原文，新输出使用当前语言。设备操作期间也可以切换语言；恢复默认会立即应用系统语言。

## 保存与恢复

| 分组 | 内容 |
| --- | --- |
| `appearance` | 主题、语言 |
| `window` | 标签页、正常窗口位置与尺寸、最大化及置顶 |
| `upgrade` | 固件路径、回读校验模式、完成后重启 |
| `download` | 布局来源、parameter 路径、镜像目录、文件映射、勾选、回读校验及重启 |
| `partition` | 分区写入后回读校验选项 |
| `paths` | 默认备份目录 |
| `fileDialog` | 固件、parameter、Loader、分区镜像等操作上次使用的目录 |

用户修改自动保存，正常关闭前再次同步。窗口位置会调整到当前屏幕范围。设备身份、已读取 GPT、MD5 成功、烧录结果和进度不保存为可执行状态。

固件路径恢复后，点击升级会重新解析和校验 MD5。镜像下载恢复为草稿，重新检查本地文件；设备 GPT 来源需要重新读取当前设备，再恢复分区清单并检查映射。缺失或无效文件显示状态，不能沿用上次校验结果烧录。

## 配置文件

正式配置为所选目录下的 `settings.ini`。普通模式在固定用户配置目录下用 `startup.ini` 记录位置；应用目录模式通过同目录的 `zzrk-portable.ini` 标记定位，不依赖用户目录。

建议退出应用后再手动编辑。普通选项为分组字段，例如：

```ini
schemaVersion=1

[appearance]
language=zh_CN
theme=dark

[upgrade]
reboot=true
verify=critical
```

校验模式为 `critical`、`all` 或 `none`；主题为 `system`、`light` 或 `dark`；语言为 `system`、`zh_CN` 或 `en`。镜像映射使用 JSON，Qt 处理 INI 字符串转义，`meta/jsonMapKeys` 记录 JSON 字段。未知版本不会被旧程序覆盖。

目录暂时不可用、文件损坏或版本不支持时，启动允许重新定位；运行中保存失败不会静默重建目录或覆盖异常配置。原文件保留，可重新挂载目录或选择有效位置。

## 维护入口

| 文件 | 职责 |
| --- | --- |
| `apps/gui_qt/GuiSettings.hpp/.cpp` | 默认值、版本检查、存储、目录定位和迁移 |
| `apps/gui_qt/SettingsLocationDialog.hpp/.cpp` | 首次选择与目录迁移弹窗 |
| `apps/gui_qt/pages/SettingsPage.hpp/.cpp` | 设置页展示与用户操作 |
| `apps/gui_qt/GuiWindowShell.cpp` | 外观、窗口恢复、自动保存与关闭同步 |
| `apps/gui_qt/MainWorkspaceSettings.cpp` | 业务草稿保存与恢复 |
| `apps/gui_qt/GuiTranslations.hpp/.cpp` | 翻译资源安装、文案绑定与生命周期 |
| `apps/gui_qt/translations/zzrkdevtool_en.ts` | 英文文案，构建时生成并内嵌 QM |

新增可见文案使用 `guiText("中文文案")`，并在 TS 的 `ZzRkDevTool` 上下文补齐英文，保持 `%1` 等占位符一致。`GuiText` 保存源文本和参数副本；实时控件使用 `setGuiText(control, "text", guiText("文案"))`，或使用已绑定的 `ui::button`、`ui::sectionTitle`、`guiLabel` 等工厂。不要在送入实时控件前转为普通 `QString`；普通字符串适合保留历史输出。

更新同一属性始终使用 `setGuiText`，包括清空和写入普通路径，避免语言事件恢复旧文案。组合框选项、表头等复杂内容使用 `bindGuiTranslation`，刷新时通过 `QSignalBlocker` 避免触发业务操作。绑定随控件销毁，回调不应捕获已经结束的任务或局部变量引用。框架系统按钮使用自己的 `LanguageChange` 实现，项目翻译资源提供对应框架上下文。

新增配置字段同步更新默认值、恢复逻辑、版本迁移及测试。

测试注入临时存储；`--smoke-test` 不显示首次启动弹窗，也不读写真实用户配置。
