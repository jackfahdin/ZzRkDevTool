# GitHub CI、发布与 Gitee 准备指南

本文对应 `.github/workflows/`，参考 [VNote 工作流](https://github.com/vnotex/vnote/tree/master/.github/workflows)的组织方式，保留本项目的 Qt GUI、CLI 和 TGZ/DEB/DMG 打包规则。所有工作流均带中文注释，可以直接修改 Runner、Qt 版本、构建预设和触发分支。

## 1. 工作流分工

| 文件 | Runner | 职责 |
| --- | --- | --- |
| `ci-linux.yml` | `ubuntu-latest` | 严格编译、完整测试、DEB 与便携 TGZ、解包启动检查 |
| `ci-windows.yml` | `windows-latest` | MSVC x64 TGZ、包内程序检查；独立 MinGW GUI 兼容检查 |
| `ci-macos.yml` | `macos-latest` | 最低 macOS 14.4 的 arm64 + x86_64 Universal DMG、包内启动与 ad-hoc 签名检查 |
| `publish-artifacts.yml` | `ubuntu-latest` | 三个平台共用的发布流程，仅接收本次 CI 的 artifact |
| `portable.yml` | 三平台官方 Runner | 不依赖 Qt/USB 的核心测试、Linux ASan/UBSan、工作流语法与发布脚本隔离测试 |

Linux 应用只配置、编译并完整测试一次，DEB 和便携 TGZ 均从 `build/linux-release` 生成。CPack 在各自暂存区完成布局和运行库部署，TGZ 额外整理便携目录与相对库路径，不再次编译应用。最终两种包仍分别解包、清除 Qt 环境变量后启动验证，并检查便携插件从包内加载 Qt。完整测试中的 CLI-only 安装检查用于验证另一种功能配置，仍保留。

macOS 窗口框架依赖 Cocoa，GUI 测试使用 `QT_QPA_PLATFORM=cocoa`，通过 CTest 资源锁避免多个原生 GUI 进程同时争抢菜单和焦点。Linux、Windows 继续使用 `offscreen`。Qt 6.12 官方库最低支持 macOS 14.4，部署目标与依赖保持一致。

macOS 测试失败时会上传 `macos-gui-diagnostics` 附件，保留原始 CTest 日志、JUnit 结果及应用的系统崩溃报告，并在 LLDB 下重跑 GUI 冒烟和配置恢复测试，输出全部线程的崩溃调用栈。每个调试进程最多运行 40 秒；调试工具不可用或超时也会记录到日志。该步骤只收集证据，不覆盖或放行原测试失败；Linux 上测试通过也不能替代 Cocoa 的原生验证。

三个应用工作流都监听 `main`、`master` 的 push 和 PR、`v*` 标签的 push，并支持 Actions 页面的手动运行。版本标签必须严格匹配 `vX.Y.Z`，且与被标记提交的 CMake 项目版本一致；不匹配时在准备源码阶段直接失败。`main` 是本仓库当前主分支；如果以后改成其他名称，需要同步修改触发分支、发布 job 条件和发布脚本的分支校验。

Qt 当前统一为 **6.12.0**，可以修改各平台文件中的 `QT_VERSION`。Qt 的公开头文件、私有头文件、库与部署工具必须来自同一套安装。`qtsvg`、`qttools` 在基础桌面归档中，不能填到 aqt 的附加 `modules` 列表；这样可以避免安装阶段报“模块不存在”。Windows 的 MinGW 编译器工具包单独固定为 `tools_mingw1310`，变更 Qt 套件时应再次检查匹配关系。

Windows 的 Qt 6.11+ 下载仓库按架构拆分，`install-qt-action@v4` 默认安装的 aqt 3.3.0 会请求不存在的旧路径。Windows 工作流通过 `AQT_SOURCE` 固定到 aqt 官方修复提交 [`7e5a5c3`](https://github.com/miurahr/aqtinstall/commit/7e5a5c3d95cf962cfc2f36c86ffa0d2c07f1a0d4)，MSVC 和 MinGW 共用；它是尚未正式发布的 aqt 修复版本。待 aqt 稳定版包含该修复后，可移除 `aqtsource` 覆盖并重新验证下载。不改为跟踪上游 `master`，也不关闭下载校验。

CI 通过 `.github/actions/prepare-source/action.yml` 调用 `.github/scripts/prepare-submodules.sh` 拉取固定子模块。`.gitmodules` 使用 GitCode HTTPS；CI 只对当前 Git 命令及子进程应用公开 GitHub 镜像转换，不修改 `.gitmodules`，也不持久化转换规则。ZzPureTools、Zzlibusb 映射到 `jackfahdin` 下的同名仓库，spdlog 映射到 `gabime/spdlog`。镜像必须包含主仓库固定的提交；分支名相同不能保证提交存在。若以后改成私有仓库，需要另行配置只读凭据。

## 2. 触发与产物

| 操作 | 构建、测试和 artifact | GitHub Release |
| --- | --- | --- |
| 提交 PR | 执行 | 不发布，无 Release 写权限 |
| Actions 手动运行 | 执行 | 不发布 |
| 主分支 push | 执行 | 各平台通过后分别更新 `continuous-build` 预发布 |
| 推送 `vX.Y.Z` 版本标签 | 执行，先校验版本 | 创建或补充对应版本的正式版草稿 |

`continuous-build` 保留主分支 push 后更新的行为，并非每天定时构建。提交信息中的 `[Release]` 不再具有特殊含义。仅在本地创建标签不会触发 GitHub Actions，必须把标签推送到 GitHub。

普通分支开发可提交 PR 来运行完整检查；可移植核心工作流还会检查其他分支的 push。artifact 默认保留 14 天。平台包先经 CPack 压缩，再作为 Actions artifact 上传；下载 artifact 后里面才是最终的 `.tar.gz`、`.deb` 或 `.dmg`。

普通主分支提交产生的持续构建附件使用稳定文件名，例如：

```text
ZzRkDevTool-continuous-linux-x86_64-portable.tar.gz
zzrkdevtool_continuous_amd64.deb
ZzRkDevTool-continuous-windows-x86_64.tar.gz
ZzRkDevTool-continuous-macos-arm64-x86_64.dmg
```

三个平台编译耗时不同，预发布附件会依次更新，短时间内可能属于不同提交。Release 说明中包含提交号；需验证一个确定版本时，请使用同一组版本草稿附件。旧的持续构建会在移动标签前检查主分支提交，过期时只保留 artifact，跳过预发布。各平台只替换自己的同名附件，不删除其他平台文件。

### 正式版发布步骤

1. 修改根 `CMakeLists.txt` 的 `project(... VERSION x.y.z ...)`，准备更新说明。
2. 在主分支提交版本修改，并自行 push 到 GitHub；这一步照常更新持续构建。
3. 为需要发布的提交创建 `vX.Y.Z` 标签并推送到 GitHub，等待三个平台构建完成。
4. 在 Releases 中检查 `v0.2.0` 草稿附件是否齐全，编辑标题和更新说明。
5. 完成平台、真机及签名验收后，手动发布草稿。

例如项目版本已提交为 `0.2.0`，并且当前 HEAD 就是要发布的提交时：

```bash
# github 是示例远端名，请替换成实际指向 GitHub 的远端。
git tag -a v0.2.0 -m "发布 0.2.0"
git push github v0.2.0
```

轻量标签和附注标签均支持，建议使用上述附注标签。当前仅接受三段版本号，不接受 `-rc1` 等后缀；标签可以指向指定提交，CI 不额外要求该提交仍在主分支尖端。

版本号从 CMake 读取，不需要在 CI 中再写一份。正式版构建与主分支构建按 ref 隔离，不会被后续日常提交取消；它始终构建标签指向的提交。CI 不创建或移动正式版标签，远端标签缺失或已指向其他提交时会拒绝发布。不同提交不得复用同一版本标签。已公开发布的正式版不能通过重跑 CI 改写附件，脚本会报错；需要更新时应升版本。

第一次运行前确认 GitHub 仓库已启用 Actions，仓库或组织策略允许工作流申请 `contents: write`。本地 `origin` 当前指向 GitCode，GitHub Actions 只在推送到 GitHub 的仓库中运行；本次开发不会替你 push。Release 使用 GitHub 自动提供的 `GITHUB_TOKEN`，不需要额外保存 GitHub PAT。

Windows 产物暂未 Authenticode 签名，macOS 仅 ad-hoc 签名，尚未 Developer ID 签名或 Apple 公证。构建成功和冒烟通过不能替代 USB 真机验收。

## 3. Action 版本规则

GitHub 官方托管 Runner（`ubuntu-latest`、`windows-latest`、`macos-latest`）默认优先采用 Action 的**最新稳定 Major**，不照搬参考项目的旧版本。这里的 `latest` 仅用于官方 Runner 标签；Action 使用可审查的 `@vN`，不使用 `@main`。

本次已对照官方 releases 和 `action.yml` 核对：

| Action | 当前采用 | runtime / 说明 |
| --- | --- | --- |
| `actions/checkout` | `@v7` | Node.js 24；关闭凭据持久化，子模块由公共准备步骤获取 |
| `actions/upload-artifact` | `@v7` | Node.js 24；维持默认归档模式，最终发布包不再二次压缩 |
| `actions/download-artifact` | `@v8` | Node.js 24；使用默认严格摘要校验，不忽略摘要不匹配 |
| `jurplel/install-qt-action` | `@v4` | 最新稳定 Major；复合 Action 的内部安装步骤使用 Node.js 24，Qt 缓存由其管理 |
| `ilammy/msvc-dev-cmd` | `@v1` | 最新稳定 Major，Node.js 20；自动定位 VS 开发环境 |

`actions/cache` 当前稳定 Major 已核对为 v6；本项目未单独调用它。今后新增独立缓存步骤时默认使用 `@v6`，不要因复制旧示例退回 v4。Qt 安装 Action 的内部依赖由该 Action 上游管理。

### self-hosted Runner 的升级关卡

当前文件没有使用 self-hosted。将来迁移或升级 Action Major 时，必须先核对以下三项，并在修改说明中记录结论：

1. **最低 Runner 版本**：阅读目标 Major 的 README 和 release notes，检查实际 Runner.Listener 版本；不能只检查开发机安装的 Node。当前 Node.js 24 Action 的已知 Runner 基线为 **2.327.1**。checkout 的容器内认证 Git 操作另要求 **2.329.0** 或以上；实际要求取所有使用步骤的最高值。
2. **Node.js runtime**：检查 `action.yml` 的 `runs.using` 及复合 Action 的内部依赖。Runner 自带的 Node.js 必须能在宿主系统上运行；例如 Linux 的 glibc、架构、系统版本以及容器环境要满足对应 Node.js 二进制要求。
3. **Breaking Changes**：逐个阅读跨过的 Major 发布说明，检查权限、缓存服务、artifact 上传/下载与目录行为、摘要校验、网络代理及防火墙要求。先在一台测试 Runner 上跑配置检查、构建测试与解包启动，再升级其他 Runner。

Linux/macOS 可用 `./bin/Runner.Listener --version` 查看 Runner 版本，Windows 对应 `bin\Runner.Listener.exe --version`。安装在 PATH 中的 `node --version` 不代表 Action 使用的 runtime。

这套规则不将 GitHub Enterprise Server 与 self-hosted 混为一谈：artifact v4 以上对 GHES 的支持有限制，迁移到 GHES 时还需另行核对服务端兼容性。

参考：[checkout](https://github.com/actions/checkout/releases)、[upload-artifact](https://github.com/actions/upload-artifact/releases)、[download-artifact](https://github.com/actions/download-artifact/releases)、[cache](https://github.com/actions/cache/releases)、[Qt 安装 Action](https://github.com/jurplel/install-qt-action/releases)。

## 4. 本地检查 CI 配置

在仓库根目录执行：

```bash
actionlint
shellcheck .github/scripts/*.sh .github/scripts/tests/*.sh
bash .github/scripts/tests/prepare-submodules-test.sh
bash .github/scripts/tests/publish-release-test.sh
python3 .github/scripts/tests/macos-gui-diagnostics-test.py
```

隔离测试模拟 `gh`，覆盖持续构建、标签触发的正式版草稿、后续开发提交、过期持续构建、标签版本不符、来源类型错误、标签缺失或变更、已发布版本、API 故障、附件缺失和首次创建竞争，不会访问 GitHub API 或创建 Release。

Linux 的真实发布包还可执行：

```bash
bash .github/scripts/verify-linux-packages.sh
```

该脚本要求已经生成当前构建目录下唯一的 TGZ 和 DEB，将它们解包到带空格的临时目录，清除 Qt 路径环境变量后运行 GUI/CLI 冒烟检查，不安装软件包、不访问 USB。

## 5. Gitee 同步准备清单（尚未启用）

目前未创建 Gitee 同步 workflow、未配置凭据，也不执行同步。用户尚未提供目标 Gitee 仓库，后续能补齐以下条件时再接入：

| 需要准备 | 用途与要求 |
| --- | --- |
| Gitee 仓库地址、owner、repo | 建议创建空仓库，不初始化不同的 README；确认目标主分支及是否已有独立开发历史 |
| 仓库管理/写入权限 | 同步账号需要推送代码、标签；如同步 Release，还要有发布与附件权限，并确认保护分支允许该账号更新 |
| `GITEE_USERNAME` | Gitee 账号的实际登录路径，不是展示昵称 |
| `GITEE_TOKEN` | 具有所需 `projects` 权限的私人令牌，后续由用户保存到 GitHub Environment/Secrets，禁止发在聊天或提交到仓库 |
| GitHub 环境配置 | 可仿照 VNote 创建 `GiteeMirror` Environment；如设审批，明确谁负责审批 |
| 同步范围 | 建议先同步主分支和正式版 `v*` 标签；排除可移动的 `continuous-build` 标签，不全量覆盖其他分支 |
| Release 附件策略 | 确认只同步标题/说明还是也上传发布包，先实测 Gitee 当前 API 的大小、速度与限额 |

VNote 的 Gitee 工作流把代码和正式版 Release 同步分开；它刻意不自动上传较大的平台包，只处理较小的更新附件。因此不能直接复制它就假设本项目的 TGZ/DEB/DMG 都能自动同步。若 Gitee 上传限制或网络条件不合适，可只同步源码与说明，发布包仍放 GitHub；也可以完全不接入 Gitee。

镜像主仓库不会自动镜像子模块的内容；当前子模块仍分别指向 GitHub/GitCode。若需要 Gitee 环境下完全独立获取源码，必须另行决定各子模块的镜像和地址策略。
