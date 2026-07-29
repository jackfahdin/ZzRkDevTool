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
./build/linux-debug/apps/cli/zzrkdevtool-cli list --json
./build/linux-debug/apps/cli/zzrkdevtool-cli info 1-5 --json
./build/linux-debug/apps/cli/zzrkdevtool-cli probe 1-5
./build/linux-debug/apps/cli/zzrkdevtool-cli read-chip-info 1-5
./build/linux-debug/apps/cli/zzrkdevtool-cli read-flash-info 1-5
./build/linux-debug/apps/cli/zzrkdevtool-cli read-flash-id 1-5
./build/linux-debug/apps/cli/zzrkdevtool-cli inspect-image update.img
./build/linux-debug/apps/cli/zzrkdevtool-cli plan-image update.img
./build/linux-debug/apps/cli/zzrkdevtool-cli inspect-image update.img --json
./build/linux-debug/apps/cli/zzrkdevtool-cli plan-image update.img --json
./build/linux-debug/apps/cli/zzrkdevtool-cli partitions 1-5 --json
./build/linux-debug/apps/cli/zzrkdevtool-cli \
    read-partition 1-5 boot_a --output boot_a.img
./build/linux-debug/apps/cli/zzrkdevtool-cli \
    write-partition 1-5 boot_a --input boot.img --yes
./build/linux-debug/apps/cli/zzrkdevtool-cli \
    erase-partition 1-5 cache --yes
./build/linux-debug/apps/cli/zzrkdevtool-cli \
    flash-image 1-5 update.img --yes --prepare-only
./build/linux-debug/apps/cli/zzrkdevtool-cli \
    flash-image 1-5 update.img --yes --verify critical --json
./build/linux-debug/apps/cli/zzrkdevtool-cli \
    flash-image 1-5 update.img --yes --report upgrade-report.json
