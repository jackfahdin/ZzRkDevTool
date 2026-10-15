# Windows packaging and driver policy

The Windows release baseline is a self-contained x86_64 TGZ (`.tar.gz`) produced by CPack.
It contains the GUI, CLI, Qt runtime, MSVC runtime, Qt plugins, and
license texts. It is intentionally an unsigned validation artifact until the
project has an Authenticode certificate and a signed driver package.

## Build

Use an MSVC x64 developer shell with an ABI-compatible Qt kit. Zzlibusb is
built statically from its pinned submodule with Windows hotplug enabled;
vcpkg and a separate libusb DLL are no longer required. The Ninja presets
use the compiler from the developer shell:

```powershell
$env:QT_PREFIX_PATH = "D:/SoftWare/Qt/6.12.0/msvc2022_64"
cmake --preset windows-msvc-release
cmake --build --preset windows-msvc-release --parallel
ctest --preset windows-msvc-release
cpack --preset windows-msvc-tgz
```

The executables, Qt DLLs and `qt.conf` are placed at the package root;
plugins remain under `plugins/`. Do not move an executable out of this tree.
For Visual Studio project files use `windows-vs2022-release` and
`windows-vs2022-tgz`, or the corresponding `vs2026` presets (CMake >= 4.2).
Each generator has its own build directory.

## USB driver

RockUSB access through libusb on Windows requires a compatible WinUSB driver
bound to `USB\VID_2207&PID_350B`. A production installer must use a project-owned
INF/CAT package signed by a trusted code-signing identity. The application TGZ
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
