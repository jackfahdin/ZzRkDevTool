# Linux USB Access

The initial Ubuntu rule grants access only to the RK3588S RockUSB identity
observed in both Loader and Maskrom modes. It deliberately excludes the board's
ADB identity and all unverified Rockchip product IDs.

Install and activate it with:

```bash
sudo install -m 0644 packaging/linux/60-zzrkdevtool.rules \
  /etc/udev/rules.d/60-zzrkdevtool.rules
sudo udevadm control --reload-rules
sudo udevadm trigger --subsystem-match=usb \
  --attr-match=idVendor=2207 --attr-match=idProduct=350b
udevadm settle
```

Then reconnect or re-enter Loader/Maskrom and verify the device node has an ACL
for the active user or group read/write access. Do not run ZzRkDevTool as root.

The rule uses `uaccess` for an active desktop session and `plugdev` mode `0660`
for non-desktop development. Release packaging must create or validate the
group instead of assuming every distribution provides it.
