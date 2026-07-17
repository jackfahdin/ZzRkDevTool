// SPDX-License-Identifier: MIT

#include "DeviceListPrinter.hpp"
#include "ProbeSupport.hpp"

#include <zzrk/logging/Logging.hpp>
#include <zzrk/platform/PlatformServices.hpp>
#include <zzrk/session/ReadChipInfo.hpp>
#include <zzrk/session/ReadFlashId.hpp>
#include <zzrk/session/ReadFlashInfo.hpp>
#include <zzrk/session/RockusbSession.hpp>
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

void printUsage(const std::string_view executable, std::ostream& output)
{
    output << "Usage: " << executable << " list\n"
           << "       " << executable << " probe <port-path>\n"
           << "       " << executable << " read-chip-info <port-path>\n"
           << "       " << executable << " read-flash-info <port-path>\n"
           << "       " << executable << " read-flash-id <port-path>\n"
           << "       " << executable << " --version\n\n"
           << "The probe command opens and claims one RK3588 Loader "
              "interface, then releases it without sending a RockUSB "
              "command.\n"
           << "The read-chip-info command sends one read-only RockUSB "
              "command to the explicitly selected Loader device.\n"
           << "The read-flash-info command sends one read-only RockUSB "
              "command to the explicitly selected Loader device.\n"
           << "The read-flash-id command sends one read-only RockUSB "
              "command to the explicitly selected Loader device.\n";
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

[[nodiscard]] std::string_view describeSessionError(
    const zzrk::session::SessionErrorCode code) noexcept
{
    using zzrk::session::SessionErrorCode;
    switch (code) {
    case SessionErrorCode::sessionFaulted:
        return "the session is faulted";
    case SessionErrorCode::invalidTimeout:
        return "a session timeout is invalid";
    case SessionErrorCode::commandEncodeFailed:
        return "the RockUSB command could not be encoded";
    case SessionErrorCode::emptyDataBuffer:
        return "the RockUSB data buffer is empty";
    case SessionErrorCode::bufferTooLarge:
        return "the RockUSB data buffer is too large";
    case SessionErrorCode::transportFailure:
        return "the USB transfer failed";
    case SessionErrorCode::shortTransfer:
        return "the USB transfer was short";
    case SessionErrorCode::invalidTransferCount:
        return "the USB backend returned an invalid byte count";
    case SessionErrorCode::deviceCommandFailed:
        return "the Loader rejected the RockUSB command";
    case SessionErrorCode::statusDecodeFailed:
        return "the RockUSB status wrapper is invalid";
    case SessionErrorCode::nonZeroResidue:
        return "the Loader reported untransferred data";
    }
    return "unknown session error";
}

[[nodiscard]] std::string_view
describeSessionStage(const zzrk::session::SessionStage stage) noexcept
{
    using zzrk::session::SessionStage;
    switch (stage) {
    case SessionStage::request:
        return "request";
    case SessionStage::command:
        return "command";
    case SessionStage::data:
        return "data";
    case SessionStage::status:
        return "status";
    }
    return "unknown";
}

void logScanFailure(const zzrk::transport::ScanError& error,
                    const std::string_view operation)
{
    std::ostringstream message;
    message << operation << " scan failed (" << error.code
            << "): " << error.message;
    zzrk::logging::error(message.str());

    if (error.kind == zzrk::transport::ScanErrorKind::AccessDenied) {
        const auto platform = zzrk::platform::createPlatformServices();
        zzrk::logging::error(platform->usbAccessHelp());
    }
}

void logReadFailure(const zzrk::session::SessionError& error,
                    const std::string_view operation)
{
    std::ostringstream message;
    message << operation << " failed during "
            << describeSessionStage(error.stage) << ": "
            << describeSessionError(error.code);
    if (error.expectedBytes != 0 || error.actualBytes != 0) {
        message << " (expected " << error.expectedBytes << " bytes, got "
                << error.actualBytes << ')';
    }
    if (error.transportError.has_value()) {
        message << " (transport code "
                << static_cast<int>(error.transportError->code);
        if (error.transportError->nativeCode != 0) {
            message << ", native " << error.transportError->nativeCode;
        }
        message << ')';
    }
    if (error.timeout.count() != 0) {
        message << " (timeout " << error.timeout.count() << " ms)";
    }
    if (error.residue != 0U) {
        message << " (residue " << error.residue << ')';
    }
    if (error.commandEncodeError.has_value()) {
        message << " (command encode code "
                << static_cast<int>(error.commandEncodeError->code)
                << ", value "
                << static_cast<unsigned int>(error.commandEncodeError->value)
                << ')';
    }
    if (error.statusDecodeError.has_value()) {
        const auto& status = *error.statusDecodeError;
        message << " (status decode code " << static_cast<int>(status.code)
                << ", raw status "
                << static_cast<unsigned int>(status.rawStatus)
                << ", residue " << status.dataResidue << ", expected tag "
                << status.expectedTag << ", actual tag " << status.actualTag
                << ')';
    }
    zzrk::logging::error(message.str());
}

int probeDevice(const std::string_view portPath)
{
    zzrk::logging::debug("Scanning for the explicitly selected Loader device");
    zzrk::transport::UsbDeviceScanner scanner;
    auto scan = scanner.scanRockchipDevices();
    const auto selected = zzrk::cli::selectLoaderDevice(scan, portPath);
    if (!selected) {
        switch (selected.error) {
        case zzrk::cli::LoaderSelectionError::scanFailed:
            if (scan.error.has_value()) {
                logScanFailure(*scan.error, "USB probe");
            } else {
                zzrk::logging::error("USB probe scan failed");
            }
            break;
        case zzrk::cli::LoaderSelectionError::ambiguous:
            zzrk::logging::error(
                "USB probe refused an ambiguous physical port path");
            break;
        case zzrk::cli::LoaderSelectionError::notFound: {
            std::string message{"No Rockchip USB device found at port "};
            message += portPath;
            zzrk::logging::error(message);
            break;
        }
        case zzrk::cli::LoaderSelectionError::unsupported:
            zzrk::logging::error(
                "USB probe refused the selected device: this milestone "
                "requires the validated RK3588 Loader identity "
                "(2207:350b, bcdUSB 0x0201, bcdDevice 0x0223)");
            break;
        case zzrk::cli::LoaderSelectionError::none:
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

template <typename Value, typename Reader, typename Printer>
int readLoaderDevice(const std::string_view portPath,
                     const std::string_view operation,
                     Reader reader,
                     Printer printer)
{
    zzrk::logging::debug(
        "Scanning for the explicitly selected Loader device");
    zzrk::transport::UsbDeviceScanner scanner;
    auto scan = scanner.scanRockchipDevices();
    const auto selected = zzrk::cli::selectLoaderDevice(scan, portPath);
    if (!selected) {
        switch (selected.error) {
        case zzrk::cli::LoaderSelectionError::scanFailed:
            if (scan.error.has_value()) {
                logScanFailure(*scan.error, operation);
            } else {
                std::string message{operation};
                message += " USB scan failed";
                zzrk::logging::error(message);
            }
            break;
        case zzrk::cli::LoaderSelectionError::ambiguous: {
            std::string message{operation};
            message += " refused an ambiguous physical port path";
            zzrk::logging::error(message);
            break;
        }
        case zzrk::cli::LoaderSelectionError::notFound: {
            std::string message{"No Rockchip USB device found at port "};
            message += portPath;
            zzrk::logging::error(message);
            break;
        }
        case zzrk::cli::LoaderSelectionError::unsupported: {
            std::string message{operation};
            message +=
                " refused the selected device: the validated RK3588 Loader "
                "identity is required (2207:350b, bcdUSB 0x0201, bcdDevice "
                "0x0223)";
            zzrk::logging::error(message);
            break;
        }
        case zzrk::cli::LoaderSelectionError::none: {
            std::string message{operation};
            message += " device selection failed";
            zzrk::logging::error(message);
            break;
        }
        }
        return EXIT_FAILURE;
    }

    auto opened = zzrk::transport::LibusbBulkTransport::open(*selected.device);
    const auto* openError =
        std::get_if<zzrk::transport::LibusbOpenError>(&opened);
    if (openError != nullptr) {
        std::ostringstream message;
        message << operation << " could not open the Loader interface: "
                << describeOpenError(openError->code);
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
    const auto readResult = [&transport, &reader] {
        zzrk::session::RockusbSession session{*transport};
        return reader(session);
    }();

    const int releaseResult = transport->close();
    transport.reset();

    const auto* readError =
        std::get_if<zzrk::session::SessionError>(&readResult);
    if (readError != nullptr) {
        logReadFailure(*readError, operation);
    }
    if (releaseResult < 0) {
        std::ostringstream message;
        message << operation
                << " could not release the Loader interface (libusb "
                << releaseResult << ')';
        zzrk::logging::error(message.str());
    }
    if (readError != nullptr || releaseResult < 0) {
        return EXIT_FAILURE;
    }

    printer(*selected.device, std::get<Value>(readResult), std::cout);
    std::string successMessage{operation};
    successMessage += " completed successfully at ";
    successMessage += selected.device->portPath;
    zzrk::logging::info(successMessage);
    return EXIT_SUCCESS;
}

int readChipInfoDevice(const std::string_view portPath)
{
    return readLoaderDevice<zzrk::session::ChipInfo>(
        portPath, "ReadChipInfo", zzrk::session::readChipInfo,
        zzrk::cli::printChipInfo);
}

int readFlashInfoDevice(const std::string_view portPath)
{
    return readLoaderDevice<zzrk::session::FlashInfo>(
        portPath, "ReadFlashInfo", zzrk::session::readFlashInfo,
        zzrk::cli::printFlashInfo);
}

int readFlashIdDevice(const std::string_view portPath)
{
    return readLoaderDevice<zzrk::session::FlashId>(
        portPath, "ReadFlashID", zzrk::session::readFlashId,
        zzrk::cli::printFlashId);
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
    if (argc == 3 && std::string_view{argv[1]} == "read-chip-info") {
        return readChipInfoDevice(argv[2]);
    }
    if (argc == 3 && std::string_view{argv[1]} == "read-flash-info") {
        return readFlashInfoDevice(argv[2]);
    }
    if (argc == 3 && std::string_view{argv[1]} == "read-flash-id") {
        return readFlashIdDevice(argv[2]);
    }
    if (argc == 2 && std::string_view{argv[1]} == "--version") {
        std::cout << "ZzRkDevTool " ZZRK_VERSION "\n";
        return EXIT_SUCCESS;
    }

    if (argc == 1) {
        printUsage(executable, std::cout);
        return EXIT_SUCCESS;
    }
    printUsage(executable, std::cerr);
    return 64;
}
