// SPDX-License-Identifier: MIT

#pragma once

#include <iosfwd>
#include <string_view>

#include <zzrk/transport/UsbDeviceScanner.hpp>

namespace zzrk::cli {

inline constexpr int kFatalScanExitCode = 1;
inline constexpr int kPartialScanExitCode = 2;

[[nodiscard]] int printScanResult(
    const transport::ScanResult& result,
    std::ostream& output,
    std::ostream& errorOutput,
    std::string_view usbAccessHelp);

[[nodiscard]] int printScanResultJson(
    const transport::ScanResult& result,
    std::ostream& output,
    std::string_view usbAccessHelp);

} // namespace zzrk::cli
