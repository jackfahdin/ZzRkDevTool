# ZzRkDevTool Linux 便携版

解压后，在本目录启动 GUI 或命令行程序：

```bash
./ZzRkDevTool
./zzrkdevtool-cli --version
./zzrkdevtool-cli list
```

程序入口在根目录；Qt 和 ICU 库位于 `lib/`，Qt 插件位于 `plugins/`，文档和许可证位于 `docs/`。可以移动或重命名整个目录，包括包含空格的路径。请保留 `qt.conf` 和完整目录结构，不要只复制可执行文件。

此包携带 Qt 运行库，Zzlibusb 已静态链接；仍需要兼容的 Linux glibc、C++ 运行库和图形系统。它不是跨所有 Linux 发行版通用的容器，建议在与构建环境兼容的系统上运行。

## USB 权限

便携包不会自动安装系统规则。如果普通用户没有 USB 访问权限，可在本目录手动执行一次：

```bash
sudo groupadd --system --force zzrkdevtool
sudo install -m 0644 usb/60-zzrkdevtool.rules /etc/udev/rules.d/60-zzrkdevtool.rules
sudo udevadm control --reload-rules
```

重新插拔设备。桌面用户通常通过 `uaccess` 获得权限；无桌面会话可执行 `sudo usermod -aG zzrkdevtool "$USER"`，注销并重新登录。不需要以 root 身份运行软件。

卸载便携版只需删除软件目录；若不再使用任何版本的本软件，可另外移除手动安装的 `/etc/udev/rules.d/60-zzrkdevtool.rules` 并重新加载规则。
