// SPDX-License-Identifier: MIT

#pragma once

#include <zzrk/session/ReadChipInfo.hpp>
#include <zzrk/session/ReadCapability.hpp>
#include <zzrk/session/ReadFlashId.hpp>
#include <zzrk/session/ReadFlashInfo.hpp>
#include <zzrk/transport/LibusbBulkTransport.hpp>
#include <zzrk/transport/UsbDeviceScanner.hpp>

#include <iosfwd>
#include <string_view>

namespace zzrk::cli {

enum class LoaderSelectionError {
    none,
    scanFailed,
    notFound,
    ambiguous,
    unsupported,
};

struct LoaderSelectionResult {
    const transport::UsbDeviceInfo* device{};
    LoaderSelectionError error{LoaderSelectionError::notFound};

    [[nodiscard]] explicit constexpr operator bool() const noexcept
    {
        return device != nullptr && error == LoaderSelectionError::none;
    }
};

[[nodiscard]] LoaderSelectionResult
selectLoaderDevice(const transport::ScanResult& scan,
                   std::string_view portPath) noexcept;

void printProbeSuccess(const transport::UsbDeviceInfo& device,
                       const transport::LibusbInterfaceInfo& interfaceInfo,
                       std::ostream& output);

void printChipInfo(const transport::UsbDeviceInfo& device,
                   const session::ChipInfo& chipInfo,
                   std::ostream& output);

void printCapability(const transport::UsbDeviceInfo& device,
                     const session::Capability& capability,
                     std::ostream& output);

void printFlashInfo(const transport::UsbDeviceInfo& device,
                    const session::FlashInfo& flashInfo,
                    std::ostream& output);

void printFlashId(const transport::UsbDeviceInfo& device,
                  const session::FlashId& flashId,
                  std::ostream& output);

} // namespace zzrk::cli
