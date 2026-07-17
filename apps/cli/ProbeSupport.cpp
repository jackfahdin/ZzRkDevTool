// SPDX-License-Identifier: MIT

#include "ProbeSupport.hpp"

#include <cstdint>
#include <iomanip>
#include <ostream>

namespace zzrk::cli {
namespace {

constexpr std::uint16_t kRk3588LoaderVendorId = 0x2207;
constexpr std::uint16_t kRk3588LoaderProductId = 0x350b;
constexpr std::uint16_t kRk3588LoaderBcdUsb = 0x0201;

[[nodiscard]] constexpr bool
isSupportedLoader(const transport::UsbDeviceInfo& device) noexcept
{
    return device.vendorId == kRk3588LoaderVendorId
           && device.productId == kRk3588LoaderProductId
           && device.bcdUsb == kRk3588LoaderBcdUsb
           && device.mode == transport::DeviceMode::Loader;
}

} // namespace

ProbeSelectionResult selectProbeDevice(const transport::ScanResult& scan,
                                       const std::string_view portPath) noexcept
{
    if (scan.error.has_value()) {
        return {.error = ProbeSelectionError::scanFailed};
    }

    const transport::UsbDeviceInfo* selection = nullptr;
    for (const auto& device : scan.devices) {
        if (device.portPath != portPath) {
            continue;
        }
        if (selection != nullptr) {
            return {.error = ProbeSelectionError::ambiguous};
        }
        selection = &device;
    }

    if (selection == nullptr) {
        return {.error = ProbeSelectionError::notFound};
    }
    if (!isSupportedLoader(*selection)) {
        return {.error = ProbeSelectionError::unsupported};
    }
    return {.device = selection, .error = ProbeSelectionError::none};
}

void printProbeSuccess(const transport::UsbDeviceInfo& device,
                       const transport::LibusbInterfaceInfo& interfaceInfo,
                       std::ostream& output)
{
    output << "Probe succeeded: " << device.portPath << " (" << std::hex
           << std::setfill('0') << std::setw(4) << device.vendorId << ':'
           << std::setw(4) << device.productId << std::dec << std::setfill(' ')
           << ", " << transport::toString(device.mode) << ")\n"
           << "Interface "
           << static_cast<unsigned int>(interfaceInfo.interfaceNumber)
           << ", alternate "
           << static_cast<unsigned int>(interfaceInfo.alternateSetting)
           << ", bulk IN 0x" << std::hex << std::setfill('0') << std::setw(2)
           << static_cast<unsigned int>(interfaceInfo.bulkInEndpoint)
           << ", bulk OUT 0x" << std::setw(2)
           << static_cast<unsigned int>(interfaceInfo.bulkOutEndpoint)
           << std::dec << std::setfill(' ') << ", max packet "
           << interfaceInfo.bulkInMaxPacketSize << '/'
           << interfaceInfo.bulkOutMaxPacketSize << " bytes\n"
           << "Interface claim and release succeeded; no RockUSB command was "
              "sent.\n";
}

} // namespace zzrk::cli
