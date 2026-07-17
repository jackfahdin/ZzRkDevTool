# RK3588S Pudu USB Observations

- Observation date: 2026-07-17
- Host: Ubuntu 26.04 LTS x86_64
- Host controller path: `pci-0000:00:14.0-usb-0:5`
- Stable libusb port path: `1-5`
- Board-reported product: `pudu_rk3588s`
- Device serial: intentionally omitted
- Method: host-side `lsusb`, sysfs, udev and ZzRkDevTool read-only operations

No USB interface was opened and no RockUSB command was sent while collecting
the descriptor tables below. Later verification sections explicitly record
the claim-only probe and the three typed, read-only RockUSB transactions.

## ADB Mode

| Field | Observed value |
|---|---|
| VID:PID | `2207:0006` |
| `bcdUSB` | `0x0201` |
| `bcdDevice` | `0x0310` |
| Manufacturer | `rockchip` |
| Product | `pudu_rk3588s` |
| Device class/subclass/protocol | `00/00/00` |
| Interface/alternate setting | `0/0` |
| Interface class/subclass/protocol | `ff/42/01` |
| Bulk OUT | `0x01`, MaxPacket 512 |
| Bulk IN | `0x81`, MaxPacket 512 |
| Negotiated speed | USB 2.0 High Speed, 480 Mbps |
| Linux interface driver | `usbfs` |

The ADB serial was visible to the host and `adb devices -l` identified the
product and model as `pudu_rk3588s`. ZzRkDevTool correctly excluded this
low-byte-only PID from its RockUSB device list.

## Loader Mode

The board was switched manually without moving the cable or host port.

| Field | Observed value |
|---|---|
| VID:PID | `2207:350b` |
| `bcdUSB` | `0x0201` |
| `bcdDevice` | `0x0223` |
| Manufacturer | `Rockchip` |
| Product | `USB download gadget` |
| Device class/subclass/protocol | `00/00/00` |
| Interface/alternate setting | `0/0` |
| Interface class/subclass/protocol | `ff/06/05` |
| Bulk IN | `0x81`, MaxPacket 512 |
| Bulk OUT | `0x02`, MaxPacket 512 |
| Negotiated speed | USB 2.0 High Speed, 480 Mbps |
| Linux interface driver | none |

ZzRkDevTool enumerated the device at port `1-5` as Loader. The ADB and Loader
identities therefore have a project-owned, same-port re-enumeration record.

At observation time `/dev/bus/usb/001/009` was `root:root` with mode `0664` and
no user ACL. Descriptor enumeration worked, but an unprivileged process did not
have the write permission required for future libusb bulk transfers. A scoped
udev rule is required before implementing or testing read-only RockUSB commands.

## Maskrom Mode

The board was switched manually from Loader to Maskrom without moving the cable
or host port. VID:PID remained unchanged; the mode discriminator changed.

| Field | Observed value |
|---|---|
| VID:PID | `2207:350b` |
| `bcdUSB` | `0x0200` |
| `bcdDevice` | `0x0100` |
| Manufacturer/product/serial strings | none |
| Device class/subclass/protocol | `00/00/00` |
| Interface/alternate setting | `0/0` |
| Interface class/subclass/protocol | `ff/06/05` |
| Bulk IN | `0x81`, MaxPacket 512 |
| Bulk OUT | `0x02`, MaxPacket 512 |
| Negotiated speed | USB 2.0 High Speed, 480 Mbps |
| Linux interface driver | none |

ZzRkDevTool enumerated the same `2207:350b` identity at port `1-5` as Maskrom.
This independently confirms the mode rule for this board and firmware pair:
`bcdUSB & 1` is zero in Maskrom (`0x0200`) and one in Loader (`0x0201`).

The Maskrom device node was also `root:root` with mode `0664` and no user ACL.
Because Loader and Maskrom share VID:PID, one exact rule for `2207:350b` can
grant access to both without granting access to the ADB PID `2207:0006`.

## Linux Access Rule Verification

The repository rule `packaging/linux/60-zzrkdevtool.rules` was installed and
reloaded while the board was in Maskrom. The existing node changed to
`root:plugdev` mode `0660`, received the `uaccess` tag and an active-user ACL,
and was readable and writable by the unprivileged development user.

After switching back to Loader on the same port, the newly enumerated node
automatically received the same group, mode, tag and ACL. `lsusb -v` could open
the device as the normal user. No interface was claimed and no command or bulk
payload was sent during this permission verification.

## Claim-Only Probe Verification

The first Linux libusb backend was then validated against the Loader device at
the explicit stable path `1-5`:

