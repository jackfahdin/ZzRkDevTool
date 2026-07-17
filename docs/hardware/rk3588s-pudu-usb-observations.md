# RK3588S Pudu USB Observations

- Observation date: 2026-07-17
- Host: Ubuntu 26.04 LTS x86_64
- Host controller path: `pci-0000:00:14.0-usb-0:5`
- Stable libusb port path: `1-5`
- Board-reported product: `pudu_rk3588s`
- Device serial: intentionally omitted
- Method: host-side `lsusb`, sysfs, udev and ZzRkDevTool read-only enumeration

No USB interface was opened and no RockUSB command was sent while collecting
these observations.

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

## Not Yet Observed

- Project-owned USB packet capture for any RockUSB command
- Loader response data for chip, flash or capability information
- Stall, timeout, short transfer and CSW residue behavior on hardware
