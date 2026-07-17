// SPDX-License-Identifier: MIT

#include <zzrk/transport/LibusbBulkTransport.hpp>

#include <libusb.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <new>
#include <string_view>
#include <utility>
#include <vector>

namespace zzrk::transport {
namespace {

constexpr std::uint16_t kLoaderVendorId = 0x2207;
constexpr std::uint16_t kRk3588ProductId = 0x350b;
constexpr std::uint16_t kLoaderBcdUsb = 0x0201;
constexpr std::uint16_t kLoaderBcdDevice = 0x0223;
constexpr std::uint8_t kRockusbInterfaceClass = 0xff;
constexpr std::uint8_t kRockusbInterfaceSubclass = 0x06;
constexpr std::uint8_t kRockusbInterfaceProtocol = 0x05;
constexpr std::uint8_t kLoaderInterfaceNumber = 0;
constexpr std::uint8_t kLoaderAlternateSetting = 0;
constexpr std::uint8_t kLoaderBulkInEndpoint = 0x81;
constexpr std::uint8_t kLoaderBulkOutEndpoint = 0x02;
constexpr std::uint16_t kLoaderMaxPacketSize = 512;
constexpr std::uint16_t kMaxPacketSizeMask = 0x07ff;

struct ContextGuard {
    libusb_context* context{};

    ~ContextGuard()
    {
        if (context != nullptr) {
            libusb_exit(context);
        }
    }

    [[nodiscard]] libusb_context* release() noexcept
    {
        return std::exchange(context, nullptr);
    }
};

struct DeviceListGuard {
    libusb_device** devices{};

    ~DeviceListGuard()
    {
        if (devices != nullptr) {
            libusb_free_device_list(devices, 1);
        }
    }
};

struct ConfigGuard {
    libusb_config_descriptor* config{};

    ~ConfigGuard()
    {
        if (config != nullptr) {
            libusb_free_config_descriptor(config);
        }
    }
};

struct HandleGuard {
    libusb_device_handle* handle{};

    ~HandleGuard()
    {
        if (handle != nullptr) {
            libusb_close(handle);
        }
    }

    [[nodiscard]] libusb_device_handle* release() noexcept
    {
        return std::exchange(handle, nullptr);
    }
};

struct InterfaceClaimGuard {
    libusb_device_handle* handle{};
    int interfaceNumber{};
    bool claimed{};

    ~InterfaceClaimGuard()
    {
        if (claimed && handle != nullptr) {
            static_cast<void>(
                libusb_release_interface(handle, interfaceNumber));
        }
    }

