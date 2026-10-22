# Windows 内置驱动管理实现计划

> **面向 AI 代理的工作者：** 使用 subagent-driven-development 逐任务实施；用户已确认设计并授权完成，不在任务之间再次请求批准。

**目标：** Windows 包自带 libwdi，提供设备驱动查询、WinUSB 安装和已备份原驱动的恢复入口。

**架构：** libwdi DLL 仅由 Windows 驱动管理辅助程序加载。普通 GUI 查询设备并显示确认，由独立辅助程序执行提权操作，安装成功后重新检查绑定与 USB 访问。驱动恢复使用 Windows 驱动 API，不能只把包加入 Driver Store。

**技术栈：** Qt 6 Core/Widgets/Concurrent、Windows SetupAPI/NewDev/ConfigMgr、libwdi 1.5.1、CMake/Ninja/MSBuild。

## 全局约束

- 不 push；变更仅保存在本地隔离分支。代码添加中文注释。
- 不在本开发机修改任何设备驱动。Windows 真机测试由用户执行。
- GUI 不整体提权，不在启动或烧录失败时自动替换驱动。
- 仅支持已验证的 Rockchip 硬件 ID 与目标接口，禁止通用任意设备驱动安装。
- 无备份不替换现有第三方驱动；恢复不删除共享驱动包。
- 不禁用签名校验，保留 UAC 与证书提示，不声称自签名等同于生产可信签名。
- Linux/macOS 构建不引入 libwdi，也不显示 Windows 驱动管理入口。

### 任务 1：固定 libwdi 构建与打包

负责文件：`cmake/Libwdi.cmake`、`scripts/build-libwdi.ps1`、`thirdParty/libwdi/README.md`，以及必要的独立构建测试。不修改 GUI 或 native manager 代码。

- [x] 检查上游 v1.5.1（SHA `9b23b82a2dd1cbffc16d46c212f92c6bf8c0c602`）构建方式，避免依赖未验证的预编译 DLL。
- [x] 实现 Windows-only 的构建、资源嵌入和 install 规则；兼容本项目 Ninja 与 VS 生成器，不改动上游源码许可证。提供 `zzrk_libwdi` 构建目标和 `ZZRK_LIBWDI_INCLUDE_DIR`。
- [x] 产物 DLL 安装到应用根目录，附 LGPL/GPL 许可证与精确源码版本、构建说明。允许显式关闭功能，但 Windows 正常发布默认自带。
- [x] 用最小 CMake 项目验证 Linux 不下载或构建 Windows 依赖，检查 Windows 上游构建参数与 MSBuild 路径来源；记录无法在当前 Linux 执行的原生验证。
- [x] 报告构建目标名、DLL 文件名和 CMake 接入方式，供主控制器集成。

### 任务 2：驱动身份、操作策略、Windows 辅助程序

负责文件：`apps/driver_helper/`、`src/driver/`、`tests/driver_policy_test.cpp`。

- [x] 先写真实策略测试：非 Rockchip、复合父设备、硬件 ID 与实例 ID 不符、已有 WinUSB、任务忙、备份不存在、驱动改变、重复安装均须有明确决策；先运行确认失败。
- [x] 实现跨平台纯策略，公开 `DriverDevice`、`DriverAction`、`DriverDecision`，由 GUI 与 helper 共用。实例严格匹配，不接受任意命令或 DLL 路径。
- [x] Windows helper 提供 `list` JSON 输出及 `install <instance-id>` / `restore <instance-id>`；用系统 APIs 查询驱动，用受保护的 ProgramData 目录保存备份与记录；拒绝 reparse point、路径越界和不可信恢复记录。
- [x] 切换前重新查询设备并导出当前原厂包；通过固定应用目录的 libwdi DLL 准备及安装 WinUSB。恢复需显式选择原包中的匹配驱动，不依赖驱动排序，也不删除全局驱动包。
- [x] 返回独立的取消、失败、重启要求、安装成功退出码；记录操作结果。WinUSB 状态验证失败不得报告就绪。
- [x] 用 MinGW 交叉编译检查 Windows helper 与 Win32 API 调用；用 Linux Qt 运行策略和记录测试，不伪称 Windows 硬件测试通过。

