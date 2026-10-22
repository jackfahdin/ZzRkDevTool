# Third-Party and Research Material Notice

Project-authored source code and documentation are licensed under the MIT
License in [`LICENSE`](LICENSE).

The local `temp/` directory contains third-party research snapshots. Those
files retain their own copyright and license terms. They are not part of the
ZzRkDevTool product source, are not covered by this project's MIT License, and
must not be compiled, linked, copied into, or distributed with the product.

Runtime and build dependencies such as Qt and libusb retain their respective
licenses. Release packages must include a generated third-party component list,
the applicable license texts, source/relink information required by those
licenses, and an SBOM.

The GUI statically links ZzPureTools from commit
`2bad5099d806bd3d637f270d4f93b50054bc9bfe` of
`git@github.com:jackfahdin/ZzPureTools.git`. Its bundled ZzLog and QWindowKit
components are part of that same source revision. They are not packaged as
runtime shared libraries; their license texts are installed with the product.

Windows driver management dynamically loads libwdi v1.5.1, revision
`9b23b82a2dd1cbffc16d46c212f92c6bf8c0c602`, from
<https://github.com/pbatard/libwdi>. libwdi is LGPL-3.0-or-later;
its upstream installers and other files retain their applicable licenses.
The package includes the complete pinned source, build/adaptation scripts,
COPYING and COPYING-LGPL under `share/licenses/libwdi/`. Build adaptations
use a static Release CRT and Administrators ownership for extracted resources;
the unmodified source plus corresponding recipe are supplied for rebuilding.
The dynamically loaded DLL may be rebuilt and replaced by the user.
Embedded Microsoft WDF/WinUSB redistributables retain Microsoft's license,
shipped with the redistribution file lists in the same directory. They are
not licensed under this project's MIT License.
