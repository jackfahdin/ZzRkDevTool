# RK3588S Read Flash Info Wire Observation

- Observation date: 2026-07-17
- Host: Ubuntu 26.04 LTS x86_64
- Board-reported product: `pudu_rk3588s`
- Stable libusb port path: `1-5`
- USB node during both observations: bus `001`, device `013`
- Device mode: Loader
- VID:PID: `2207:350b`
- `bcdUSB`: `0x0201`
- `bcdDevice`: `0x0223`

This MIT-licensed project document records black-box behavior independently
observed on project-owned hardware. It contains captured bytes and externally
visible behavior only. No source code, data structures, comments or
implementation from the GPL reference program are reproduced here.

## Reference Executable

The reference executable came from the official Rockchip Linux `rkdeveloptool`
source checkout at commit:

```text
304f073752fd25c854e1bcf05d8e7f925b1f4e14
```

The checkout origin was
`https://github.com/rockchip-linux/rkdeveloptool.git`. The executable was built
on the observation host; its digest, rather than an assumed release version,
identifies the exact program used below.

The exact executable used for both observations had this SHA-256 digest:

```text
fe90320782daa079ce71a93cc3f815c87a6eac0e6af1fed9a8d03e3bbf5bde66
```

## Capture Method

The retained [libusb trace helper](../../tools/usbtrace/README.md) intercepted
the public libusb bulk transfer API through `LD_PRELOAD` and recorded direction,
endpoint, requested length, result, transferred length and payload. It was
built with GCC 15.2.0 using the command documented with its source. The exact
recorder shared object used for both runs had SHA-256:

```text
ecf4123b7b0685600b33810561e0aacc97165b48e843de5592dd0d2224f5fe90
```

A simultaneous `strace` recorded Linux usbfs ioctls so the three API-level
transfers could be correlated with the kernel submissions. The invocation shape
is retained with the trace helper; absolute local paths were the only run-time
substitutions.

For both reference observations, the audited command tail after the common
`strace`, `timeout` and trace-helper arguments was exactly:

```text
/home/zz/Jackfahdin/qt/github/rkdeveloptool/rkdeveloptool rfi
```

The absolute checkout prefix is host-specific; the `rfi` argument is part of
the retained observation.

The command was run twice against the same board without moving the USB cable.
The two API traces were byte-for-byte identical and both have this SHA-256
digest:

```text
9d79850da0d1e7d2883d93550569db1918c6327f3c76dc12e07c657b7fb0b300
```

The shared three-line result is retained as the
[committed API trace](traces/rk3588s-pudu-rfi-api.trace). The much larger kernel
traces are not committed; their digests and semantic counts are recorded below.

The kernel traces contain run-specific timestamps, process IDs, addresses and
libusb event activity, so their files are not byte-identical. Their SHA-256
digests are:

| Run | Kernel trace SHA-256 |
|---|---|
| First | `478f64dc3d2b5d6a6254d52204c4b0d463c2fe7af5b2ad9897e3fa8ed853385a` |
| Second | `cdf4f9b601cbd6fab7effaa0129b7f8d1f32814cb527c996c97ebc93e15d76a4` |

`usbmon` was not used because capture required interactive administrator
authentication that was unavailable in this session. Correlating the
project-owned API recorder with the kernel ioctl trace provided two layers of
corroboration for the same runs. It was not an independent second capture, and
the kernel traces are identified by digest rather than retained in the
repository.

## Observed Transfer Sequence

Both runs completed the following exact sequence. Every libusb call returned
success. The reference process requested a 512-byte data buffer but the device
completed that transfer with exactly 11 bytes; the other two transfers completed
their requested lengths.

| Sequence | Direction | Endpoint | Requested | Actual | Timeout argument | Exact payload |
|---|---|---|---:|---:|---:|---|
| 1 | OUT | `0x02` | 31 | 31 | `0 ms` | `55534243ccc864d70b0000008000061a000000000000000000000000000000` |
| 2 | IN | `0x81` | 512 | 11 | `5000 ms` | `0000670700040400280001` |
| 3 | IN | `0x81` | 13 | 13 | `0 ms` | `55534253ccc864d70000000000` |

The timeout values above record the libusb arguments used by the reference
process; they are not timeout policy requirements for ZzRkDevTool. Likewise,
the 512-byte reference receive buffer is compatibility behavior, not evidence
that the response has a 512-byte representation. The observed response is
exactly 11 bytes.

The 31-byte command wrapper decodes as follows:

| Field | Observed bytes/value |
|---|---|
| Signature | `55534243` |
| Tag | `ccc864d7` |
| Expected data length | `0b000000` (11, little-endian) |
| Direction flag | `80` (device to host) |
| LUN | `00` |
| CDB length | `06` |
| Six-byte CDB | `1a0000000000` |
| Remaining wrapper padding | all zero |

The 13-byte status wrapper decodes as follows:

| Field | Observed bytes/value |
|---|---|
| Signature | `55534253` |
| Mirrored tag | `ccc864d7` |
| Residue | `00000000` (zero) |
| Status | `00` (success) |

The reference program printed this result for both runs:

```text
Flash Info:
    Manufacturer: SAMSUNG, value=00
    Flash Size: 60640 MB
    Flash Size: 124190720 Sectors
    Block Size: 512 KB
    Page Size: 2 KB
    ECC Bits: 0
    Access Time: 40
    Flash CS: Flash<0>
```

This output establishes how that executable presented the response. It does not
establish that all labels describe physical properties of the attached storage.

## Kernel Correlation and Side Effects

