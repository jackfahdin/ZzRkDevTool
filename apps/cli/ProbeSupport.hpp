// SPDX-License-Identifier: MIT

#pragma once

#include <zzrk/transport/LibusbBulkTransport.hpp>
#include <zzrk/transport/UsbDeviceScanner.hpp>

#include <iosfwd>
#include <string_view>

namespace zzrk::cli {

enum class ProbeSelectionError {
    none,
    scanFailed,
    notFound,
    ambiguous,
    unsupported,
};

struct ProbeSelectionResult {
    const transport::UsbDeviceInfo* device{};
    ProbeSelectionError error{ProbeSelectionError::notFound};

    [[nodiscard]] explicit constexpr operator bool() const noexcept
    {
        return device != nullptr && error == ProbeSelectionError::none;
    }
};

[[nodiscard]] ProbeSelectionResult
selectProbeDevice(const transport::ScanResult& scan,
                  std::string_view portPath) noexcept;

void printProbeSuccess(const transport::UsbDeviceInfo& device,
                       const transport::LibusbInterfaceInfo& interfaceInfo,
                       std::ostream& output);

} // namespace zzrk::cli
