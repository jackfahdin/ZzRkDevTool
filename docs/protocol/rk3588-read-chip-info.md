# RK3588S Read Chip Info Wire Observation

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
/home/zz/Jackfahdin/qt/github/rkdeveloptool/rkdeveloptool rci
```

For the ZzRkDevTool validation recorded below, it was exactly:

```text
/home/zz/Jackfahdin/qt/github/ZzRkDevTool/build/local-linux-debug/apps/cli/zzrkdevtool-cli read-chip-info 1-5
```

The absolute checkout prefixes are host-specific; the `rci` and
`read-chip-info 1-5` arguments are part of the retained observation.

The command was run twice against the same board without moving the USB cable.
The two API traces were byte-for-byte identical and both have this SHA-256
digest:

```text
5fa2de938ca02bb0760274fa1ca7bdd98df576c589a6b8e96bf017fa0220cc42
```

The shared three-line result is retained as the
[committed API trace](traces/rk3588s-pudu-rci-api.trace). The much larger kernel
traces are not committed; their digests and semantic counts are recorded below.

The kernel traces contain run-specific timestamps, process IDs, addresses and
libusb event activity, so their files are not byte-identical. Their SHA-256
digests are:

| Run | Kernel trace SHA-256 |
|---|---|
| First | `fbe1bf291698bd1937f271f75431dbce4d4ad7d9decf39016de91124f401dc77` |
| Second | `5f329c3eddbf3415bdadb6f471cca8a6a44d2717de4ed3c7285de44499d299a6` |

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
| 1 | OUT | `0x02` | 31 | `0 ms` | `55534243ccc864d7100000008000061b000000000000000000000000000000` |
| 2 | IN | `0x81` | 16 | `0 ms` | `38383533000000000000000000000000` |
| 3 | IN | `0x81` | 13 | `0 ms` | `55534253ccc864d70000000000` |

The zero timeout above records the libusb argument used by the reference
process; it is not a timeout policy requirement for ZzRkDevTool.

The 31-byte command wrapper decodes as follows:

| Field | Observed bytes/value |
|---|---|
| Signature | `55534243` |
| Tag | `ccc864d7` |
| Expected data length | `10000000` (16, little-endian) |
| Direction flag | `80` (device to host) |
| LUN | `00` |
| CDB length | `06` |
| Six-byte CDB | `1b0000000000` |
| Remaining wrapper padding | all zero |

The 13-byte status wrapper decodes as follows:

| Field | Observed bytes/value |
|---|---|
| Signature | `55534253` |
| Mirrored tag | `ccc864d7` |
| Residue | `00000000` (zero) |
| Status | `00` (success) |

The bytes displayed under the reference program's `Chip Info` label were
exactly `38383533000000000000000000000000`, matching the data-stage payload.
That label establishes the command's externally visible purpose, but it does
not establish the internal meaning of any response byte.

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

The response is an opaque 16-byte value in the first implementation:

```text
38383533000000000000000000000000
```

Its first four bytes are printable ASCII `8853`; the remaining twelve observed
bytes are zero. No field meaning, byte order, stability or uniqueness has been
established. In particular, the value must not be named or presented as a UID,
serial number or security identity without independent evidence from additional
boards and documented protocol semantics.

The initial typed API should therefore return the complete 16 bytes without
parsing subfields.

## Golden Vector Normalization

The captured tag `ccc864d7` was chosen by the reference process and is not a
fixed command constant. ZzRkDevTool allocates session tags starting at 1, so a
golden test should normalize only bytes 4 through 7 of both wrappers:

- command wrapper tag: `01000000`
- status wrapper mirrored tag: `01000000`

The signatures, transfer length, flags, LUN, CDB length, complete CDB, padding,
16-byte response, residue and status must otherwise match the captured values
exactly.

## ZzRkDevTool Validation

The first typed implementation was run once against the same Loader identity
and physical port. Its retained
[API trace](traces/rk3588s-pudu-zzrk-rci-api.trace) has SHA-256:

```text
efa478284b66212c67c7455dfc9eb571c1eb6b9aad689e43510364469635023a
```

It sent the normalized tag-1 command wrapper, received the same 16-byte data
payload, and received a tag-1 success status with zero residue. Its timeout
arguments were the finite project defaults: 5000 ms for command, 30000 ms for
data and 5000 ms for status.

The correlated kernel trace had SHA-256
`fbfb04c88a536e746146d3187053cb93c31653018bc1a0bb1c81cc5770275dce`.
It recorded exactly three `USBDEVFS_SUBMITURB` calls, one interface claim and
one successful explicit interface release. It contained no control, reset,
disconnect, set-interface, set-configuration or clear-halt ioctl. The device
remained at port `1-5` in Loader mode with `bcdDevice 0x0223` after the command.
