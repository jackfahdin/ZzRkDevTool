#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# 验证真正生成的两个发布包，不依赖构建机上的 Qt 路径设置。
set -euo pipefail
shopt -s nullglob

archives=(build/linux-release/ZzRkDevTool-*.tar.gz)
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

# 两种包各自部署的 Qt 必须相同，且许可证、SBOM 随便携包保留。
cmp "$portable/lib/libQt6Core.so.6" "$verify_dir/deb/usr/lib/zzrkdevtool/libQt6Core.so.6"
test -f "$portable/docs/sbom.spdx.json"
test -f "$portable/docs/licenses/Qt/LGPL-3.0-only.txt"
test -f "$portable/docs/licenses/libusb/LGPL-2.1-or-later.txt"
for executable in ZzRkDevTool zzrkdevtool-cli; do
    test "$(patchelf --print-rpath "$portable/$executable")" = "\$ORIGIN/lib"
done
# 插件从私有库子目录移到顶层后，也必须只从便携目录加载 Qt。
while IFS= read -r -d '' plugin; do
    dependencies=$(env -u LD_LIBRARY_PATH ldd "$plugin")
    if [[ "$dependencies" == *'not found'* ]]; then
        echo "插件未正确使用便携包内的 Qt：$plugin" >&2
        exit 1
    fi
    qt_library_found=false
    while IFS= read -r library; do
        qt_library_found=true
        # ldd 保留 RUNPATH 中的 ../，先规范路径，同时保留目录名中的空格。
        if [[ "$(realpath -- "$library")" != "$portable/lib/"* ]]; then
            echo "插件加载了包外的 Qt：$plugin -> $library" >&2
            exit 1
        fi
    done < <(awk '/libQt6.* =>/ { sub(/^.* => /, ""); sub(/ \([^)]*\)$/, ""); print }' <<<"$dependencies")
    if [[ "$qt_library_found" != true ]]; then
        echo "插件缺少 Qt 动态依赖：$plugin" >&2
        exit 1
    fi
done < <(find "$portable/plugins" -type f -name '*.so' -print0)

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
