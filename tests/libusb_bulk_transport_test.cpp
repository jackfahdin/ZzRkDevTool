// SPDX-License-Identifier: MIT

#include <zzrk/transport/LibusbBulkTransport.hpp>

#include <libusb.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <memory>
#include <span>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

struct libusb_context {};

struct libusb_device {
  std::size_t index{};
};

struct libusb_device_handle {
  std::size_t index{};
};

namespace {

using namespace std::chrono_literals;
using zzrk::session::TransferSuccess;
using zzrk::session::TransportError;
using zzrk::session::TransportErrorCode;
using zzrk::transport::LibusbBulkTransport;
using zzrk::transport::LibusbOpenError;
using zzrk::transport::LibusbOpenErrorCode;
using zzrk::transport::UsbDeviceInfo;

constexpr std::size_t kMaximumFakeDevices = 8;
constexpr std::size_t kMaximumInterfaces = 4;
constexpr std::size_t kMaximumEndpoints = 6;
constexpr std::size_t kNoDevice = std::numeric_limits<std::size_t>::max();

enum class EventKind {
  initialize,
  enumerate,
  readTopology,
  readDescriptor,
  readConfiguration,
  freeConfiguration,
  open,
  queryKernelDriver,
  claim,
  bulk,
  control,
  release,
  close,
  freeDeviceList,
  exit,
};

struct Event {
  EventKind kind{};
  std::size_t deviceIndex{kNoDevice};
  int value{};
};

struct EndpointSpec {
  std::uint8_t address{};
  std::uint8_t attributes{};
  std::uint16_t maximumPacketSize{};
};

struct FakeDevice {
  libusb_device_descriptor descriptor{};
  int descriptorResult{LIBUSB_SUCCESS};
  std::array<std::uint8_t, 8> ports{};
  int portResult{};
  std::uint8_t busNumber{};

  int configurationResult{LIBUSB_SUCCESS};
  libusb_config_descriptor configuration{};
  std::array<libusb_interface, kMaximumInterfaces> interfaces{};
  std::array<libusb_interface_descriptor, kMaximumInterfaces> alternates{};
  std::array<std::array<libusb_endpoint_descriptor, kMaximumEndpoints>,
             kMaximumInterfaces>
      endpoints{};

