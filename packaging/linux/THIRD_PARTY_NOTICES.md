# Runtime Components and Relinking

ZzRkDevTool's own source code is distributed under the MIT License. The Linux
package uses the following independently licensed components:

| Component | Baseline | License | Source |
|---|---:|---|---|
| Qt Base (Core, Concurrent, DBus, Gui, Widgets and plugins) | Qt kit selected at build time, >= 6.8 | LGPL-3.0-only or commercial | https://code.qt.io/cgit/qt/qtbase.git/ |
| Zzlibusb (statically linked libusb) | 1.0.30, pinned submodule | LGPL-2.1-or-later | https://gitcode.com/JackfahdinQt/Zzlibusb |
| ICU | 73.2 | Unicode-DFS-2016 and bundled component terms | https://github.com/unicode-org/icu/releases/tag/release-73-2 |
| spdlog | pinned git submodule | MIT | https://github.com/gabime/spdlog |
| fmt | 12.1.0, fetched by spdlog | MIT | https://github.com/fmtlib/fmt/releases/tag/12.1.0 |

The package keeps Qt and ICU as shared objects under `/usr/lib/zzrkdevtool`.
A recipient can replace those files with ABI-compatible modified builds.
Zzlibusb is statically incorporated into the executables; its source is in
`thirdParty/Zzlibusb`, and modified versions can be rebuilt with the project.
The complete build instructions are in `docs/BUILD_AND_PACKAGE.zh-CN.md`.
`ZZRK_BUNDLE_RUNTIME=OFF` disables copying Qt runtime libraries but does not
change USB linkage. The corresponding license texts are installed beside this notice.

The GUI statically links the following components from ZzPureTools commit
`2bad5099d806bd3d637f270d4f93b50054bc9bfe`, sourced from
`git@github.com:jackfahdin/ZzPureTools.git`. They are not runtime shared
libraries in the package.

| Component | Version source | License | Installed license path |
|---|---|---|---|
| ZzPureTools | commit `2bad5099d806bd3d637f270d4f93b50054bc9bfe` | MIT | `/usr/share/doc/zzrkdevtool/licenses/ZzPureTools/ZzPureTools-MIT.txt` |
| ZzLog | vendored at ZzPureTools commit `2bad5099d806bd3d637f270d4f93b50054bc9bfe` | MIT | `/usr/share/doc/zzrkdevtool/licenses/ZzLog/LICENSE` |
| QWindowKit | vendored at ZzPureTools commit `2bad5099d806bd3d637f270d4f93b50054bc9bfe` | Apache-2.0 | `/usr/share/doc/zzrkdevtool/licenses/QWindowKit/QWindowKit-Apache-2.0.txt` |

The generated Debian dependency list covers the remaining operating-system
libraries. Those libraries are not copied into this package and retain the
licenses supplied by the Linux distribution.
