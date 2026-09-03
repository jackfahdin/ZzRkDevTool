# 任务 8 报告：安装、许可证和跨平台打包接入

## 结果

- 基线：`4080c770ad57a2dc724a9c83605040eb9c6ddeda`
- ZzPureTools 来源：`git@github.com:jackfahdin/ZzPureTools.git`，固定 commit
  `d6b90f1d266b6aa97b0ffb3a75deaa4bc2314514`。
- GUI 继续是 Linux、Windows、macOS Qt 部署主目标；CLI 作为附加可执行文件。
  三个平台的部署规则均包含 `qoffscreen`。
- ZzPureTools、ZzLog、QWindowKit 均是静态框架内容，未登记为虚假的运行时
  动态库。

## RED

先仅扩展 `tests/linux_install_smoke.cmake`：让单配置时空的
`INSTALL_CONFIG` 省略 `--config`，并加入三个框架许可证和逐字内容断言。

```text
$ ctest --test-dir build-task2 -R '^linux_install_smoke$' --output-on-failure
1/1 Test #24: linux_install_smoke ...***Failed
Installed file is missing:
.../share/doc/zzrkdevtool/licenses/ZzPureTools/ZzPureTools-MIT.txt
0% tests passed, 1 tests failed out of 1
```

该失败证明测试已越过任务 7 遗留的空 `INSTALL_CONFIG` 问题，且准确暴露了
缺少的框架许可证安装规则。

## GREEN 与验证

```text
$ cmake -S . -B build-task8-package -G Ninja -DCMAKE_BUILD_TYPE=Debug
-- Configuring done
-- Generating done
-- Build files have been written to: .../build-task8-package
exit 0

$ cmake --build build-task8-package --parallel 4
[351/354] Linking CXX executable tests/gui_image_download_test
exit 0

$ ctest --test-dir build-task8-package -R '^linux_install_smoke$' --output-on-failure
1/1 Test #24: linux_install_smoke ... Passed
100% tests passed, 0 tests failed out of 1
exit 0

$ cmake --install build-task8-package --prefix /tmp/zzrkdevtool-task8-install
-- Installing: .../licenses/ZzPureTools/ZzPureTools-MIT.txt
-- Installing: .../licenses/ZzLog/LICENSE
-- Installing: .../licenses/QWindowKit/QWindowKit-Apache-2.0.txt
-- Installing: .../plugins/platforms/libqoffscreen.so
exit 0

$ QT_QPA_PLATFORM=offscreen /tmp/zzrkdevtool-task8-install/bin/ZzRkDevTool --smoke-test
[info] [ZzRkDevTool] GUI application started
exit 0

$ cpack --config build-task8-package/CPackConfig.cmake
CPackDeb: - Generating dependency list
CPack: - package: .../zzrkdevtool_0.1.0_amd64.deb generated.
exit 0

$ ctest --test-dir build-task8-package --output-on-failure
100% tests passed, 0 tests failed out of 24
exit 0
```

`dpkg-deb -c zzrkdevtool_0.1.0_amd64.deb` 确认包内包含
`usr/bin/ZzRkDevTool`、`libqoffscreen.so`、
`ZzPureTools-MIT.txt`、`ZzLog/LICENSE` 与
`QWindowKit-Apache-2.0.txt`。`cmp -s` 确认安装树中的三份许可证逐字匹配
各自上游源文件；`git diff --check` 退出码为 0。

## 文件清单

- `cmake/InstallAndPackage.cmake`
- `NOTICE.md`
- `packaging/linux/THIRD_PARTY_NOTICES.md`
- `packaging/licenses/ZzPureTools-MIT.txt`
- `packaging/licenses/QWindowKit-Apache-2.0.txt`
- `tests/linux_install_smoke.cmake`

## 疑虑

- 简报列出的 `QWindowKit-MIT.txt` 与上游
  `thirdParty/ZzPureTools/ZzThirdParty/qwindowkit/LICENSE` 冲突：上游文本是
  Apache-2.0。实际源文本和许可证类型优先，故使用
  `QWindowKit-Apache-2.0.txt`；Linux notice 同样标注 Apache-2.0。
- 首次干净配置因受限环境无法解析 `github.com`（fmt 12.1.0 下载）；获得网络
  权限后用相同命令重新配置成功。未绕过依赖下载或降低验证。
