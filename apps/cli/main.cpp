// SPDX-License-Identifier: MIT

#include "DeviceListPrinter.hpp"
#include "FirmwareImagePrinter.hpp"
#include "ProbeSupport.hpp"
#include "StructuredOutput.hpp"

#include <zzrk/image/RkFirmwareFlasher.hpp>
#include <zzrk/image/RkFirmwareImage.hpp>
#include <zzrk/logging/Logging.hpp>
#include <zzrk/platform/PlatformServices.hpp>
#include <zzrk/qt/FirmwareMd5Verifier.hpp>
#include <zzrk/qt/FirmwareUpgradeReportWriter.hpp>
#include <zzrk/session/ReadCapability.hpp>
#include <zzrk/session/ReadChipInfo.hpp>
#include <zzrk/session/ReadFlashId.hpp>
#include <zzrk/session/ReadFlashInfo.hpp>
#include <zzrk/session/ReadLba.hpp>
#include <zzrk/session/ResetDevice.hpp>
#include <zzrk/session/RockusbSession.hpp>
#include <zzrk/session/TestUnitReady.hpp>
#include <zzrk/session/WriteLba.hpp>
#include <zzrk/session/EraseLba.hpp>
#include <zzrk/storage/GptPartitionTable.hpp>
#include <zzrk/transport/LibusbBulkTransport.hpp>
#include <zzrk/transport/UsbDeviceScanner.hpp>
#include <zzrk/workflow/FirmwareUpgradeWorkflow.hpp>
#include <zzrk/workflow/FirmwareUpgradeReport.hpp>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {

volatile std::sig_atomic_t gInterruptRequested = 0;

extern "C" void requestInterrupt(int) noexcept { gInterruptRequested = 1; }

class InterruptHandlerGuard final {
public:
  InterruptHandlerGuard() noexcept {
    gInterruptRequested = 0;
    previous_ = std::signal(SIGINT, requestInterrupt);
  }

  ~InterruptHandlerGuard() {
    if (previous_ != SIG_ERR) {
      static_cast<void>(std::signal(SIGINT, previous_));
    }
  }

  InterruptHandlerGuard(const InterruptHandlerGuard &) = delete;
  InterruptHandlerGuard &operator=(const InterruptHandlerGuard &) = delete;

  [[nodiscard]] static bool requested() noexcept {
    return gInterruptRequested != 0;
  }

private:
  using Handler = void (*)(int);
  Handler previous_{};
};

void printUsage(const std::string_view executable, std::ostream &output) {
  output << "Usage: " << executable << " list [--json]\n"
         << "       " << executable << " info <port-path> [--json]\n"
         << "       " << executable << " inspect-image <update.img> [--json]\n"
         << "       " << executable << " plan-image <update.img> [--json]\n"
         << "       " << executable
         << " flash-image <port-path> <update.img> --yes "
            "[--verify critical|all|none] [--no-reboot] [--prepare-only] "
            "[--report <file>] [--report-include-firmware-path] [--json]\n"
         << "       " << executable << " reset <port-path>\n"
         << "       " << executable << " probe <port-path>\n"
         << "       " << executable << " read-chip-info <port-path>\n"
         << "       " << executable << " read-capability <port-path>\n"
         << "       " << executable
         << " read-lba <port-path> --start <lba> --count <sectors> "
            "--output <file>\n"
         << "       " << executable
         << " partitions <port-path> [--json]\n"
         << "       " << executable
         << " read-partition <port-path> <name> --output <file> [--json]\n"
         << "       " << executable
         << " write-partition <port-path> <name> --input <file> --yes "
            "[--no-verify] [--json]\n"
         << "       " << executable
         << " erase-partition <port-path> <name> --yes [--json]\n"
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
            "and the embedded Loader from an RK3588 update.img. With "
            "--prepare-only it performs no eMMC write.\n";
}

