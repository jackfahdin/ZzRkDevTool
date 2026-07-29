# Windows packaging and driver policy

The Windows release baseline is a self-contained x86_64 ZIP produced by CPack.
It contains the GUI, CLI, Qt runtime, MSVC runtime, libusb DLL, Qt plugins, and
license texts. It is intentionally an unsigned validation artifact until the
project has an Authenticode certificate and a signed driver package.

## Build

Use an MSVC x64 developer shell with Qt 6.11.1 and a dynamic libusb 1.0.30 or
newer installation. Windows libusb must be built with
`LIBUSB_WINDOWS_HOTPLUG`; a stock library that reports no hotplug capability is
not supported. The repository's vcpkg overlay pins 1.0.30 and passes
`EnableWindowsHotplug=true` to the upstream MSBuild project:

```powershell
& "$env:VCPKG_ROOT\vcpkg.exe" install libusb:x64-windows `
  "--overlay-ports=$PWD/packaging/windows/vcpkg-overlay-ports"
```

`ZZRK_LIBUSB_ROOT` must contain:

```text
include/libusb-1.0/libusb.h
lib/libusb-1.0.lib
bin/libusb-1.0.dll
lib/pkgconfig/libusb-1.0.pc
```

Then configure, test, and package:

```powershell
cmake -S . -B build/windows-release -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  -DZZRK_LIBUSB_ROOT=C:/path/to/libusb `
  -DZZRK_BUNDLE_RUNTIME=ON `
  -DZZRK_WARNINGS_AS_ERRORS=ON
cmake --build build/windows-release --parallel
ctest --test-dir build/windows-release --output-on-failure
cpack --config build/windows-release/CPackConfig.cmake -G ZIP
```

## USB driver

RockUSB access through libusb on Windows requires a compatible WinUSB driver
bound to `USB\VID_2207&PID_350B`. A production installer must use a project-owned
INF/CAT package signed by a trusted code-signing identity. The application ZIP
does not silently replace a device driver and does not ship an unsigned INF.

For isolated development hardware, a developer may bind WinUSB manually and
verify the resulting device instance in Device Manager. This can interfere
with Rockchip vendor tools that expect another driver, so the original driver
must be recorded before changing it. Driver installation and rollback belong
to the hardware validation checklist, not the application startup path.

## Signing gate

Before a public release, sign every distributed EXE and DLL with SHA-256 and a
timestamp, then rebuild the archive and verify it on a clean Windows machine:

```powershell
signtool sign /fd SHA256 /td SHA256 /tr https://timestamp.example.invalid `
  /a path/to/ZzRkDevTool.exe
signtool verify /pa /all /v path/to/ZzRkDevTool.exe
```

The timestamp URL and certificate source above are placeholders by design.
Certificates, passwords, tokens, and private keys must remain outside the
repository and CI logs.
