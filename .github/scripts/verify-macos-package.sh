#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# 固定挂载点，避免解析 hdiutil 的本地化输出；结束时始终卸载。
set -euo pipefail
shopt -s nullglob
images=(build/macos-release/ZzRkDevTool-*.dmg)
if (( ${#images[@]} != 1 )); then
    echo '应生成一个 Universal DMG' >&2
    exit 1
fi
mount_point=$(mktemp -d "${RUNNER_TEMP:-/tmp}/zzrk dmg check.XXXXXX")
trap 'hdiutil detach "$mount_point" >/dev/null' EXIT
hdiutil attach -nobrowse -readonly -mountpoint "$mount_point" "${images[0]}"
app="$mount_point/ZzRkDevTool.app"
test -f "$app/Contents/PlugIns/platforms/libqcocoa.dylib"
for executable in ZzRkDevTool zzrkdevtool-cli; do
    binary="$app/Contents/MacOS/$executable"
    test -x "$binary"
    lipo -verify_arch arm64 x86_64 "$binary"
    dependencies=$(otool -L "$binary")
    if [[ "$dependencies" == *libusb*dylib* || "$dependencies" == *"$PWD"* ]]; then
        echo "包内程序仍引用共享 USB 库或构建目录：$executable" >&2
        exit 1
    fi
done
# 使用包中必备的 Cocoa 插件；不依赖 macdeployqt 是否收集 offscreen 插件。
env -u DYLD_LIBRARY_PATH -u QT_PLUGIN_PATH -u QT_QPA_PLATFORM_PLUGIN_PATH \
    QT_QPA_PLATFORM=cocoa "$app/Contents/MacOS/ZzRkDevTool" --smoke-test
env -u DYLD_LIBRARY_PATH "$app/Contents/MacOS/zzrkdevtool-cli" --version
# 当前为 ad-hoc 签名验证，不等同于 Developer ID 签名和 Apple 公证。
codesign --verify --deep --strict "$app"
