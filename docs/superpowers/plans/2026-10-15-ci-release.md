# 三平台 CI 与发布流程调整计划

目标：参考 VNote 的三平台独立工作流、continuous-build 预发布和 `[Release]` 正式版草稿机制，沿用本项目 TGZ/DEB/DMG 产物。仅本地提交，不 push；不实现 Gitee 同步。

设计已由用户确认。实现时使用相同的发布辅助流程，避免三套重复发布脚本；各平台只更新自己的附件，不改写其他平台附件或已编辑的正式版说明。PR 只编译测试和保存构建产物，只有主分支 push 可以使用发布权限。

- [x] 分批提交已有源码构建、打包与文档修改；重新运行 37 项测试。
- [x] 拆分 Linux、Windows、macOS 工作流，补充 Linux 双格式发布和中文注释，保留 MinGW 与可移植核心检查。
- [x] 添加公开子模块 HTTPS 拉取及版本提取公共步骤，保留本地 SSH 配置。
- [x] 添加共享发布流程；主分支普通 push 更新 continuous-build，`[Release]` push 创建/补充 `v版本号` 草稿；拒绝过期持续构建和版本标签冲突，正式版独立于后续开发提交。
- [x] 核对官方托管 Runner 使用的 Action 最新稳定 Major；文档记录 self-hosted 的 Runner、Node runtime 和 Breaking Changes 检查规则。
- [x] 编写 CI 使用指南和 Gitee 准备清单，不读取或创建 Gitee 凭据、不执行同步。
- [x] 使用 actionlint、ShellCheck、辅助脚本的隔离测试与本机 Linux 构建验证；请求只读复审，修正问题并分批提交。

参考基线：VNote `1cc75f70cb47c2977778b014d566e79c1972e8ca` 的 `ci-linux.yml`、`ci-win.yml`、`ci-macos.yml` 和 `gitee-mirror.yml`。不照搬其旧 Action 版本、Qt 5 兼容、应用更新签名和项目专有打包逻辑。

验证记录：actionlint 1.7.12（启用 ShellCheck）、ShellCheck 0.11.0 与发布脚本 10 项隔离测试通过；Linux 真实 TGZ/DEB 解包启动检查通过，源码 USB 的公开 HTTPS 拉取在全新克隆中通过，Qt 6.11.1 的基础归档和 MinGW 工具包通过 aqt 查询确认。只读复审发现并修正了 Linux artifact 目录层级与 macOS 部署目标缺失问题，macOS 最低目标显式固定为 13.3。

限制：没有 push 或触发远端工作流，没有创建/移动远端标签或 Release；Windows/macOS 实际编译、原生打包及 GitHub 发布行为仍需首次 CI 运行验证。

## 后续调整：正式版改由版本标签触发

用户确认将正式版触发条件从 `[Release]` 主分支提交改为推送 `vX.Y.Z` 标签；以上原始计划仅作为历史记录，以本节及 CI 发布指南为准。

- 主分支 push 继续更新 `continuous-build`，无每日定时任务；提交信息不再决定发布类型。
- 三平台监听 `v*` 标签 push，构建前核对标签与 CMake 三段版本一致，仍先创建正式版草稿。
- 按完整 ref 隔离构建，保留版本标签任务；正式版不依赖主分支后续进度。
- CI 不创建或移动正式版标签，发布前检查远端标签解析后的提交；支持轻量标签及附注标签，拒绝改写已公开版本附件。
- 更新隔离测试和中文操作文档；验证后本地提交，不 push。

调整验证：actionlint（启用 ShellCheck）、独立 ShellCheck、20 项发布/版本校验隔离测试及 diff 格式检查通过；独立只读审查未发现需要修改的问题。未触发远端工作流，实际标签发布仍待首次 GitHub CI 验证。