Each kernel trace contains exactly three `USBDEVFS_SUBMITURB` calls matching the
API sequence above and one `USBDEVFS_CLAIMINTERFACE` call for interface 0. The
device file used for all three submissions was `/dev/bus/usb/001/013`.

Neither run contained a control transfer, device reset, kernel-driver detach,
configuration change, alternate-setting change or clear-halt operation. No
device re-enumeration was observed; the board remained in Loader mode at port
`1-5` after both commands. The reference process closed its device handle after
the command; no explicit release-interface ioctl appeared in either kernel
trace.

These observations establish a read-only transaction for this board and Loader
version. They do not establish error recovery behavior or compatibility with
other chips, Loader versions or USB layouts.

## Eleven-Byte Legacy Compatibility Layout

The complete observed response was:

```text
00 00 67 07 00 04 04 00 28 00 01
```

The stable legacy layout used to preserve these bytes is:

| Offset | Width | Neutral field name | Observed value |
|---:|---:|---|---|
| 0 | 4 | sector count, little-endian | `0x07670000` = 124190720 sectors |
| 4 | 2 | legacy block-size code, little-endian | `0x0400` = 1024 |
| 6 | 1 | legacy page-size/code field | `0x04` |
| 7 | 1 | legacy ECC field | `0x00` |
| 8 | 1 | legacy access-time field | `0x28` = 40 |
| 9 | 1 | legacy manufacturer/code field | `0x00` |
| 10 | 1 | legacy flash-mask/chip-select field | `0x01` |

The sector count is well supported as a capacity value: 124190720 sectors at
512 bytes per sector is 63585648640 bytes, or 60640 MiB. The first typed API may
therefore expose the sector count as capacity while retaining all 11 raw bytes.

The remaining fields are legacy compatibility values. Modern Rockchip Loader
implementations can populate block-device defaults such as block code 1024,
page code 4, ECC 0, access value 40, manufacturer code 0 and mask 1. Some newer
Loaders also overload manufacturer and page fields with flags. Consequently:

- block and page values must not be asserted as physical erase-block or page
  geometry;
- manufacturer code `0x00` must not be mapped to a physical Samsung device;
- access value 40 has no established unit here;
- this response alone must not be used to classify the storage as eMMC.

The typed result should use neutral legacy field names and preserve the complete
raw response. It must not expose a trusted manufacturer enum, storage type,
`isEmmc` flag or physical page/block geometry from this command.

## Media-Type Evidence Boundary

`ReadFlashInfo` does not contain a reliable media-type field. The next command
needed for this board is a separately traced `ReadFlashID` observation. Modern
Loaders are known to return fixed five-byte ASCII media identifiers such as
`EMMC `, `NAND ` or `SNAND`, while older NAND Loaders may return an actual NAND
identifier. Therefore the RK3588S response must be captured before assigning
semantics.

Only a matching, independently observed `ReadFlashID` response can support an
eMMC conclusion for this exact Loader revision. That conclusion must not be
backfilled from the reference program's `SAMSUNG` display or the legacy block
and page compatibility values above.

## Golden Vector Normalization

The captured tag `ccc864d7` was chosen by the reference process and is not a
fixed command constant. ZzRkDevTool allocates session tags starting at 1, so a
golden test should normalize only bytes 4 through 7 of both wrappers:

- command wrapper tag: `01000000`
- status wrapper mirrored tag: `01000000`

The signatures, transfer length, flags, LUN, CDB length, complete CDB, padding,
11-byte response, residue and status must otherwise match the captured values
exactly. A strict implementation may receive exactly 11 bytes for this validated
Loader identity; broader variable-length behavior requires separate evidence.

## ZzRkDevTool Validation

The first typed implementation was run once against the same Loader identity
and physical port with this exact command:

```text
/home/zz/Jackfahdin/qt/github/ZzRkDevTool/build/local-linux-debug/apps/cli/zzrkdevtool-cli read-flash-info 1-5
```

Its retained [API trace](traces/rk3588s-pudu-zzrk-rfi-api.trace) has SHA-256:

```text
a545dc3d514f18dc087892cac7dbe123a1145e8ff4f28eabaacb6b00e41fd730
```

The CLI printed:

```text
Port: 1-5 (bcdDevice 0x0223)
Sector count: 124190720
Capacity bytes: 63585648640
Legacy block-size code: 1024
Legacy page-size code: 4
Legacy ECC field: 0
Legacy access-time field: 40
Legacy manufacturer/code: 0x00
Legacy flash-mask field: 0x01
Storage type: not determined by ReadFlashInfo
Raw bytes: 00 00 67 07 00 04 04 00 28 00 01
```

It sent the normalized 31-byte tag-1 command wrapper, received exactly the
observed 11-byte response and then received a 13-byte tag-1 success status with
zero residue. Its finite timeout arguments were 5000 ms for command, 30000 ms
for data and 5000 ms for status.

The correlated kernel trace had SHA-256:

```text
4e3441e3ba0038ddfdc0f6ce38752038c9c7c346e95ab6fbdc26c6ff9d23aadb
```

It recorded exactly three `USBDEVFS_SUBMITURB` calls, one interface claim and
one successful explicit interface release. It contained no control, reset,
disconnect, set-interface, set-configuration or clear-halt ioctl. The device
remained Loader `2207:350b`, `bcdUSB 0x0201`, `bcdDevice 0x0223` at physical
port `1-5` after the command.

The project output deliberately treats only the sector count as capacity. It
preserves and neutrally labels the remaining compatibility fields, and states
that storage type is not determined. This hardware validation therefore does
not establish eMMC media, physical geometry or manufacturer identity.
