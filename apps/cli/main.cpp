// SPDX-License-Identifier: MIT

#include "DeviceListPrinter.hpp"
#include "ProbeSupport.hpp"

#include <zzrk/logging/Logging.hpp>
#include <zzrk/platform/PlatformServices.hpp>
#include <zzrk/transport/LibusbBulkTransport.hpp>
#include <zzrk/transport/UsbDeviceScanner.hpp>

#include <cstdlib>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace {

void printUsage(const std::string_view executable)
{
    std::cout << "Usage: " << executable << " list\n"
              << "       " << executable << " probe <port-path>\n"
              << "       " << executable << " --version\n\n"
              << "The probe command opens and claims one RK3588 Loader "
                 "interface, then releases it without sending a RockUSB "
                 "command.\n";
}

[[nodiscard]] std::string_view describeOpenError(
    const zzrk::transport::LibusbOpenErrorCode code) noexcept
{
    using zzrk::transport::LibusbOpenErrorCode;
    switch (code) {
    case LibusbOpenErrorCode::invalidSelection:
        return "invalid device selection";
    case LibusbOpenErrorCode::initializationFailed:
        return "libusb initialization failed";
    case LibusbOpenErrorCode::enumerationFailed:
        return "USB enumeration failed";
    case LibusbOpenErrorCode::topologyReadFailed:
        return "USB topology read failed";
    case LibusbOpenErrorCode::descriptorReadFailed:
        return "USB device descriptor read failed";
    case LibusbOpenErrorCode::deviceNotFound:
        return "the selected device changed or disappeared";
    case LibusbOpenErrorCode::deviceAmbiguous:
        return "the selected USB topology is ambiguous";
    case LibusbOpenErrorCode::configurationReadFailed:
        return "active USB configuration read failed";
    case LibusbOpenErrorCode::rockusbInterfaceNotFound:
        return "the RK3588 RockUSB interface was not found";
    case LibusbOpenErrorCode::rockusbInterfaceAmbiguous:
        return "multiple RockUSB interfaces were found";
    case LibusbOpenErrorCode::invalidEndpointLayout:
        return "the RockUSB endpoint layout does not match RK3588 Loader";
    case LibusbOpenErrorCode::openFailed:
        return "opening the USB device failed";
    case LibusbOpenErrorCode::kernelDriverQueryFailed:
        return "kernel-driver state query failed";
    case LibusbOpenErrorCode::kernelDriverActive:
        return "a kernel driver owns the RockUSB interface";
    case LibusbOpenErrorCode::claimFailed:
        return "claiming the RockUSB interface failed";
    case LibusbOpenErrorCode::resourceExhausted:
        return "the USB backend ran out of resources";
    }
    return "unknown USB open error";
}

void logScanFailure(const zzrk::transport::ScanError& error)
{
    std::ostringstream message;
    message << "USB probe scan failed (" << error.code
            << "): " << error.message;
    zzrk::logging::error(message.str());

    if (error.kind == zzrk::transport::ScanErrorKind::AccessDenied) {
        const auto platform = zzrk::platform::createPlatformServices();
        zzrk::logging::error(platform->usbAccessHelp());
    }
}