### 任务 3：GUI、部署与验收说明

负责文件：`apps/gui_qt/driver/DriverManagementDialog.*`、`pages/DevicePage.*`、`MainWorkspaceController.*`、GUI CMake、`cmake/InstallAndPackage.cmake`、Windows 文档与翻译。

接入契约：同目录的 `zzrk-driver-helper.exe list` 普通权限输出 JSON，顶层 `libraryAvailable` 布尔与 `devices` 数组。每项字符串字段为 `instanceId`、`hardwareId`、`service`、`infName`、`description`、`provider`、`version`、`location`、`portPath`、`backupPath`、`lastMessage`，以及布尔 `backupAvailable`。helper `probe <instance-id>` 普通权限仅打开声明释放接口。`install <instance-id>` / `restore <instance-id>` 需要 runas：退出码 0=绑定已验证，10=需要重启，20=取消，22=错误，23=绑定待确认需插拔，24=另一驱动操作正在运行。辅助进程不会接受额外路径参数。错误22的详细信息可随后通过 list 的 lastMessage 取得。

GUI 使用公共纯 C++ 头文件 `<zzrk/driver/DriverPolicy.hpp>` 和 `zzrk::driver_policy`，`DriverDevice` 含上述基本字符串字段（metadata字段仅在GUI保留）；`evaluateDriverAction(device, DriverAction::{installWinUsb,restoreOriginal}, DriverContext{busy, libraryAvailable, originalBackupAvailable})` 返回 `allowed`、`backupRequired`、`reason`。只匹配当前状态栏端口的实例，身份不唯一或缺失时禁止操作，不默认选择列表第一块板卡。所有 helper 查询都异步执行且有合理超时；驱动变更不能超时强杀。helper stdout 单次查询不得无界积累。

- [x] 先测试操作忙时不能启动驱动变更、设备选择不唯一不能安装、恢复无记录禁用和非 Windows 入口隐藏。
- [x] “设备”页增加驱动管理入口，沿用框架控件和弹窗。显示服务、硬件与实例 ID、提供商、版本、原包及恢复记录；同硬件 ID 影响需要明确确认。
- [x] 通过 QProcess 普通权限运行 list；仅点击操作并确认时通过 ShellExecuteEx runas 执行 helper，异步等待，避免阻塞 UI。主窗口禁止退出或开始设备任务直到操作结束。
- [x] 操作完成重新枚举；使用当前设备身份执行只打开、声明和释放的 probe，不发送 RockUSB 命令，不在失败时自动重复安装。
- [x] 添加翻译，验证 Linux 全量配置、编译、测试及 Windows helper 交叉编译。补 Windows 真机安装→probe→烧录→恢复→原厂工具验收清单。
- [x] 完成独立代码审查，修复发现后提交本地分批变更，不 push。

## 实施与验证记录

开发实现已完成，并经独立审查修正以下问题：复合接口 REV/MI 匹配、未知绑定与无绑定区分、同硬件 ID 多板卡安装门禁、复合父节点过滤、原包内容核实、安装中断后的无绑定恢复、重启/启动状态判定，以及恢复后避免错误执行 WinUSB probe。

安装资源采用管理员组 owner 和继承保护 ACL；原始导出与 libwdi 资源均从创建时保护，准备前后检查整棵资源树。DLL 的 Release 配置使用静态 CRT，随包提供完整固定源码及适配配方。

本地验证：Qt 6.12.0 Linux Release、warnings-as-errors 全量构建通过；46/46 项测试通过，包含安装/CLI-only 冒烟；PowerShell 依赖脚本及固定上游适配检查通过；MinGW Windows helper 交叉编译通过。

Windows 原生 Qt GUI / MSBuild / MSI 构建和驱动实机闭环仍待验收，未声称已验证 Windows 安装成功。详细验收步骤已写入 `packaging/windows/README.md` 和项目待办。功能分支在本地保留，不 push。
