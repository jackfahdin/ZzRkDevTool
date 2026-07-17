// SPDX-License-Identifier: MIT

#include <zzrk/transport/UsbDeviceScanner.hpp>

#include <libusb.h>

#include <algorithm>
#include <array>
#include <iomanip>
#include <sstream>
#include <utility>

namespace zzrk::transport {
namespace {

constexpr std::uint16_t kRockchipVendorId = 0x2207;
constexpr std::array<std::uint16_t, 2> kKnownNonRockusbProductIds{
    0x0000,
    0x0010,
};

[[nodiscard]] bool isRockusbDevice(const std::uint16_t vendorId,
                                   const std::uint16_t productId) noexcept
{
    if (vendorId != kRockchipVendorId
        || std::ranges::find(kKnownNonRockusbProductIds, productId)
               != kKnownNonRockusbProductIds.end()) {
        return false;
    }

    // RockUSB PIDs encode the SoC family in the high byte. Low-byte-only PIDs
    // are used by non-flashing modes and must not enter the flashing list.
    return (productId & 0xff00U) != 0U;
}

[[nodiscard]] DeviceMode inferMode(const std::uint16_t bcdUsb) noexcept
{
    // Rockchip's tools use bit 0 of bcdUSB as the Maskrom/Loader mode flag.
    return (bcdUsb & 0x0001U) == 0U ? DeviceMode::Maskrom
                                    : DeviceMode::Loader;
}

[[nodiscard]] std::string describeError(const int code)
{
    const char* const name = libusb_error_name(code);
    const char* const detail = libusb_strerror(static_cast<libusb_error>(code));

    std::string message = name != nullptr ? name : "libusb error";
    if (detail != nullptr && message != detail) {
        message += ": ";
        message += detail;
    }
    return message;
}

[[nodiscard]] ScanError makeScanError(const int code)
{
    return ScanError{
        .code = code,
        .message = describeError(code),
        .kind = code == LIBUSB_ERROR_ACCESS ? ScanErrorKind::AccessDenied
                                            : ScanErrorKind::Other,
    };
}

[[nodiscard]] std::string makePortPath(const std::uint8_t busNumber,
                                       const std::vector<std::uint8_t>& ports)
{
    if (ports.empty()) {
        return {};
    }

    std::ostringstream stream;
    stream << static_cast<unsigned int>(busNumber) << '-';
    for (std::size_t index = 0; index < ports.size(); ++index) {
        if (index != 0) {
            stream << '.';
        }
        stream << static_cast<unsigned int>(ports[index]);
    }
    return stream.str();
}

} // namespace

struct UsbDeviceScanner::Impl {
    libusb_context* context{};
    int initializationResult{LIBUSB_ERROR_OTHER};

    Impl()
    {
        initializationResult = libusb_init(&context);
    }

    ~Impl()
    {
        if (context != nullptr) {
            libusb_exit(context);
        }
    }
};

UsbDeviceScanner::UsbDeviceScanner() : impl_(std::make_unique<Impl>()) {}

UsbDeviceScanner::~UsbDeviceScanner() = default;
UsbDeviceScanner::UsbDeviceScanner(UsbDeviceScanner&&) noexcept = default;
UsbDeviceScanner& UsbDeviceScanner::operator=(UsbDeviceScanner&&) noexcept =
    default;

ScanResult UsbDeviceScanner::scanRockchipDevices() const
{
    ScanResult result;
    if (impl_ == nullptr || impl_->initializationResult < 0) {
        const int code = impl_ != nullptr ? impl_->initializationResult
                                          : LIBUSB_ERROR_OTHER;
        result.error = makeScanError(code);
        return result;
    }

    libusb_device** rawDevices = nullptr;
    const ssize_t deviceCount =
        libusb_get_device_list(impl_->context, &rawDevices);
    if (deviceCount < 0) {
        const int code = static_cast<int>(deviceCount);
        result.error = makeScanError(code);
        return result;
    }

    struct DeviceListGuard {
        libusb_device** devices{};
        ~DeviceListGuard()
        {
            if (devices != nullptr) {
                libusb_free_device_list(devices, 1);
            }
        }
    } guard{rawDevices};

    for (ssize_t index = 0; index < deviceCount; ++index) {
        libusb_device* const device = rawDevices[index];
        libusb_device_descriptor descriptor{};
        const int descriptorResult =
            libusb_get_device_descriptor(device, &descriptor);
        if (descriptorResult < 0) {
            if (!result.error.has_value()) {
                result.error = makeScanError(descriptorResult);
            }
            continue;
        }
        if (!isRockusbDevice(descriptor.idVendor, descriptor.idProduct)) {
            continue;
        }

        const auto busNumber = libusb_get_bus_number(device);
        const auto address = libusb_get_device_address(device);
        std::array<std::uint8_t, 8> rawPorts{};
        const int portCount = libusb_get_port_numbers(
            device, rawPorts.data(), static_cast<int>(rawPorts.size()));
        std::vector<std::uint8_t> ports;
        if (portCount > 0) {
            ports.assign(rawPorts.begin(), rawPorts.begin() + portCount);
        } else if (!result.error.has_value()) {
            const int code = portCount < 0 ? portCount : LIBUSB_ERROR_NOT_FOUND;
            ScanError error = makeScanError(code);
            error.message =
                "Unable to determine a stable USB port path for bus "
                + std::to_string(busNumber) + ", address "
                + std::to_string(address) + ": " + error.message;
            result.error = std::move(error);
        }

        const std::string portPath = makePortPath(busNumber, ports);
        result.devices.push_back(UsbDeviceInfo{
            .vendorId = descriptor.idVendor,
            .productId = descriptor.idProduct,
            .bcdUsb = descriptor.bcdUSB,
            .bcdDevice = descriptor.bcdDevice,
            .busNumber = busNumber,
            .portNumbers = std::move(ports),
            .portPath = portPath,
            .mode = inferMode(descriptor.bcdUSB),
            .deviceAddress = address,
        });
    }

    std::ranges::sort(result.devices, {}, &UsbDeviceInfo::portPath);
    return result;
}

} // namespace zzrk::transport
