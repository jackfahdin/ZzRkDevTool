// SPDX-License-Identifier: MIT

#include "DeviceListPrinter.hpp"
#include "FirmwareImagePrinter.hpp"
#include "ProbeSupport.hpp"

#include <zzrk/image/RkFirmwareFlasher.hpp>
#include <zzrk/image/RkFirmwareImage.hpp>
#include <zzrk/logging/Logging.hpp>
#include <zzrk/platform/PlatformServices.hpp>
#include <zzrk/session/ReadChipInfo.hpp>
#include <zzrk/session/ReadCapability.hpp>
#include <zzrk/session/ReadFlashId.hpp>
#include <zzrk/session/ReadFlashInfo.hpp>
#include <zzrk/session/ReadLba.hpp>
#include <zzrk/session/RockusbSession.hpp>
#include <zzrk/transport/LibusbBulkTransport.hpp>
#include <zzrk/transport/UsbDeviceScanner.hpp>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {

void printUsage(const std::string_view executable, std::ostream& output)
{
    output << "Usage: " << executable << " list [--json]\n"
           << "       " << executable << " inspect-image <update.img>\n"
           << "       " << executable << " plan-image <update.img>\n"
           << "       " << executable
           << " flash-image <port-path> <update.img> --yes\n"
           << "       " << executable << " probe <port-path>\n"
           << "       " << executable << " read-chip-info <port-path>\n"
           << "       " << executable << " read-capability <port-path>\n"
           << "       " << executable
           << " read-lba <port-path> --start <lba> --count <sectors> "
              "--output <file>\n"
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
              "command to the explicitly selected Loader device.\n"
           << "The flash-image command writes GPT, every partition payload, "
              "and the embedded Loader from an RK3588 update.img.\n";
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
    case SessionErrorCode::invalidAddress:
        return "the LBA address is outside the protocol range";
    case SessionErrorCode::invalidSectorCount:
        return "the LBA sector count is invalid";
    case SessionErrorCode::bufferSizeMismatch:
        return "the LBA buffer size does not match the sector count";
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

[[nodiscard]] std::string_view describeFlashError(
    const zzrk::image::FirmwareFlashErrorCode code) noexcept
{
    using zzrk::image::FirmwareFlashErrorCode;
    switch (code) {
    case FirmwareFlashErrorCode::invalidPlan:
        return "the flash plan is inconsistent";
    case FirmwareFlashErrorCode::deviceTooSmall:
        return "the eMMC is smaller than the image layout";
    case FirmwareFlashErrorCode::addressOverflow:
        return "the eMMC exceeds the current RockUSB LBA address range";
    case FirmwareFlashErrorCode::imageOpenFailed:
        return "the firmware image could not be opened";
    case FirmwareFlashErrorCode::imageSeekFailed:
        return "seeking in the firmware image failed";
    case FirmwareFlashErrorCode::imageReadFailed:
        return "reading the firmware image failed";
    case FirmwareFlashErrorCode::unsupportedPayload:
        return "the partition payload is unsupported or inconsistent";
    case FirmwareFlashErrorCode::invalidLoader:
        return "the embedded Loader cannot form a valid eMMC IDBlock";
    case FirmwareFlashErrorCode::loaderUnsupported:
        return "the device does not report New IDBlock Loader support";
    case FirmwareFlashErrorCode::partitionTableGenerationFailed:
        return "GPT generation failed";
    case FirmwareFlashErrorCode::eraseFailed:
        return "partition erase failed";
    case FirmwareFlashErrorCode::writeFailed:
        return "eMMC write failed";
    case FirmwareFlashErrorCode::verifyFailed:
        return "eMMC read-back verification failed";
    }
    return "unknown flash error";
}

[[nodiscard]] std::string_view describeFlashPhase(
    const zzrk::image::FirmwareFlashPhase phase) noexcept
{
    using zzrk::image::FirmwareFlashPhase;
    switch (phase) {
    case FirmwareFlashPhase::writePartitionTable:
        return "write GPT";
    case FirmwareFlashPhase::verifyPartitionTable:
        return "verify GPT";
    case FirmwareFlashPhase::erasePartition:
        return "erase";
    case FirmwareFlashPhase::writePartition:
        return "write";
    case FirmwareFlashPhase::writeLoader:
        return "write Loader";
    case FirmwareFlashPhase::verifyLoader:
        return "verify Loader";
    }
    return "flash";
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

int readCapabilityDevice(const std::string_view portPath)
{
    return readLoaderDevice<zzrk::session::Capability>(
        portPath, "ReadCapability", zzrk::session::readCapability,
        zzrk::cli::printCapability);
}

[[nodiscard]] bool parseUnsigned(std::string_view text,
                                 std::uint64_t& value) noexcept
{
    int base = 10;
    if (text.starts_with("0x") || text.starts_with("0X")) {
        text.remove_prefix(2);
        base = 16;
    }
    if (text.empty()) {
        return false;
    }
    const auto result = std::from_chars(text.data(), text.data() + text.size(),
                                        value, base);
    return result.ec == std::errc{}
           && result.ptr == text.data() + text.size();
}

int readLbaDevice(const std::string_view portPath, const std::uint64_t start,
                  const std::uint64_t count,
                  const std::filesystem::path& outputPath)
{
    if (count == 0U || start > std::numeric_limits<std::uint32_t>::max()
        || count > std::numeric_limits<std::uint32_t>::max()
        || start + count < start
        || start + count
               > static_cast<std::uint64_t>(
                   std::numeric_limits<std::uint32_t>::max())
                     + 1U) {
        zzrk::logging::error("ReadLBA range is invalid");
        return EXIT_FAILURE;
    }

    zzrk::transport::UsbDeviceScanner scanner;
    auto scan = scanner.scanRockchipDevices();
    const auto selected = zzrk::cli::selectLoaderDevice(scan, portPath);
    if (!selected) {
        zzrk::logging::error(
            "ReadLBA requires the validated RK3588 Loader at the selected "
            "port");
        return EXIT_FAILURE;
    }
    auto opened = zzrk::transport::LibusbBulkTransport::open(*selected.device);
    if (const auto* error =
            std::get_if<zzrk::transport::LibusbOpenError>(&opened)) {
        std::ostringstream message;
        message << "ReadLBA could not open Loader: "
                << describeOpenError(error->code) << " (libusb "
                << error->nativeCode << ')';
        zzrk::logging::error(message.str());
        return EXIT_FAILURE;
    }
    auto transport = std::move(
        std::get<std::unique_ptr<zzrk::transport::LibusbBulkTransport>>(opened));
    zzrk::session::RockusbSession session{*transport};
    const auto flashInfoResult = zzrk::session::readFlashInfo(session);
    const auto* flashInfo =
        std::get_if<zzrk::session::FlashInfo>(&flashInfoResult);
    if (flashInfo == nullptr
        || start + count > flashInfo->legacy.sectorCount) {
        if (const auto* error =
                std::get_if<zzrk::session::SessionError>(&flashInfoResult)) {
            logReadFailure(*error, "ReadLBA capacity check");
        } else {
            zzrk::logging::error("ReadLBA range exceeds eMMC capacity");
        }
        static_cast<void>(transport->close());
        return EXIT_FAILURE;
    }

    std::filesystem::path partialPath = outputPath;
    partialPath += ".partial";
    std::ofstream output{partialPath, std::ios::binary | std::ios::trunc};
    if (!output) {
        zzrk::logging::error("ReadLBA could not create the partial output");
        static_cast<void>(transport->close());
        return EXIT_FAILURE;
    }

    std::vector<std::byte> buffer(
        static_cast<std::size_t>(
            zzrk::session::kMaximumLbaSectorsPerCommand
            * zzrk::core::kSectorSize));
    std::uint64_t completed = 0;
    bool failed = false;
    while (completed < count) {
        const std::uint64_t sectors = std::min(
            zzrk::session::kMaximumLbaSectorsPerCommand, count - completed);
        const std::size_t bytes = static_cast<std::size_t>(
            sectors * zzrk::core::kSectorSize);
        const auto result = zzrk::session::readLba(
            session, zzrk::core::Lba{start + completed},
            zzrk::core::SectorCount{sectors},
            std::span<std::byte>{buffer}.first(bytes));
        if (const auto* error =
                std::get_if<zzrk::session::SessionError>(&result)) {
            logReadFailure(*error, "ReadLBA");
            failed = true;
            break;
        }
        output.write(reinterpret_cast<const char*>(buffer.data()),
                     static_cast<std::streamsize>(bytes));
        if (!output) {
            zzrk::logging::error("ReadLBA could not write the output file");
            failed = true;
            break;
        }
        completed += sectors;
    }
    output.close();
    const int releaseResult = transport->close();
    transport.reset();
    if (failed || releaseResult < 0) {
        return EXIT_FAILURE;
    }
    std::error_code renameError;
    std::filesystem::rename(partialPath, outputPath, renameError);
    if (renameError) {
        zzrk::logging::error("ReadLBA could not finalize the output file");
        return EXIT_FAILURE;
    }
    std::ostringstream success;
    success << "ReadLBA saved " << count * zzrk::core::kSectorSize
            << " bytes from LBA " << start << " to " << outputPath.string();
    zzrk::logging::info(success.str());
    return EXIT_SUCCESS;
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

int listDevices(const bool json)
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
    if (json) {
        return zzrk::cli::printScanResultJson(result, std::cout,
                                              usbAccessHelp);
    }
    return zzrk::cli::printScanResult(result, std::cout, std::cerr,
                                      usbAccessHelp);
}

int inspectImage(const std::string_view path)
{
    zzrk::logging::info("Inspecting Rockchip firmware image");
    const auto parsed = zzrk::image::parseRkFirmwareImage(std::string{path});
    if (const auto* error =
            std::get_if<zzrk::image::FirmwareImageError>(&parsed)) {
        std::ostringstream message;
        message << "Image inspection failed: "
                << zzrk::cli::describeFirmwareImageError(error->code)
                << " (offset " << error->offset;
        if (error->entryIndex != 0U) {
            message << ", entry " << error->entryIndex;
        }
        message << ')';
        zzrk::logging::error(message.str());
        return EXIT_FAILURE;
    }
    zzrk::cli::printFirmwareImage(
        std::get<zzrk::image::RkFirmwareImage>(parsed), std::cout);
    return EXIT_SUCCESS;
}

int planImage(const std::string_view path)
{
    zzrk::logging::info("Building complete firmware flash plan");
    const auto parsed = zzrk::image::parseRkFirmwareImage(std::string{path});
    if (const auto* error =
            std::get_if<zzrk::image::FirmwareImageError>(&parsed)) {
        std::ostringstream message;
        message << "Image planning failed: "
                << zzrk::cli::describeFirmwareImageError(error->code)
                << " (offset " << error->offset << ')';
        zzrk::logging::error(message.str());
        return EXIT_FAILURE;
    }
    const auto plan = zzrk::image::buildRkFirmwareFlashPlan(
        std::get<zzrk::image::RkFirmwareImage>(parsed));
    if (const auto* error =
            std::get_if<zzrk::image::FirmwarePlanError>(&plan)) {
        std::ostringstream message;
        message << "Image planning failed: "
                << zzrk::cli::describeFirmwarePlanError(error->code)
                << " (target " << error->target << ')';
        zzrk::logging::error(message.str());
        return EXIT_FAILURE;
    }
    zzrk::cli::printFirmwareFlashPlan(
        std::get<zzrk::image::RkFirmwareFlashPlan>(plan), std::cout);
    return EXIT_SUCCESS;
}

int flashImage(const std::string_view portPath,
               const std::filesystem::path& imagePath)
{
    zzrk::logging::info("Parsing and validating the complete firmware image");
    auto parsed = zzrk::image::parseRkFirmwareImage(imagePath);
    if (const auto* error =
            std::get_if<zzrk::image::FirmwareImageError>(&parsed)) {
        std::ostringstream message;
        message << "Flash refused: "
                << zzrk::cli::describeFirmwareImageError(error->code)
                << " (offset " << error->offset << ')';
        zzrk::logging::error(message.str());
        return EXIT_FAILURE;
    }
    auto image =
        std::get<zzrk::image::RkFirmwareImage>(std::move(parsed));
    auto planned = zzrk::image::buildRkFirmwareFlashPlan(image);
    if (const auto* error =
            std::get_if<zzrk::image::FirmwarePlanError>(&planned)) {
        std::ostringstream message;
        message << "Flash refused: "
                << zzrk::cli::describeFirmwarePlanError(error->code)
                << " (target " << error->target << ')';
        zzrk::logging::error(message.str());
        return EXIT_FAILURE;
    }
    auto plan = std::get<zzrk::image::RkFirmwareFlashPlan>(
        std::move(planned));

    zzrk::transport::UsbDeviceScanner scanner;
    auto scan = scanner.scanRockchipDevices();
    const auto selected = zzrk::cli::selectLoaderDevice(scan, portPath);
    if (!selected) {
        zzrk::logging::error(
            "Flash requires the validated RK3588 Loader at the selected port");
        return EXIT_FAILURE;
    }
    auto opened = zzrk::transport::LibusbBulkTransport::open(*selected.device);
    if (const auto* error =
            std::get_if<zzrk::transport::LibusbOpenError>(&opened)) {
        std::ostringstream message;
        message << "Flash could not open Loader: "
                << describeOpenError(error->code);
        if (error->nativeCode != 0) {
            message << " (libusb " << error->nativeCode << ')';
        }
        zzrk::logging::error(message.str());
        return EXIT_FAILURE;
    }
    auto transport = std::move(
        std::get<std::unique_ptr<zzrk::transport::LibusbBulkTransport>>(opened));
    auto closeTransport = [&transport] {
        const int result = transport->close();
        if (result < 0) {
            std::ostringstream message;
            message << "Could not release Loader interface (libusb " << result
                    << ')';
            zzrk::logging::error(message.str());
        }
        return result;
    };

    using namespace std::chrono_literals;
    zzrk::session::SessionTimeouts timeouts{};
    timeouts.status = 120s;
    zzrk::session::RockusbSession rockusb{*transport, timeouts};
    const auto flashIdResult = zzrk::session::readFlashId(rockusb);
    const auto* flashId =
        std::get_if<zzrk::session::FlashId>(&flashIdResult);
    if (flashId == nullptr) {
        logReadFailure(std::get<zzrk::session::SessionError>(flashIdResult),
                       "Flash eMMC identity check");
        static_cast<void>(closeTransport());
        return EXIT_FAILURE;
    }
    if (zzrk::session::classifyFlashId(*flashId)
        != zzrk::session::FlashIdKind::emmcMediaMarker) {
        zzrk::logging::error(
            "Flash refused: selected Loader did not report the EMMC media "
            "marker");
        static_cast<void>(closeTransport());
        return EXIT_FAILURE;
    }

    const auto flashInfoResult = zzrk::session::readFlashInfo(rockusb);
    const auto* flashInfo =
        std::get_if<zzrk::session::FlashInfo>(&flashInfoResult);
    if (flashInfo == nullptr) {
        logReadFailure(std::get<zzrk::session::SessionError>(flashInfoResult),
                       "Flash capacity check");
        static_cast<void>(closeTransport());
        return EXIT_FAILURE;
    }
    const std::uint64_t deviceSectors = flashInfo->legacy.sectorCount;
    if (deviceSectors < plan.minimumDeviceSectors) {
        std::ostringstream message;
        message << "Flash refused: eMMC has " << deviceSectors
                << " sectors, image requires at least "
                << plan.minimumDeviceSectors;
        zzrk::logging::error(message.str());
        static_cast<void>(closeTransport());
        return EXIT_FAILURE;
    }

    {
        std::ostringstream summary;
        summary << "Starting RK3588 system flash on " << portPath << ": "
                << image.model << ", eMMC " << deviceSectors
                << " sectors, partition payload "
                << plan.totalPartitionBytes
                << " bytes; embedded Loader will be upgraded last and "
                   "verified";
        zzrk::logging::warning(summary.str());
    }

    std::string lastProgressKey;
    std::uint64_t lastProgressPercent = 101U;
    const auto progress = [&](const zzrk::image::FirmwareFlashProgress& value) {
        const std::uint64_t percent = value.totalBytes == 0U
                                          ? 100U
                                          : std::min<std::uint64_t>(
                                                100U,
                                                value.completedBytes * 100U
                                                    / value.totalBytes);
        std::string key{describeFlashPhase(value.phase)};
        key += ':';
        key += value.target;
        const bool changed = key != lastProgressKey;
        if (!changed && value.completedBytes != value.totalBytes
            && percent < lastProgressPercent + 5U) {
            return;
        }
        lastProgressKey = std::move(key);
        lastProgressPercent = percent;
        std::ostringstream message;
        message << describeFlashPhase(value.phase) << ' ' << value.target
                << ": " << percent << "% (" << value.completedBytes << '/'
                << value.totalBytes << " bytes)";
        zzrk::logging::info(message.str());
    };

    const auto result = zzrk::image::flashRkFirmwareImage(
        rockusb, image, plan, deviceSectors, {}, progress);
    const int releaseResult = closeTransport();
    transport.reset();
    if (const auto* error =
            std::get_if<zzrk::image::FirmwareFlashError>(&result)) {
        std::ostringstream message;
        message << "System flash failed at " << error->target << ": "
                << describeFlashError(error->code);
        if (error->completedBytes != 0U) {
            message << " after " << error->completedBytes << " bytes";
        }
        zzrk::logging::error(message.str());
        if (error->sessionError.has_value()) {
            logReadFailure(*error->sessionError, "RockUSB flash command");
        }
        return EXIT_FAILURE;
    }
    if (releaseResult < 0) {
        return EXIT_FAILURE;
    }
    const auto& success =
        std::get<zzrk::image::FirmwareFlashSuccess>(result);
    std::ostringstream message;
    message << "System flash completed: " << success.partitionBytesWritten
            << " partition bytes processed, " << success.sectorsErased
            << " sectors erased, primary GPT verified, backup GPT write "
               "submitted, Loader "
            << (success.loaderWritten ? "written" : "not written")
            << (success.loaderVerified ? " and verified" : "");
    zzrk::logging::info(message.str());
    return EXIT_SUCCESS;
}

} // namespace

int main(const int argc, char* argv[])
{
    [[maybe_unused]] const auto loggingSession =
        zzrk::logging::initializeConsoleLogging("zzrkdevtool-cli");
    const std::string_view executable = argc > 0 ? argv[0] : "zzrkdevtool-cli";
    if (argc == 2 && std::string_view{argv[1]} == "list") {
        return listDevices(false);
    }
    if (argc == 3 && std::string_view{argv[1]} == "list"
        && std::string_view{argv[2]} == "--json") {
        return listDevices(true);
    }
    if (argc == 3 && std::string_view{argv[1]} == "inspect-image") {
        return inspectImage(argv[2]);
    }
    if (argc == 3 && std::string_view{argv[1]} == "plan-image") {
        return planImage(argv[2]);
    }
    if (argc == 5 && std::string_view{argv[1]} == "flash-image"
        && std::string_view{argv[4]} == "--yes") {
        return flashImage(argv[2], argv[3]);
    }
    if (argc >= 2 && std::string_view{argv[1]} == "flash-image") {
        zzrk::logging::error(
            "flash-image requires: <port-path> <update.img> --yes");
        return 64;
    }
    if (argc == 3 && std::string_view{argv[1]} == "probe") {
        return probeDevice(argv[2]);
    }
    if (argc == 3 && std::string_view{argv[1]} == "read-chip-info") {
        return readChipInfoDevice(argv[2]);
    }
    if (argc == 3 && std::string_view{argv[1]} == "read-capability") {
        return readCapabilityDevice(argv[2]);
    }
    if (argc == 9 && std::string_view{argv[1]} == "read-lba"
        && std::string_view{argv[3]} == "--start"
        && std::string_view{argv[5]} == "--count"
        && std::string_view{argv[7]} == "--output") {
        std::uint64_t start = 0;
        std::uint64_t count = 0;
        if (!parseUnsigned(argv[4], start) || !parseUnsigned(argv[6], count)) {
            zzrk::logging::error("ReadLBA start or count is invalid");
            return 64;
        }
        return readLbaDevice(argv[2], start, count, argv[8]);
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
