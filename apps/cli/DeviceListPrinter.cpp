// SPDX-License-Identifier: MIT

#include "DeviceListPrinter.hpp"

#include <cstdlib>
#include <iomanip>
#include <ostream>
#include <string_view>

namespace zzrk::cli {
namespace {

void printAccessHelp(const transport::ScanError& error,
                     std::ostream& output,
                     const std::string_view usbAccessHelp)
{
    if (error.kind == transport::ScanErrorKind::AccessDenied
        && !usbAccessHelp.empty()) {
        output << usbAccessHelp << '\n';
    }
}

void printDevices(const transport::ScanResult& result, std::ostream& output)
{
    output << "PORT          VID:PID    MODE      ACCESS           BCDUSB  "
              "BCDDEVICE\n";
    for (const auto& device : result.devices) {
        const std::string_view portPath =
            device.portPath.empty() ? std::string_view{"-"} : device.portPath;
        output << std::left << std::setw(14) << portPath << "  " << std::right
               << std::hex << std::setfill('0') << std::setw(4)
               << device.vendorId << ':' << std::setw(4) << device.productId
               << std::setfill(' ') << std::dec << "  " << std::left
               << std::setw(10) << transport::toString(device.mode)
               << std::setw(17)
               << ([&device]() -> std::string_view {
                      using transport::DeviceMode;
                      switch (device.mode) {
                      case DeviceMode::Adb:
                          return "Not flashable";
                      case DeviceMode::Maskrom:
                          return "Loader required";
                      case DeviceMode::Loader:
                          return "RockUSB";
                      case DeviceMode::Unknown:
                          return "Unsupported";
                      }
                      return "Unsupported";
                  })()
               << "0x"
               << std::right << std::hex << std::setfill('0') << std::setw(4)
               << device.bcdUsb
               << std::setfill(' ') << "  0x" << std::right << std::hex
               << std::setfill('0') << std::setw(4) << device.bcdDevice
               << std::setfill(' ') << std::dec << '\n';
    }
}

void printJsonString(const std::string_view value, std::ostream& output)
{
    constexpr char hexDigits[] = "0123456789abcdef";
    output << '"';
    for (const char rawCharacter : value) {
        const auto character = static_cast<unsigned char>(rawCharacter);
        switch (character) {
        case '"':
            output << "\\\"";
            break;
        case '\\':
            output << "\\\\";
            break;
        case '\b':
            output << "\\b";
            break;
        case '\f':
            output << "\\f";
            break;
        case '\n':
            output << "\\n";
            break;
        case '\r':
            output << "\\r";
            break;
        case '\t':
            output << "\\t";
            break;
        default:
            if (character < 0x20U) {
                output << "\\u00" << hexDigits[(character >> 4U) & 0x0fU]
                       << hexDigits[character & 0x0fU];
            } else {
                output << static_cast<char>(character);
            }
            break;
        }
    }
    output << '"';
}

void printJsonError(const transport::ScanError& error,
                    const std::string_view usbAccessHelp,
                    std::ostream& output)
{
    output << "{\"code\":" << error.code << ",\"kind\":";
    printJsonString(error.kind == transport::ScanErrorKind::AccessDenied
                        ? "access-denied"
                        : "other",
                    output);
    output << ",\"message\":";
    printJsonString(error.message, output);
    output << ",\"help\":";
    if (error.kind == transport::ScanErrorKind::AccessDenied) {
        printJsonString(usbAccessHelp, output);
    } else {
        printJsonString({}, output);
    }
    output << '}';
}

} // namespace

int printScanResult(const transport::ScanResult& result,
                    std::ostream& output,
                    std::ostream& errorOutput,
                    const std::string_view usbAccessHelp)
{
    if (result.error.has_value() && result.devices.empty()) {
        errorOutput << "USB scan failed (" << result.error->code
                    << "): " << result.error->message << '\n';
        printAccessHelp(*result.error, errorOutput, usbAccessHelp);
        return kFatalScanExitCode;
    }

    if (result.devices.empty()) {
        output << "No Rockchip USB devices found.\n";
        return EXIT_SUCCESS;
    }

    printDevices(result, output);
    if (result.error.has_value()) {
        errorOutput << "Warning: the scan was partial: "
                    << result.error->message << '\n';
        printAccessHelp(*result.error, errorOutput, usbAccessHelp);
        return kPartialScanExitCode;
    }
    return EXIT_SUCCESS;
}

int printScanResultJson(const transport::ScanResult& result,
                        std::ostream& output,
                        const std::string_view usbAccessHelp)
{
    const bool fatal = result.error.has_value() && result.devices.empty();
    output << "{\"schemaVersion\":1,\"success\":"
           << (fatal ? "false" : "true") << ",\"result\":{\"partial\":"
           << (result.error.has_value() && !fatal ? "true" : "false")
           << ",\"devices\":[";

    bool firstDevice = true;
    for (const auto& device : result.devices) {
        if (!firstDevice) {
            output << ',';
        }
        firstDevice = false;
        output << "{\"portPath\":";
        printJsonString(device.portPath, output);
        output << ",\"vendorId\":" << device.vendorId
               << ",\"productId\":" << device.productId
               << ",\"bcdUsb\":" << device.bcdUsb
               << ",\"bcdDevice\":" << device.bcdDevice
               << ",\"busNumber\":"
               << static_cast<unsigned int>(device.busNumber)
               << ",\"deviceAddress\":"
               << static_cast<unsigned int>(device.deviceAddress)
               << ",\"portNumbers\":[";
        for (std::size_t index = 0; index < device.portNumbers.size(); ++index) {
            if (index != 0U) {
                output << ',';
            }
            output << static_cast<unsigned int>(device.portNumbers[index]);
        }
        output << "],\"mode\":";
        printJsonString(transport::toString(device.mode), output);
        output << ",\"rockusbOperable\":"
               << (device.mode == transport::DeviceMode::Loader ? "true"
                                                                  : "false")
               << '}';
    }
    output << "]},\"error\":";
    if (result.error.has_value()) {
        printJsonError(*result.error, usbAccessHelp, output);
    } else {
        output << "null";
    }
    output << "}\n";

    if (fatal) {
        return kFatalScanExitCode;
    }
    return result.error.has_value() ? kPartialScanExitCode : EXIT_SUCCESS;
}

} // namespace zzrk::cli
