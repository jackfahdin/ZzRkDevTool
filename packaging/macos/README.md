# macOS packaging and signing policy

The macOS release baseline is a DMG containing `ZzRkDevTool.app`. The CLI is
installed in the same bundle under `Contents/MacOS` so it shares the deployed
Qt frameworks. Zzlibusb is statically built from its pinned source submodule
for the selected architecture. macOS does not require
a separately installed kernel extension or USB driver.

## Build

Configure with a Qt kit supporting the selected architecture. No Homebrew
libusb installation is needed. Zzlibusb must report `LIBUSB_CAP_HAS_HOTPLUG`;
unavailable hotplug is not replaced by device polling:

```bash
cmake --preset macos-arm64-release \
  -DCMAKE_PREFIX_PATH="$HOME/Qt/6.12.0/macos"
cmake --build --preset macos-arm64-release --parallel
ctest --preset macos-arm64-release
cpack --preset macos-arm64-dmg
```

The CI package uses `-DZZRK_MACOS_CODESIGN_IDENTITY=-` for an ad-hoc signature.
That lets CI verify the final bundle after Qt deployment, but it is
not a trusted distribution signature and is not notarized. An unsigned or
ad-hoc-signed DMG is suitable only for CI and local validation. For a release,
pass a Developer ID Application identity at configure time so `macdeployqt`
enables the hardened runtime and signs the completed bundle:

```bash
cmake --preset macos-arm64-release \
  -DZZRK_MACOS_CODESIGN_IDENTITY="Developer ID Application: Example (TEAMID)"
```

## Notarization gate

After CPack creates the DMG, submit and staple it with credentials stored in
the developer keychain or CI secret store:

```bash
xcrun notarytool submit ZzRkDevTool-0.1.0-macos-arm64.dmg \
  --keychain-profile ZZRK_NOTARY --wait
xcrun stapler staple ZzRkDevTool-0.1.0-macos-arm64.dmg
spctl --assess --type open --context context:primary-signature -v \
  ZzRkDevTool-0.1.0-macos-arm64.dmg
```

Signing identities, App Store Connect keys, passwords, and notarization
profiles must never be committed or printed by CI. USB behavior still requires
validation on real Apple Silicon and Intel hosts before a public release.
