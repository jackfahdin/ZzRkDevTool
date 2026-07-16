// SPDX-License-Identifier: MIT

#include <zzrk/transport/UsbDeviceScanner.hpp>

#include <libusb.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <iostream>
#include <string_view>
#include <utility>

struct libusb_context {
};

struct libusb_device {
    std::size_t index{};
};

namespace {

struct FakeDevice {
    libusb_device_descriptor descriptor{};
    int descriptorResult{LIBUSB_SUCCESS};
    std::array<std::uint8_t, 8> ports{};
    int portResult{};
    std::uint8_t busNumber{};
    std::uint8_t deviceAddress{};
};

constexpr std::size_t kMaximumFakeDevices = 12;
libusb_context g_context;
std::array<FakeDevice, kMaximumFakeDevices> g_devices{};
std::array<libusb_device, kMaximumFakeDevices> g_deviceHandles{};
std::size_t g_deviceCount = 0;
int g_initializationResult = LIBUSB_SUCCESS;
int g_deviceListError = LIBUSB_SUCCESS;
int g_exitCallCount = 0;
int g_freeListCallCount = 0;
int g_failures = 0;

void expect(const bool condition, const std::string_view description)
{
    if (!condition) {
        std::cerr << "FAIL: " << description << '\n';
        ++g_failures;
    }
}

void resetFakeLibusb()
{
    g_devices = {};
    g_deviceHandles = {};
    g_deviceCount = 0;
    g_initializationResult = LIBUSB_SUCCESS;
    g_deviceListError = LIBUSB_SUCCESS;
    g_exitCallCount = 0;
    g_freeListCallCount = 0;
}

FakeDevice& addDevice(const std::uint16_t vendorId,
                      const std::uint16_t productId,
                      const std::uint16_t bcdUsb,
                      const std::uint8_t busNumber,
                      const std::uint8_t deviceAddress,
                      const std::initializer_list<std::uint8_t> ports)
{
    FakeDevice& device = g_devices.at(g_deviceCount);
    device.descriptor.idVendor = vendorId;
    device.descriptor.idProduct = productId;
    device.descriptor.bcdUSB = bcdUsb;
    device.busNumber = busNumber;
    device.deviceAddress = deviceAddress;
    device.portResult = static_cast<int>(ports.size());
    std::ranges::copy(ports, device.ports.begin());
    g_deviceHandles.at(g_deviceCount).index = g_deviceCount;
    ++g_deviceCount;
    return device;
}

const zzrk::transport::UsbDeviceInfo* findDevice(
    const zzrk::transport::ScanResult& result,
    const std::uint16_t productId)
{
    const auto found = std::ranges::find(result.devices, productId,
                                         &zzrk::transport::UsbDeviceInfo::productId);
    return found == result.devices.end() ? nullptr : &*found;
}

void testRockusbFilteringAndModeInference()
{
    resetFakeLibusb();
    addDevice(0x1234, 0x350b, 0x0200, 1, 1, {1});
    addDevice(0x2207, 0x0000, 0x0200, 1, 2, {2});
    addDevice(0x2207, 0x0010, 0x0201, 1, 3, {3});
    addDevice(0x2207, 0x0001, 0x0201, 1, 4, {4});
    addDevice(0x2207, 0x350b, 0x0300, 2, 5, {2, 3});
    addDevice(0x2207, 0x350c, 0x0301, 2, 6, {4});

    {
        zzrk::transport::UsbDeviceScanner scanner;
        const auto result = scanner.scanRockchipDevices();
        expect(!result.error.has_value(), "a complete fake scan has no error");
        expect(result.devices.size() == 2,
               "only RockUSB PIDs with a SoC-family high byte are returned");

        const auto* maskrom = findDevice(result, 0x350b);
        expect(maskrom != nullptr, "the RK3588-family PID is retained");
        expect(maskrom != nullptr
                   && maskrom->mode == zzrk::transport::DeviceMode::Maskrom,
               "an even bcdUSB value is Maskrom");
        expect(maskrom != nullptr && maskrom->portPath == "2-2.3",
               "the stable path contains the complete USB topology");

        const auto* loader = findDevice(result, 0x350c);
        expect(loader != nullptr
                   && loader->mode == zzrk::transport::DeviceMode::Loader,
               "an odd bcdUSB value is Loader even when it is not 0x0201");
    }

    expect(g_freeListCallCount == 1, "the libusb device list is released once");
    expect(g_exitCallCount == 1, "the libusb context is released once");
}

void testMissingPortPathIsPartialFailure()
{
    resetFakeLibusb();
    FakeDevice& device = addDevice(0x2207, 0x350b, 0x0200, 3, 9, {});
    device.portResult = LIBUSB_ERROR_NOT_SUPPORTED;

    zzrk::transport::UsbDeviceScanner scanner;
    const auto result = scanner.scanRockchipDevices();
    expect(result.devices.size() == 1,
           "a device remains visible when its topology is unavailable");
    expect(result.error.has_value()
               && result.error->code == LIBUSB_ERROR_NOT_SUPPORTED,
           "a topology lookup failure makes the scan partial");
    expect(result.error.has_value()
               && result.error->kind == zzrk::transport::ScanErrorKind::Other,
           "a topology lookup failure is not reported as a permission error");
    expect(result.error.has_value()
               && result.error->message.find("stable USB port path")
                      != std::string::npos,
           "the partial error explains that stable identity is missing");
    expect(result.devices.front().portPath.empty(),
           "bus and address are not presented as a stable port path");
    expect(result.devices.front().busNumber == 3
               && result.devices.front().deviceAddress == 9,
           "diagnostic bus and address fields remain available");
}

void testAccessErrorClassification()
{
    resetFakeLibusb();
    g_initializationResult = LIBUSB_ERROR_ACCESS;
    {
        zzrk::transport::UsbDeviceScanner scanner;
        const auto result = scanner.scanRockchipDevices();
        expect(result.devices.empty() && result.error.has_value(),
               "an initialization error is fatal");
        expect(result.error.has_value()
                   && result.error->kind
                          == zzrk::transport::ScanErrorKind::AccessDenied,
               "LIBUSB_ERROR_ACCESS has a typed permission category");
    }
    expect(g_exitCallCount == 0,
           "a context that failed to initialize is not passed to libusb_exit");

    resetFakeLibusb();
    g_deviceListError = LIBUSB_ERROR_OTHER;
    {
        zzrk::transport::UsbDeviceScanner scanner;
        const auto result = scanner.scanRockchipDevices();
        expect(result.devices.empty() && result.error.has_value()
                   && result.error->kind == zzrk::transport::ScanErrorKind::Other,
               "non-access list failures retain the generic error category");
    }
    expect(g_exitCallCount == 1,
           "a successfully initialized context is released after list failure");
}

} // namespace