[[nodiscard]] std::string_view
describeOpenError(const zzrk::transport::LibusbOpenErrorCode code) noexcept {
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

[[nodiscard]] std::string_view
describeSessionError(const zzrk::session::SessionErrorCode code) noexcept {
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
describeSessionStage(const zzrk::session::SessionStage stage) noexcept {
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

void logScanFailure(const zzrk::transport::ScanError &error,
                    const std::string_view operation) {
  std::ostringstream message;
  message << operation << " scan failed (" << error.code
          << "): " << error.message;
  zzrk::logging::error(message.str());

  if (error.kind == zzrk::transport::ScanErrorKind::AccessDenied) {
    const auto platform = zzrk::platform::createPlatformServices();
    zzrk::logging::error(platform->usbAccessHelp());
  }
}

void logReadFailure(const zzrk::session::SessionError &error,
                    const std::string_view operation) {
  std::ostringstream message;
  message << operation << " failed during " << describeSessionStage(error.stage)
          << ": " << describeSessionError(error.code);
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
            << static_cast<int>(error.commandEncodeError->code) << ", value "
            << static_cast<unsigned int>(error.commandEncodeError->value)
            << ')';
  }
  if (error.statusDecodeError.has_value()) {
    const auto &status = *error.statusDecodeError;
    message << " (status decode code " << static_cast<int>(status.code)
            << ", raw status " << static_cast<unsigned int>(status.rawStatus)
            << ", residue " << status.dataResidue << ", expected tag "
            << status.expectedTag << ", actual tag " << status.actualTag << ')';
  }
  zzrk::logging::error(message.str());
}

[[nodiscard]] std::string_view
describeFlashPhase(const zzrk::image::FirmwareFlashPhase phase) noexcept {
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
  case FirmwareFlashPhase::verifyPartition:
    return "verify";
  case FirmwareFlashPhase::writeLoader:
    return "write Loader";
  case FirmwareFlashPhase::verifyLoader:
    return "verify Loader";
  }
  return "flash";
}

int probeDevice(const std::string_view portPath) {
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
      zzrk::logging::error("USB probe refused an ambiguous physical port path");
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
  const auto *const selection = selected.device;

  zzrk::logging::debug(
      "Opening and claiming the selected Loader interface without a "
      "RockUSB command");
  auto opened = zzrk::transport::LibusbBulkTransport::open(*selection);
  const auto *openError =
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
                     const std::string_view operation, Reader reader,
                     Printer printer) {
  zzrk::logging::debug("Scanning for the explicitly selected Loader device");
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
      message += " refused the selected device: the validated RK3588 Loader "
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
  const auto *openError =
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

  const auto *readError = std::get_if<zzrk::session::SessionError>(&readResult);
  if (readError != nullptr) {
    logReadFailure(*readError, operation);
  }
  if (releaseResult < 0) {
    std::ostringstream message;
    message << operation << " could not release the Loader interface (libusb "
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

int readChipInfoDevice(const std::string_view portPath) {
  return readLoaderDevice<zzrk::session::ChipInfo>(portPath, "ReadChipInfo",
                                                   zzrk::session::readChipInfo,
                                                   zzrk::cli::printChipInfo);
}

int readCapabilityDevice(const std::string_view portPath) {
  return readLoaderDevice<zzrk::session::Capability>(
      portPath, "ReadCapability", zzrk::session::readCapability,
      zzrk::cli::printCapability);
}

int resetDeviceCommand(const std::string_view portPath) {
  zzrk::transport::UsbDeviceScanner scanner;
  auto scan = scanner.scanRockchipDevices();
  const auto selected = zzrk::cli::selectLoaderDevice(scan, portPath);
  if (!selected) {
    zzrk::logging::error(
        "Reset requires the validated RK3588 Loader at the selected port");
    return EXIT_FAILURE;
  }

  auto opened = zzrk::transport::LibusbBulkTransport::open(*selected.device);
  if (const auto *error =
          std::get_if<zzrk::transport::LibusbOpenError>(&opened)) {
    std::ostringstream message;
    message << "Reset could not open Loader: "
            << describeOpenError(error->code);
    if (error->nativeCode != 0) {
      message << " (libusb " << error->nativeCode << ')';
    }
    zzrk::logging::error(message.str());
    return EXIT_FAILURE;
  }

  auto transport = std::move(
      std::get<std::unique_ptr<zzrk::transport::LibusbBulkTransport>>(opened));
  zzrk::session::RockusbSession rockusb{*transport};
  const auto result = zzrk::session::resetDevice(rockusb);
  const bool accepted =
      std::holds_alternative<zzrk::session::SessionSuccess>(result);
  const int releaseResult = transport->close();
  transport.reset();
  if (!accepted) {
    logReadFailure(std::get<zzrk::session::SessionError>(result),
                   "ResetDevice");
    if (releaseResult < 0) {
      zzrk::logging::error(
          "ResetDevice also failed to release the Loader interface");
    }
    return EXIT_FAILURE;
  }

  if (releaseResult < 0) {
    zzrk::logging::debug(
        "Loader disconnected before its claimed interface was released");
  }
  zzrk::logging::info(
      "Reset command accepted; the device is leaving Loader mode");
  return EXIT_SUCCESS;
}

[[nodiscard]] bool parseUnsigned(std::string_view text,
                                 std::uint64_t &value) noexcept {
  int base = 10;
  if (text.starts_with("0x") || text.starts_with("0X")) {
    text.remove_prefix(2);
    base = 16;
  }
  if (text.empty()) {
    return false;
  }
  const auto result =
      std::from_chars(text.data(), text.data() + text.size(), value, base);
  return result.ec == std::errc{} && result.ptr == text.data() + text.size();
}

[[nodiscard]] bool hasArgument(const int argc, char *argv[],
                               const std::string_view expected) noexcept {
  for (int index = 1; index < argc; ++index) {
    if (std::string_view{argv[index]} == expected) {
      return true;
    }
  }
  return false;
}

int readLbaDevice(const std::string_view portPath, const std::uint64_t start,
                  const std::uint64_t count,
                  const std::filesystem::path &outputPath) {
  if (count == 0U || start > std::numeric_limits<std::uint32_t>::max() ||
      count > std::numeric_limits<std::uint32_t>::max() ||
      start + count < start ||
      start + count > static_cast<std::uint64_t>(
                          std::numeric_limits<std::uint32_t>::max()) +
                          1U) {
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
  if (const auto *error =
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
  const auto *flashInfo =
      std::get_if<zzrk::session::FlashInfo>(&flashInfoResult);
  if (flashInfo == nullptr || start + count > flashInfo->legacy.sectorCount) {
    if (const auto *error =
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

  std::vector<std::byte> buffer(static_cast<std::size_t>(
      zzrk::session::kMaximumLbaSectorsPerCommand * zzrk::core::kSectorSize));
  std::uint64_t completed = 0;
  bool failed = false;
  while (completed < count) {
    const std::uint64_t sectors = std::min(
        zzrk::session::kMaximumLbaSectorsPerCommand, count - completed);
    const std::size_t bytes =
        static_cast<std::size_t>(sectors * zzrk::core::kSectorSize);
    const auto result =
        zzrk::session::readLba(session, zzrk::core::Lba{start + completed},
                               zzrk::core::SectorCount{sectors},
                               std::span<std::byte>{buffer}.first(bytes));
    if (const auto *error = std::get_if<zzrk::session::SessionError>(&result)) {
      logReadFailure(*error, "ReadLBA");
      failed = true;
      break;
    }
    output.write(reinterpret_cast<const char *>(buffer.data()),
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

struct EmmcLoaderContext {
  zzrk::transport::UsbDeviceInfo device;
  std::unique_ptr<zzrk::transport::LibusbBulkTransport> transport;
  std::uint64_t deviceSectors{};
};

using EmmcLoaderResult = std::variant<EmmcLoaderContext, std::string>;

[[nodiscard]] EmmcLoaderResult
openEmmcLoader(const std::string_view portPath,
               const std::string_view operation) {
  zzrk::transport::UsbDeviceScanner scanner;
  auto scan = scanner.scanRockchipDevices();
  const auto selected = zzrk::cli::selectLoaderDevice(scan, portPath);
  if (!selected) {
    if (scan.error.has_value()) {
      logScanFailure(*scan.error, operation);
    }
    return std::string{operation} +
           " requires the validated RK3588 Loader at the selected port";
  }
  auto opened = zzrk::transport::LibusbBulkTransport::open(*selected.device);
  if (const auto *error =
          std::get_if<zzrk::transport::LibusbOpenError>(&opened)) {
    std::ostringstream message;
    message << operation << " could not open Loader: "
            << describeOpenError(error->code);
    if (error->nativeCode != 0) {
      message << " (libusb " << error->nativeCode << ')';
    }
    return message.str();
  }
  auto transport = std::move(
      std::get<std::unique_ptr<zzrk::transport::LibusbBulkTransport>>(opened));
  zzrk::session::RockusbSession session{
      *transport, {},
      zzrk::session::StatusResiduePolicy::allowNonZeroOnSuccess};
  const auto flashId = zzrk::session::readFlashId(session);
  if (const auto *error = std::get_if<zzrk::session::SessionError>(&flashId)) {
    logReadFailure(*error, operation);
    static_cast<void>(transport->close());
    return std::string{operation} + " could not identify eMMC";
  }
  if (zzrk::session::classifyFlashId(
          std::get<zzrk::session::FlashId>(flashId)) !=
      zzrk::session::FlashIdKind::emmcMediaMarker) {
    static_cast<void>(transport->close());
    return std::string{operation} +
           " refused the device because ReadFlashID did not report eMMC";
  }
  const auto flashInfo = zzrk::session::readFlashInfo(session);
  if (const auto *error =
          std::get_if<zzrk::session::SessionError>(&flashInfo)) {
    logReadFailure(*error, operation);
    static_cast<void>(transport->close());
    return std::string{operation} + " could not read eMMC capacity";
  }
  return EmmcLoaderContext{
      .device = *selected.device,
      .transport = std::move(transport),
      .deviceSectors =
          std::get<zzrk::session::FlashInfo>(flashInfo).legacy.sectorCount,
  };
}

struct NamedPartitionContext {
  EmmcLoaderContext loader;
  zzrk::storage::GptPartitionTable table;
  std::size_t partitionIndex{};
};

using NamedPartitionResult = std::variant<NamedPartitionContext, std::string>;

[[nodiscard]] NamedPartitionResult
openNamedPartition(const std::string_view portPath,
                   const std::string_view partitionName,
                   const std::string_view operation) {
  auto opened = openEmmcLoader(portPath, operation);
  if (const auto *error = std::get_if<std::string>(&opened)) {
    return *error;
  }
  auto loader = std::get<EmmcLoaderContext>(std::move(opened));
  zzrk::session::SessionTimeouts timeouts{};
  timeouts.status = std::chrono::seconds{120};
  zzrk::session::RockusbSession session{
      *loader.transport, timeouts,
      zzrk::session::StatusResiduePolicy::allowNonZeroOnSuccess};
  auto parsed =
      zzrk::storage::readDeviceGpt(session, loader.deviceSectors, false);
  if (const auto *error = std::get_if<zzrk::storage::GptError>(&parsed)) {
    if (error->sessionError.has_value()) {
      logReadFailure(*error->sessionError, operation);
    }
    static_cast<void>(loader.transport->close());
    std::ostringstream message;
    message << operation << " refused invalid primary GPT: "
            << zzrk::storage::describeGptError(error->code) << " (LBA "
            << error->lba << ')';
    return message.str();
  }
  auto table =
      std::get<zzrk::storage::GptPartitionTable>(std::move(parsed));
  std::optional<std::size_t> match;
  for (std::size_t index = 0; index < table.partitions.size(); ++index) {
    if (table.partitions[index].name != partitionName) {
      continue;
    }
    if (match.has_value()) {
      static_cast<void>(loader.transport->close());
      return std::string{operation} +
             " refused an ambiguous duplicate GPT partition name";
    }
    match = index;
  }
  if (!match.has_value()) {
    static_cast<void>(loader.transport->close());
    return std::string{operation} + " could not find GPT partition " +
           std::string{partitionName};
  }
  return NamedPartitionContext{
      .loader = std::move(loader),
      .table = std::move(table),
      .partitionIndex = *match,
  };
}

int partitionFailure(const bool json, const std::string_view command,
                     const std::string_view code,
                     const std::string &message,
                     const int exitCode = EXIT_FAILURE) {
  zzrk::logging::error(message);
  if (json) {
    zzrk::cli::printJsonError(command, code, message, std::cout);
  }
  return exitCode;
}

void reportPartitionProgress(const std::string_view operation,
                             const std::string_view partition,
                             const std::uint64_t completed,
                             const std::uint64_t total,
                             std::optional<std::uint64_t> &lastPercent) {
  const std::uint64_t percent =
      total == 0U ? 100U : std::min<std::uint64_t>(100U, completed * 100U / total);
  if (completed != total && lastPercent.has_value() &&
      percent < *lastPercent + 5U) {
    return;
  }
  lastPercent = percent;
  std::ostringstream message;
  message << operation << ' ' << partition << ": " << percent << "% ("
          << completed << '/' << total << " sectors)";
  zzrk::logging::info(message.str());
}

int listPartitionsDevice(const std::string_view portPath, const bool json) {
  auto opened = openEmmcLoader(portPath, "Partitions");
  if (const auto *error = std::get_if<std::string>(&opened)) {
    return partitionFailure(json, "partitions", "device-unavailable", *error);
  }
  auto loader = std::get<EmmcLoaderContext>(std::move(opened));
  zzrk::session::SessionTimeouts timeouts{};
  timeouts.status = std::chrono::seconds{120};
  zzrk::session::RockusbSession session{
      *loader.transport, timeouts,
      zzrk::session::StatusResiduePolicy::allowNonZeroOnSuccess};
  auto parsed =
      zzrk::storage::readDeviceGpt(session, loader.deviceSectors, true);
  const int closeResult = loader.transport->close();
  if (const auto *error = std::get_if<zzrk::storage::GptError>(&parsed)) {
    if (error->sessionError.has_value()) {
      logReadFailure(*error->sessionError, "Partitions");
    }
    std::ostringstream message;
    message << "Partitions refused invalid primary GPT: "
            << zzrk::storage::describeGptError(error->code) << " (LBA "
            << error->lba << ')';
    return partitionFailure(json, "partitions", "gpt-invalid",
                            message.str());
  }
  if (closeResult < 0) {
    return partitionFailure(json, "partitions", "close-failed",
                            "Partitions could not release Loader interface");
  }
  const auto &table = std::get<zzrk::storage::GptPartitionTable>(parsed);
  if (json) {
    zzrk::cli::printPartitionsJson(table, loader.deviceSectors, std::cout);
  } else {
    zzrk::cli::printPartitions(table, loader.deviceSectors, std::cout);
  }
  return EXIT_SUCCESS;
}

int readPartitionDevice(const std::string_view portPath,
                        const std::string_view partitionName,
                        const std::filesystem::path &outputPath,
                        const bool json) {
  const InterruptHandlerGuard interruptHandler;
  auto opened = openNamedPartition(portPath, partitionName, "Read partition");
  if (const auto *error = std::get_if<std::string>(&opened)) {
    return partitionFailure(json, "read-partition", "partition-unavailable",
                            *error);
  }
  auto context = std::get<NamedPartitionContext>(std::move(opened));
  const auto &partition = context.table.partitions[context.partitionIndex];
  if (std::filesystem::exists(outputPath)) {
    static_cast<void>(context.loader.transport->close());
    return partitionFailure(json, "read-partition", "output-exists",
                            "Read partition refused to overwrite the output");
  }
  std::filesystem::path partialPath = outputPath;
  partialPath += ".partial";
  std::ofstream output{partialPath, std::ios::binary | std::ios::trunc};
  if (!output) {
    static_cast<void>(context.loader.transport->close());
    return partitionFailure(json, "read-partition", "output-open-failed",
                            "Read partition could not create partial output");
  }

  zzrk::session::SessionTimeouts timeouts{};
  timeouts.status = std::chrono::seconds{120};
  zzrk::session::RockusbSession session{
      *context.loader.transport, timeouts,
      zzrk::session::StatusResiduePolicy::allowNonZeroOnSuccess};
  std::vector<std::byte> buffer(static_cast<std::size_t>(
      zzrk::session::kMaximumLbaSectorsPerCommand * zzrk::core::kSectorSize));
  std::uint64_t completed = 0;
  std::optional<std::uint64_t> lastPercent;
  bool failed = false;
  while (completed < partition.sectorCount()) {
    if (InterruptHandlerGuard::requested()) {
      failed = true;
      break;
    }
    const auto sectors = std::min<std::uint64_t>(
        zzrk::session::kMaximumLbaSectorsPerCommand,
        partition.sectorCount() - completed);
    const auto bytes =
        static_cast<std::size_t>(sectors * zzrk::core::kSectorSize);
    const auto result = zzrk::session::readLba(
        session, zzrk::core::Lba{partition.firstLba + completed},
        zzrk::core::SectorCount{sectors}, std::span{buffer}.first(bytes));
    if (const auto *error =
            std::get_if<zzrk::session::SessionError>(&result)) {
      logReadFailure(*error, "Read partition");
      failed = true;
      break;
    }
    output.write(reinterpret_cast<const char *>(buffer.data()),
                 static_cast<std::streamsize>(bytes));
    if (!output) {
      failed = true;
      break;
    }
    completed += sectors;
    reportPartitionProgress("read", partition.name, completed,
                            partition.sectorCount(), lastPercent);
  }
  output.close();
  const int closeResult = context.loader.transport->close();
  if (failed || closeResult < 0) {
    std::error_code removeError;
    std::filesystem::remove(partialPath, removeError);
    if (InterruptHandlerGuard::requested()) {
      return partitionFailure(json, "read-partition", "cancelled",
                              "Read partition was cancelled", 130);
    }
    return partitionFailure(json, "read-partition", "read-failed",
                            "Read partition failed before completion");
  }
  std::error_code renameError;
  std::filesystem::rename(partialPath, outputPath, renameError);
  if (renameError) {
    return partitionFailure(json, "read-partition", "output-finalize-failed",
                            "Read partition could not finalize the output");
  }
  const auto bytes = partition.sectorCount() * zzrk::core::kSectorSize;
  if (json) {
    zzrk::cli::printPartitionOperationJson(
        "read-partition", partition.name, bytes, partition.sectorCount(),
        false, std::cout);
  } else {
    std::ostringstream message;
    message << "Read partition " << partition.name << " saved " << bytes
            << " bytes to " << outputPath.string();
    zzrk::logging::info(message.str());
  }
  return EXIT_SUCCESS;
}

int writePartitionDevice(const std::string_view portPath,
                         const std::string_view partitionName,
                         const std::filesystem::path &inputPath,
                         const bool verify, const bool json) {
  const InterruptHandlerGuard interruptHandler;
  std::error_code sizeError;
  const auto inputSize = std::filesystem::file_size(inputPath, sizeError);
  if (sizeError || inputSize == 0U) {
    return partitionFailure(json, "write-partition", "input-invalid",
                            "Write partition input is missing or empty");
  }
  auto opened =
      openNamedPartition(portPath, partitionName, "Write partition");
  if (const auto *error = std::get_if<std::string>(&opened)) {
    return partitionFailure(json, "write-partition", "partition-unavailable",
                            *error);
  }
  auto context = std::get<NamedPartitionContext>(std::move(opened));
  const auto &partition = context.table.partitions[context.partitionIndex];
  const auto capacityBytes =
      partition.sectorCount() * zzrk::core::kSectorSize;
  if (inputSize > capacityBytes) {
    static_cast<void>(context.loader.transport->close());
    return partitionFailure(json, "write-partition", "input-too-large",
                            "Write partition input exceeds GPT capacity");
  }
  std::ifstream input{inputPath, std::ios::binary};
  if (!input) {
    static_cast<void>(context.loader.transport->close());
    return partitionFailure(json, "write-partition", "input-open-failed",
                            "Write partition could not open input");
  }

  zzrk::session::SessionTimeouts timeouts{};
  timeouts.status = std::chrono::seconds{120};
  zzrk::session::RockusbSession session{
      *context.loader.transport, timeouts,
      zzrk::session::StatusResiduePolicy::allowNonZeroOnSuccess};
  const std::uint64_t sectorsToWrite =
      (inputSize + zzrk::core::kSectorSize - 1U) /
      zzrk::core::kSectorSize;
  std::vector<std::byte> buffer(static_cast<std::size_t>(
      zzrk::session::kMaximumWriteLbaSectorsPerCommand *
      zzrk::core::kSectorSize));
  std::vector<std::byte> readback(buffer.size());
  std::uint64_t completedSectors = 0;
  std::uint64_t completedBytes = 0;
  std::optional<std::uint64_t> lastPercent;
  bool failed = false;
  while (completedSectors < sectorsToWrite) {
    if (InterruptHandlerGuard::requested()) {
      failed = true;
      break;
    }
    const auto sectors = std::min<std::uint64_t>(
        zzrk::session::kMaximumWriteLbaSectorsPerCommand,
        sectorsToWrite - completedSectors);
    const auto bytes =
        static_cast<std::size_t>(sectors * zzrk::core::kSectorSize);
    const auto sourceBytes = static_cast<std::size_t>(std::min<std::uint64_t>(
        inputSize - completedBytes, bytes));
    std::fill_n(buffer.begin(), bytes, std::byte{});
    input.read(reinterpret_cast<char *>(buffer.data()),
               static_cast<std::streamsize>(sourceBytes));
    if (input.gcount() != static_cast<std::streamsize>(sourceBytes)) {
      failed = true;
      break;
    }
    const auto written = zzrk::session::writeLba(
        session, zzrk::core::Lba{partition.firstLba + completedSectors},
        zzrk::core::SectorCount{sectors}, std::span{buffer}.first(bytes));
    if (const auto *error =
            std::get_if<zzrk::session::SessionError>(&written)) {
      logReadFailure(*error, "Write partition");
      failed = true;
      break;
    }
    if (verify) {
      const auto read = zzrk::session::readLba(
          session, zzrk::core::Lba{partition.firstLba + completedSectors},
          zzrk::core::SectorCount{sectors},
          std::span{readback}.first(bytes));
      if (const auto *error =
              std::get_if<zzrk::session::SessionError>(&read)) {
        logReadFailure(*error, "Verify partition");
        failed = true;
        break;
      }
      if (!std::ranges::equal(std::span{buffer}.first(bytes),
                              std::span{readback}.first(bytes))) {
        zzrk::logging::error("Write partition read-back mismatch");
        failed = true;
        break;
      }
    }
    completedSectors += sectors;
    completedBytes += sourceBytes;
    reportPartitionProgress("write", partition.name, completedSectors,
                            sectorsToWrite, lastPercent);
  }
  const int closeResult = context.loader.transport->close();
  if (failed || closeResult < 0) {
    if (InterruptHandlerGuard::requested()) {
      return partitionFailure(json, "write-partition", "cancelled",
                              "Write partition was cancelled", 130);
    }
    return partitionFailure(json, "write-partition", "write-failed",
                            "Write partition failed before completion");
  }
  if (json) {
    zzrk::cli::printPartitionOperationJson(
        "write-partition", partition.name, inputSize, sectorsToWrite, verify,
        std::cout);
  } else {
    std::ostringstream message;
    message << "Write partition " << partition.name << " completed: "
            << inputSize << " input bytes, " << sectorsToWrite
            << " sectors" << (verify ? " verified" : " not verified");
    zzrk::logging::info(message.str());
  }
  return EXIT_SUCCESS;
}

int erasePartitionDevice(const std::string_view portPath,
                         const std::string_view partitionName,
                         const bool json) {
  const InterruptHandlerGuard interruptHandler;
  auto opened =
      openNamedPartition(portPath, partitionName, "Erase partition");
  if (const auto *error = std::get_if<std::string>(&opened)) {
    return partitionFailure(json, "erase-partition", "partition-unavailable",
                            *error);
  }
  auto context = std::get<NamedPartitionContext>(std::move(opened));
  const auto &partition = context.table.partitions[context.partitionIndex];
  zzrk::session::SessionTimeouts timeouts{};
  timeouts.status = std::chrono::seconds{120};
  zzrk::session::RockusbSession session{
      *context.loader.transport, timeouts,
      zzrk::session::StatusResiduePolicy::allowNonZeroOnSuccess};
  std::uint64_t completed = 0;
  std::optional<std::uint64_t> lastPercent;
  bool failed = false;
  while (completed < partition.sectorCount()) {
    if (InterruptHandlerGuard::requested()) {
      failed = true;
      break;
    }
    const auto sectors = std::min<std::uint64_t>(
        zzrk::session::kMaximumEraseLbaSectorsPerCommand,
        partition.sectorCount() - completed);
    const auto result = zzrk::session::eraseLba(
        session, zzrk::core::Lba{partition.firstLba + completed},
        zzrk::core::SectorCount{sectors});
    if (const auto *error =
            std::get_if<zzrk::session::SessionError>(&result)) {
      logReadFailure(*error, "Erase partition");
      failed = true;
      break;
    }
    completed += sectors;
    reportPartitionProgress("erase", partition.name, completed,
                            partition.sectorCount(), lastPercent);
  }
  const int closeResult = context.loader.transport->close();
  if (failed || closeResult < 0) {
    if (InterruptHandlerGuard::requested()) {
      return partitionFailure(json, "erase-partition", "cancelled",
                              "Erase partition was cancelled", 130);
    }
    return partitionFailure(json, "erase-partition", "erase-failed",
                            "Erase partition failed before completion");
  }
  const auto bytes = partition.sectorCount() * zzrk::core::kSectorSize;
  if (json) {
    zzrk::cli::printPartitionOperationJson(
        "erase-partition", partition.name, bytes, partition.sectorCount(),
        false, std::cout);
  } else {
    std::ostringstream message;
    message << "Erase partition " << partition.name << " completed: "
            << partition.sectorCount() << " sectors";
    zzrk::logging::info(message.str());
  }
  return EXIT_SUCCESS;
}

int readFlashInfoDevice(const std::string_view portPath) {
  return readLoaderDevice<zzrk::session::FlashInfo>(
      portPath, "ReadFlashInfo", zzrk::session::readFlashInfo,
      zzrk::cli::printFlashInfo);
}

int readFlashIdDevice(const std::string_view portPath) {
  return readLoaderDevice<zzrk::session::FlashId>(portPath, "ReadFlashID",
                                                  zzrk::session::readFlashId,
                                                  zzrk::cli::printFlashId);
}

int listDevices(const bool json) {
  zzrk::logging::debug("Starting read-only Rockchip USB device scan");
  zzrk::transport::UsbDeviceScanner scanner;
  const auto result = scanner.scanRockchipDevices();
  std::string usbAccessHelp;
  if (result.error.has_value() &&
      result.error->kind == zzrk::transport::ScanErrorKind::AccessDenied) {
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
    return zzrk::cli::printScanResultJson(result, std::cout, usbAccessHelp);
  }
  return zzrk::cli::printScanResult(result, std::cout, std::cerr,
                                    usbAccessHelp);
}

int infoDevice(const std::string_view portPath, const bool json) {
  const auto fail = [json](const std::string_view code,
                           const std::string &message) {
    zzrk::logging::error(message);
    if (json) {
      zzrk::cli::printJsonError("info", code, message, std::cout);
    }
    return EXIT_FAILURE;
  };

  zzrk::transport::UsbDeviceScanner scanner;
  auto scan = scanner.scanRockchipDevices();
  const auto selected = zzrk::cli::selectLoaderDevice(scan, portPath);
  if (!selected) {
    if (scan.error.has_value()) {
      logScanFailure(*scan.error, "Device info");
    }
    return fail("device-unavailable",
                "Device info requires the validated RK3588 Loader at the "
                "selected physical port");
  }

  auto opened = zzrk::transport::LibusbBulkTransport::open(*selected.device);
  if (const auto *error =
          std::get_if<zzrk::transport::LibusbOpenError>(&opened)) {
    std::ostringstream message;
    message << "Device info could not open Loader: "
            << describeOpenError(error->code);
    if (error->nativeCode != 0) {
      message << " (libusb " << error->nativeCode << ')';
    }
    return fail("open-failed", message.str());
  }

  auto transport = std::move(
      std::get<std::unique_ptr<zzrk::transport::LibusbBulkTransport>>(opened));
  const auto interfaceInfo = transport->interfaceInfo();
  zzrk::session::RockusbSession session{
      *transport, {},
      zzrk::session::StatusResiduePolicy::allowNonZeroOnSuccess};

  const auto chip = zzrk::session::readChipInfo(session);
  if (const auto *error = std::get_if<zzrk::session::SessionError>(&chip)) {
    logReadFailure(*error, "Device info ReadChipInfo");
    static_cast<void>(transport->close());
    return fail("read-chip-info-failed", "Device info ReadChipInfo failed");
  }
  const auto capability = zzrk::session::readCapability(session);
  if (const auto *error =
          std::get_if<zzrk::session::SessionError>(&capability)) {
    logReadFailure(*error, "Device info ReadCapability");
    static_cast<void>(transport->close());
    return fail("read-capability-failed",
                "Device info ReadCapability failed");
  }
  const auto flashInfo = zzrk::session::readFlashInfo(session);
  if (const auto *error =
          std::get_if<zzrk::session::SessionError>(&flashInfo)) {
    logReadFailure(*error, "Device info ReadFlashInfo");
    static_cast<void>(transport->close());
    return fail("read-flash-info-failed",
                "Device info ReadFlashInfo failed");
  }
  const auto flashId = zzrk::session::readFlashId(session);
  if (const auto *error = std::get_if<zzrk::session::SessionError>(&flashId)) {
    logReadFailure(*error, "Device info ReadFlashID");
    static_cast<void>(transport->close());
    return fail("read-flash-id-failed", "Device info ReadFlashID failed");
  }

  const int closeResult = transport->close();
  if (closeResult < 0) {
    std::ostringstream message;
    message << "Device info could not release Loader interface (libusb "
            << closeResult << ')';
    return fail("close-failed", message.str());
  }

  const zzrk::cli::DeviceInfoReport report{
      .device = *selected.device,
      .interfaceInfo = interfaceInfo,
      .chipInfo = std::get<zzrk::session::ChipInfo>(chip),
      .capability = std::get<zzrk::session::Capability>(capability),
      .flashInfo = std::get<zzrk::session::FlashInfo>(flashInfo),
      .flashId = std::get<zzrk::session::FlashId>(flashId),
  };
  if (json) {
    zzrk::cli::printDeviceInfoJson(report, std::cout);
  } else {
    zzrk::cli::printDeviceInfo(report, std::cout);
  }
  zzrk::logging::info("Device info completed successfully");
  return EXIT_SUCCESS;
}

int inspectImage(const std::string_view path, const bool json) {
  zzrk::logging::info("Inspecting Rockchip firmware image");
  auto parsed = zzrk::image::parseRkFirmwareImage(std::string{path});
  if (const auto *error =
          std::get_if<zzrk::image::FirmwareImageError>(&parsed)) {
    std::ostringstream message;
    message << "Image inspection failed: "
            << zzrk::cli::describeFirmwareImageError(error->code) << " (offset "
            << error->offset;
    if (error->entryIndex != 0U) {
      message << ", entry " << error->entryIndex;
    }
    message << ')';
    zzrk::logging::error(message.str());
    if (json) {
      zzrk::cli::printJsonError("inspect-image", "image-invalid",
                                message.str(), std::cout);
    }
    return EXIT_FAILURE;
  }
  auto image = std::get<zzrk::image::RkFirmwareImage>(std::move(parsed));
  zzrk::logging::info("Verifying firmware MD5 with Qt 6");
  zzrk::qt::FirmwareMd5Verifier verifier;
  const auto verified = verifier.verify(image);
  if (const auto *error =
          std::get_if<zzrk::workflow::FirmwareDigestError>(&verified)) {
    std::ostringstream message;
    message << "Image inspection failed: "
            << zzrk::qt::describeFirmwareDigestError(error->code);
    if (error->code ==
        zzrk::workflow::FirmwareDigestErrorCode::mismatch) {
      message << " (expected " << image.trailingMd5.data() << ", actual "
              << error->actualMd5.data() << ')';
    }
    zzrk::logging::error(message.str());
    if (json) {
      zzrk::cli::printJsonError("inspect-image", "digest-failed",
                                message.str(), std::cout);
    }
    return EXIT_FAILURE;
  }
  if (json) {
    zzrk::cli::printFirmwareImageJson(image, std::cout);
  } else {
    zzrk::cli::printFirmwareImage(image, std::cout);
  }
  return EXIT_SUCCESS;
}

int planImage(const std::string_view path, const bool json) {
  zzrk::logging::info("Building complete firmware flash plan");
  auto parsed = zzrk::image::parseRkFirmwareImage(std::string{path});
  if (const auto *error =
          std::get_if<zzrk::image::FirmwareImageError>(&parsed)) {
    std::ostringstream message;
    message << "Image planning failed: "
            << zzrk::cli::describeFirmwareImageError(error->code) << " (offset "
            << error->offset << ')';
    zzrk::logging::error(message.str());
    if (json) {
      zzrk::cli::printJsonError("plan-image", "image-invalid", message.str(),
                                std::cout);
    }
    return EXIT_FAILURE;
  }
  auto image = std::get<zzrk::image::RkFirmwareImage>(std::move(parsed));
  zzrk::logging::info("Verifying firmware MD5 with Qt 6");
  zzrk::qt::FirmwareMd5Verifier verifier;
  const auto verified = verifier.verify(image);
  if (const auto *error =
          std::get_if<zzrk::workflow::FirmwareDigestError>(&verified)) {
    std::ostringstream message;
    message << "Image planning failed: "
            << zzrk::qt::describeFirmwareDigestError(error->code);
    if (error->code ==
        zzrk::workflow::FirmwareDigestErrorCode::mismatch) {
      message << " (expected " << image.trailingMd5.data() << ", actual "
              << error->actualMd5.data() << ')';
    }
    zzrk::logging::error(message.str());
    if (json) {
      zzrk::cli::printJsonError("plan-image", "digest-failed", message.str(),
                                std::cout);
    }
    return EXIT_FAILURE;
  }
  const auto plan = zzrk::image::buildRkFirmwareFlashPlan(image);
  if (const auto *error = std::get_if<zzrk::image::FirmwarePlanError>(&plan)) {
    std::ostringstream message;
    message << "Image planning failed: "
            << zzrk::cli::describeFirmwarePlanError(error->code) << " (target "
            << error->target << ')';
    zzrk::logging::error(message.str());
    if (json) {
      zzrk::cli::printJsonError("plan-image", "plan-invalid", message.str(),
                                std::cout);
    }
    return EXIT_FAILURE;
  }
  const auto &flashPlan = std::get<zzrk::image::RkFirmwareFlashPlan>(plan);
  if (json) {
    zzrk::cli::printFirmwareFlashPlanJson(flashPlan, std::cout);
  } else {
    zzrk::cli::printFirmwareFlashPlan(flashPlan, std::cout);
  }
  return EXIT_SUCCESS;
}

struct FlashCommandOptions {
  bool reboot{true};
  bool prepareOnly{};
  bool json{};
  bool reportIncludeFirmwarePath{};
  std::optional<std::filesystem::path> reportPath;
  zzrk::image::FirmwareVerifyMode verifyMode{
      zzrk::image::FirmwareVerifyMode::critical};
};

struct WritePartitionOptions {
  std::filesystem::path inputPath;
  bool verify{true};
  bool json{};
};

[[nodiscard]] std::optional<WritePartitionOptions>
parseWritePartitionOptions(const int argc, char *argv[]) {
  WritePartitionOptions options;
  bool inputSpecified = false;
  bool confirmed = false;
  bool verifySpecified = false;
  bool jsonSpecified = false;
  for (int index = 4; index < argc; ++index) {
    const std::string_view argument{argv[index]};
    if (argument == "--input" && !inputSpecified && index + 1 < argc) {
      inputSpecified = true;
      options.inputPath = argv[++index];
      continue;
    }
    if (argument == "--yes" && !confirmed) {
      confirmed = true;
      continue;
    }
    if (argument == "--no-verify" && !verifySpecified) {
      verifySpecified = true;
      options.verify = false;
      continue;
    }
    if (argument == "--json" && !jsonSpecified) {
      jsonSpecified = true;
      options.json = true;
      continue;
    }
    return std::nullopt;
  }
  if (!inputSpecified || options.inputPath.empty() || !confirmed) {
    return std::nullopt;
  }
  return options;
}

struct ErasePartitionOptions {
  bool json{};
};

[[nodiscard]] std::optional<ErasePartitionOptions>
parseErasePartitionOptions(const int argc, char *argv[]) noexcept {
  bool confirmed = false;
  bool jsonSpecified = false;
  ErasePartitionOptions options;
  for (int index = 4; index < argc; ++index) {
    const std::string_view argument{argv[index]};
    if (argument == "--yes" && !confirmed) {
      confirmed = true;
      continue;
    }
    if (argument == "--json" && !jsonSpecified) {
      jsonSpecified = true;
      options.json = true;
      continue;
    }
    return std::nullopt;
  }
  return confirmed ? std::optional{options} : std::nullopt;
}

[[nodiscard]] std::optional<FlashCommandOptions>
parseFlashCommandOptions(const int argc, char *argv[]) {
  if (argc < 5) {
    return std::nullopt;
  }
  FlashCommandOptions options{};
  bool confirmed = false;
  bool rebootSpecified = false;
  bool prepareOnlySpecified = false;
  bool jsonSpecified = false;
  bool verifySpecified = false;
  bool reportSpecified = false;
  bool reportPathSpecified = false;
  for (int index = 4; index < argc; ++index) {
    const std::string_view argument{argv[index]};
    if (argument == "--yes" && !confirmed) {
      confirmed = true;
      continue;
    }
    if (argument == "--no-reboot" && !rebootSpecified) {
      rebootSpecified = true;
      options.reboot = false;
      continue;
    }
    if (argument == "--prepare-only" && !prepareOnlySpecified) {
      prepareOnlySpecified = true;
      options.prepareOnly = true;
      continue;
    }
    if (argument == "--json" && !jsonSpecified) {
      jsonSpecified = true;
      options.json = true;
      continue;
    }
    if (argument == "--report" && !reportPathSpecified &&
        index + 1 < argc) {
      reportPathSpecified = true;
      options.reportPath = std::filesystem::path{argv[++index]};
      if (options.reportPath->empty()) {
        return std::nullopt;
      }
      continue;
    }
    if (argument == "--report-include-firmware-path" && !reportSpecified) {
      reportSpecified = true;
      options.reportIncludeFirmwarePath = true;
      continue;
    }
    if (argument == "--verify" && !verifySpecified && index + 1 < argc) {
      verifySpecified = true;
      const std::string_view value{argv[++index]};
      if (value == "critical") {
        options.verifyMode = zzrk::image::FirmwareVerifyMode::critical;
      } else if (value == "all") {
        options.verifyMode = zzrk::image::FirmwareVerifyMode::all;
      } else if (value == "none") {
        options.verifyMode = zzrk::image::FirmwareVerifyMode::none;
      } else {
        return std::nullopt;
      }
      continue;
    }
    return std::nullopt;
  }
  if (!confirmed ||
      (options.reportIncludeFirmwarePath && !options.reportPath.has_value())) {
    return std::nullopt;
  }
  return options;
}

[[nodiscard]] std::string_view describeWorkflowError(
    const zzrk::workflow::FirmwareUpgradeErrorCode code) noexcept {
  using zzrk::workflow::FirmwareUpgradeErrorCode;
  switch (code) {
  case FirmwareUpgradeErrorCode::cancelled:
    return "the operation was cancelled";
  case FirmwareUpgradeErrorCode::imageInvalid:
    return "the firmware image is invalid";
  case FirmwareUpgradeErrorCode::digestFailed:
    return "the firmware MD5 verification failed";
  case FirmwareUpgradeErrorCode::planInvalid:
    return "the firmware flash plan is invalid";
  case FirmwareUpgradeErrorCode::scanFailed:
    return "USB device scanning failed";
  case FirmwareUpgradeErrorCode::deviceNotFound:
    return "the selected physical USB port was not found";
  case FirmwareUpgradeErrorCode::deviceAmbiguous:
    return "the selected physical USB port is ambiguous";
  case FirmwareUpgradeErrorCode::deviceUnsupported:
    return "the selected device identity is unsupported";
  case FirmwareUpgradeErrorCode::openFailed:
    return "the selected RockUSB interface could not be opened";
  case FirmwareUpgradeErrorCode::loaderResetFailed:
    return "the current Loader could not enter Maskrom";
  case FirmwareUpgradeErrorCode::reenumerationTimedOut:
    return "the expected device did not re-enumerate in time";
  case FirmwareUpgradeErrorCode::loaderDownloadFailed:
    return "the embedded RAM Loader download failed";
  case FirmwareUpgradeErrorCode::loaderNotReady:
    return "the RAM Loader did not pass readiness checks";
  case FirmwareUpgradeErrorCode::wrongChip:
    return "the RAM Loader did not report RK3588";
  case FirmwareUpgradeErrorCode::storageCheckFailed:
    return "the eMMC readiness check failed";
  case FirmwareUpgradeErrorCode::storageNotEmmc:
    return "the selected storage is not the validated eMMC target";
  case FirmwareUpgradeErrorCode::deviceTooSmall:
    return "the eMMC is smaller than the firmware plan";
  case FirmwareUpgradeErrorCode::flashFailed:
    return "the firmware flash operation failed";
  case FirmwareUpgradeErrorCode::resetFailed:
    return "the firmware was written but device reset failed";
  case FirmwareUpgradeErrorCode::closeFailed:
    return "the RockUSB interface could not be released";
  }
  return "unknown firmware workflow error";
}

int flashImage(const std::string_view portPath,
               const std::filesystem::path &imagePath,
               const FlashCommandOptions &options) {
  if (options.reportPath.has_value()) {
    std::error_code imageError;
    std::error_code reportError;
    const auto absoluteImage =
        std::filesystem::absolute(imagePath, imageError).lexically_normal();
    const auto absoluteReport =
        std::filesystem::absolute(*options.reportPath, reportError)
            .lexically_normal();
    if (!imageError && !reportError && absoluteImage == absoluteReport) {
      zzrk::logging::error(
          "The firmware report path must not overwrite the input image");
      return 64;
    }
  }
  const InterruptHandlerGuard interruptHandler;
  auto backend = zzrk::workflow::createLibusbFirmwareUpgradeBackend();
  zzrk::qt::FirmwareMd5Verifier verifier;
  zzrk::workflow::FirmwareUpgradeWorkflow workflow{*backend, verifier};

  std::string lastProgressKey;
  std::uint64_t lastPercent = 101U;
  const auto progress = [&](const zzrk::workflow::FirmwareUpgradeEvent &event) {
    if (options.json) {
      zzrk::cli::printFirmwareUpgradeEventJson(event, std::cout);
      std::cout.flush();
      return;
    }
    std::string key{zzrk::workflow::toString(event.stage)};
    key += ':';
    key += event.target;
    const std::uint64_t percent =
        event.totalBytes == 0U
            ? 100U
            : std::min<std::uint64_t>(
                  100U, event.completedBytes * 100U / event.totalBytes);
    if (key == lastProgressKey &&
        (event.totalBytes == 0U ||
         (event.completedBytes != event.totalBytes &&
          percent < lastPercent + 5U))) {
      return;
    }
    lastProgressKey = std::move(key);
    lastPercent = percent;

    std::ostringstream message;
    if (event.flashPhase.has_value()) {
      message << describeFlashPhase(*event.flashPhase) << ' ' << event.target;
    } else {
      message << zzrk::workflow::toString(event.stage);
      if (!event.target.empty()) {
        message << ' ' << event.target;
      }
    }
    if (event.totalBytes != 0U) {
      message << ": " << percent << "% (" << event.completedBytes << '/'
              << event.totalBytes << " bytes)";
    }
    zzrk::logging::info(message.str());
  };
  const auto cancellation = [] { return InterruptHandlerGuard::requested(); };
  const zzrk::workflow::FirmwareUpgradeRequest request{
      .portPath = std::string{portPath},
      .imagePath = imagePath,
      .verifyMode = options.verifyMode,
      .reboot = options.reboot,
      .prepareOnly = options.prepareOnly,
  };

  auto execution = zzrk::workflow::runFirmwareUpgradeWithReport(
      workflow, request, progress, cancellation);
  const auto reportError = options.reportPath.has_value()
                               ? zzrk::qt::writeFirmwareUpgradeReport(
                                     execution.report, *options.reportPath,
                                     zzrk::qt::FirmwareUpgradeReportWriteOptions{
                                         .includeFirmwarePath =
                                             options.reportIncludeFirmwarePath,
                                         .applicationVersion = ZZRK_VERSION,
                                     })
                               : std::nullopt;
  if (reportError.has_value()) {
    std::ostringstream message;
    message << "Could not write firmware upgrade report to "
            << options.reportPath->string() << ": " << reportError->message;
    zzrk::logging::error(message.str());
  } else if (options.reportPath.has_value()) {
    zzrk::logging::info("Firmware upgrade report written to " +
                        options.reportPath->string());
  }

  const auto &result = execution.result;
  if (const auto *error =
          std::get_if<zzrk::workflow::FirmwareUpgradeError>(&result)) {
    std::ostringstream message;
    message << "System flash failed during "
            << zzrk::workflow::toString(error->stage) << " at "
            << error->target << ": " << describeWorkflowError(error->code);
    if (error->completedBytes != 0U) {
      message << " after " << error->completedBytes << " bytes";
    }
    if (error->code ==
        zzrk::workflow::FirmwareUpgradeErrorCode::cancelled) {
      zzrk::logging::warning(message.str());
    } else {
      zzrk::logging::error(message.str());
    }
    if (error->sessionError.has_value()) {
      logReadFailure(*error->sessionError, "Firmware workflow RockUSB command");
    }
    if (error->digestError.has_value() &&
        error->digestError->code ==
            zzrk::workflow::FirmwareDigestErrorCode::mismatch) {
      std::ostringstream digest;
      digest << "Firmware MD5 mismatch (actual "
             << error->digestError->actualMd5.data() << ')';
      zzrk::logging::error(digest.str());
    }
    if (options.json) {
      zzrk::cli::printFirmwareUpgradeErrorJson(*error, message.str(),
                                               std::cout);
    }
    return error->code ==
                   zzrk::workflow::FirmwareUpgradeErrorCode::cancelled
               ? 130
               : EXIT_FAILURE;
  }

  const auto &success =
      std::get<zzrk::workflow::FirmwareUpgradeSuccess>(result);
  if (options.json) {
    zzrk::cli::printFirmwareUpgradeSuccessJson(success, std::cout);
    return reportError.has_value() ? EXIT_FAILURE : EXIT_SUCCESS;
  }
  if (success.preparedOnly) {
    std::ostringstream message;
    message << "RAM Loader preparation succeeded on " << success.portPath
            << ": RK3588, eMMC " << success.deviceSectors
            << " sectors; no eMMC write was performed";
    zzrk::logging::info(message.str());
    return reportError.has_value() ? EXIT_FAILURE : EXIT_SUCCESS;
  }

  std::ostringstream message;
  message << "System flash completed: " << success.partitionBytesWritten
          << " partition bytes processed, " << success.sectorsErased
          << " sectors erased, primary GPT "
          << (success.partitionTableVerified ? "verified" : "not verified")
          << ", backup GPT write submitted, Loader "
          << (success.loaderWritten ? "written" : "not written")
          << (success.loaderVerified ? " and verified" : "") << ", "
          << success.partitionsVerified << " partition payloads verified";
  zzrk::logging::info(message.str());
  if (success.resetAccepted) {
    zzrk::logging::info(
        "Reset command accepted; the device is booting the new system");
  } else {
    zzrk::logging::info(
        "Automatic reboot disabled; the device remains in the RAM Loader");
  }
  return reportError.has_value() ? EXIT_FAILURE : EXIT_SUCCESS;
}

} // namespace

int main(const int argc, char *argv[]) {
  [[maybe_unused]] const auto loggingSession =
      zzrk::logging::initializeConsoleLogging("zzrkdevtool-cli");
  const std::string_view executable = argc > 0 ? argv[0] : "zzrkdevtool-cli";
  if (argc == 2 && std::string_view{argv[1]} == "list") {
    return listDevices(false);
  }
  if (argc == 3 && std::string_view{argv[1]} == "list" &&
      std::string_view{argv[2]} == "--json") {
    return listDevices(true);
  }
  if ((argc == 3 || argc == 4) &&
      std::string_view{argv[1]} == "info" &&
      (argc == 3 || std::string_view{argv[3]} == "--json")) {
    return infoDevice(argv[2], argc == 4);
  }
  if ((argc == 3 || argc == 4) &&
      std::string_view{argv[1]} == "inspect-image" &&
      (argc == 3 || std::string_view{argv[3]} == "--json")) {
    return inspectImage(argv[2], argc == 4);
  }
  if ((argc == 3 || argc == 4) &&
      std::string_view{argv[1]} == "plan-image" &&
      (argc == 3 || std::string_view{argv[3]} == "--json")) {
    return planImage(argv[2], argc == 4);
  }
  if (argc >= 2 && std::string_view{argv[1]} == "flash-image") {
    const auto options = parseFlashCommandOptions(argc, argv);
    if (options.has_value()) {
      return flashImage(argv[2], argv[3], *options);
    }
    zzrk::logging::error(
        "flash-image requires: <port-path> <update.img> --yes "
        "[--verify critical|all|none] [--no-reboot] [--prepare-only] "
        "[--report <file>] [--report-include-firmware-path] [--json]");
    if (hasArgument(argc, argv, "--json")) {
      zzrk::cli::printJsonError("flash-image", "invalid-arguments",
                                "flash-image arguments are invalid",
                                std::cout);
    }
    return 64;
  }
  if (argc == 3 && std::string_view{argv[1]} == "reset") {
    return resetDeviceCommand(argv[2]);
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
  if ((argc == 3 || argc == 4) &&
      std::string_view{argv[1]} == "partitions" &&
      (argc == 3 || std::string_view{argv[3]} == "--json")) {
    return listPartitionsDevice(argv[2], argc == 4);
  }
  if ((argc == 6 || argc == 7) &&
      std::string_view{argv[1]} == "read-partition" &&
      std::string_view{argv[4]} == "--output" &&
      (argc == 6 || std::string_view{argv[6]} == "--json")) {
    return readPartitionDevice(argv[2], argv[3], argv[5], argc == 7);
  }
  if (argc >= 2 && std::string_view{argv[1]} == "write-partition") {
    const auto options = parseWritePartitionOptions(argc, argv);
    if (argc >= 4 && options.has_value()) {
      return writePartitionDevice(argv[2], argv[3], options->inputPath,
                                  options->verify, options->json);
    }
    zzrk::logging::error(
        "write-partition requires: <port-path> <name> --input <file> --yes "
        "[--no-verify] [--json]");
    if (hasArgument(argc, argv, "--json")) {
      zzrk::cli::printJsonError("write-partition", "invalid-arguments",
                                "write-partition arguments are invalid",
                                std::cout);
    }
    return 64;
  }
  if (argc >= 2 && std::string_view{argv[1]} == "erase-partition") {
    const auto options = parseErasePartitionOptions(argc, argv);
    if (argc >= 4 && options.has_value()) {
      return erasePartitionDevice(argv[2], argv[3], options->json);
    }
    zzrk::logging::error(
        "erase-partition requires: <port-path> <name> --yes [--json]");
    if (hasArgument(argc, argv, "--json")) {
      zzrk::cli::printJsonError("erase-partition", "invalid-arguments",
                                "erase-partition arguments are invalid",
                                std::cout);
    }
    return 64;
  }
  if (argc == 9 && std::string_view{argv[1]} == "read-lba" &&
      std::string_view{argv[3]} == "--start" &&
      std::string_view{argv[5]} == "--count" &&
      std::string_view{argv[7]} == "--output") {
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
  if (hasArgument(argc, argv, "--json")) {
    const std::string_view command = argc > 1 ? argv[1] : "unknown";
    zzrk::cli::printJsonError(command, "invalid-arguments",
                              "command arguments are invalid", std::cout);
    return 64;
  }
  printUsage(executable, std::cerr);
  return 64;
}
