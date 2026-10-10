# Runtime Components and Relinking

ZzRkDevTool's own source code is distributed under the MIT License. The Linux
package dynamically links the following independently licensed components:

| Component | Baseline | License | Source |
|---|---:|---|---|
| Qt Base (Core, Concurrent, DBus, Gui, Widgets and plugins) | 6.11.1 | LGPL-3.0-only or commercial | https://code.qt.io/cgit/qt/qtbase.git/tag/?h=v6.11.1 |
| libusb | 1.0.30 | LGPL-2.1-or-later | https://github.com/libusb/libusb/releases/tag/v1.0.30 |
| ICU | 73.2 | Unicode-DFS-2016 and bundled component terms | https://github.com/unicode-org/icu/releases/tag/release-73-2 |
| spdlog | pinned git submodule | MIT | https://github.com/gabime/spdlog |
| fmt | 12.1.0, fetched by spdlog | MIT | https://github.com/fmtlib/fmt/releases/tag/12.1.0 |

The package keeps Qt, libusb and ICU as shared objects under
`/usr/lib/zzrkdevtool`; they are not statically incorporated into the
executables. A recipient can replace those files with ABI-compatible modified
builds. The complete project build instructions are in `README.md`; configure
with `ZZRK_BUNDLE_RUNTIME=OFF` to link and install without copying runtime
libraries. The corresponding license texts are installed beside this notice.

The GUI statically links the following components from ZzPureTools commit
`38e96bb08a44c838e2748979a9683c085fcdb9e1`, sourced from
`git@github.com:jackfahdin/ZzPureTools.git`. They are not runtime shared
libraries in the package.

| Component | Version source | License | Installed license path |
|---|---|---|---|
| ZzPureTools | commit `38e96bb08a44c838e2748979a9683c085fcdb9e1` | MIT | `/usr/share/doc/zzrkdevtool/licenses/ZzPureTools/ZzPureTools-MIT.txt` |
| ZzLog | vendored at ZzPureTools commit `38e96bb08a44c838e2748979a9683c085fcdb9e1` | MIT | `/usr/share/doc/zzrkdevtool/licenses/ZzLog/LICENSE` |
| QWindowKit | vendored at ZzPureTools commit `38e96bb08a44c838e2748979a9683c085fcdb9e1` | Apache-2.0 | `/usr/share/doc/zzrkdevtool/licenses/QWindowKit/QWindowKit-Apache-2.0.txt` |

The generated Debian dependency list covers the remaining operating-system
libraries. Those libraries are not copied into this package and retain the
licenses supplied by the Linux distribution.
