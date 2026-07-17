// SPDX-License-Identifier: MIT

#include "ProbeSupport.hpp"

#include <zzrk/core/StorageUnits.hpp>

#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <ostream>

namespace zzrk::cli {
namespace {

constexpr std::uint16_t kRk3588LoaderVendorId = 0x2207;
constexpr std::uint16_t kRk3588LoaderProductId = 0x350b;
constexpr std::uint16_t kRk3588LoaderBcdUsb = 0x0201;
constexpr std::uint16_t kRk3588LoaderBcdDevice = 0x0223;

[[nodiscard]] constexpr bool
isSupportedLoader(const transport::UsbDeviceInfo& device) noexcept
{
    return device.vendorId == kRk3588LoaderVendorId
           && device.productId == kRk3588LoaderProductId
           && device.bcdUsb == kRk3588LoaderBcdUsb
           && device.bcdDevice == kRk3588LoaderBcdDevice
           && device.mode == transport::DeviceMode::Loader;
}

} // namespace

LoaderSelectionResult selectLoaderDevice(
    const transport::ScanResult& scan,
    const std::string_view portPath) noexcept
{
    if (scan.error.has_value()) {
        return {.error = LoaderSelectionError::scanFailed};
    }

    const transport::UsbDeviceInfo* selection = nullptr;
    for (const auto& device : scan.devices) {
        if (device.portPath != portPath) {
            continue;
        }
        if (selection != nullptr) {
            return {.error = LoaderSelectionError::ambiguous};
        }
        selection = &device;
    }

    if (selection == nullptr) {
        return {.error = LoaderSelectionError::notFound};
    }
    if (!isSupportedLoader(*selection)) {
        return {.error = LoaderSelectionError::unsupported};
    }
    return {.device = selection, .error = LoaderSelectionError::none};
}

void printProbeSuccess(const transport::UsbDeviceInfo& device,
                       const transport::LibusbInterfaceInfo& interfaceInfo,
                       std::ostream& output)
{
    output << "Probe succeeded: " << device.portPath << " (" << std::hex
           << std::setfill('0') << std::setw(4) << device.vendorId << ':'
           << std::setw(4) << device.productId << std::dec << std::setfill(' ')
           << ", " << transport::toString(device.mode) << ", bcdDevice 0x"
           << std::hex << std::setfill('0') << std::setw(4)
           << device.bcdDevice << std::dec << std::setfill(' ') << ")\n"
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

void printChipInfo(const transport::UsbDeviceInfo& device,
                   const session::ChipInfo& chipInfo,
                   std::ostream& output)
{
    output << "Port: " << device.portPath << " (bcdDevice 0x" << std::hex
           << std::setfill('0') << std::setw(4) << device.bcdDevice << std::dec
           << std::setfill(' ') << ")\n"
           << "Chip info:" << std::hex << std::setfill('0');
    for (const auto byte : chipInfo.bytes) {
        output << ' ' << std::setw(2) << std::to_integer<unsigned int>(byte);
    }
    output << std::dec << std::setfill(' ') << '\n';
}

void printFlashInfo(const transport::UsbDeviceInfo& device,
                    const session::FlashInfo& flashInfo,
                    std::ostream& output)
{
    output << "Port: " << device.portPath << " (bcdDevice 0x" << std::hex
           << std::setfill('0') << std::setw(4) << device.bcdDevice << std::dec
           << std::setfill(' ') << ")\n"
           << "Sector count: " << flashInfo.legacy.sectorCount << '\n';

    const auto capacityBytes =
        core::toBytes(core::SectorCount{flashInfo.legacy.sectorCount});
    output << "Capacity bytes: ";
    if (capacityBytes.has_value()) {
        output << capacityBytes->value;
    } else {
        output << "overflow";
    }

    output << "\nLegacy block-size code: "
           << flashInfo.legacy.blockSizeCode
           << "\nLegacy page-size code: "
           << static_cast<unsigned int>(flashInfo.legacy.pageSizeCode)
           << "\nLegacy ECC field: "
           << static_cast<unsigned int>(flashInfo.legacy.eccBits)
           << "\nLegacy access-time field: "
           << static_cast<unsigned int>(flashInfo.legacy.accessTime)
           << "\nLegacy manufacturer/code: 0x" << std::hex
           << std::setfill('0')
           << std::setw(2)
           << static_cast<unsigned int>(flashInfo.legacy.manufacturerCode)
           << "\nLegacy flash-mask field: 0x" << std::setw(2)
           << static_cast<unsigned int>(flashInfo.legacy.flashMask)
           << "\nStorage type: not determined by ReadFlashInfo"
           << "\nRaw bytes:";
    for (const auto byte : flashInfo.rawBytes) {
        output << ' ' << std::setw(2) << std::to_integer<unsigned int>(byte);
    }
    output << std::dec << std::setfill(' ') << '\n';
}

} // namespace zzrk::cli
