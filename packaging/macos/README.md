# macOS packaging and signing policy

The macOS release baseline is a DMG containing `ZzRkDevTool.app`. The CLI is
installed in the same bundle under `Contents/MacOS` so it shares the deployed
Qt frameworks and libusb dylib. macOS uses libusb directly and does not require
a separately installed kernel extension or USB driver.

## Build

Configure with Qt 6.11.1 and a dynamic libusb 1.0.30 or newer installation.
The runtime must report `LIBUSB_CAP_HAS_HOTPLUG`; older or capability-disabled
libraries are rejected and are not replaced by device polling:

```bash
cmake -S . -B build/macos-release -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$HOME/Qt/6.11.1/macos" \
  -DZZRK_LIBUSB_ROOT="$(brew --prefix libusb)" \
  -DZZRK_BUNDLE_RUNTIME=ON \
  -DZZRK_WARNINGS_AS_ERRORS=ON
cmake --build build/macos-release --parallel
ctest --test-dir build/macos-release --output-on-failure
cpack --config build/macos-release/CPackConfig.cmake -G DragNDrop
```

The CI package uses `-DZZRK_MACOS_CODESIGN_IDENTITY=-` for an ad-hoc signature.
That lets CI verify the final bundle after Qt and libusb deployment, but it is
not a trusted distribution signature and is not notarized. An unsigned or
ad-hoc-signed DMG is suitable only for CI and local validation. For a release,
pass a Developer ID Application identity at configure time so `macdeployqt`
enables the hardened runtime and signs the completed bundle:

```bash
cmake -S . -B build/macos-release \
  -DZZRK_MACOS_CODESIGN_IDENTITY="Developer ID Application: Example (TEAMID)"
```

## Notarization gate

After CPack creates the DMG, submit and staple it with credentials stored in
the developer keychain or CI secret store:

```bash
xcrun notarytool submit zzrkdevtool-0.1.0-macos.dmg \
  --keychain-profile ZZRK_NOTARY --wait
xcrun stapler staple zzrkdevtool-0.1.0-macos.dmg
spctl --assess --type open --context context:primary-signature -v \
  zzrkdevtool-0.1.0-macos.dmg
```

Signing identities, App Store Connect keys, passwords, and notarization
profiles must never be committed or printed by CI. USB behavior still requires
validation on real Apple Silicon and Intel hosts before a public release.
