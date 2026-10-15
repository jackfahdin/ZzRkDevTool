#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# 验证真正生成的两个发布包，不依赖构建机上的 Qt 路径设置。
set -euo pipefail
shopt -s nullglob

archives=(build/linux-portable-release/ZzRkDevTool-*.tar.gz)
debs=(build/linux-release/*.deb)
if (( ${#archives[@]} != 1 || ${#debs[@]} != 1 )); then
    echo '应分别生成一个 TGZ 和一个 DEB 包' >&2
    exit 1
fi
verify_dir=$(mktemp -d "${RUNNER_TEMP:-/tmp}/zzrk package check.XXXXXX")
trap 'rm -rf "$verify_dir"' EXIT
tar -xzf "${archives[0]}" -C "$verify_dir"
portable="$verify_dir/$(basename "${archives[0]}" .tar.gz)"
dpkg-deb -x "${debs[0]}" "$verify_dir/deb"

test ! -d "$portable/bin"
test ! -d "$portable/usr"
test -f "$portable/README.zh-CN.md"
test -f "$portable/lib/libQt6Core.so.6"
test -f "$portable/plugins/platforms/libqxcb.so"
test -f "$portable/usb/60-zzrkdevtool.rules"
test -f "$verify_dir/deb/usr/lib/zzrkdevtool/libQt6Core.so.6"
test -f "$verify_dir/deb/usr/lib/udev/rules.d/60-zzrkdevtool.rules"

for bindir in "$portable" "$verify_dir/deb/usr/bin"; do
    env -u LD_LIBRARY_PATH -u QT_PLUGIN_PATH -u QT_QPA_PLATFORM_PLUGIN_PATH \
        "$bindir/zzrkdevtool-cli" --version
    env -u LD_LIBRARY_PATH -u QT_PLUGIN_PATH -u QT_QPA_PLATFORM_PLUGIN_PATH \
        QT_QPA_PLATFORM=offscreen "$bindir/ZzRkDevTool" --smoke-test
    for executable in ZzRkDevTool zzrkdevtool-cli; do
        dynamic=$(readelf -d "$bindir/$executable")
        if [[ "$dynamic" == *libusb* || "$dynamic" == *"$PWD"* ]]; then
            echo "动态依赖仍引用共享 USB 库或构建目录：$executable" >&2
            exit 1
        fi
    done
done
