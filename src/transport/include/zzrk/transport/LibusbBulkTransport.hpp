// SPDX-License-Identifier: MIT

#pragma once

#include <zzrk/session/IUsbTransport.hpp>
#include <zzrk/transport/UsbDeviceScanner.hpp>

#include <cstdint>
#include <memory>
#include <variant>

namespace zzrk::transport {

enum class LibusbOpenErrorCode {
    invalidSelection,
    initializationFailed,
    enumerationFailed,
    topologyReadFailed,
    descriptorReadFailed,
    deviceNotFound,
    deviceAmbiguous,
    configurationReadFailed,
    rockusbInterfaceNotFound,
    rockusbInterfaceAmbiguous,
    invalidEndpointLayout,
    openFailed,
    kernelDriverQueryFailed,
    kernelDriverActive,
    claimFailed,
    resourceExhausted,
};

struct LibusbOpenError {
    LibusbOpenErrorCode code{LibusbOpenErrorCode::initializationFailed};
    // A negative libusb error code when the failure originated in libusb.
    int nativeCode{};

    friend constexpr bool operator==(const LibusbOpenError&,
                                     const LibusbOpenError&) = default;
};

[[nodiscard]] bool isAccessDenied(const LibusbOpenError& error) noexcept;

struct LibusbInterfaceInfo {
    std::uint8_t interfaceNumber{};
    std::uint8_t alternateSetting{};
    std::uint8_t bulkInEndpoint{};
    std::uint8_t bulkOutEndpoint{};
    std::uint16_t bulkInMaxPacketSize{};
    std::uint16_t bulkOutMaxPacketSize{};

    friend constexpr bool operator==(const LibusbInterfaceInfo&,
                                     const LibusbInterfaceInfo&) = default;
};

class LibusbBulkTransport final : public session::IUsbTransport {
  public:
    using OpenResult =
        std::variant<std::unique_ptr<LibusbBulkTransport>, LibusbOpenError>;

    // Re-selects a scanned device by stable topology and immutable USB
    // descriptor identity. Opening claims the RockUSB interface, but does not
    // change configuration, detach a driver, select an alternate setting, or
    // perform a USB transfer.
    [[nodiscard]] static OpenResult open(const UsbDeviceInfo& selection) noexcept;

    ~LibusbBulkTransport() override;

    [[nodiscard]] const LibusbInterfaceInfo& interfaceInfo() const noexcept;

    // Releases the interface before closing the handle and context. The
    // return value is LIBUSB_SUCCESS (also when already closed), or the native
    // libusb_release_interface error. All resources are closed in either case.
    [[nodiscard]] int close() noexcept;

    [[nodiscard]] session::TransferResult
    bulkOut(std::span<const std::byte> bytes,
            std::chrono::milliseconds timeout) noexcept override;

    [[nodiscard]] session::TransferResult
    bulkIn(std::span<std::byte> bytes,
           std::chrono::milliseconds timeout) noexcept override;

  private:
    struct Impl;

    explicit LibusbBulkTransport(std::unique_ptr<Impl> impl) noexcept;

    std::unique_ptr<Impl> impl_;
};

} // namespace zzrk::transport
