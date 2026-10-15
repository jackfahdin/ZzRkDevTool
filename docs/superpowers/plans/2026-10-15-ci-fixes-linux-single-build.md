# CI 修复与 Linux 单次构建计划

用户已确认：不同问题分别提交，本地提交后不 push；Linux 共用一次应用编译和测试，继续提供 DEB 和便携 TGZ，两种发布包分别解包验证。

## 独立修复

1. MSVC：模拟 libusb 测试只继承头文件，遗漏生产库的 `NOMINMAX`，补充到头文件接口目标。
2. MinGW：Qt 包发现从函数移到目录作用域，避免入口目标的 `include_guard()` 随函数退出失效。
3. macOS：日志状态使用短临界区的 mutex 保护 shared_ptr，兼容缺少 C++20 原子智能指针特化的 libc++。

以上逐项复现、验证、独立提交；本机的交叉验证不能替代后续 Windows、macOS Runner 实测。

## Linux 打包设计

- 保留 `linux-release` 作为 CI 唯一应用构建目录，执行一次完整测试。
- DEB 继续使用 `/usr/bin`、私有库目录和系统集成文件。
- TGZ 在独立的 CPack 暂存目录中整理为根目录程序、`lib/`、`plugins/`、`docs/`、`usb/`，只修改包内副本的 RUNPATH 和 qt.conf。
- 使用 CPack 官方的逐生成器项目配置和 `CPACK_PRE_BUILD_SCRIPTS`；不重新配置、编译应用，不修改原始构建产物。
- 保留开发者直接安装便携布局的 `linux-portable-release` 配置，但 CI 不再执行第二套构建。
- 两种包均解包到含空格路径，清除 Qt 相关环境变量后启动 CLI 与 GUI；检查便携程序及插件是否使用包内库。
- 更新预设、CI 中文注释及编译打包指南，并运行真实打包验证、工作流语法检查和发布脚本回归。

## 验证记录

- Qt 6.12.0 Linux 严格构建、37 项 CTest 通过；MSVC 问题对应的模拟 USB 测试通过 MinGW 交叉编译。
- Qt MinGW 入口目标同构复现由配置失败变为通过；真实 Linux Qt 首次及重复配置通过。
- clang 20/libc++、GCC 15/libstdc++ 的真实日志测试和严格编译通过。
- 相同 `linux-release` 产物生成 DEB 与便携 TGZ，两个包在含空格目录内解包，清除 Qt 环境变量后 GUI/CLI 启动及插件包内依赖检查通过。
- 同一 CPack 调用按 `TGZ;DEB` 顺序打包通过；打包前后原始 GUI、CLI 的 SHA-256 完全相同，打包后 Ninja 无需编译。
- actionlint、ShellCheck、20 个发布脚本场景通过。Windows 与 macOS 原生 CI 回归需后续推送后执行；本次仅本地提交，不 push。
