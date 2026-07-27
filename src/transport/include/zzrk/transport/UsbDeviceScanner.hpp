// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace zzrk::transport {

enum class DeviceMode {
    Unknown,
    Adb,
    Maskrom,
    Loader,
};

[[nodiscard]] constexpr std::string_view toString(const DeviceMode mode) noexcept
{
    switch (mode) {
    case DeviceMode::Adb:
        return "ADB";
    case DeviceMode::Maskrom:
        return "Maskrom";
    case DeviceMode::Loader:
        return "Loader";
    case DeviceMode::Unknown:
        return "Unknown";
    }
    return "Unknown";
}

struct UsbDeviceInfo {
    std::uint16_t vendorId{};
    std::uint16_t productId{};
    std::uint16_t bcdUsb{};
    std::uint16_t bcdDevice{};
    std::uint8_t busNumber{};
    std::vector<std::uint8_t> portNumbers;
    std::string portPath;
    DeviceMode mode{DeviceMode::Unknown};
    std::uint8_t deviceAddress{};
};

enum class ScanErrorKind {
    Other,
    AccessDenied,
};

struct ScanError {
    int code{};
    std::string message;
    ScanErrorKind kind{ScanErrorKind::Other};
};

struct ScanResult {
    std::vector<UsbDeviceInfo> devices;
    std::optional<ScanError> error;
};

class UsbDeviceScanner final {
  public:
    UsbDeviceScanner();
    ~UsbDeviceScanner();

    UsbDeviceScanner(const UsbDeviceScanner&) = delete;
    UsbDeviceScanner& operator=(const UsbDeviceScanner&) = delete;
    UsbDeviceScanner(UsbDeviceScanner&&) noexcept;
    UsbDeviceScanner& operator=(UsbDeviceScanner&&) noexcept;

    [[nodiscard]] ScanResult scanRockchipDevices() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace zzrk::transport
