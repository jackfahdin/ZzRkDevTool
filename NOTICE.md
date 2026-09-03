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
`d6b90f1d266b6aa97b0ffb3a75deaa4bc2314514` of
`git@github.com:jackfahdin/ZzPureTools.git`. Its bundled ZzLog and QWindowKit
components are part of that same source revision. They are not packaged as
runtime shared libraries; their license texts are installed with the product.
