# SDD ledger — plan: docs/superpowers/plans/2026-09-02-zzpuretools-framework-integration.md

Base branch: `feature/zzpuretools-framework`

Task 1: complete — commits 594e5b3..77f335f; review findings addressed, one Minor deferred (cache help/ADVANCED assertion depth)
Task 2: complete — commit b60c8fa; review passed, Minors deferred (public framework headers require transitive links; shutdown test lacks explicit state assertion)
Task 3: fix round 1/5 (2 addressed, 0 open — Controller 接管设备协调并统一共享 ViewModel；commits 0b51fc8..2dcc5c5)
Task 3: minor (deferred): attachWorkspace 的状态控件依赖宿主已有布局，真实 WorkspaceShell 可见性留待任务 6 验证
Task 3: minor (deferred): startDeviceMonitoring 失败后 monitoringStarted 状态及重试语义可进一步收紧
Task 3: minor (deferred): 重复调用 createInitialDownloadPage 会先创建 parented view 再返回状态已接管错误
Task 3: complete (commits b60c8fa..2dcc5c5, review clean)
Task 4: fix round 1/5 (2 addressed, 0 open — GPT 读取归 Controller 且页面不再承载业务准入/表格内部协议；commits 9ae6c18..c721cf0)
Task 4: minor (deferred): 设备表固定列宽、总线数值排序与空 portPath 的 portNumbers 降级显示未恢复
Task 4: minor (deferred): MainWorkspaceController 工作区 QObject 观察指针的动态重挂生命周期可进一步加固
Task 4: minor (deferred): MainWindow 热插拔错误状态和已迁移列/role 常量待任务 6 删除适配器时清理
Task 4: complete (commits 2dcc5c5..c721cf0, review clean)
Task 5: fix round 1/5 (1 addressed, 0 open — 升级/分区取消等待态按钮保持禁用；commits ce17c79..cbf34a9)
Task 5: minor (deferred): 交互式文件选择/保存和危险操作确认未自动化驱动
Task 5: complete (commits c721cf0..cbf34a9, review clean)
Task 6: fix round 1/5 (1 addressed, 0 open - 完整回滚已提交的工作区、导航面板和页面宿主；commits 1c9da07..1120ff7)
Task 6: complete (commits cbf34a9..1120ff7, review clean)
Task 7: fix round 1/5 (1 addressed, 0 open - 关闭窗口后直接断言 Controller 已析构；commits 0c13747..4080c77)
Task 7: minor (deferred to Task 8): 单配置 Ninja 中 linux_install_smoke 的 INSTALL_CONFIG=$<CONFIG> 展开为空
Task 7: environment limitation: LSan 在 ptrace 环境不可用；ASan/UBSan 以 detect_leaks=0 验证 gui_framework 1/1 通过
Task 7: complete (commits 1120ff7..4080c77, review clean)
Task 8: complete (commits 4080c77..e21ea3b, review clean)
Task 9: fix round 1/5 (3 addressed, 0 open — 终态信号同步销毁边界、回归覆盖和文档测试数量；commits 7e48f15..b629391)
Task 9: environment limitation: 当前 Linux 主机无法实际执行 Windows MSVC/MinGW 和 macOS 构建；已完成静态检查并在中文文档中给出可复现命令
Task 9: complete (commits e21ea3b..b629391, review clean)
Final review fix round 1/2: commits b629391..50c9f93 — CLI-only 安装、ViewModel setter/嵌套 helper 生命周期、macOS/MinGW 门禁和 CMake cache 元数据已加固；复审仍发现 2 个 Important
Final review fix round 2/2: commit 51c3fda — 补齐间接 setter 上层调用栈和重入 shutdown 确定性清理；Debug/ASan/Release 各 25/25 通过，actionlint 与 Release DEB 检查通过
Final rereview: 0 Critical, 0 Important, 1 deferred Minor — 可以合并；Windows MSVC/MinGW 与 macOS 仍待对应 GitHub Actions runner 实测，LSan 因当前环境限制未执行
Plan complete: tasks 1..9 and final review fixes complete on feature/zzpuretools-framework