./build/linux-debug/apps/cli/zzrkdevtool-cli reset 1-5
./build/linux-debug/apps/gui/ZzRkDevTool
```

Use the stable port path printed by `list` for all device commands. Read-only
commands accept only the observed RK3588 Loader identity `2207:350b`, `bcdUSB
0x0201`, `bcdDevice 0x0223` and exact interface layout. `flash-image` also
accepts the observed RK3588 Maskrom identity `2207:350b`, `bcdUSB 0x0200`,
`bcdDevice 0x0100`. `probe` performs no bulk or control transfer.
`read-chip-info` sends the single fixed transaction recorded in its project
protocol trace and returns an opaque 16-byte value.
`read-flash-info` sends its separately observed fixed transaction, preserves
the 11-byte response and reports the sector-derived capacity. Its other fields
are neutral legacy compatibility codes; it does not determine manufacturer,
physical geometry or storage type. `read-flash-id` preserves its complete
five-byte response and recognizes only the exact observed `EMMC ` marker as
eMMC. That marker is not a CID, manufacturer, capacity or hardware-partition
identifier; every other five-byte response remains opaque.

`info` opens the selected Loader once and returns the device identity,
interface, ChipInfo, capability, FlashInfo and FlashID under one typed result.
`list`, `info`, `inspect-image`, `plan-image`, `partitions`, named partition
operations and `flash-image` provide a versioned JSON contract. A structured
`flash-image` run writes JSON Lines to stdout: zero or more `progress` records
followed by one `result` record. Human logs remain on stderr, so stdout can be
consumed without filtering log text. The current schema version is `1`.

`flash-image --report <file>` atomically writes a schema-versioned diagnostic
report containing the selected USB identity, firmware MD5, immutable flash
plan, verification policy, per-operation timing and terminal result or typed
error. Reports contain only the firmware file name by default. The explicit
`--report-include-firmware-path` option adds the full source path when it is
needed for local diagnostics.

`inspect-image`, `plan-image`, and `flash-image` calculate the RKFW trailing
MD5 with Qt 6 before accepting the image. Before any eMMC write, `flash-image`
enters Maskrom when necessary, downloads the embedded `MiniLoaderAll.bin`
`0x471` DDR and `0x472` usbplug stages to RAM, waits for the same physical USB
port to re-enumerate, and requires typed readiness and RK3588 chip checks. It
then writes the Loader as a New IDBlock at eMMC LBA 64, generated GPT, and every
partition payload actually present in the image. A partition that exists only
in `parameter` and has no image payload is left unchanged. `--prepare-only`
stops after the RAM Loader, chip, eMMC identity, and capacity checks and exits
without writing eMMC; use it before destructive hardware validation.

The default `--verify critical` mode reads back GPT, Loader, and boot-chain
partition payloads. `--verify all` also checks all written Raw and Android
Sparse RAW/FILL extents; Sparse DONT_CARE extents are intentionally skipped.
`--verify none` disables read-back verification. A successful flash resets the
device by default; `--no-reboot` leaves the RAM Loader active. `Ctrl+C` is
observed between RockUSB transfer blocks, stops new storage commands,
suppresses the automatic reset, and exits with status 130.

`partitions` reads the eMMC primary GPT and requires valid header and entry
array CRCs before exposing typed names and ranges. It attempts to validate the
backup copy for diagnostics, but reports it as `unavailable` without rejecting
a valid primary copy on the validated Loader. `read-partition`,
`write-partition`, and `erase-partition` accept only an exact, unambiguous name
from that validated primary GPT. Writes require `--yes`, must fit within the
partition, zero-pad only the final partial sector, and are read back by default;
`--no-verify` explicitly disables that read-back. These commands never select
an eMMC hardware partition and never accept a caller-provided write LBA.

The Qt Widgets application uses the same typed workflows through an injectable
operation backend. Its device page reads ChipInfo, Capability, FlashInfo,
FlashID, eMMC capacity, and the claimed USB interface. The complete-upgrade
page supports asynchronous MD5 and plan preview, verification mode,
post-flash reboot, block-boundary cancellation, cumulative plan progress, and
current-operation progress. The partition page validates GPT before listing
names and LBA ranges, then supports backup, bounded image write with optional
per-block read-back, erase, progress, and cancellation. Partition writes and
erases use separate confirmations that identify the port, partition, LBA range,
and capacity. Device polling is paused while any USB operation owns the device.
The current session retains a bounded 50-entry upgrade history whose completed
rows can export the same redacted JSON report model used by the CLI. Offscreen
automated backend tests cover discovery, device details, partition listing,
upgrade progress, completion, report availability, and history without opening
USB hardware.

`CMakeUserPresets.json` may add workstation-specific convenience presets and is
intentionally ignored by Git. It is not required by the commands above.

## Linux installation package

Build the Ubuntu 26.04 x86_64 Debian package after the release build and tests:

```bash
cpack --preset linux-deb
sudo apt install ./build/linux-release/zzrkdevtool_0.1.0_amd64.deb
```

The package installs the GUI, CLI, a desktop entry, the scoped Rockchip udev
rule, private ABI-compatible Qt/libusb/ICU shared libraries, license texts and
an SPDX component inventory. It creates a dedicated `zzrkdevtool` group; the
udev rule combines that group's `0660` access with desktop `uaccess` and never
uses global `0666`. See `packaging/linux/README.md` for headless membership,
manual rule installation and removal details.

The automated `linux_install_smoke` test installs to a temporary prefix,
starts both installed applications, checks their relative RPATH and packaged
runtime files, then removes the installation tree. Set
`-DZZRK_BUNDLE_RUNTIME=OFF` for an installation whose compatible Qt and libusb
libraries are managed externally.

## Windows and macOS validation packages

The cross-platform application workflow builds and tests a self-contained
Windows x86_64 ZIP and a macOS DMG. The ZIP includes the Qt/MSVC runtimes and
libusb DLL but intentionally does not install or replace a USB driver. Production
RockUSB access requires a project-owned, signed WinUSB INF/CAT package. The
macOS bundle contains both applications, Qt frameworks, and a relocatable
libusb dylib; CI applies only an ad-hoc signature. Developer ID signing,
notarization, and both platforms' RK3588 hardware tests remain release gates.
See `packaging/windows/README.md` and `packaging/macos/README.md` for the build,
driver, signing, and secret-handling policies.

## Safety and current limits

All device operations require the physical USB port path printed by `list`.
Flashing and named partition writes additionally require `--yes`, the
validated Loader identity, the
exact `EMMC ` media marker, sufficient eMMC capacity, a valid firmware MD5,
and a complete typed flash plan. No CLI command exposes raw RockUSB CDB input.

The claim-only probe requires an exact physical port and fails closed on a
partial scan, descriptor change, unexpected interface layout, active kernel
driver, claim failure or release failure. It never selects the first matching
VID/PID implicitly.

Current write support is deliberately limited to complete validated images or
an exact primary-GPT partition name on the observed RK3588
Loader/Maskrom identities and eMMC user area. It does not write eMMC boot0,
boot1, or RPMB. The RK3588 Loader 0x0223 used for validation returns opaque data
when reading the disk-tail backup GPT, so the tool verifies the primary GPT
strictly and reports the backup GPT as write-submitted. Additional Loader
revisions, second-board validation, and Windows/macOS hardware validation remain
future work. Windows/macOS packaging is covered by CI validation artifacts, not
formally signed or notarized releases. The current Debian package is an Ubuntu
26.04 x86_64 baseline rather than a cross-distribution artifact.
