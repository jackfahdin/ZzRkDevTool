// SPDX-License-Identifier: MIT

#include "DeviceListPrinter.hpp"

#include <zzrk/platform/PlatformServices.hpp>
#include <zzrk/transport/UsbDeviceScanner.hpp>

#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

namespace {

void printUsage(const std::string_view executable)
{
    std::cout << "Usage: " << executable << " list\n"
              << "       " << executable << " --version\n\n"
              << "The current milestone only enumerates Rockchip USB devices; "
                 "it never opens or writes them.\n";
}

int listDevices()
{
    zzrk::transport::UsbDeviceScanner scanner;
    const auto result = scanner.scanRockchipDevices();
    std::string usbAccessHelp;
    if (result.error.has_value()
        && result.error->kind
               == zzrk::transport::ScanErrorKind::AccessDenied) {
        const auto platform = zzrk::platform::createPlatformServices();
        usbAccessHelp = platform->usbAccessHelp();
    }
    return zzrk::cli::printScanResult(result, std::cout, std::cerr,
                                      usbAccessHelp);
}

} // namespace

int main(const int argc, char* argv[])
{
    const std::string_view executable = argc > 0 ? argv[0] : "zzrkdevtool-cli";
    if (argc == 2 && std::string_view{argv[1]} == "list") {
        return listDevices();
    }
    if (argc == 2 && std::string_view{argv[1]} == "--version") {
        std::cout << "ZzRkDevTool " ZZRK_VERSION "\n";
        return EXIT_SUCCESS;
    }

    printUsage(executable);
    return argc == 1 ? EXIT_SUCCESS : 64;
}
