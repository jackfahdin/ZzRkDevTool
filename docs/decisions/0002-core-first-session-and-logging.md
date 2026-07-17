# ADR-0002: Core-First Session And Application Logging

- Status: Accepted
- Date: 2026-07-17

## Context

The project needs a testable RockUSB execution layer before real device
commands or additional GUI workflows are added. It also needs consistent
runtime diagnostics without coupling protocol code to a logging framework.

The local `thirdParty/spdlog` checkout tracks spdlog's `v2.x` branch. At the
selected commit, v2 reports version 2.0.0 but remains unreleased and requires
fmt 12.1.0 as a compiled dependency.

## Decision

1. Development proceeds in this order: portable protocol/session core, fake
   transport tests, Linux libusb backend, CLI and hardware validation, then
   GUI workflows.
2. The existing GUI device-list shell remains a smoke-test consumer. No new
   GUI feature defines or bypasses a core contract.
3. `IUsbTransport` represents one already selected device and endpoint pair.
   Each call maps to exactly one bounded bulk transfer; it does not retry,
   complete short transfers, clear stalls, or reset a device implicitly.
4. The target-private `RockusbSession` executor supports data-in transactions
   only. It owns tags and wire direction/length, executes CBW, data and CSW in
   order, and becomes faulted after any potentially desynchronizing result.
5. Raw command blocks remain outside the public include tree as an internal
   mechanism for future typed read operations. CLI and GUI inputs cannot
   provide raw CDB bytes.
6. Protocol and session libraries return typed results and do not log.
   Application entry points and orchestration use a small `zzrk_logging`
   facade backed by spdlog.
7. CLI result data remains on stdout. Runtime diagnostics go through spdlog to
   stderr so future JSON output remains machine-readable.
8. spdlog is a Git submodule pinned to commit
   `d24088deaa441a79267df8ae3dbc567fbe2a5e03`. Its examples, tests, benchmarks,
   install rules and shared build are disabled. Its own URL and SHA-256 pinned
   fmt 12.1.0 FetchContent declaration is used for application builds.

## Consequences

- Portable builds and session tests do not require Qt, libusb, spdlog, fmt or
  network access.
- A session in the faulted state must be destroyed with its transport before
  another command is attempted.
- A complete, tag-matching command-failed CSW remains synchronized; malformed
  status, short transfer, transport failure or inconsistent residue does not.
- No real command is added until its opcode, CDB, response length and semantics
  are fixed by project-owned RK3588 traces and fixtures.
- Application builds need the spdlog submodule and an initial fmt download.
  If offline application builds become a release requirement, fmt must be
  pinned locally rather than silently falling back to an incompatible system
  version.