    void release() noexcept
    {
        claimed = false;
    }
};

[[nodiscard]] constexpr LibusbOpenError makeOpenError(
    const LibusbOpenErrorCode code, const int nativeCode = 0) noexcept
{
    return LibusbOpenError{.code = code, .nativeCode = nativeCode};
}

[[nodiscard]] constexpr session::TransportErrorCode
mapTransferError(const int nativeCode) noexcept
{
    switch (nativeCode) {
    case LIBUSB_ERROR_TIMEOUT:
        return session::TransportErrorCode::timedOut;
    case LIBUSB_ERROR_NO_DEVICE:
        return session::TransportErrorCode::disconnected;
    case LIBUSB_ERROR_ACCESS:
        return session::TransportErrorCode::accessDenied;
    case LIBUSB_ERROR_PIPE:
        return session::TransportErrorCode::stalled;
    case LIBUSB_ERROR_OVERFLOW:
        return session::TransportErrorCode::overflow;
    case LIBUSB_ERROR_BUSY:
        return session::TransportErrorCode::busy;
    case LIBUSB_ERROR_INTERRUPTED:
        return session::TransportErrorCode::interrupted;
    case LIBUSB_ERROR_NOT_SUPPORTED:
        return session::TransportErrorCode::notSupported;
    case LIBUSB_ERROR_NO_MEM:
        return session::TransportErrorCode::resourceExhausted;
    case LIBUSB_ERROR_INVALID_PARAM:
        return session::TransportErrorCode::invalidArgument;
    case LIBUSB_ERROR_IO:
        return session::TransportErrorCode::io;
    case LIBUSB_ERROR_NOT_FOUND:
    case LIBUSB_ERROR_OTHER:
        return session::TransportErrorCode::other;
    default:
        return session::TransportErrorCode::other;
    }
}

[[nodiscard]] constexpr bool isValidTimeout(
    const std::chrono::milliseconds timeout) noexcept
{
    return timeout.count() > 0
           && timeout
                  <= std::chrono::milliseconds{
                      std::numeric_limits<unsigned int>::max()};
}

[[nodiscard]] bool matchesNumberAt(const std::string_view text,
                                   std::size_t& offset,
                                   const std::uint8_t value) noexcept
{
    std::array<char, 3> digits{};
    const auto converted =
        std::to_chars(digits.data(), digits.data() + digits.size(), value);
    if (converted.ec != std::errc{}) {
        return false;
    }
    const auto length = static_cast<std::size_t>(converted.ptr - digits.data());
    if (offset > text.size() || length > text.size() - offset
        || text.substr(offset, length)
               != std::string_view{digits.data(), length}) {
        return false;
    }
    offset += length;
    return true;
}

[[nodiscard]] bool hasConsistentPortPath(
    const UsbDeviceInfo& selection) noexcept
{
    if (selection.portPath.empty() || selection.portNumbers.empty()) {
        return false;
    }

    std::size_t offset = 0;
    if (!matchesNumberAt(selection.portPath, offset, selection.busNumber)
        || offset >= selection.portPath.size()
        || selection.portPath[offset] != '-') {
        return false;
    }
    ++offset;

    for (std::size_t index = 0; index < selection.portNumbers.size(); ++index) {
        if (index != 0U) {
            if (offset >= selection.portPath.size()
                || selection.portPath[offset] != '.') {
                return false;
            }
            ++offset;
        }
        if (!matchesNumberAt(selection.portPath, offset,
                             selection.portNumbers[index])) {
            return false;
        }
    }
    return offset == selection.portPath.size();
}

[[nodiscard]] constexpr bool isSupportedLoaderSelection(
    const UsbDeviceInfo& selection) noexcept
{
    return selection.vendorId == kLoaderVendorId
           && selection.productId == kRk3588ProductId
           && selection.bcdUsb == kLoaderBcdUsb
           && selection.bcdDevice == kLoaderBcdDevice
           && selection.mode == DeviceMode::Loader;
}

[[nodiscard]] constexpr bool isSupportedLoaderInterface(
    const LibusbInterfaceInfo& info) noexcept
{
    return info.interfaceNumber == kLoaderInterfaceNumber
           && info.alternateSetting == kLoaderAlternateSetting
           && info.bulkInEndpoint == kLoaderBulkInEndpoint
           && info.bulkOutEndpoint == kLoaderBulkOutEndpoint
           && info.bulkInMaxPacketSize == kLoaderMaxPacketSize
           && info.bulkOutMaxPacketSize == kLoaderMaxPacketSize;
}

[[nodiscard]] bool isSupportedLoaderConfiguration(
    const libusb_config_descriptor& config,
    const LibusbInterfaceInfo& info) noexcept
{
    if (!isSupportedLoaderInterface(info) || config.bNumInterfaces != 1U
        || config.interface == nullptr) {
        return false;
    }
    const libusb_interface& interface = config.interface[0];
    return interface.num_altsetting == 1 && interface.altsetting != nullptr
           && interface.altsetting[0].bNumEndpoints == 2U;
}

template <typename Byte>
[[nodiscard]] session::TransferResult bulkTransfer(
    libusb_device_handle* const handle, const std::uint8_t endpoint,
    const std::span<Byte> bytes,
    const std::chrono::milliseconds timeout) noexcept
{
    if (handle == nullptr || bytes.empty()
        || bytes.size()
            > static_cast<std::size_t>(std::numeric_limits<int>::max())
        || !isValidTimeout(timeout)) {
        return session::TransportError{
            .code = session::TransportErrorCode::invalidArgument,
            .nativeCode = LIBUSB_ERROR_INVALID_PARAM,
            .bytesTransferred = 0,
        };
    }

    int transferred = 0;
    auto* const data = reinterpret_cast<unsigned char*>(
        const_cast<std::byte*>(bytes.data()));
    const int result = libusb_bulk_transfer(
        handle, endpoint, data, static_cast<int>(bytes.size()), &transferred,
        static_cast<unsigned int>(timeout.count()));
    const std::size_t transferredBytes =
        transferred > 0 ? static_cast<std::size_t>(transferred) : 0U;
    if (result < 0) {
        return session::TransportError{
            .code = mapTransferError(result),
            .nativeCode = result,
            .bytesTransferred = transferredBytes,
        };
    }
    return session::TransferSuccess{.bytesTransferred = transferredBytes};
}

[[nodiscard]] bool isRockusbInterface(
    const libusb_interface_descriptor& descriptor) noexcept
{
    return descriptor.bInterfaceClass == kRockusbInterfaceClass
           && descriptor.bInterfaceSubClass == kRockusbInterfaceSubclass
           && descriptor.bInterfaceProtocol == kRockusbInterfaceProtocol;
}

[[nodiscard]] std::variant<LibusbInterfaceInfo, LibusbOpenError>
inspectRockusbInterface(const libusb_config_descriptor& config) noexcept
{
    if (config.bNumInterfaces == 0U || config.interface == nullptr) {
        return makeOpenError(LibusbOpenErrorCode::rockusbInterfaceNotFound);
    }

    const libusb_interface_descriptor* match = nullptr;
    std::size_t matchCount = 0;
    for (std::size_t interfaceIndex = 0;
         interfaceIndex < static_cast<std::size_t>(config.bNumInterfaces);
         ++interfaceIndex) {
        const libusb_interface& interface = config.interface[interfaceIndex];
        if (interface.num_altsetting < 0
            || (interface.num_altsetting > 0
                && interface.altsetting == nullptr)) {
            return makeOpenError(
                LibusbOpenErrorCode::rockusbInterfaceNotFound);
        }
        for (int alternateIndex = 0;
             alternateIndex < interface.num_altsetting; ++alternateIndex) {
            const auto& alternate = interface.altsetting[alternateIndex];
            if (isRockusbInterface(alternate)) {
                match = &alternate;
                ++matchCount;
            }
        }
    }

    if (matchCount == 0U || match == nullptr) {
        return makeOpenError(LibusbOpenErrorCode::rockusbInterfaceNotFound);
    }
    if (matchCount != 1U) {
        return makeOpenError(LibusbOpenErrorCode::rockusbInterfaceAmbiguous);
    }
    if (match->bNumEndpoints != 0U && match->endpoint == nullptr) {
        return makeOpenError(LibusbOpenErrorCode::invalidEndpointLayout);
    }

    const libusb_endpoint_descriptor* bulkIn = nullptr;
    const libusb_endpoint_descriptor* bulkOut = nullptr;
    std::size_t bulkInCount = 0;
    std::size_t bulkOutCount = 0;
    for (std::size_t endpointIndex = 0;
         endpointIndex < static_cast<std::size_t>(match->bNumEndpoints);
         ++endpointIndex) {
        const auto& endpoint = match->endpoint[endpointIndex];
        if ((endpoint.bmAttributes & LIBUSB_TRANSFER_TYPE_MASK)
            != LIBUSB_TRANSFER_TYPE_BULK) {
            continue;
        }
        if ((endpoint.bEndpointAddress & LIBUSB_ENDPOINT_DIR_MASK)
            == LIBUSB_ENDPOINT_IN) {
            bulkIn = &endpoint;
            ++bulkInCount;
        } else {
            bulkOut = &endpoint;
            ++bulkOutCount;
        }
    }
    if (bulkInCount != 1U || bulkOutCount != 1U || bulkIn == nullptr
        || bulkOut == nullptr) {
        return makeOpenError(LibusbOpenErrorCode::invalidEndpointLayout);
    }

    return LibusbInterfaceInfo{
        .interfaceNumber = match->bInterfaceNumber,
        .alternateSetting = match->bAlternateSetting,
        .bulkInEndpoint = bulkIn->bEndpointAddress,
        .bulkOutEndpoint = bulkOut->bEndpointAddress,
        .bulkInMaxPacketSize = static_cast<std::uint16_t>(
            bulkIn->wMaxPacketSize & kMaxPacketSizeMask),
        .bulkOutMaxPacketSize = static_cast<std::uint16_t>(
            bulkOut->wMaxPacketSize & kMaxPacketSizeMask),
    };
}

} // namespace

struct LibusbBulkTransport::Impl {
    libusb_context* context{};
    libusb_device_handle* handle{};
    LibusbInterfaceInfo interfaceInfo{};
    bool claimed{};

