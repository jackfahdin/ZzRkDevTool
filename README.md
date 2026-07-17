# ZzRkDevTool

ZzRkDevTool is an MIT-licensed C++20 and Qt 6 Rockchip flashing tool. The first
development target is RK3588 with eMMC on Ubuntu 26.04 LTS. The current code is
a non-writing technical preview: it enumerates Rockchip USB devices and can
claim and release one explicitly selected RK3588 Loader interface. It does not
send RockUSB commands or write storage.

## Development build

Initialize the pinned spdlog v2 submodule after cloning:

```bash
git submodule update --init thirdParty/spdlog
```

The tracked Linux presets use libusb 1.0.30 from `thirdParty/libusb`. Point
CMake at a Qt 6.11 or newer installation when configuring a fresh clone:

```bash
cmake --preset linux-debug -DCMAKE_PREFIX_PATH="$HOME/Qt/6.11.1/gcc_64"
cmake --build --preset linux-debug
ctest --preset linux-debug
```

`linux-sanitize` and `linux-release` provide sanitizer and optimized builds.
Application builds compile the pinned spdlog v2 source. On the first configure,
spdlog downloads fmt 12.1.0 from its SHA-256-pinned CMake declaration.
To compile the portable core/protocol and the current host's platform service
source set without Qt or libusb:

```bash
cmake --preset portable-debug
cmake --build --preset portable-debug
ctest --preset portable-debug
```

Run the read-only tools:

```bash
./build/linux-debug/apps/cli/zzrkdevtool-cli list
./build/linux-debug/apps/cli/zzrkdevtool-cli probe 1-5
./build/linux-debug/apps/gui/ZzRkDevTool
```

Use the stable port path printed by `list` as the `probe` argument. The current
probe accepts only the observed RK3588 Loader identity `2207:350b` with
`bcdUSB 0x0201`; it opens, claims, releases and closes the interface without a
bulk or control transfer.

`CMakeUserPresets.json` may add workstation-specific convenience presets and is
intentionally ignored by Git. It is not required by the commands above.

This technical preview is a development-tree build. Installation and release
packaging stay disabled until Qt, its platform plugins, and libusb can be
deployed together on each supported host platform.

## Safety

No write, erase, loader download, or reset operation belongs in the first
milestone. Such operations will only be added behind typed plans, device and
range validation, explicit confirmation, and hardware tests.

The claim-only probe requires an exact physical port and fails closed on a
partial scan, descriptor change, unexpected interface layout, active kernel
driver, claim failure or release failure. It never selects the first matching
VID/PID implicitly.

The target-private `RockusbSession` executor supports data-in transactions only
and is not wired to the libusb backend or CLI. Public operations will be typed;
raw command blocks must never be accepted from a CLI or GUI input surface.