int probeDevice(const std::string_view portPath)
{
    zzrk::logging::debug("Scanning for the explicitly selected Loader device");
    zzrk::transport::UsbDeviceScanner scanner;
    auto scan = scanner.scanRockchipDevices();
    const auto selected = zzrk::cli::selectProbeDevice(scan, portPath);
    if (!selected) {
        switch (selected.error) {
        case zzrk::cli::ProbeSelectionError::scanFailed:
            if (scan.error.has_value()) {
                logScanFailure(*scan.error);
            } else {
                zzrk::logging::error("USB probe scan failed");
            }
            break;
        case zzrk::cli::ProbeSelectionError::ambiguous:
            zzrk::logging::error(
                "USB probe refused an ambiguous physical port path");
            break;
        case zzrk::cli::ProbeSelectionError::notFound: {
            std::string message{"No Rockchip USB device found at port "};
            message += portPath;
            zzrk::logging::error(message);
            break;
        }
        case zzrk::cli::ProbeSelectionError::unsupported:
            zzrk::logging::error(
                "USB probe refused the selected device: this milestone "
                "requires RK3588 Loader mode (2207:350b, bcdUSB 0x0201)");
            break;
        case zzrk::cli::ProbeSelectionError::none:
            zzrk::logging::error("USB probe selection failed");
            break;
        }
        return EXIT_FAILURE;
    }
    const auto* const selection = selected.device;

    zzrk::logging::debug(
        "Opening and claiming the selected Loader interface without a "
        "RockUSB command");
    auto opened = zzrk::transport::LibusbBulkTransport::open(*selection);
    const auto* openError =
        std::get_if<zzrk::transport::LibusbOpenError>(&opened);
    if (openError != nullptr) {
        std::ostringstream message;
        message << "USB probe failed: " << describeOpenError(openError->code);
        if (openError->nativeCode != 0) {
            message << " (libusb " << openError->nativeCode << ')';
        }
        zzrk::logging::error(message.str());
        if (zzrk::transport::isAccessDenied(*openError)) {
            const auto platform = zzrk::platform::createPlatformServices();
            zzrk::logging::error(platform->usbAccessHelp());
        }
        return EXIT_FAILURE;
    }

    auto transport = std::move(
        std::get<std::unique_ptr<zzrk::transport::LibusbBulkTransport>>(opened));
    const auto interfaceInfo = transport->interfaceInfo();
    const int releaseResult = transport->close();
    transport.reset();
    if (releaseResult < 0) {
        std::ostringstream message;
        message << "USB probe could not release the Loader interface (libusb "
                << releaseResult << ')';
        zzrk::logging::error(message.str());
        return EXIT_FAILURE;
    }

    zzrk::cli::printProbeSuccess(*selection, interfaceInfo, std::cout);
    zzrk::logging::info(
        "Loader interface probe completed without sending a RockUSB command");
    return EXIT_SUCCESS;
}

int listDevices()
{
    zzrk::logging::debug("Starting read-only Rockchip USB device scan");
    zzrk::transport::UsbDeviceScanner scanner;
    const auto result = scanner.scanRockchipDevices();
    std::string usbAccessHelp;
    if (result.error.has_value()
        && result.error->kind
               == zzrk::transport::ScanErrorKind::AccessDenied) {
        const auto platform = zzrk::platform::createPlatformServices();
        usbAccessHelp = platform->usbAccessHelp();
    }
    if (result.error.has_value()) {
        if (result.devices.empty()) {
            zzrk::logging::error("Rockchip USB device scan failed");
        } else {
            zzrk::logging::warning("Rockchip USB device scan was incomplete");
        }
    } else {
        zzrk::logging::debug("Rockchip USB device scan completed");
    }
    return zzrk::cli::printScanResult(result, std::cout, std::cerr,
                                      usbAccessHelp);
}

} // namespace

int main(const int argc, char* argv[])
{
    [[maybe_unused]] const auto loggingSession =
        zzrk::logging::initializeConsoleLogging("zzrkdevtool-cli");
    const std::string_view executable = argc > 0 ? argv[0] : "zzrkdevtool-cli";
    if (argc == 2 && std::string_view{argv[1]} == "list") {
        return listDevices();
    }
    if (argc == 3 && std::string_view{argv[1]} == "probe") {
        return probeDevice(argv[2]);
    }
    if (argc == 2 && std::string_view{argv[1]} == "--version") {
        std::cout << "ZzRkDevTool " ZZRK_VERSION "\n";
        return EXIT_SUCCESS;
    }

    printUsage(executable);
    return argc == 1 ? EXIT_SUCCESS : 64;
}