    [[nodiscard]] int close() noexcept
    {
        int releaseResult = LIBUSB_SUCCESS;
        if (claimed && handle != nullptr) {
            claimed = false;
            releaseResult = libusb_release_interface(
                handle, static_cast<int>(interfaceInfo.interfaceNumber));
        }
        if (handle != nullptr) {
            libusb_close(handle);
            handle = nullptr;
        }
        if (context != nullptr) {
            libusb_exit(context);
            context = nullptr;
        }
        return releaseResult;
    }

    ~Impl()
    {
        static_cast<void>(close());
    }
};

LibusbBulkTransport::LibusbBulkTransport(std::unique_ptr<Impl> impl) noexcept
    : impl_(std::move(impl))
{
}

LibusbBulkTransport::~LibusbBulkTransport() = default;

bool isAccessDenied(const LibusbOpenError& error) noexcept
{
    return error.nativeCode == LIBUSB_ERROR_ACCESS;
}

LibusbBulkTransport::OpenResult
LibusbBulkTransport::open(const UsbDeviceInfo& selection) noexcept
{
    if (selection.portNumbers.size()
            >= static_cast<std::size_t>(std::numeric_limits<int>::max())
        || !isSupportedLoaderSelection(selection)
        || !hasConsistentPortPath(selection)) {
        return makeOpenError(LibusbOpenErrorCode::invalidSelection);
    }

    try {
        ContextGuard context;
        libusb_context* initializedContext = nullptr;
        const int initializationResult = libusb_init(&initializedContext);
        if (initializationResult < 0 || initializedContext == nullptr) {
            return makeOpenError(LibusbOpenErrorCode::initializationFailed,
                                 initializationResult < 0
                                     ? initializationResult
                                     : LIBUSB_ERROR_OTHER);
        }
        context.context = initializedContext;

        DeviceListGuard list;
        const ssize_t deviceCount =
            libusb_get_device_list(context.context, &list.devices);
        if (deviceCount < 0) {
            return makeOpenError(LibusbOpenErrorCode::enumerationFailed,
                                 static_cast<int>(deviceCount));
        }
        if (deviceCount > 0 && list.devices == nullptr) {
            return makeOpenError(LibusbOpenErrorCode::enumerationFailed,
                                 LIBUSB_ERROR_OTHER);
        }

        libusb_device* selectedDevice = nullptr;
        std::size_t matchCount = 0;
        int firstTopologyError = LIBUSB_SUCCESS;
        int firstDescriptorError = LIBUSB_SUCCESS;
        std::vector<std::uint8_t> ports(selection.portNumbers.size() + 1U);
        for (ssize_t index = 0; index < deviceCount; ++index) {
            libusb_device* const device = list.devices[index];
            if (libusb_get_bus_number(device) != selection.busNumber) {
                continue;
            }

            const int portCount = libusb_get_port_numbers(
                device, ports.data(), static_cast<int>(ports.size()));
            if (portCount < 0) {
                if (portCount == LIBUSB_ERROR_OVERFLOW) {
                    continue;
                }
                if (firstTopologyError == LIBUSB_SUCCESS) {
                    firstTopologyError = portCount;
                }
                continue;
            }
            const auto portCountSize = static_cast<std::size_t>(portCount);
            if (portCountSize > ports.size()
                || portCountSize
                    != selection.portNumbers.size()
                || !std::ranges::equal(
                    ports.begin(), ports.begin() + portCount,
                    selection.portNumbers.begin(),
                    selection.portNumbers.end())) {
                continue;
            }

            libusb_device_descriptor descriptor{};
            const int descriptorResult =
                libusb_get_device_descriptor(device, &descriptor);
            if (descriptorResult < 0) {
                if (firstDescriptorError == LIBUSB_SUCCESS) {
                    firstDescriptorError = descriptorResult;
                }
                continue;
            }
            if (descriptor.idVendor != selection.vendorId
                || descriptor.idProduct != selection.productId
                || descriptor.bcdUSB != selection.bcdUsb
                || descriptor.bcdDevice != selection.bcdDevice) {
                continue;
            }

            selectedDevice = device;
            ++matchCount;
        }

        if (matchCount == 0U) {
            if (firstDescriptorError < 0) {
                return makeOpenError(
                    LibusbOpenErrorCode::descriptorReadFailed,
                    firstDescriptorError);
            }
            if (firstTopologyError < 0) {
                return makeOpenError(LibusbOpenErrorCode::topologyReadFailed,
                                     firstTopologyError);
            }
            return makeOpenError(LibusbOpenErrorCode::deviceNotFound);
        }
        if (matchCount != 1U || selectedDevice == nullptr) {
            return makeOpenError(LibusbOpenErrorCode::deviceAmbiguous);
        }

        ConfigGuard config;
        const int configResult =
            libusb_get_active_config_descriptor(selectedDevice, &config.config);
        if (configResult < 0 || config.config == nullptr) {
            return makeOpenError(
                LibusbOpenErrorCode::configurationReadFailed,
                configResult < 0 ? configResult : LIBUSB_ERROR_OTHER);
        }
        auto inspected = inspectRockusbInterface(*config.config);
        const auto* interfaceInfo = std::get_if<LibusbInterfaceInfo>(&inspected);
        if (interfaceInfo == nullptr) {
            return std::get<LibusbOpenError>(inspected);
        }
        if (!isSupportedLoaderConfiguration(*config.config, *interfaceInfo)) {
            return makeOpenError(LibusbOpenErrorCode::invalidEndpointLayout);
        }

        HandleGuard handle;
        const int openResult = libusb_open(selectedDevice, &handle.handle);
        if (openResult < 0 || handle.handle == nullptr) {
            return makeOpenError(LibusbOpenErrorCode::openFailed,
                                 openResult < 0 ? openResult
                                                : LIBUSB_ERROR_OTHER);
        }

        const int kernelDriverResult = libusb_kernel_driver_active(
            handle.handle, static_cast<int>(interfaceInfo->interfaceNumber));
        if (kernelDriverResult < 0
            && kernelDriverResult != LIBUSB_ERROR_NOT_SUPPORTED) {
            return makeOpenError(
                LibusbOpenErrorCode::kernelDriverQueryFailed,
                kernelDriverResult);
        }
        if (kernelDriverResult > 0) {
            return makeOpenError(LibusbOpenErrorCode::kernelDriverActive,
                                 kernelDriverResult);
        }

        const int claimResult = libusb_claim_interface(
            handle.handle, static_cast<int>(interfaceInfo->interfaceNumber));
        if (claimResult < 0) {
            return makeOpenError(LibusbOpenErrorCode::claimFailed,
                                 claimResult);
        }
        InterfaceClaimGuard claimedInterface{
            .handle = handle.handle,
            .interfaceNumber = static_cast<int>(interfaceInfo->interfaceNumber),
            .claimed = true,
        };

        auto impl = std::make_unique<Impl>();
        auto transport = std::unique_ptr<LibusbBulkTransport>(
            new LibusbBulkTransport(std::move(impl)));
        transport->impl_->context = context.release();
        transport->impl_->handle = handle.release();
        transport->impl_->interfaceInfo = *interfaceInfo;
        transport->impl_->claimed = true;
        claimedInterface.release();
        return transport;
    } catch (const std::bad_alloc&) {
        return makeOpenError(LibusbOpenErrorCode::resourceExhausted,
                             LIBUSB_ERROR_NO_MEM);
    } catch (...) {
        return makeOpenError(LibusbOpenErrorCode::resourceExhausted,
                             LIBUSB_ERROR_NO_MEM);
    }
}

const LibusbInterfaceInfo& LibusbBulkTransport::interfaceInfo() const noexcept
{
    return impl_->interfaceInfo;
}

int LibusbBulkTransport::close() noexcept
{
    return impl_->close();
}

session::TransferResult LibusbBulkTransport::bulkOut(
    const std::span<const std::byte> bytes,
    const std::chrono::milliseconds timeout) noexcept
{
    return bulkTransfer(impl_->handle, impl_->interfaceInfo.bulkOutEndpoint,
                        bytes, timeout);
}

session::TransferResult LibusbBulkTransport::bulkIn(
    const std::span<std::byte> bytes,
    const std::chrono::milliseconds timeout) noexcept
{
    return bulkTransfer(impl_->handle, impl_->interfaceInfo.bulkInEndpoint,
                        bytes, timeout);
}

} // namespace zzrk::transport
