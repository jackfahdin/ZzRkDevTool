# Windows 打包与 USB 驱动管理

Windows 发布基线是包含 GUI、CLI、Qt 运行库、插件、驱动管理辅助程序和 `libwdi.dll` 的 x86_64 TGZ。解压不会自动安装驱动，普通设备操作也不会申请管理员权限。目前仍是待 Windows 实机验收的验证产物。

## 构建与打包

安装 Visual Studio 2022 或更新版本 / Build Tools、**MSVC v143 的 x86/x64 工具和 Windows SDK**、PowerShell 5.1+、CMake、Ninja，以及 ABI 匹配的 Qt。VS 2026 和 MinGW 构建应用时，内置 libwdi 仍需单独具备 v143。Zzlibusb 从固定子模块静态构建，不需要 vcpkg 或 `libusb-1.0.dll`。

在 MSVC x64 开发者 PowerShell 中执行：

```powershell
$env:QT_PREFIX_PATH = "D:/SoftWare/Qt/6.12.0/msvc2022_64"
cmake --preset windows-msvc-release
cmake --build --preset windows-msvc-release --parallel
ctest --preset windows-msvc-release
cpack --preset windows-msvc-tgz
```

Ninja 使用开发者环境的应用编译器；libwdi 使用 MSBuild，在独立源码副本中生成 WinUSB 专用 DLL。DLL 使用静态 Release CRT，避免 MinGW 版额外依赖 MSVC CRT。WDF 重分发资源从微软官方下载并核验固定 SHA256，MSI 只在构建目录管理解压。完整条件和离线输入见 [libwdi 构建说明](../../thirdParty/libwdi/README.md)。

可使用 `windows-vs2022-release` / `windows-vs2022-tgz` 或 VS 2026 对应预设（CMake >= 4.2）。每种生成器使用独立构建目录。添加 `-DZZRK_ENABLE_WINDOWS_DRIVER_MANAGEMENT=OFF` 可关闭驱动管理；发布包默认开启，不会静默忽略依赖构建失败。

程序、`zzrk-driver-helper.exe`、`libwdi.dll`、Qt DLL 与 `qt.conf` 位于包的根目录，插件在 `plugins/`。不要单独移动 exe。`share/licenses/libwdi/` 包含许可、固定版本完整源码与构建配方，允许按 LGPL 要求重建并替换 DLL。

## 使用内置驱动管理

1. 仅连接要操作的目标板卡，在状态栏选择它的端口，打开“设备”页的“驱动管理”。当前白名单为 RK3588 的 `USB\VID_2207&PID_350B` 及其明确匹配的 RockUSB 接口；不操作 Hub 或复合父设备。
2. 查看当前服务、提供商、版本、当前 INF 和原包恢复记录。原厂 `rockusb` 驱动可能导致 libusb 打开时报 `-12`；切换为 WinUSB 后，原厂 RK 工具可能暂时无法使用。
3. 点击“切换为 WinUSB”，阅读确认说明并允许 UAC。只有辅助程序提权；安装前先导出原驱动。导出失败、查询不完整、存在不同原包备份或同时连接另一块相同硬件 ID 板卡时中止安装。
4. 保留 Windows 的证书和签名提示。不关闭驱动签名校验。安装完成后重新枚举并执行仅打开、声明和释放接口的 probe；probe 不发送 RockUSB 命令、不擦写存储。
5. 若提示需要重新插拔或重启，完成该步骤后刷新，再运行 probe。不能把“安装程序返回成功”当作已经可烧录。
6. 需要回到原厂工具时点击“恢复原驱动”。程序显式选择并重新绑定备份包，不删除系统中共享的 WinUSB 包。缺少可信备份时禁止恢复；安装失败后处于无绑定状态，也可使用已存在的可信备份恢复。

Windows 按硬件 ID 匹配驱动，选择物理端口并不保证只影响该端口。首版拒绝同时连接相同硬件 ID 的其他板卡；以后连接相同硬件 ID 的设备也可能使用已经安装的驱动。恢复记录按硬件 ID 保存，不能覆盖不同原包版本。

备份及结果位于 `%ProgramData%\ZzRkDevTool-Drivers\<硬件 ID 十六进制编码>\`，其中 `original/` 是原包、`state.ini` 是恢复记录、`last.ini` 是最近操作结果、`winusb/` 是安装资源。目录与资源从创建时使用管理员组所有权，普通用户只读；不接受可写的旧目录、junction 或不可信记录。不要手工删除原包、混入别的 INF 或放宽目录权限。

驱动变更期间不能开始烧录或退出主程序。请等待辅助程序结束；应用不会因为等待时间长就强杀系统安装。设备被拔出、UAC 取消或签名策略阻止安装时不会自动再次安装。

## Windows 实机验收（待执行）

目前已进行 Linux 构建、策略和 GUI 自动化测试，以及 MinGW 的原生 helper 交叉编译检查；这些不能证明 Windows 上的 MSI、MSBuild、UAC、ACL 或驱动安装已通过。

- 原生 MSVC/Ninja 完整配置、编译、测试、TGZ 打包；解压后的 helper 和 DLL 齐全。MinGW / VS 生成器各自验证构建。
- 查询原厂驱动 → 原包导出 → WinUSB 安装 → probe → 只读设备信息 → Loader/Maskrom 切换 → 完整升级及回读校验 → 恢复原驱动 → 原厂工具重新识别。
- UAC 取消、中文路径、拔出设备、安装失败、需要重启、恢复记录不可信，以及空服务名但有关联 INF 的情况。
- 连接两块相同硬件 ID 板卡，确认安装被阻止且两者驱动没有变化；第二块板空闲后再测，不中断其已有任务。
- 在干净 Windows 环境验证动态依赖及 Windows 安全策略。未通过以上闭环前，不将此功能标注为已完成真机支持。

## 正式签名

libwdi 的自签名机制不等同于项目自有的生产可信签名，也不保证通过企业策略。正式发布还需项目确定签名与分发策略，对所有 EXE/DLL 进行 Authenticode SHA-256 签名及时间戳验证，并在干净 Windows 上验证：

```powershell
signtool sign /fd SHA256 /td SHA256 /tr https://timestamp.example.invalid `
  /a path/to/ZzRkDevTool.exe
signtool verify /pa /all /v path/to/ZzRkDevTool.exe
```

以上时间戳 URL 和证书来源为占位示例。证书、密码、Token 和私钥不得进入仓库或 CI 日志。
