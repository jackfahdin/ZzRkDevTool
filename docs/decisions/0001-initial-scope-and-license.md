# ADR-0001: Initial Product Scope And License

- Status: Accepted
- Date: 2026-07-16

## Context

ZzRkDevTool will be a C++20 and Qt 6 Rockchip flashing tool. Development starts
on the current Ubuntu 26.04 LTS x86_64 workstation, while the final architecture
must remain suitable for Windows and macOS.

The local `temp/` directory contains GPL and other third-party reference
projects. The new project must use the MIT License, so those implementations
cannot become a source-code base for the product.

## Decision

1. Project-authored code and documentation use the MIT License.
2. GPL reference implementations are external behavior oracles only. Product
   code will not copy, translate, modify, link, compile, or distribute them.
3. Protocol specifications and golden tests will be independently written from
   public facts, USB traces, controlled device observations, and legally usable
   test fixtures. Each non-trivial fact or fixture records its provenance.
4. The first supported hardware combination is RK3588 with eMMC user-area
   storage.
5. The first host target is Ubuntu 26.04 LTS x86_64. Windows and macOS follow
   after the Linux MVP.
6. Cross-platform boundaries exist from the start: portable core libraries,
   `IUsbTransport`, `IPlatformServices`, and separate Linux, Windows, and macOS
   source sets.
7. Qt and libusb remain third-party dependencies under their own licenses.
   Dynamic linking is preferred where it simplifies LGPL compliance.

## Consequences

- `temp/` is ignored by the root repository and excluded from every product
  target and release package.
- Linux-specific udev, path, permission, and system-integration code cannot be
  added to protocol, image, core, or workflow libraries.
- Windows/macOS compile-only CI starts with the portable core even before USB
  hardware validation is scheduled on those platforms.
- Supporting another SoC or storage medium requires an explicit compatibility
  decision and hardware-test entry; it is not implied by protocol similarity.
- eMMC boot0/boot1 and RPMB, full `update.img` scope, the Windows driver model,
  macOS architectures, and mass-production requirements remain separate
  decisions.