```text
Probe succeeded: 1-5 (2207:350b, Loader, bcdDevice 0x0223)
Interface 0, alternate 0, bulk IN 0x81, bulk OUT 0x02, max packet 512/512 bytes
Interface claim and release succeeded; no RockUSB command was sent.
```

Before opening, the backend re-selected the device by bus, complete port chain,
VID, PID, `bcdUSB` and `bcdDevice`, then required the exact observed
configuration and endpoint layout. The explicit release returned success and
the device remained in Loader mode at `1-5`.

A host-side `strace` of the successful probe recorded one
`USBDEVFS_CLAIMINTERFACE` and one `USBDEVFS_RELEASEINTERFACE`. It recorded zero
`USBDEVFS_BULK`, `USBDEVFS_CONTROL`, reset, disconnect, set-interface or
set-configuration ioctls. Therefore this verification exercised USB resource
ownership only; it did not send a RockUSB CBW, CDB, data stage or CSW.

## Typed ReadChipInfo Verification

After two matching reference observations established the wire transaction,
the typed ZzRkDevTool command was run against the same Loader:

```text
Port: 1-5 (bcdDevice 0x0223)
Chip info: 38 38 35 33 00 00 00 00 00 00 00 00 00 00 00 00
```

The retained [protocol evidence](../protocol/rk3588-read-chip-info.md) records
the complete reference and ZzRkDevTool API traces. The project command sent one
31-byte CBW on `0x02`, received 16-byte data and a 13-byte CSW on `0x81`, and
used finite timeouts of 5/30/5 seconds. The CSW matched tag 1 with zero residue
and success status.

The correlated kernel trace contained exactly three bulk URB submissions, one
claim and one successful explicit release. It contained no control, reset,
disconnect, set-interface, set-configuration or clear-halt operation. The
device remained Loader `2207:350b`, `bcdUSB 0x0201`, `bcdDevice 0x0223` at
physical port `1-5`.

The response remains an opaque 16-byte ChipInfo value. Its first bytes resemble
a reversed model string, but no field order, uniqueness or stability is claimed.

## Typed ReadFlashInfo Verification

After two matching reference observations established the 11-byte wire
transaction, the typed ZzRkDevTool command was run against the same Loader:

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

The retained [protocol evidence](../protocol/rk3588-read-flash-info.md) records
the complete reference and ZzRkDevTool API traces. The project command sent one
31-byte CBW on `0x02`, received 11-byte data and a 13-byte CSW on `0x81`, and
used finite timeouts of 5/30/5 seconds. The CSW matched tag 1 with zero residue
and success status.

The correlated kernel trace contained exactly three bulk URB submissions, one
claim and one successful explicit release. It contained no control, reset,
disconnect, set-interface, set-configuration or clear-halt operation. The
device remained Loader `2207:350b`, `bcdUSB 0x0201`, `bcdDevice 0x0223` at
physical port `1-5`.

Only the little-endian sector count is treated as capacity. The remaining
legacy fields are preserved under neutral names and do not establish physical
block/page geometry, manufacturer or storage type. In particular, this command
does not identify the attached storage as eMMC.

## Typed ReadFlashID Verification

After two matching reference observations established the five-byte wire
transaction, the typed ZzRkDevTool command was run against the same Loader:

```text
Port: 1-5 (bcdDevice 0x0223)
Flash ID bytes: 45 4d 4d 43 20
Storage type: eMMC (reported by ReadFlashID)
```

The retained [protocol evidence](../protocol/rk3588-read-flash-id.md) records
the complete reference and ZzRkDevTool API traces. The project command sent one
31-byte CBW on `0x02`, received five-byte data and a 13-byte CSW on `0x81`, and
used finite timeouts of 5/30/5 seconds. The CSW matched tag 1 with zero residue
and success status.

The correlated kernel trace contained exactly three bulk URB submissions, one
claim and one successful explicit release. It contained no control, reset,
disconnect, set-interface, set-configuration or clear-halt operation. The
device remained Loader `2207:350b`, `bcdUSB 0x0201`, `bcdDevice 0x0223` at
physical port `1-5`.

The exact bytes spell the ASCII media marker `EMMC `, including its trailing
space, and support identifying the storage media as eMMC for this exact Loader.
They are not a CID and do not establish manufacturer, capacity, physical
geometry or the active eMMC hardware partition. Unknown five-byte responses
remain opaque and have no guessed media mapping.

## Not Yet Observed

- A root-level usbmon/pcap capture of the ReadChipInfo, ReadFlashInfo or
  ReadFlashID command
- Loader response data for capability
- eMMC CID, physical manufacturer or active hardware-partition information
- Physical geometry or manufacturer semantics for the legacy FlashInfo fields
- Stall, timeout, short transfer and CSW residue behavior on hardware