  int openResult{LIBUSB_SUCCESS};
  int kernelDriverResult{};
  int claimResult{LIBUSB_SUCCESS};
  int releaseResult{LIBUSB_SUCCESS};
};

struct BulkAction {
  int result{LIBUSB_SUCCESS};
  int transferred{};
  std::vector<unsigned char> input;
};

struct BulkCall {
  std::size_t deviceIndex{};
  std::uint8_t endpoint{};
  int length{};
  unsigned int timeout{};
  std::vector<unsigned char> output;
};

struct ControlAction {
  int result{};
};

struct ControlCall {
  std::size_t deviceIndex{};
  std::uint8_t requestType{};
  std::uint8_t request{};
  std::uint16_t value{};
  std::uint16_t index{};
  unsigned int timeout{};
  std::vector<unsigned char> output;
};

libusb_context g_context;
std::array<FakeDevice, kMaximumFakeDevices> g_devices{};
std::array<libusb_device, kMaximumFakeDevices> g_deviceHandles{};
std::array<libusb_device_handle, kMaximumFakeDevices> g_openHandles{};
std::size_t g_deviceCount{};
int g_initializationResult{LIBUSB_SUCCESS};
int g_enumerationResult{LIBUSB_SUCCESS};
std::vector<Event> g_events;
std::vector<BulkAction> g_bulkActions;
std::size_t g_nextBulkAction{};
std::vector<BulkCall> g_bulkCalls;
std::vector<ControlAction> g_controlActions;
std::size_t g_nextControlAction{};
std::vector<ControlCall> g_controlCalls;
bool g_bulkScriptMismatch{};
int g_failures{};

void expect(const bool condition, const std::string_view description) {
  if (!condition) {
    std::cerr << "FAIL: " << description << '\n';
    ++g_failures;
  }
}

void record(const EventKind kind, const std::size_t deviceIndex = kNoDevice,
            const int value = 0) {
  g_events.push_back(Event{kind, deviceIndex, value});
}

void resetFakeLibusb() {
  g_devices = {};
  g_deviceHandles = {};
  g_openHandles = {};
  g_deviceCount = 0;
  g_initializationResult = LIBUSB_SUCCESS;
  g_enumerationResult = LIBUSB_SUCCESS;
  g_events.clear();
  g_bulkActions.clear();
  g_nextBulkAction = 0;
  g_bulkCalls.clear();
  g_controlActions.clear();
  g_nextControlAction = 0;
  g_controlCalls.clear();
  g_bulkScriptMismatch = false;
}

FakeDevice &addDevice(const std::uint8_t busNumber,
                      const std::initializer_list<std::uint8_t> ports,
                      const std::uint16_t vendorId = 0x2207,
                      const std::uint16_t productId = 0x350b,
                      const std::uint16_t bcdUsb = 0x0201,
                      const std::uint16_t bcdDevice = 0x0223) {
  FakeDevice &device = g_devices.at(g_deviceCount);
  device.busNumber = busNumber;
  device.portResult = static_cast<int>(ports.size());
  std::ranges::copy(ports, device.ports.begin());
  device.descriptor.idVendor = vendorId;
  device.descriptor.idProduct = productId;
  device.descriptor.bcdUSB = bcdUsb;
  device.descriptor.bcdDevice = bcdDevice;
  g_deviceHandles.at(g_deviceCount).index = g_deviceCount;
  g_openHandles.at(g_deviceCount).index = g_deviceCount;
  ++g_deviceCount;
  return device;
}

void addInterface(FakeDevice &device, const bool rockusb,
                  const std::initializer_list<EndpointSpec> endpoints,
                  const std::uint8_t interfaceNumber = 0,
                  const std::uint8_t alternateSetting = 0) {
  const std::size_t index = device.configuration.bNumInterfaces;
  auto &interface = device.interfaces.at(index);
  auto &alternate = device.alternates.at(index);
  auto &endpointStorage = device.endpoints.at(index);

  alternate.bInterfaceNumber = interfaceNumber;
  alternate.bAlternateSetting = alternateSetting;
  alternate.bInterfaceClass = rockusb ? 0xff : 0x08;
  alternate.bInterfaceSubClass = rockusb ? 0x06 : 0x06;
  alternate.bInterfaceProtocol = rockusb ? 0x05 : 0x50;
  alternate.bNumEndpoints = static_cast<std::uint8_t>(endpoints.size());
  std::size_t endpointIndex = 0;
  for (const auto &endpoint : endpoints) {
    auto &descriptor = endpointStorage.at(endpointIndex);
    descriptor = {};
    descriptor.bEndpointAddress = endpoint.address;
    descriptor.bmAttributes = endpoint.attributes;
    descriptor.wMaxPacketSize = endpoint.maximumPacketSize;
    ++endpointIndex;
  }
  alternate.endpoint = endpointStorage.data();
  interface.altsetting = &alternate;
  interface.num_altsetting = 1;

  ++device.configuration.bNumInterfaces;
  device.configuration.interface = device.interfaces.data();
}

void addDefaultRockusbInterface(FakeDevice &device) {
  addInterface(device, true,
               {
                   EndpointSpec{0x81, LIBUSB_TRANSFER_TYPE_BULK, 0x0a00},
                   EndpointSpec{0x02, LIBUSB_TRANSFER_TYPE_BULK, 0x1200},
               },
               0, 0);
}

void addDefaultMaskromInterface(FakeDevice &device) {
  addInterface(device, true,
               {
                   EndpointSpec{0x81, LIBUSB_TRANSFER_TYPE_BULK, 512},
                   EndpointSpec{0x01, LIBUSB_TRANSFER_TYPE_BULK, 512},
               },
               0, 0);
}

void addBootromMaskromInterface(FakeDevice &device) {
  addInterface(device, true,
               {
                   EndpointSpec{0x81, LIBUSB_TRANSFER_TYPE_BULK, 512},
                   EndpointSpec{0x02, LIBUSB_TRANSFER_TYPE_BULK, 512},
               },
               0, 0);
}

FakeDevice &addUsableDevice(const std::uint8_t busNumber = 1,
                            const std::initializer_list<std::uint8_t> ports = {
                                5}) {
  FakeDevice &device = addDevice(busNumber, ports);
  addDefaultRockusbInterface(device);
  return device;
}

UsbDeviceInfo makeSelection() {
  return UsbDeviceInfo{
      .vendorId = 0x2207,
      .productId = 0x350b,
      .bcdUsb = 0x0201,
      .bcdDevice = 0x0223,
      .busNumber = 1,
      .portNumbers = {5},
      .portPath = "1-5",
      .mode = zzrk::transport::DeviceMode::Loader,
      // Bus addresses are ephemeral and deliberately do not participate in
      // re-selection.
      .deviceAddress = 0xff,
  };
}

UsbDeviceInfo makeMaskromSelection() {
  auto selection = makeSelection();
  selection.bcdUsb = 0x0200;
  selection.bcdDevice = 0x0100;
  selection.mode = zzrk::transport::DeviceMode::Maskrom;
  return selection;
}

const LibusbOpenError *
getOpenError(const LibusbBulkTransport::OpenResult &result,
             const std::string_view description) {
  const auto *error = std::get_if<LibusbOpenError>(&result);
  expect(error != nullptr, description);
  return error;
}

std::unique_ptr<LibusbBulkTransport>
takeTransport(LibusbBulkTransport::OpenResult &&result,
              const std::string_view description) {
  auto *transport = std::get_if<std::unique_ptr<LibusbBulkTransport>>(&result);
  expect(transport != nullptr && *transport != nullptr, description);
  expect(g_bulkCalls.empty() && g_controlCalls.empty(),
         "opening a transport performs no bulk or control transfer");
  if (transport == nullptr) {
    const auto *error = std::get_if<LibusbOpenError>(&result);
    if (error != nullptr) {
      std::cerr << "  open error code=" << static_cast<int>(error->code)
                << " native=" << error->nativeCode << '\n';
    }
  }
  return transport != nullptr ? std::move(*transport) : nullptr;
}

void expectOpenError(const LibusbBulkTransport::OpenResult &result,
                     const LibusbOpenErrorCode code, const int nativeCode,
                     const std::string_view description) {
  const auto *error = getOpenError(result, description);
  expect(error != nullptr && error->code == code &&
             error->nativeCode == nativeCode,
         description);
  expect(g_bulkCalls.empty() && g_controlCalls.empty(),
         "a failed probe/open performs no bulk or control transfer");
  if (error != nullptr &&
      (error->code != code || error->nativeCode != nativeCode)) {
    std::cerr << "  expected code=" << static_cast<int>(code)
              << " native=" << nativeCode
              << ", actual code=" << static_cast<int>(error->code)
              << " native=" << error->nativeCode << '\n';
  }
}

void expectNoTransfers(const std::string_view description) {
  expect(g_bulkCalls.empty() && g_controlCalls.empty(), description);
}

std::size_t eventPosition(const EventKind kind) {
  const auto found = std::ranges::find(g_events, kind, &Event::kind);
  return found == g_events.end()
             ? std::numeric_limits<std::size_t>::max()
             : static_cast<std::size_t>(found - g_events.begin());
}

void testStableTopologyAndDescriptorSelection() {
  {
    resetFakeLibusb();
    auto invalidSelection = makeSelection();
    invalidSelection.portNumbers.clear();
    const auto result = LibusbBulkTransport::open(invalidSelection);
    expectOpenError(result, LibusbOpenErrorCode::invalidSelection, 0,
                    "an empty stable topology is rejected");
    expect(g_events.empty(),
           "invalid selection is rejected before initializing libusb");
  }

  {
    resetFakeLibusb();
    auto inconsistentSelection = makeSelection();
    inconsistentSelection.portPath = "1-6";
    const auto result = LibusbBulkTransport::open(inconsistentSelection);
    expectOpenError(result, LibusbOpenErrorCode::invalidSelection, 0,
                    "display path must agree with structured topology");
    expect(g_events.empty(), "an inconsistent port path cannot reach libusb");
  }

  {
    resetFakeLibusb();
    auto maskromSelection = makeSelection();
    maskromSelection.bcdUsb = 0x0200;
    maskromSelection.mode = zzrk::transport::DeviceMode::Maskrom;
    const auto result = LibusbBulkTransport::open(maskromSelection);
    expectOpenError(result, LibusbOpenErrorCode::invalidSelection, 0,
                    "the initial backend accepts Loader mode only");
    expect(g_events.empty(),
           "an unsupported device mode cannot be opened accidentally");
  }

  {
    resetFakeLibusb();
    auto wrongVendor = makeSelection();
    wrongVendor.vendorId = 0x1234;
    auto wrongProduct = makeSelection();
    wrongProduct.productId = 0x350a;
    auto maskromDescriptor = makeSelection();
    maskromDescriptor.bcdUsb = 0x0200;
    auto wrongRevision = makeSelection();
    wrongRevision.bcdDevice = 0x0224;
    auto inconsistentMode = makeSelection();
    inconsistentMode.mode = zzrk::transport::DeviceMode::Maskrom;
    auto unknownMode = makeSelection();
    unknownMode.mode = zzrk::transport::DeviceMode::Unknown;

    const std::array selections{
        wrongVendor,   wrongProduct,     maskromDescriptor,
        wrongRevision, inconsistentMode, unknownMode,
    };
    for (const auto &selection : selections) {
      const auto result = LibusbBulkTransport::open(selection);
      expectOpenError(result, LibusbOpenErrorCode::invalidSelection, 0,
                      "only the observed RK3588 Loader identity is accepted");
    }
    expect(g_events.empty(),
           "all unsupported identities are rejected before libusb init");
  }

  {
    resetFakeLibusb();
    addUsableDevice(1, {4});
    addUsableDevice(2, {5});
    addUsableDevice(1, {5});
    auto result = LibusbBulkTransport::open(makeSelection());
    auto transport =
        takeTransport(std::move(result), "the exact stable path opens");
    expect(transport != nullptr,
           "bus plus complete port chain selects one device");
    const auto openEvent =
        std::ranges::find(g_events, EventKind::open, &Event::kind);
    expect(openEvent != g_events.end() && openEvent->deviceIndex == 2,
           "selection ignores same-VID/PID devices on other topologies");
    expectNoTransfers("opening a selected device performs no USB transfer");
  }

  {
    resetFakeLibusb();
    FakeDevice &changed = addUsableDevice();
    changed.descriptor.bcdUSB = 0x0200;
    const auto result = LibusbBulkTransport::open(makeSelection());
    expectOpenError(result, LibusbOpenErrorCode::deviceNotFound, 0,
                    "a descriptor identity change invalidates selection");
    expect(eventPosition(EventKind::open) ==
               std::numeric_limits<std::size_t>::max(),
           "a changed descriptor is not opened");
    expectNoTransfers("descriptor revalidation performs no transfer");
  }

  {
    resetFakeLibusb();
    FakeDevice &changed = addUsableDevice();
    changed.descriptor.bcdDevice = 0x0224;
    const auto result = LibusbBulkTransport::open(makeSelection());
    expectOpenError(result, LibusbOpenErrorCode::deviceNotFound, 0,
                    "a device revision change invalidates selection");
    expect(eventPosition(EventKind::open) ==
               std::numeric_limits<std::size_t>::max(),
           "a changed device revision is not opened");
    expectNoTransfers("device revision revalidation performs no transfer");
  }

  {
    resetFakeLibusb();
    addUsableDevice();
    addUsableDevice();
    const auto result = LibusbBulkTransport::open(makeSelection());
    expectOpenError(result, LibusbOpenErrorCode::deviceAmbiguous, 0,
                    "duplicate exact topology matches are rejected");
    expect(eventPosition(EventKind::open) ==
               std::numeric_limits<std::size_t>::max(),
           "an ambiguous device selection is never opened");
  }
}

void testEnumerationAndIdentityFailures() {
  {
    resetFakeLibusb();
    g_initializationResult = LIBUSB_ERROR_ACCESS;
    const auto result = LibusbBulkTransport::open(makeSelection());
    expectOpenError(result, LibusbOpenErrorCode::initializationFailed,
                    LIBUSB_ERROR_ACCESS,
                    "libusb initialization errors retain native codes");
    expect(eventPosition(EventKind::exit) ==
               std::numeric_limits<std::size_t>::max(),
           "a failed context is not passed to libusb_exit");
  }

  {
    resetFakeLibusb();
    g_enumerationResult = LIBUSB_ERROR_IO;
    const auto result = LibusbBulkTransport::open(makeSelection());
    expectOpenError(result, LibusbOpenErrorCode::enumerationFailed,
                    LIBUSB_ERROR_IO,
                    "device-list failures retain native codes");
    expect(eventPosition(EventKind::exit) !=
               std::numeric_limits<std::size_t>::max(),
           "an initialized context is released after enumeration fails");
  }

  {
    resetFakeLibusb();
    FakeDevice &device = addUsableDevice();
    device.portResult = LIBUSB_ERROR_NOT_SUPPORTED;
    const auto result = LibusbBulkTransport::open(makeSelection());
    expectOpenError(result, LibusbOpenErrorCode::topologyReadFailed,
                    LIBUSB_ERROR_NOT_SUPPORTED,
                    "stable-topology lookup failure is distinguished");
    expectNoTransfers("topology probing performs no transfer");
  }

  {
    resetFakeLibusb();
    FakeDevice &device = addUsableDevice();
    device.descriptorResult = LIBUSB_ERROR_NO_DEVICE;
    const auto result = LibusbBulkTransport::open(makeSelection());
    expectOpenError(result, LibusbOpenErrorCode::descriptorReadFailed,
                    LIBUSB_ERROR_NO_DEVICE,
                    "descriptor lookup failure is distinguished");
    expectNoTransfers("descriptor probing performs no transfer");
  }
}

void testInterfaceAndEndpointDiscovery() {
  {
    resetFakeLibusb();
    FakeDevice &device = addDevice(1, {5});
    addInterface(device, false,
                 {EndpointSpec{0x81, LIBUSB_TRANSFER_TYPE_BULK, 512},
                  EndpointSpec{0x02, LIBUSB_TRANSFER_TYPE_BULK, 512}});
    const auto result = LibusbBulkTransport::open(makeSelection());
    expectOpenError(result, LibusbOpenErrorCode::rockusbInterfaceNotFound, 0,
                    "a non-RockUSB interface is rejected");
    expect(eventPosition(EventKind::open) ==
               std::numeric_limits<std::size_t>::max(),
           "interface probing precedes opening the device");
  }

  {
    resetFakeLibusb();
    FakeDevice &device = addUsableDevice();
    addDefaultRockusbInterface(device);
    const auto result = LibusbBulkTransport::open(makeSelection());
    expectOpenError(result, LibusbOpenErrorCode::rockusbInterfaceAmbiguous, 0,
                    "multiple RockUSB interfaces are rejected");
  }

  {
    resetFakeLibusb();
    FakeDevice &device = addDevice(1, {5});
    addDefaultRockusbInterface(device);
    auto result = LibusbBulkTransport::open(makeSelection());
    auto transport =
        takeTransport(std::move(result), "one RockUSB interface is discovered");
    if (transport != nullptr) {
      const auto &info = transport->interfaceInfo();
      expect(info.interfaceNumber == 0 && info.alternateSetting == 0 &&
                 info.bulkInEndpoint == 0x81 && info.bulkOutEndpoint == 0x02,
             "interface and endpoint addresses are exposed exactly");
      expect(info.bulkInMaxPacketSize == 512 &&
                 info.bulkOutMaxPacketSize == 512,
             "packet-size metadata masks non-size descriptor bits");
    }
    expectNoTransfers("descriptor and endpoint discovery performs no I/O");
  }

  {
    resetFakeLibusb();
    FakeDevice &device = addDevice(1, {5});
    addInterface(device, false, {});
    addDefaultRockusbInterface(device);
    const auto result = LibusbBulkTransport::open(makeSelection());
    expectOpenError(result, LibusbOpenErrorCode::invalidEndpointLayout, 0,
                    "an unobserved extra USB interface is rejected");
  }

  {
    resetFakeLibusb();
    FakeDevice &device = addDevice(1, {5});
    addInterface(device, true,
                 {EndpointSpec{0x81, LIBUSB_TRANSFER_TYPE_BULK, 512},
                  EndpointSpec{0x02, LIBUSB_TRANSFER_TYPE_BULK, 512},
                  EndpointSpec{0x83, LIBUSB_TRANSFER_TYPE_INTERRUPT, 64}});
    const auto result = LibusbBulkTransport::open(makeSelection());
    expectOpenError(result, LibusbOpenErrorCode::invalidEndpointLayout, 0,
                    "an unobserved extra endpoint is rejected");
  }

  {
    resetFakeLibusb();
    FakeDevice &device = addDevice(1, {5});
    addInterface(device, true,
                 {EndpointSpec{0x81, LIBUSB_TRANSFER_TYPE_BULK, 512},
                  EndpointSpec{0x83, LIBUSB_TRANSFER_TYPE_INTERRUPT, 64}},
                 0);
    const auto result = LibusbBulkTransport::open(makeSelection());
    expectOpenError(result, LibusbOpenErrorCode::invalidEndpointLayout, 0,
                    "a missing bulk OUT endpoint is rejected");
  }

  {
    resetFakeLibusb();
    FakeDevice &device = addDevice(1, {5});
    addInterface(device, true,
                 {EndpointSpec{0x81, LIBUSB_TRANSFER_TYPE_BULK, 512},
                  EndpointSpec{0x02, LIBUSB_TRANSFER_TYPE_BULK, 512},
                  EndpointSpec{0x04, LIBUSB_TRANSFER_TYPE_BULK, 512}},
                 0);
    const auto result = LibusbBulkTransport::open(makeSelection());
    expectOpenError(result, LibusbOpenErrorCode::invalidEndpointLayout, 0,
                    "duplicate bulk OUT endpoints are rejected");
  }

  {
    resetFakeLibusb();
    FakeDevice &device = addDevice(1, {5});
    addInterface(device, true,
                 {EndpointSpec{0x81, LIBUSB_TRANSFER_TYPE_BULK, 512},
                  EndpointSpec{0x02, LIBUSB_TRANSFER_TYPE_BULK, 512}},
                 1);
    const auto result = LibusbBulkTransport::open(makeSelection());
    expectOpenError(result, LibusbOpenErrorCode::invalidEndpointLayout, 0,
                    "an unobserved RockUSB interface number is rejected");
  }

  {
    resetFakeLibusb();
    FakeDevice &device = addDevice(1, {5});
    addInterface(device, true,
                 {EndpointSpec{0x81, LIBUSB_TRANSFER_TYPE_BULK, 512},
                  EndpointSpec{0x02, LIBUSB_TRANSFER_TYPE_BULK, 512}},
                 0, 1);
    const auto result = LibusbBulkTransport::open(makeSelection());
    expectOpenError(result, LibusbOpenErrorCode::invalidEndpointLayout, 0,
                    "an unobserved alternate setting is rejected");
  }

  {
    resetFakeLibusb();
    FakeDevice &device = addDevice(1, {5});
    addInterface(device, true,
                 {EndpointSpec{0x81, LIBUSB_TRANSFER_TYPE_BULK, 1024},
                  EndpointSpec{0x02, LIBUSB_TRANSFER_TYPE_BULK, 512}});
    const auto result = LibusbBulkTransport::open(makeSelection());
    expectOpenError(result, LibusbOpenErrorCode::invalidEndpointLayout, 0,
                    "an unobserved maximum packet size is rejected");
  }

  {
    resetFakeLibusb();
    FakeDevice &device = addUsableDevice();
    device.configurationResult = LIBUSB_ERROR_NOT_FOUND;
    const auto result = LibusbBulkTransport::open(makeSelection());
    expectOpenError(result, LibusbOpenErrorCode::configurationReadFailed,
                    LIBUSB_ERROR_NOT_FOUND,
                    "active-configuration failures retain native codes");
  }
}

void testOpenAndClaimFailures() {
  {
    resetFakeLibusb();
    FakeDevice &device = addUsableDevice();
    device.openResult = LIBUSB_ERROR_ACCESS;
    const auto result = LibusbBulkTransport::open(makeSelection());
    expectOpenError(result, LibusbOpenErrorCode::openFailed,
                    LIBUSB_ERROR_ACCESS,
                    "permission-denied open is reported precisely");
    const auto *error = std::get_if<LibusbOpenError>(&result);
    expect(error != nullptr && zzrk::transport::isAccessDenied(*error),
           "access-denied classification works for open failures");
    expect(eventPosition(EventKind::queryKernelDriver) ==
               std::numeric_limits<std::size_t>::max(),
           "an unopened handle is never queried or claimed");
    expectNoTransfers("failed open performs no transfer");
  }

  {
    resetFakeLibusb();
    FakeDevice &device = addUsableDevice();
    device.kernelDriverResult = LIBUSB_ERROR_IO;
    const auto result = LibusbBulkTransport::open(makeSelection());
    expectOpenError(result, LibusbOpenErrorCode::kernelDriverQueryFailed,
                    LIBUSB_ERROR_IO,
                    "kernel-driver query errors retain native codes");
    expect(eventPosition(EventKind::claim) ==
               std::numeric_limits<std::size_t>::max(),
           "a failed kernel-driver query prevents claim");
    expect(eventPosition(EventKind::close) < eventPosition(EventKind::exit),
           "kernel-driver query failure closes before exiting context");
  }

  {
    resetFakeLibusb();
    FakeDevice &device = addUsableDevice();
    device.kernelDriverResult = LIBUSB_ERROR_NOT_SUPPORTED;
    auto result = LibusbBulkTransport::open(makeSelection());
    auto transport =
        takeTransport(std::move(result),
                      "unsupported kernel-driver query can still claim safely");
    expect(transport != nullptr && eventPosition(EventKind::claim) !=
                                       std::numeric_limits<std::size_t>::max(),
           "platforms without kernel-driver queries continue to claim");
    expectNoTransfers(
        "kernel query fallback and claim perform no USB transfer");
  }

  {
    resetFakeLibusb();
    FakeDevice &device = addUsableDevice();
    device.kernelDriverResult = 1;
    const auto result = LibusbBulkTransport::open(makeSelection());
    expectOpenError(result, LibusbOpenErrorCode::kernelDriverActive, 1,
                    "an active kernel driver is not detached implicitly");
    expect(eventPosition(EventKind::claim) ==
               std::numeric_limits<std::size_t>::max(),
           "an active kernel driver prevents interface claim");
    expectNoTransfers("kernel-driver inspection performs no transfer");
  }

  {
    resetFakeLibusb();
    FakeDevice &device = addUsableDevice();
    device.claimResult = LIBUSB_ERROR_BUSY;
    const auto result = LibusbBulkTransport::open(makeSelection());
    expectOpenError(result, LibusbOpenErrorCode::claimFailed, LIBUSB_ERROR_BUSY,
                    "claim failure retains its native code");
    expect(eventPosition(EventKind::release) ==
               std::numeric_limits<std::size_t>::max(),
           "an interface that was not claimed is not released");
    expect(eventPosition(EventKind::close) < eventPosition(EventKind::exit),
           "claim failure still closes the handle before context exit");
    expectNoTransfers("claiming an interface performs no transfer");
  }
}

void testDestructorLifetimeOrder() {
  resetFakeLibusb();
  addUsableDevice();
  {
    auto result = LibusbBulkTransport::open(makeSelection());
    auto transport =
        takeTransport(std::move(result), "RAII test transport opens");
    expect(transport != nullptr && eventPosition(EventKind::exit) ==
                                       std::numeric_limits<std::size_t>::max(),
           "the transport owns its claimed handle and context");
    expectNoTransfers("constructing the RAII transport performs no I/O");
  }

  const auto release = eventPosition(EventKind::release);
  const auto close = eventPosition(EventKind::close);
  const auto exit = eventPosition(EventKind::exit);
  expect(release < close && close < exit,
         "destructor releases the interface, closes the handle, then exits");
}

void testExplicitCloseOrderAndIdempotency() {
  resetFakeLibusb();
  FakeDevice &device = addUsableDevice();
  device.releaseResult = LIBUSB_ERROR_NO_DEVICE;
  auto result = LibusbBulkTransport::open(makeSelection());
  auto transport = takeTransport(std::move(result), "a usable interface opens");

  expect(eventPosition(EventKind::freeConfiguration) !=
                 std::numeric_limits<std::size_t>::max() &&
             eventPosition(EventKind::freeDeviceList) !=
                 std::numeric_limits<std::size_t>::max(),
         "temporary descriptor and device-list resources are released");
  expect(eventPosition(EventKind::exit) ==
             std::numeric_limits<std::size_t>::max(),
         "the context remains alive with the transport");
  expectNoTransfers("probe and open contain no bulk or control requests");

  const int closeResult = transport->close();
  expect(closeResult == LIBUSB_ERROR_NO_DEVICE,
         "explicit close exposes the native interface-release failure");
  const auto release = eventPosition(EventKind::release);
  const auto close = eventPosition(EventKind::close);
  const auto exit = eventPosition(EventKind::exit);
  expect(
      release < close && close < exit,
      "destruction orders interface release, handle close, then context exit");
  const auto releaseEvent =
      std::ranges::find(g_events, EventKind::release, &Event::kind);
  expect(releaseEvent != g_events.end() && releaseEvent->value == 0,
         "close releases the exact claimed interface");

  const auto eventCountAfterClose = g_events.size();
  expect(transport->close() == LIBUSB_SUCCESS &&
             g_events.size() == eventCountAfterClose,
         "close is idempotent and does not release resources twice");
  std::array<std::byte, 1> byte{};
  const auto closedTransfer = transport->bulkIn(byte, 1ms);
  const auto *closedError = std::get_if<TransportError>(&closedTransfer);
  expect(closedError != nullptr &&
             closedError->code == TransportErrorCode::invalidArgument &&
             g_bulkCalls.empty(),
         "a closed transport rejects I/O before calling libusb");
  transport.reset();
  expect(g_events.size() == eventCountAfterClose,
         "destruction after explicit close has no duplicate side effects");
}

void queueBulk(const int result, const int transferred,
               const std::initializer_list<unsigned char> input = {}) {
  g_bulkActions.push_back(BulkAction{
      .result = result,
      .transferred = transferred,
      .input = {input.begin(), input.end()},
  });
}

void queueControl(const int result) {
  g_controlActions.push_back(ControlAction{.result = result});
}

void testBulkDirectionLengthAndTimeout() {
  resetFakeLibusb();
  addUsableDevice();
  auto openResult = LibusbBulkTransport::open(makeSelection());
  auto transport =
      takeTransport(std::move(openResult), "bulk test transport opens");
  if (transport == nullptr) {
    return;
  }

  g_bulkCalls.clear();
  queueBulk(LIBUSB_SUCCESS, 3);
  const std::array<const std::byte, 4> output{std::byte{0x10}, std::byte{0x20},
                                              std::byte{0x30}, std::byte{0x40}};
  const auto outResult = transport->bulkOut(output, 123ms);
  const auto *outSuccess = std::get_if<TransferSuccess>(&outResult);
  expect(outSuccess != nullptr && outSuccess->bytesTransferred == 3,
         "a valid short bulk OUT remains a successful single transfer");
  expect(g_bulkCalls.size() == 1 && g_bulkCalls[0].endpoint == 0x02 &&
             g_bulkCalls[0].length == 4 && g_bulkCalls[0].timeout == 123 &&
             g_bulkCalls[0].output ==
                 std::vector<unsigned char>{0x10, 0x20, 0x30, 0x40},
         "bulk OUT forwards endpoint, bytes, length, and finite timeout");

  queueBulk(LIBUSB_SUCCESS, 4, {0xde, 0xad, 0xbe, 0xef});
  std::array<std::byte, 4> input{};
  const auto inResult = transport->bulkIn(input, 456ms);
  const auto *inSuccess = std::get_if<TransferSuccess>(&inResult);
  expect(inSuccess != nullptr && inSuccess->bytesTransferred == 4,
         "bulk IN returns the libusb byte count");
  expect(input == std::array<std::byte, 4>{std::byte{0xde}, std::byte{0xad},
                                           std::byte{0xbe}, std::byte{0xef}},
         "bulk IN writes the exact received payload");
  expect(g_bulkCalls.size() == 2 && g_bulkCalls[1].endpoint == 0x81 &&
             g_bulkCalls[1].length == 4 && g_bulkCalls[1].timeout == 456,
         "bulk IN uses the discovered IN endpoint exactly once");
  expect(g_controlCalls.empty(),
         "bulk operations never issue hidden control transfers");
}

void testBulkErrorMappingAndPartialCounts() {
  resetFakeLibusb();
  addUsableDevice();
  auto openResult = LibusbBulkTransport::open(makeSelection());
  auto transport =
      takeTransport(std::move(openResult), "error-mapping transport opens");
  if (transport == nullptr) {
    return;
  }

  struct Mapping {
    int nativeCode{};
    TransportErrorCode code{};
  };
  constexpr std::array mappings{
      Mapping{LIBUSB_ERROR_TIMEOUT, TransportErrorCode::timedOut},
      Mapping{LIBUSB_ERROR_NO_DEVICE, TransportErrorCode::disconnected},
      Mapping{LIBUSB_ERROR_ACCESS, TransportErrorCode::accessDenied},
      Mapping{LIBUSB_ERROR_PIPE, TransportErrorCode::stalled},
      Mapping{LIBUSB_ERROR_OVERFLOW, TransportErrorCode::overflow},
      Mapping{LIBUSB_ERROR_BUSY, TransportErrorCode::busy},
      Mapping{LIBUSB_ERROR_INTERRUPTED, TransportErrorCode::interrupted},
      Mapping{LIBUSB_ERROR_NOT_SUPPORTED, TransportErrorCode::notSupported},
      Mapping{LIBUSB_ERROR_NO_MEM, TransportErrorCode::resourceExhausted},
      Mapping{LIBUSB_ERROR_INVALID_PARAM, TransportErrorCode::invalidArgument},
      Mapping{LIBUSB_ERROR_IO, TransportErrorCode::io},
      Mapping{LIBUSB_ERROR_NOT_FOUND, TransportErrorCode::other},
      Mapping{LIBUSB_ERROR_OTHER, TransportErrorCode::other},
  };

  std::array<std::byte, 4> buffer{};
  for (const auto &mapping : mappings) {
    queueBulk(mapping.nativeCode, 2, {0xaa, 0xbb});
    const auto result = transport->bulkIn(buffer, 10ms);
    const auto *error = std::get_if<TransportError>(&result);
    expect(error != nullptr && error->code == mapping.code &&
               error->nativeCode == mapping.nativeCode &&
               error->bytesTransferred == 2,
           "native bulk error maps with its partial transfer count");
  }
  expect(g_bulkCalls.size() == mappings.size() && !g_bulkScriptMismatch,
         "each mapped failure performs exactly one libusb bulk call");

  queueBulk(LIBUSB_ERROR_TIMEOUT, -1);
  const auto negativeCountResult = transport->bulkIn(buffer, 10ms);
  const auto *negativeCountError =
      std::get_if<TransportError>(&negativeCountResult);
  expect(negativeCountError != nullptr &&
             negativeCountError->bytesTransferred == 0,
         "a nonsensical negative native transfer count is clamped to zero");
}

void testInvalidTimeoutsDoNotReachLibusb() {
  resetFakeLibusb();
  addUsableDevice();
  auto openResult = LibusbBulkTransport::open(makeSelection());
  auto transport = takeTransport(std::move(openResult),
                                 "timeout-validation transport opens");
  if (transport == nullptr) {
    return;
  }

  std::array<std::byte, 1> byte{};
  const auto zeroResult = transport->bulkIn(byte, 0ms);
  const auto negativeResult = transport->bulkOut(byte, -1ms);
  constexpr auto tooLarge =
      std::chrono::milliseconds{static_cast<std::chrono::milliseconds::rep>(
                                    std::numeric_limits<unsigned int>::max()) +
                                1};
  const auto largeResult = transport->bulkIn(byte, tooLarge);
  std::span<std::byte> emptyInput;
  std::span<const std::byte> emptyOutput;
  const auto emptyInResult = transport->bulkIn(emptyInput, 1ms);
  const auto emptyOutResult = transport->bulkOut(emptyOutput, 1ms);

  for (const auto *result : {&zeroResult, &negativeResult, &largeResult,
                             &emptyInResult, &emptyOutResult}) {
    const auto *error = std::get_if<TransportError>(result);
    expect(error != nullptr &&
               error->code == TransportErrorCode::invalidArgument &&
               error->nativeCode == LIBUSB_ERROR_INVALID_PARAM &&
               error->bytesTransferred == 0,
           "invalid finite-timeout input has a typed local error");
  }
  expect(g_bulkCalls.empty(),
         "invalid timeouts and empty spans perform no I/O");
  expect(g_controlCalls.empty(),
         "timeout validation performs no control transfer");
}

void testMaskromOpenAndRestrictedControlTransfer() {
  resetFakeLibusb();
  FakeDevice &device = addDevice(1, {5}, 0x2207, 0x350b, 0x0200, 0x0100);
  addDefaultMaskromInterface(device);

  auto openResult = LibusbBulkTransport::openMaskrom(makeMaskromSelection());
  auto transport =
      takeTransport(std::move(openResult), "the exact Maskrom identity opens");
  if (transport == nullptr) {
    return;
  }
  expect(transport->interfaceInfo().bulkOutEndpoint == 0x01,
         "the RAM usbplug Maskrom descriptor accepts bulk OUT 0x01");

  const std::array<const std::byte, 3> payload{std::byte{0x10}, std::byte{0x20},
                                               std::byte{0x30}};
  queueControl(3);
  const auto result = transport->maskromControlOut(0x0471, payload, 321ms);
  const auto *success = std::get_if<TransferSuccess>(&result);
  expect(success != nullptr && success->bytesTransferred == payload.size(),
         "a complete Maskrom control transfer returns its byte count");
  expect(g_controlCalls.size() == 1 && g_controlCalls[0].requestType == 0x40 &&
             g_controlCalls[0].request == 0x0c &&
             g_controlCalls[0].value == 0 &&
             g_controlCalls[0].index == 0x0471 &&
             g_controlCalls[0].timeout == 321 &&
             g_controlCalls[0].output ==
                 std::vector<unsigned char>{0x10, 0x20, 0x30},
         "Maskrom control forwards the fixed vendor request exactly");

  const auto invalid = transport->maskromControlOut(0x0470, payload, 1ms);
  const auto *error = std::get_if<TransportError>(&invalid);
  expect(error != nullptr &&
             error->code == TransportErrorCode::invalidArgument &&
             g_controlCalls.size() == 1,
         "control request indexes other than 0x471/0x472 fail locally");

  queueControl(LIBUSB_ERROR_TIMEOUT);
  const auto timedOut = transport->maskromControlOut(0x0472, payload, 2ms);
  const auto *timeoutError = std::get_if<TransportError>(&timedOut);
  expect(timeoutError != nullptr &&
             timeoutError->code == TransportErrorCode::timedOut &&
             timeoutError->nativeCode == LIBUSB_ERROR_TIMEOUT,
         "Maskrom control errors retain typed libusb failure details");
}

void testMaskromBootromEndpointLayout() {
  {
    resetFakeLibusb();
    FakeDevice &device = addDevice(1, {5}, 0x2207, 0x350b, 0x0200, 0x0100);
    addBootromMaskromInterface(device);

    auto openResult =
        LibusbBulkTransport::openMaskrom(makeMaskromSelection());
    auto transport = takeTransport(
        std::move(openResult), "the BootROM Maskrom endpoint layout opens");
    expect(transport != nullptr &&
               transport->interfaceInfo().bulkOutEndpoint == 0x02,
           "the BootROM Maskrom descriptor accepts bulk OUT 0x02");
  }

  resetFakeLibusb();
  FakeDevice &unsupported =
      addDevice(1, {5}, 0x2207, 0x350b, 0x0200, 0x0100);
  addDefaultMaskromInterface(unsupported);
  unsupported.endpoints[0][1].bEndpointAddress = 0x03;
  const auto rejected =
      LibusbBulkTransport::openMaskrom(makeMaskromSelection());
  const auto *error = std::get_if<LibusbOpenError>(&rejected);
  expect(error != nullptr &&
             error->code == LibusbOpenErrorCode::invalidEndpointLayout,
         "an unobserved Maskrom bulk OUT endpoint remains rejected");
}

} // namespace

