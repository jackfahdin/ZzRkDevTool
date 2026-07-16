// SPDX-License-Identifier: MIT

#include "DeviceListPrinter.hpp"

#include <cstdlib>
#include <iomanip>
#include <ostream>

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
    output << "PORT          VID:PID    MODE      BCDUSB\n";
    for (const auto& device : result.devices) {
        const std::string_view portPath =
            device.portPath.empty() ? std::string_view{"-"} : device.portPath;
        output << std::left << std::setw(14) << portPath << "  " << std::right
               << std::hex << std::setfill('0') << std::setw(4)
               << device.vendorId << ':' << std::setw(4) << device.productId
               << std::setfill(' ') << std::dec << "  " << std::left
               << std::setw(10) << transport::toString(device.mode) << "0x"
               << std::hex << std::setfill('0') << std::setw(4) << device.bcdUsb
               << std::setfill(' ') << std::dec << '\n';
    }
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

} // namespace zzrk::cli
