# libwdi Windows 构建与分发

本目录说明本项目的 WinUSB 安装依赖；不包含第三方预编译 DLL。Windows 正常构建默认从固定源码生成 `libwdi.dll`，随应用安装到根目录。Linux/macOS 不下载、不构建此依赖。

上游：[pbatard/libwdi v1.5.1](https://github.com/pbatard/libwdi/tree/9b23b82a2dd1cbffc16d46c212f92c6bf8c0c602)，精确提交 `9b23b82a2dd1cbffc16d46c212f92c6bf8c0c602`。源码归档 SHA256 为 `a65be68e20f0db155c98fa57f7748088165b2a8d0c19c2968e01c9ea5b2dfe4e`。

## 构建条件与流程

需要原生 Windows、Visual Studio 2022 或更新版本/Build Tools、MSVC v143 的 x86/x64 工具、Windows SDK，以及 PowerShell 5.1 或更新版本。应用本身可由 Ninja、MinGW 或 Visual Studio 生成器构建。支持 Win32/x64 DLL；不支持原生 ARM64 应用发布。

在顶层 CMake 中 `include(cmake/Libwdi.cmake)`，对 helper 设置 `add_dependencies(helper_target zzrk_libwdi)` 和 `target_include_directories(helper_target PRIVATE "${ZZRK_LIBWDI_INCLUDE_DIR}")`。模块提供目标 `zzrk_libwdi`、头文件目录 `ZZRK_LIBWDI_INCLUDE_DIR`、生成器表达式路径 `ZZRK_LIBWDI_DLL`，并注册安装规则。helper 只动态加载应用目录的 DLL，不需要链接导入库。需要在本地构建目录运行时，可对应用设置 `POST_BUILD`，用 `cmake -E copy_if_different "${ZZRK_LIBWDI_DLL}" "$<TARGET_FILE_DIR:application_target>"` 复制 DLL，并让应用依赖 `zzrk_libwdi`。

`cmake --build <build-dir> --config Release --target zzrk_libwdi` 生成固定 Release 依赖，`cmake --install <build-dir> --config Release` 安装 DLL、许可、源码与构建配方。显式设置 `-DZZRK_ENABLE_WINDOWS_DRIVER_MANAGEMENT=OFF` 可以禁用驱动管理。功能开启时构建失败会直接失败，不会静默发布缺少 DLL 的包。

模块使用 CMake 自带的源码下载和 SHA256 校验。脚本只修改构建目录里的源码副本，生成 WinUSB-only `config.h`，保留上游许可证和源包。它按顺序构建 x86 installer、x64 installer、Win32 embedder、目标平台 DLL；上游 DLL 的预构建事件生成并编译 `embedded.h`，包含安装器、INF/CAT 模板和 WinUSB/WDF 资源。未嵌入 libusb0/libusbK，也不要求 ARM64 编译器。依赖固定用 Release，DLL 的 RuntimeLibrary 显式改为 `/MT`，避免 Debug CRT 或 MinGW 包缺少 MSVC CRT 的问题。

`patch-libwdi.ps1` 还对固定版本的两个资源创建点作受限适配：目录和文件从创建时归 Administrators 组所有，禁止普通权限进程利用个人 owner 权限替换随后提权执行的 installer。IPC 中用于验证调用者的 `GetSid()` 保持原样；上下文不匹配时构建直接失败。helper 在准备资源前设置管理员组默认 owner，执行前再次核查完整资源树。完整适配脚本随包提供，不能只复制 `build-libwdi.ps1` 而遗漏此脚本。

Visual Studio 生成器直接传递 `CMAKE_VS_MSBUILD_COMMAND`。Ninja/MinGW 下脚本通过 VS Installer 的 `vswhere.exe` 枚举 VS 2022+ 实例，选择实际包含 v143（14.30～14.4x）x86/x64 编译器的 MSBuild。VS 2026 仍须额外安装 v143，默认的新工具集不能替代固定的 v143。MinGW 应用同样需要旁侧的 MSVC 构建依赖。也可显式设置 `-DZZRK_LIBWDI_MSBUILD=C:/.../MSBuild.exe`。可通过 `ZZRK_LIBWDI_POWERSHELL` 指定 PowerShell 路径。

## 资源与许可证

上游 WinUSB 支持需要 WDK 8 的重分发组件，仅安装现代 Windows SDK 不够。脚本自动下载微软官方 [WDF coinstaller 重分发包](https://download.microsoft.com/download/0/5/F/05FD6919-6250-425B-86ED-9B095E54065A/wdfcoinstaller.msi)，SHA256 固定为 `29314207814ce9d5d73695f7e9239539cf37c79e750b9d5ea5a5ef5487a583d6`。使用 `msiexec /a ... /qn TARGETDIR=...` 在构建目录管理解压，不向系统安装 WDK。校验失败会终止；离线构建可预置经校验的 `wdfcoinstaller.msi` 到 `_deps/libwdi-build/<config>/`，源码可使用 CMake 标准的 `FETCHCONTENT_SOURCE_DIR_ZZRK_LIBWDI_SOURCE` 覆盖目录（使用者负责确保仍为上述版本）。

`libwdi.dll` 采用 LGPL v3 或更新版本。发行包的 `share/licenses/libwdi/` 包含上游 `COPYING`（GPL v3）、`COPYING-LGPL`（LGPL v3）、完整对应源码、构建脚本、微软重分发许可/文件清单及本次构建信息。微软 coinstaller 不改用本项目 MIT 许可。动态库可由使用者自行重建替换。源码中的上游 `configure.ac` 版本字段仍为 1.3.1；以固定提交与 `libwdi.rc` 的 1.5.1.788 版本为准。

从发行包重建时，可在安装 VS 2022 的 PowerShell 中运行：

```powershell
./share/licenses/libwdi/rebuild/build-libwdi.ps1 `
  -SourceDir ./share/licenses/libwdi/source `
  -BuildDir C:/temp/zzrk-libwdi-rebuild -Platform x64
```

Linux 可运行 `cmake -P tests/libwdi_cmake_smoke.cmake` 检查禁用路径无下载。Windows 仍须验证 Ninja/VS 两种生成器完整构建、安装包内 DLL 与资源支持，以及在 Windows VM 上的实际驱动安装；Linux 的 MinGW 交叉编译不能替代原生 MSBuild、MSI 解压与 UAC 验证。
