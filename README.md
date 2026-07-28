# ZzRkDevTool

ZzRkDevTool is an MIT-licensed C++20 and Qt 6 Rockchip flashing tool. The first
validated target is RK3588 with eMMC on Ubuntu 26.04 LTS. Its CLI enumerates
Rockchip USB devices, reads typed Loader information, inspects and plans RKFW /
RKAF update images, and performs complete eMMC system upgrades from
`update.img`.

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

Run the CLI from the selected preset's build directory:

```bash
./build/linux-debug/apps/cli/zzrkdevtool-cli list
./build/linux-debug/apps/cli/zzrkdevtool-cli probe 1-5
./build/linux-debug/apps/cli/zzrkdevtool-cli read-chip-info 1-5
./build/linux-debug/apps/cli/zzrkdevtool-cli read-flash-info 1-5
./build/linux-debug/apps/cli/zzrkdevtool-cli read-flash-id 1-5
./build/linux-debug/apps/cli/zzrkdevtool-cli inspect-image update.img
./build/linux-debug/apps/cli/zzrkdevtool-cli plan-image update.img
./build/linux-debug/apps/cli/zzrkdevtool-cli \
    flash-image 1-5 update.img --yes --verify critical
./build/linux-debug/apps/cli/zzrkdevtool-cli reset 1-5
./build/linux-debug/apps/gui/ZzRkDevTool
```

Use the stable port path printed by `list` for all device commands. They accept
only the observed RK3588 Loader identity `2207:350b`, `bcdUSB 0x0201`,
`bcdDevice 0x0223` and exact interface layout. `probe` performs no bulk or
control transfer. `read-chip-info` sends the single fixed transaction recorded
in its project protocol trace and returns an opaque 16-byte value.
`read-flash-info` sends its separately observed fixed transaction, preserves
the 11-byte response and reports the sector-derived capacity. Its other fields
are neutral legacy compatibility codes; it does not determine manufacturer,
physical geometry or storage type. `read-flash-id` preserves its complete
five-byte response and recognizes only the exact observed `EMMC ` marker as
eMMC. That marker is not a CID, manufacturer, capacity or hardware-partition
identifier; every other five-byte response remains opaque.

`inspect-image`, `plan-image`, and `flash-image` calculate the RKFW trailing
MD5 with Qt 6 before accepting the image. `flash-image` writes generated GPT,
every partition payload actually present in the image, and the embedded
`MiniLoaderAll.bin` converted to a New IDBlock at eMMC LBA 64. A partition that
exists only in `parameter` and has no image payload is left unchanged.

The default `--verify critical` mode reads back GPT, Loader, and boot-chain
partition payloads. `--verify all` also checks all written Raw and Android
Sparse RAW/FILL extents; Sparse DONT_CARE extents are intentionally skipped.
`--verify none` disables read-back verification. A successful flash resets the
device by default; `--no-reboot` leaves it in Loader mode. `Ctrl+C` is observed
between RockUSB transfer blocks, stops new storage commands, suppresses the
automatic reset, and exits with status 130.

`CMakeUserPresets.json` may add workstation-specific convenience presets and is
intentionally ignored by Git. It is not required by the commands above.

This technical preview is a development-tree build. Installation and release
packaging stay disabled until Qt, its platform plugins, and libusb can be
deployed together on each supported host platform.

## Safety and current limits

All device operations require the physical USB port path printed by `list`.
Flashing additionally requires `--yes`, the validated Loader identity, the
exact `EMMC ` media marker, sufficient eMMC capacity, a valid firmware MD5,
and a complete typed flash plan. No CLI command exposes raw RockUSB CDB input.

The claim-only probe requires an exact physical port and fails closed on a
partial scan, descriptor change, unexpected interface layout, active kernel
driver, claim failure or release failure. It never selects the first matching
VID/PID implicitly.

Current write support is deliberately limited to the observed RK3588 Loader
revision and eMMC user area. It does not write eMMC boot0, boot1, or RPMB. The
RK3588 Loader 0x0223 used for validation returns opaque data when reading the
disk-tail backup GPT, so the tool verifies the primary GPT strictly and reports
the backup GPT as write-submitted. Maskrom Loader download, additional Loader
revisions, Windows/macOS hardware validation, and release packaging remain
future work.
