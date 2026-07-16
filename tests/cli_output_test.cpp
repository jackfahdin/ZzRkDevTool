// SPDX-License-Identifier: MIT

#include "DeviceListPrinter.hpp"

#include <zzrk/transport/UsbDeviceScanner.hpp>

#include <cstdint>
#include <iostream>
#include <sstream>
#include <string_view>

namespace {

int failures = 0;

void expect(const bool condition, const std::string_view description)
{
    if (!condition) {
        std::cerr << "FAIL: " << description << '\n';
        ++failures;
    }
}

zzrk::transport::UsbDeviceInfo makeDevice()
{
    return zzrk::transport::UsbDeviceInfo{
        .vendorId = 0x2207,
        .productId = 0x350b,
        .bcdUsb = 0x0200,
        .busNumber = 1,
        .portNumbers = {2},
        .portPath = "1-2",
        .mode = zzrk::transport::DeviceMode::Maskrom,
        .deviceAddress = 4,
    };
}

void testEmptySuccess()
{
    std::ostringstream output;
    std::ostringstream errorOutput;
    const int exitCode = zzrk::cli::printScanResult(
        {}, output, errorOutput, "permission help");
    expect(exitCode == 0, "an empty complete scan succeeds");
    expect(output.str() == "No Rockchip USB devices found.\n",
           "an empty complete scan has a clear result");
    expect(errorOutput.str().empty(), "an empty complete scan has no warning");
}

void testFatalErrorHelpIsCategorySpecific()
{
    zzrk::transport::ScanResult result{
        .devices = {},
        .error = zzrk::transport::ScanError{
            .code = -99,
            .message = "backend failed",
            .kind = zzrk::transport::ScanErrorKind::Other,
        },
    };
    std::ostringstream output;
    std::ostringstream errorOutput;
    int exitCode = zzrk::cli::printScanResult(
        result, output, errorOutput, "permission help");
    expect(exitCode == zzrk::cli::kFatalScanExitCode,
           "a fatal scan error returns 1");
    expect(errorOutput.str().find("backend failed") != std::string::npos,
           "a fatal scan error is printed");
    expect(errorOutput.str().find("permission help") == std::string::npos,
           "a non-access error does not print permission guidance");

    result.error->code = -3;
    result.error->message = "access denied";
    result.error->kind = zzrk::transport::ScanErrorKind::AccessDenied;
    output.str({});
    errorOutput.str({});
    exitCode = zzrk::cli::printScanResult(
        result, output, errorOutput, "permission help");
    expect(exitCode == zzrk::cli::kFatalScanExitCode,
           "a fatal access error still returns 1");
    expect(errorOutput.str().find("permission help") != std::string::npos,
           "an access error prints permission guidance");
}

void testPartialScanReturnsTwo()
{
    auto device = makeDevice();
    device.portPath.clear();
    zzrk::transport::ScanResult result{
        .devices = {device},
        .error = zzrk::transport::ScanError{
            .code = -12,
            .message = "stable path unavailable",
            .kind = zzrk::transport::ScanErrorKind::Other,
        },
    };
    std::ostringstream output;
    std::ostringstream errorOutput;
    const int exitCode = zzrk::cli::printScanResult(
        result, output, errorOutput, "permission help");
    expect(exitCode == zzrk::cli::kPartialScanExitCode,
           "a partial scan returns 2");
    expect(output.str().find("2207:350b") != std::string::npos,
           "partial results still list usable device diagnostics");
    expect(output.str().find("-") != std::string::npos,
           "a missing stable port path is displayed explicitly");
    expect(errorOutput.str().find("scan was partial") != std::string::npos,
           "a partial scan emits a warning");
    expect(errorOutput.str().find("permission help") == std::string::npos,
           "a non-access partial error omits permission guidance");

    result.error->code = -3;
    result.error->kind = zzrk::transport::ScanErrorKind::AccessDenied;
    output.str({});
    errorOutput.str({});
    const int accessExitCode = zzrk::cli::printScanResult(
        result, output, errorOutput, "permission help");
    expect(accessExitCode == zzrk::cli::kPartialScanExitCode,
           "a partial access error still returns 2");
    expect(errorOutput.str().find("permission help") != std::string::npos,
           "a partial access error prints permission guidance");
}

void testCompleteDeviceListSucceeds()
{
    zzrk::transport::ScanResult result{.devices = {makeDevice()}, .error = {}};
    std::ostringstream output;
    std::ostringstream errorOutput;
    const int exitCode = zzrk::cli::printScanResult(
        result, output, errorOutput, "permission help");
    expect(exitCode == 0, "a complete non-empty scan succeeds");
    expect(output.str().find("1-2") != std::string::npos,
           "a complete scan prints the stable port path");
    expect(errorOutput.str().empty(), "a complete scan has no warning");
}

} // namespace

int main()
{
    testEmptySuccess();
    testFatalErrorHelpIsCategorySpecific();
    testPartialScanReturnsTwo();
    testCompleteDeviceListSucceeds();
    return failures == 0 ? 0 : 1;
}