extern "C" {

int LIBUSB_CALL libusb_init(libusb_context** context)
{
    *context = g_initializationResult == LIBUSB_SUCCESS ? &g_context : nullptr;
    return g_initializationResult;
}

void LIBUSB_CALL libusb_exit(libusb_context* context)
{
    if (context != nullptr) {
        ++g_exitCallCount;
    }
}

const char* LIBUSB_CALL libusb_error_name(const int errorCode)
{
    switch (errorCode) {
    case LIBUSB_ERROR_ACCESS:
        return "LIBUSB_ERROR_ACCESS";
    case LIBUSB_ERROR_NOT_FOUND:
        return "LIBUSB_ERROR_NOT_FOUND";
    case LIBUSB_ERROR_NOT_SUPPORTED:
        return "LIBUSB_ERROR_NOT_SUPPORTED";
    case LIBUSB_ERROR_OTHER:
        return "LIBUSB_ERROR_OTHER";
    default:
        return "LIBUSB_ERROR";
    }
}

const char* LIBUSB_CALL libusb_strerror(const int errorCode)
{
    return errorCode == LIBUSB_ERROR_ACCESS ? "Access denied" : "Fake error";
}

ssize_t LIBUSB_CALL libusb_get_device_list(libusb_context* context,
                                           libusb_device*** list)
{
    static_cast<void>(context);
    if (g_deviceListError < 0) {
        *list = nullptr;
        return g_deviceListError;
    }

    auto** devices = new libusb_device*[g_deviceCount + 1];
    for (std::size_t index = 0; index < g_deviceCount; ++index) {
        devices[index] = &g_deviceHandles[index];
    }
    devices[g_deviceCount] = nullptr;
    *list = devices;
    return static_cast<ssize_t>(g_deviceCount);
}

void LIBUSB_CALL libusb_free_device_list(libusb_device** list,
                                         const int unrefDevices)
{
    static_cast<void>(unrefDevices);
    delete[] list;
    ++g_freeListCallCount;
}

int LIBUSB_CALL libusb_get_device_descriptor(
    libusb_device* device,
    libusb_device_descriptor* descriptor)
{
    const FakeDevice& fake = g_devices.at(device->index);
    if (fake.descriptorResult < 0) {
        return fake.descriptorResult;
    }
    *descriptor = fake.descriptor;
    return LIBUSB_SUCCESS;
}

int LIBUSB_CALL libusb_get_port_numbers(libusb_device* device,
                                        std::uint8_t* portNumbers,
                                        const int portNumbersLength)
{
    const FakeDevice& fake = g_devices.at(device->index);
    if (fake.portResult <= 0) {
        return fake.portResult;
    }
    if (fake.portResult > portNumbersLength) {
        return LIBUSB_ERROR_OVERFLOW;
    }
    for (int index = 0; index < fake.portResult; ++index) {
        const auto arrayIndex = static_cast<std::size_t>(index);
        portNumbers[arrayIndex] = fake.ports[arrayIndex];
    }
    return fake.portResult;
}

std::uint8_t LIBUSB_CALL libusb_get_bus_number(libusb_device* device)
{
    return g_devices.at(device->index).busNumber;
}

std::uint8_t LIBUSB_CALL libusb_get_device_address(libusb_device* device)
{
    return g_devices.at(device->index).deviceAddress;
}

} // extern "C"

int main()
{
    testRockusbFilteringAndModeInference();
    testMissingPortPathIsPartialFailure();
    testAccessErrorClassification();
    return g_failures == 0 ? 0 : 1;
}
