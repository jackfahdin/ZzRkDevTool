# RK3588S Read Flash ID Wire Observation

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
/home/zz/Jackfahdin/qt/github/rkdeveloptool/rkdeveloptool rid
```

The absolute checkout prefix is host-specific; the `rid` argument is part of
the retained observation.

The command was run twice against the same board without moving the USB cable.
The two API traces were byte-for-byte identical and both have this SHA-256
digest:

```text
d887244d72b3dea34ccf73ea4815899fd522160eaa75cbd99e845aae79bd9c0c
```

The shared three-line result is retained as the
[committed API trace](traces/rk3588s-pudu-rid-api.trace). The much larger kernel
traces are not committed; their digests and semantic counts are recorded below.

The kernel traces contain run-specific timestamps, process IDs, addresses and
libusb event activity, so their files are not byte-identical. Their SHA-256
digests are:

| Run | Kernel trace SHA-256 |
|---|---|
| First | `8e7c1671ad3b1f534a46f70a9de54a1a7bc13804b5c7e56a87a01a1d8fd79bdf` |
| Second | `d7e70448135eef1aca26e31cefe73be8b5000541dc5cf931d312cda218b22172` |

`usbmon` was not used because capture required interactive administrator
authentication that was unavailable in this session. Correlating the
project-owned API recorder with the kernel ioctl trace provided two layers of
corroboration for the same runs. It was not an independent second capture, and
the kernel traces are identified by digest rather than retained in the
repository.

## Observed Transfer Sequence

Both runs completed the following exact sequence. Every libusb call returned
success and reported the requested number of bytes transferred.

| Sequence | Direction | Endpoint | Length | Timeout argument | Exact payload |
|---|---|---|---:|---:|---|
| 1 | OUT | `0x02` | 31 | `0 ms` | `55534243ccc864d70500000080000601000000000000000000000000000000` |
| 2 | IN | `0x81` | 5 | `0 ms` | `454d4d4320` |
| 3 | IN | `0x81` | 13 | `0 ms` | `55534253ccc864d70000000000` |

The zero timeouts above record the libusb arguments used by the reference
process; they are not timeout policy requirements for ZzRkDevTool.

The 31-byte command wrapper decodes as follows:

| Field | Observed bytes/value |
|---|---|
| Signature | `55534243` |
| Tag | `ccc864d7` |
| Expected data length | `05000000` (5, little-endian) |
| Direction flag | `80` (device to host) |
| LUN | `00` |
| CDB length | `06` |
| Six-byte CDB | `010000000000` |
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
Flash ID: 45 4D 4D 43 20
```

The five displayed bytes exactly match the data-stage payload.

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

## Response Interpretation Boundary

The complete observed response was:

```text
45 4d 4d 43 20
```

All five bytes are printable ASCII and spell `EMMC `, including the trailing
space. For this RK3588S board under the exact Loader identity recorded above,
that response is direct evidence that the Loader reports the active storage
media marker as eMMC.

The response is not an eMMC CID and does not identify a physical manufacturer,
capacity, geometry, hardware partition or user-data partition. Those properties
require their own protocol evidence. The eMMC interpretation is also scoped to
the exact observed five-byte response and Loader identity; it must not be
inferred from `ReadFlashInfo` compatibility fields.

Other five-byte `ReadFlashID` responses remain opaque until their wire bytes and
semantics are independently observed. In particular, the implementation must
not add guessed NAND, SPI NAND or other media mappings from this observation.
It should preserve the complete five-byte value even when it recognizes the
exact `EMMC ` marker.

## Golden Vector Normalization

The captured tag `ccc864d7` was chosen by the reference process and is not a
fixed command constant. ZzRkDevTool allocates session tags starting at 1, so a
golden test should normalize only bytes 4 through 7 of both wrappers:

- command wrapper tag: `01000000`
- status wrapper mirrored tag: `01000000`

The signatures, transfer length, flags, LUN, CDB length, complete CDB, padding,
five-byte response, residue and status must otherwise match the captured values
exactly. The normalized command wrapper is therefore:

```text
55534243010000000500000080000601000000000000000000000000000000
```

The normalized success status wrapper is:

```text
55534253010000000000000000
```

## ZzRkDevTool Validation

The final typed implementation was run against the same Loader identity and
physical port with this exact command:

```text
/home/zz/Jackfahdin/qt/github/ZzRkDevTool/build/local-linux-debug/apps/cli/zzrkdevtool-cli read-flash-id 1-5
```

Its retained [API trace](traces/rk3588s-pudu-zzrk-rid-api.trace) has SHA-256:

```text
e4dc3834d4937bb8b1c4e1a0fde294b32cd2e76d8345e771fb84aab2e34f7b63
```

The CLI printed:

```text
Port: 1-5 (bcdDevice 0x0223)
Flash ID bytes: 45 4d 4d 43 20
Storage type: eMMC (reported by ReadFlashID)
```

It sent the normalized 31-byte tag-1 command wrapper, received exactly the
observed five-byte response and then received a 13-byte tag-1 success status
with zero residue. Its finite timeout arguments were 5000 ms for command,
30000 ms for data and 5000 ms for status.

The correlated kernel trace had SHA-256:

```text
34c621429a266f6745bcdb8db49e7fa34d1e32063712d4b25c8d115baeb7472a
```

It recorded exactly three `USBDEVFS_SUBMITURB` calls, one interface claim and
one successful explicit interface release. It contained no control, reset,
disconnect, set-interface, set-configuration or clear-halt ioctl. The device
remained Loader `2207:350b`, `bcdUSB 0x0201`, `bcdDevice 0x0223` at physical
port `1-5` after the command.

The project recognizes only the exact `EMMC ` response as an eMMC media marker.
It retains all five bytes, and unknown responses remain opaque. This validation
does not establish an eMMC CID, physical manufacturer, capacity, geometry or
active hardware partition.
