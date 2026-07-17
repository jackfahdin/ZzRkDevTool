// SPDX-License-Identifier: MIT

#include "DeviceListPrinter.hpp"
#include "ProbeSupport.hpp"

#include <zzrk/transport/UsbDeviceScanner.hpp>

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <sstream>
#include <string_view>
#include <utility>

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
        .bcdDevice = 0x0100,
        .busNumber = 1,
        .portNumbers = {2},
        .portPath = "1-2",
        .mode = zzrk::transport::DeviceMode::Maskrom,
        .deviceAddress = 4,
    };
}

zzrk::transport::UsbDeviceInfo makeLoaderDevice()
{
    auto device = makeDevice();
    device.bcdUsb = 0x0201;
    device.bcdDevice = 0x0223;
    device.portNumbers = {5};
    device.portPath = "1-5";
    device.mode = zzrk::transport::DeviceMode::Loader;
    return device;
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
    expect(output.str().find("0x0200") != std::string::npos,
           "BCD USB values keep leading zeroes after left-aligned columns");
    expect(output.str().find("0x2000") == std::string::npos,
           "BCD USB digits are never padded on the right");
    expect(output.str().find("0x0100") != std::string::npos,
           "BCD device revisions are printed with leading zeroes");
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

void testLoaderSelectionRequiresExactUniquePath()
{
    auto otherDevice = makeLoaderDevice();
    otherDevice.portNumbers = {6};
    otherDevice.portPath = "1-6";
    zzrk::transport::ScanResult result{
        .devices = {makeLoaderDevice(), otherDevice}, .error = {}};

    const auto selected = zzrk::cli::selectLoaderDevice(result, "1-5");
    expect(static_cast<bool>(selected),
           "probe accepts one exact Loader port path");
    expect(selected.device == &result.devices.front(),
           "probe does not select a different Loader device");

    const auto missing = zzrk::cli::selectLoaderDevice(result, "1-7");
    expect(missing.error == zzrk::cli::LoaderSelectionError::notFound,
           "probe rejects a missing physical port path");

    result.devices.push_back(makeLoaderDevice());
    const auto duplicate = zzrk::cli::selectLoaderDevice(result, "1-5");
    expect(duplicate.error == zzrk::cli::LoaderSelectionError::ambiguous,
           "probe rejects duplicate devices at one physical port path");
}

void testLoaderSelectionRejectsPartialScan()
{
    zzrk::transport::ScanResult result{
        .devices = {makeLoaderDevice()},
        .error = zzrk::transport::ScanError{
            .code = -1,
            .message = "partial scan",
            .kind = zzrk::transport::ScanErrorKind::Other,
        },
    };
    const auto selected = zzrk::cli::selectLoaderDevice(result, "1-5");
    expect(selected.error == zzrk::cli::LoaderSelectionError::scanFailed,
           "probe refuses a partial scan even when the target was listed");
}

void testLoaderSelectionRequiresExactIdentity()
{
    auto expectUnsupported = [](zzrk::transport::UsbDeviceInfo device,
                                const std::string_view description) {
        zzrk::transport::ScanResult result{.devices = {std::move(device)},
                                           .error = {}};
        const auto selected = zzrk::cli::selectLoaderDevice(result, "1-5");
        expect(selected.error == zzrk::cli::LoaderSelectionError::unsupported,
               description);
    };

    auto device = makeLoaderDevice();
    device.vendorId = 0x1234;
    expectUnsupported(std::move(device), "probe rejects a non-Rockchip VID");

    device = makeLoaderDevice();
    device.productId = 0x350a;
    expectUnsupported(std::move(device), "probe rejects a different PID");

    device = makeLoaderDevice();
    device.bcdUsb = 0x0203;
    expectUnsupported(std::move(device), "probe rejects a different bcdUSB");

    device = makeLoaderDevice();
    device.bcdDevice = 0x0224;
    expectUnsupported(std::move(device),
                      "probe rejects an unvalidated Loader revision");

    device = makeLoaderDevice();
    device.mode = zzrk::transport::DeviceMode::Maskrom;
    expectUnsupported(std::move(device), "probe rejects non-Loader mode");
}

void testProbeSuccessOutputIsExplicit()
{
    const zzrk::transport::LibusbInterfaceInfo interfaceInfo{
        .interfaceNumber = 0,
        .alternateSetting = 0,
        .bulkInEndpoint = 0x81,
        .bulkOutEndpoint = 0x02,
        .bulkInMaxPacketSize = 512,
        .bulkOutMaxPacketSize = 512,
    };
    std::ostringstream output;
    zzrk::cli::printProbeSuccess(makeLoaderDevice(), interfaceInfo, output);

    expect(output.str()
               == "Probe succeeded: 1-5 (2207:350b, Loader, bcdDevice "
                  "0x0223)\n"
                  "Interface 0, alternate 0, bulk IN 0x81, bulk OUT 0x02, "
                  "max packet 512/512 bytes\n"
                  "Interface claim and release succeeded; no RockUSB command "
                  "was sent.\n",
           "probe success output reports only the verified lifecycle");
}

void testChipInfoOutputUsesFixedWidthHexBytes()
{
    const zzrk::session::ChipInfo chipInfo{
        .bytes = {
            std::byte{0x00}, std::byte{0x01}, std::byte{0x09}, std::byte{0x0a},
            std::byte{0x10}, std::byte{0x38}, std::byte{0x7f}, std::byte{0x80},
            std::byte{0xa5}, std::byte{0xfe}, std::byte{0xff}, std::byte{0x20},
            std::byte{0x03}, std::byte{0x04}, std::byte{0x05}, std::byte{0x06},
        },
    };
    std::ostringstream output;
    zzrk::cli::printChipInfo(makeLoaderDevice(), chipInfo, output);

    expect(output.str()
               == "Port: 1-5 (bcdDevice 0x0223)\n"
                  "Chip info: 00 01 09 0a 10 38 7f 80 a5 fe ff 20 03 04 05 "
                  "06\n",
           "chip info output prints exactly 16 fixed-width hexadecimal bytes");
}

} // namespace

int main()
{
    testEmptySuccess();
    testFatalErrorHelpIsCategorySpecific();
    testPartialScanReturnsTwo();
    testCompleteDeviceListSucceeds();
    testLoaderSelectionRequiresExactUniquePath();
    testLoaderSelectionRejectsPartialScan();
    testLoaderSelectionRequiresExactIdentity();
    testProbeSuccessOutputIsExplicit();
    testChipInfoOutputUsesFixedWidthHexBytes();
    return failures == 0 ? 0 : 1;
}