extern "C" {

int LIBUSB_CALL libusb_init(libusb_context **context) {
  record(EventKind::initialize);
  *context = g_initializationResult == LIBUSB_SUCCESS ? &g_context : nullptr;
  return g_initializationResult;
}

void LIBUSB_CALL libusb_exit(libusb_context *context) {
  if (context != nullptr) {
    record(EventKind::exit);
  }
}

ssize_t LIBUSB_CALL libusb_get_device_list(libusb_context *context,
                                           libusb_device ***list) {
  static_cast<void>(context);
  record(EventKind::enumerate);
  if (g_enumerationResult < 0) {
    *list = nullptr;
    return g_enumerationResult;
  }

  auto **devices = new libusb_device *[g_deviceCount + 1];
  for (std::size_t index = 0; index < g_deviceCount; ++index) {
    devices[index] = &g_deviceHandles[index];
  }
  devices[g_deviceCount] = nullptr;
  *list = devices;
  return static_cast<ssize_t>(g_deviceCount);
}

void LIBUSB_CALL libusb_free_device_list(libusb_device **list,
                                         const int unrefDevices) {
  static_cast<void>(unrefDevices);
  delete[] list;
  record(EventKind::freeDeviceList);
}

std::uint8_t LIBUSB_CALL libusb_get_bus_number(libusb_device *device) {
  return g_devices.at(device->index).busNumber;
}

int LIBUSB_CALL libusb_get_port_numbers(libusb_device *device,
                                        std::uint8_t *portNumbers,
                                        const int portNumbersLength) {
  const FakeDevice &fake = g_devices.at(device->index);
  record(EventKind::readTopology, device->index);
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

int LIBUSB_CALL libusb_get_device_descriptor(
    libusb_device *device, libusb_device_descriptor *descriptor) {
  const FakeDevice &fake = g_devices.at(device->index);
  record(EventKind::readDescriptor, device->index);
  if (fake.descriptorResult < 0) {
    return fake.descriptorResult;
  }
  *descriptor = fake.descriptor;
  return LIBUSB_SUCCESS;
}

int LIBUSB_CALL libusb_get_active_config_descriptor(
    libusb_device *device, libusb_config_descriptor **configuration) {
  FakeDevice &fake = g_devices.at(device->index);
  record(EventKind::readConfiguration, device->index);
  if (fake.configurationResult < 0) {
    *configuration = nullptr;
    return fake.configurationResult;
  }
  *configuration = &fake.configuration;
  return LIBUSB_SUCCESS;
}

void LIBUSB_CALL
libusb_free_config_descriptor(libusb_config_descriptor *configuration) {
  static_cast<void>(configuration);
  record(EventKind::freeConfiguration);
}

int LIBUSB_CALL libusb_open(libusb_device *device,
                            libusb_device_handle **handle) {
  FakeDevice &fake = g_devices.at(device->index);
  record(EventKind::open, device->index);
  if (fake.openResult < 0) {
    *handle = nullptr;
    return fake.openResult;
  }
  *handle = &g_openHandles.at(device->index);
  return LIBUSB_SUCCESS;
}

void LIBUSB_CALL libusb_close(libusb_device_handle *handle) {
  record(EventKind::close, handle->index);
}

int LIBUSB_CALL libusb_kernel_driver_active(libusb_device_handle *handle,
                                            const int interfaceNumber) {
  record(EventKind::queryKernelDriver, handle->index, interfaceNumber);
  return g_devices.at(handle->index).kernelDriverResult;
}

int LIBUSB_CALL libusb_claim_interface(libusb_device_handle *handle,
                                       const int interfaceNumber) {
  record(EventKind::claim, handle->index, interfaceNumber);
  return g_devices.at(handle->index).claimResult;
}

int LIBUSB_CALL libusb_release_interface(libusb_device_handle *handle,
                                         const int interfaceNumber) {
  record(EventKind::release, handle->index, interfaceNumber);
  return g_devices.at(handle->index).releaseResult;
}

int LIBUSB_CALL libusb_bulk_transfer(libusb_device_handle *handle,
                                     const unsigned char endpoint,
                                     unsigned char *data, const int length,
                                     int *transferred,
                                     const unsigned int timeout) {
  record(EventKind::bulk, handle->index, endpoint);
  BulkCall call{
      .deviceIndex = handle->index,
      .endpoint = endpoint,
      .length = length,
      .timeout = timeout,
      .output = {},
  };
  if ((endpoint & LIBUSB_ENDPOINT_DIR_MASK) == LIBUSB_ENDPOINT_OUT &&
      length > 0) {
    call.output.assign(data, data + length);
  }
  g_bulkCalls.push_back(std::move(call));

  if (g_nextBulkAction >= g_bulkActions.size()) {
    g_bulkScriptMismatch = true;
    *transferred = 0;
    return LIBUSB_ERROR_OTHER;
  }
  const BulkAction &action = g_bulkActions[g_nextBulkAction];
  ++g_nextBulkAction;
  *transferred = action.transferred;
  if ((endpoint & LIBUSB_ENDPOINT_DIR_MASK) == LIBUSB_ENDPOINT_IN &&
      length > 0 && action.transferred > 0) {
    const auto copySize =
        std::min({static_cast<std::size_t>(length), action.input.size(),
                  static_cast<std::size_t>(action.transferred)});
    for (std::size_t index = 0; index < copySize; ++index) {
      data[index] = action.input[index];
    }
  }
  return action.result;
}

int LIBUSB_CALL libusb_control_transfer(
    libusb_device_handle *handle, const std::uint8_t requestType,
    const std::uint8_t request, const std::uint16_t value,
    const std::uint16_t index, unsigned char *data, const std::uint16_t length,
    const unsigned int timeout) {
  ControlCall call{
      .deviceIndex = handle->index,
      .requestType = requestType,
      .request = request,
      .value = value,
      .index = index,
      .timeout = timeout,
      .output = {},
  };
  if (data != nullptr && length != 0U) {
    call.output.assign(data, data + length);
  }
  g_controlCalls.push_back(std::move(call));
  record(EventKind::control, handle->index);
  if (g_nextControlAction >= g_controlActions.size()) {
    return LIBUSB_ERROR_OTHER;
  }
  return g_controlActions[g_nextControlAction++].result;
}

} // extern "C"

int main() {
  testStableTopologyAndDescriptorSelection();
  testEnumerationAndIdentityFailures();
  testInterfaceAndEndpointDiscovery();
  testOpenAndClaimFailures();
  testDestructorLifetimeOrder();
  testExplicitCloseOrderAndIdempotency();
  testBulkDirectionLengthAndTimeout();
  testBulkErrorMappingAndPartialCounts();
  testInvalidTimeoutsDoNotReachLibusb();
  testMaskromOpenAndRestrictedControlTransfer();
  testMaskromBootromEndpointLayout();
  return g_failures == 0 ? 0 : 1;
}
