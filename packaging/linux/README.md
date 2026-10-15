# Linux Package and USB Access

Both a portable `.tar.gz` and an installable `.deb` are supported. The portable
archive puts the applications at its root, libraries in `lib/`, plugins in
`plugins/`, and documents in `docs/`; the DEB retains the standard `/usr` layout.

## Build the portable archive

```bash
cmake --preset linux-portable-release -DCMAKE_PREFIX_PATH="$HOME/Qt/6.12.0/gcc_64"
cmake --build --preset linux-portable-release
ctest --preset linux-portable-release
cpack --preset linux-tgz
```

The x86_64 output is
`build/linux-portable-release/ZzRkDevTool-0.1.0-linux-x86_64-portable.tar.gz`.
Extract the archive and run `./ZzRkDevTool` from its top-level directory.
The entire directory can be relocated. USB access rules are included under
`usb/` for manual installation; see `README.zh-CN.md` inside the archive.
The host still needs compatible glibc, C++ and graphics system libraries.

## Build the Debian package

The Ubuntu 26.04 x86_64 release package contains both applications, the
compatible Qt platform runtime, source-built static Zzlibusb 1.0.30, ICU,
license texts and an SPDX 2.3 component inventory. Zzlibusb must report hotplug capability;
older or capability-disabled runtimes are rejected without a polling fallback.
Remaining graphics and C/C++ runtime dependencies are recorded automatically
in the Debian control file.

```bash
cmake --preset linux-release -DCMAKE_PREFIX_PATH="$HOME/Qt/6.12.0/gcc_64"
cmake --build --preset linux-release
ctest --test-dir build/linux-release --output-on-failure
cpack --preset linux-deb
sudo apt install ./build/linux-release/zzrkdevtool_0.1.0_amd64.deb
```

The package creates the `zzrkdevtool` system group, installs the rule under
`/usr/lib/udev/rules.d`, reloads udev and provides a desktop entry. Active
desktop users normally receive access through `uaccess`. For a headless or
remote session, add the account explicitly and then log out and back in:

```bash
sudo usermod -aG zzrkdevtool "$USER"
```

Run `ZzRkDevTool` or `zzrkdevtool-cli --version` as the normal user. Remove the
package with `sudo apt remove zzrkdevtool`; the dedicated group is retained so
an upgrade or reinstall does not discard administrator-managed memberships.

Set `-DZZRK_BUNDLE_RUNTIME=OFF` only when creating an installation that will
provide ABI-compatible Qt shared libraries independently. Zzlibusb remains
statically linked in either mode; no separate libusb shared object is needed.

## Install the rule manually

The initial Ubuntu rule grants access only to the RK3588S RockUSB identity
observed in both Loader and Maskrom modes. It deliberately excludes the board's
ADB identity and all unverified Rockchip product IDs.

Install and activate it with:

```bash
sudo groupadd --system --force zzrkdevtool
sudo install -m 0644 packaging/linux/60-zzrkdevtool.rules \
  /etc/udev/rules.d/60-zzrkdevtool.rules
sudo udevadm control --reload-rules
sudo udevadm trigger --subsystem-match=usb \
  --attr-match=idVendor=2207 --attr-match=idProduct=350b
udevadm settle
```

Then reconnect or re-enter Loader/Maskrom and verify the device node has an ACL
for the active user or group read/write access. Do not run ZzRkDevTool as root.

The rule uses `uaccess` for an active desktop session and the dedicated
`zzrkdevtool` group with mode `0660` for non-desktop development. It never
grants global `0666` access.
