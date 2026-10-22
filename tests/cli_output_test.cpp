// SPDX-License-Identifier: MIT

#include "DeviceListPrinter.hpp"
#include "ProbeSupport.hpp"
#include "StructuredOutput.hpp"

#include <zzrk/transport/UsbDeviceScanner.hpp>

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace {

int failures = 0;

void expect(const bool condition, const std::string_view description) {
  if (!condition) {
    std::cerr << "FAIL: " << description << '\n';
    ++failures;
  }
}

zzrk::transport::UsbDeviceInfo makeDevice() {
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

zzrk::transport::UsbDeviceInfo makeLoaderDevice() {
  auto device = makeDevice();
  device.bcdUsb = 0x0201;
  device.bcdDevice = 0x0223;
  device.portNumbers = {5};
  device.portPath = "1-5";
  device.mode = zzrk::transport::DeviceMode::Loader;
  return device;
}

void testEmptySuccess() {
  std::ostringstream output;
  std::ostringstream errorOutput;
  const int exitCode =
      zzrk::cli::printScanResult({}, output, errorOutput, "permission help");
  expect(exitCode == 0, "an empty complete scan succeeds");
  expect(output.str() == "No Rockchip USB devices found.\n",
         "an empty complete scan has a clear result");
  expect(errorOutput.str().empty(), "an empty complete scan has no warning");
}

void testFatalErrorHelpIsCategorySpecific() {
  zzrk::transport::ScanResult result{
      .devices = {},
      .error =
          zzrk::transport::ScanError{
              .code = -99,
              .message = "backend failed",
              .kind = zzrk::transport::ScanErrorKind::Other,
          },
  };
  std::ostringstream output;
  std::ostringstream errorOutput;
  int exitCode = zzrk::cli::printScanResult(result, output, errorOutput,
                                            "permission help");
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
  exitCode = zzrk::cli::printScanResult(result, output, errorOutput,
                                        "permission help");
  expect(exitCode == zzrk::cli::kFatalScanExitCode,
         "a fatal access error still returns 1");
  expect(errorOutput.str().find("permission help") != std::string::npos,
         "an access error prints permission guidance");
}

void testPartialScanReturnsTwo() {
  auto device = makeDevice();
  device.portPath.clear();
  zzrk::transport::ScanResult result{
      .devices = {device},
      .error =
          zzrk::transport::ScanError{
              .code = -12,
              .message = "stable path unavailable",
              .kind = zzrk::transport::ScanErrorKind::Other,
          },
  };
  std::ostringstream output;
  std::ostringstream errorOutput;
  const int exitCode = zzrk::cli::printScanResult(result, output, errorOutput,
                                                  "permission help");
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

void testCompleteDeviceListSucceeds() {
  zzrk::transport::ScanResult result{.devices = {makeDevice()}, .error = {}};
  std::ostringstream output;
  std::ostringstream errorOutput;
  const int exitCode = zzrk::cli::printScanResult(result, output, errorOutput,
                                                  "permission help");
  expect(exitCode == 0, "a complete non-empty scan succeeds");
  expect(output.str().find("1-2") != std::string::npos,
         "a complete scan prints the stable port path");
  expect(errorOutput.str().empty(), "a complete scan has no warning");
}

void testAdbDeviceIsVisibleButNotOperable() {
  auto device = makeDevice();
  device.productId = 0x0006;
  device.mode = zzrk::transport::DeviceMode::Adb;
  zzrk::transport::ScanResult result{.devices = {device}, .error = {}};

  std::ostringstream output;
  std::ostringstream errorOutput;
  const int exitCode = zzrk::cli::printScanResult(result, output, errorOutput,
                                                  "permission help");
  expect(exitCode == 0, "an ADB device is a successful discovery result");
  expect(output.str().find("2207:0006") != std::string::npos,
         "the observed ADB PID is printed");
  expect(output.str().find("Not flashable") != std::string::npos,
         "ADB is explicitly marked as not flashable");
}

void testJsonDeviceListHasStableEnvelope() {
  auto device = makeLoaderDevice();
  zzrk::transport::ScanResult result{.devices = {device}, .error = {}};
  std::ostringstream output;
  const int exitCode =
      zzrk::cli::printScanResultJson(result, output, "permission help");
  const std::string json = output.str();

  expect(exitCode == 0, "complete JSON output succeeds");
  expect(json.find("\"schemaVersion\":1") != std::string::npos,
         "JSON output is versioned");
  expect(json.find("\"portPath\":\"1-5\"") != std::string::npos,
         "JSON output contains stable device identity");
  expect(json.find("\"mode\":\"Loader\"") != std::string::npos,
         "JSON output contains the device mode");
  expect(json.find("\"rockusbOperable\":true") != std::string::npos,
         "JSON output marks Loader as RockUSB-operable");
  expect(json.find("\"error\":null") != std::string::npos,
         "JSON success has an explicit null error");
}

void testJsonScanErrorEscapesText() {
  zzrk::transport::ScanResult result{
      .devices = {},
      .error =
          zzrk::transport::ScanError{
              .code = -3,
              .message = "denied \"usb\"\nnow",
              .kind = zzrk::transport::ScanErrorKind::AccessDenied,
          },
  };
  std::ostringstream output;
  const int exitCode =
      zzrk::cli::printScanResultJson(result, output, "use \\ rule");
  const std::string json = output.str();

  expect(exitCode == zzrk::cli::kFatalScanExitCode,
         "fatal JSON scan returns the fatal exit code");
  expect(json.find("\"success\":false") != std::string::npos,
         "fatal JSON scan reports failure");
  expect(json.find("denied \\\"usb\\\"\\nnow") != std::string::npos,
         "JSON error text is escaped");
  expect(json.find("use \\\\ rule") != std::string::npos,
         "JSON help text is escaped");
}

void testLoaderSelectionRequiresExactUniquePath() {
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

void testLoaderSelectionRejectsPartialScan() {
  zzrk::transport::ScanResult result{
      .devices = {makeLoaderDevice()},
      .error =
          zzrk::transport::ScanError{
              .code = -1,
              .message = "partial scan",
              .kind = zzrk::transport::ScanErrorKind::Other,
          },
  };
  const auto selected = zzrk::cli::selectLoaderDevice(result, "1-5");
  expect(selected.error == zzrk::cli::LoaderSelectionError::scanFailed,
         "probe refuses a partial scan even when the target was listed");
}

void testLoaderSelectionRequiresExactIdentity() {
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

void testFlashSelectionAcceptsOnlyValidatedLoaderAndMaskrom() {
  auto loader = makeLoaderDevice();
  zzrk::transport::ScanResult result{.devices = {loader}, .error = {}};
  expect(static_cast<bool>(zzrk::cli::selectFlashDevice(result, "1-5")),
         "flash accepts the validated RK3588 Loader identity");

  auto maskrom = makeLoaderDevice();
  maskrom.bcdUsb = 0x0200;
  maskrom.bcdDevice = 0x0100;
  maskrom.mode = zzrk::transport::DeviceMode::Maskrom;
  result.devices = {maskrom};
  expect(static_cast<bool>(zzrk::cli::selectFlashDevice(result, "1-5")),
         "flash accepts the observed RK3588 Maskrom identity");

  maskrom.bcdDevice = 0x0101;
  result.devices = {maskrom};
  expect(zzrk::cli::selectFlashDevice(result, "1-5").error ==
             zzrk::cli::LoaderSelectionError::unsupported,
         "flash rejects an unvalidated Maskrom descriptor revision");
}

void testProbeSuccessOutputIsExplicit() {
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

  expect(output.str() ==
             "Probe succeeded: 1-5 (2207:350b, Loader, bcdDevice "
             "0x0223)\n"
             "Interface 0, alternate 0, bulk IN 0x81, bulk OUT 0x02, "
             "max packet 512/512 bytes\n"
             "Interface claim and release succeeded; no RockUSB command "
             "was sent.\n",
         "probe success output reports only the verified lifecycle");
}

void testChipInfoOutputUsesFixedWidthHexBytes() {
  const zzrk::session::ChipInfo chipInfo{
      .bytes =
          {
              std::byte{0x00},
              std::byte{0x01},
              std::byte{0x09},
              std::byte{0x0a},
              std::byte{0x10},
              std::byte{0x38},
              std::byte{0x7f},
              std::byte{0x80},
              std::byte{0xa5},
              std::byte{0xfe},
              std::byte{0xff},
              std::byte{0x20},
              std::byte{0x03},
              std::byte{0x04},
              std::byte{0x05},
              std::byte{0x06},
          },
  };
  std::ostringstream output;
  zzrk::cli::printChipInfo(makeLoaderDevice(), chipInfo, output);

  expect(output.str() ==
             "Port: 1-5 (bcdDevice 0x0223)\n"
             "Chip info: 00 01 09 0a 10 38 7f 80 a5 fe ff 20 03 04 05 "
             "06\n",
         "chip info output prints exactly 16 fixed-width hexadecimal bytes");
}

void testFlashInfoOutputUsesNeutralFieldsAndFixedWidthHex() {
  const zzrk::session::FlashInfo flashInfo{
      .rawBytes =
          {
              std::byte{0x00},
              std::byte{0x00},
              std::byte{0x67},
              std::byte{0x07},
              std::byte{0x00},
              std::byte{0x04},
              std::byte{0x04},
              std::byte{0x00},
              std::byte{0x28},
              std::byte{0x0a},
              std::byte{0x01},
          },
      .legacy =
          {
              .sectorCount = 124'190'720,
              .blockSizeCode = 1'024,
              .pageSizeCode = 4,
              .eccBits = 0,
              .accessTime = 40,
              .manufacturerCode = 0x0a,
              .flashMask = 0x01,
          },
  };
  std::ostringstream output;
  zzrk::cli::printFlashInfo(makeLoaderDevice(), flashInfo, output);

  expect(output.str() == "Port: 1-5 (bcdDevice 0x0223)\n"
                         "Sector count: 124190720\n"
                         "Capacity bytes: 63585648640\n"
                         "Legacy block-size code: 1024\n"
                         "Legacy page-size code: 4\n"
                         "Legacy ECC field: 0\n"
                         "Legacy access-time field: 40\n"
                         "Legacy manufacturer/code: 0x0a\n"
                         "Legacy flash-mask field: 0x01\n"
                         "Storage type: not determined by ReadFlashInfo\n"
                         "Raw bytes: 00 00 67 07 00 04 04 00 28 0a 01\n",
         "flash info output reports neutral decoded fields and exactly 11 "
         "fixed-width hexadecimal bytes");
  expect(output.str().find("Samsung") == std::string::npos,
         "flash info output does not infer a manufacturer name");
  expect(output.str().find("eMMC") == std::string::npos,
         "flash info output does not infer a storage medium");
}

void testFlashIdOutputRecognizesOnlyEmmcMarker() {
  const zzrk::session::FlashId flashId{
      .rawBytes =
          {
              std::byte{0x45},
              std::byte{0x4d},
              std::byte{0x4d},
              std::byte{0x43},
              std::byte{0x20},
          },
  };
  std::ostringstream output;
  zzrk::cli::printFlashId(makeLoaderDevice(), flashId, output);

  expect(output.str() == "Port: 1-5 (bcdDevice 0x0223)\n"
                         "Flash ID bytes: 45 4d 4d 43 20\n"
                         "Storage type: eMMC (reported by ReadFlashID)\n",
         "flash ID output reports an exact eMMC media marker");
  expect(output.str().find("manufacturer") == std::string::npos,
         "flash ID output does not infer a manufacturer");
  expect(output.str().find("CID") == std::string::npos,
         "flash ID output does not infer CID data");
}

void testOpaqueFlashIdOutputKeepsStorageUnknown() {
  const zzrk::session::FlashId flashId{
      .rawBytes =
          {
              std::byte{0x00},
              std::byte{0x01},
              std::byte{0x09},
              std::byte{0x0a},
              std::byte{0xff},
          },
  };
  std::ostringstream output;
  zzrk::cli::printFlashId(makeLoaderDevice(), flashId, output);

  expect(output.str() == "Port: 1-5 (bcdDevice 0x0223)\n"
                         "Flash ID bytes: 00 01 09 0a ff\n"
                         "Storage type: unknown\n",
         "opaque flash ID output prints fixed-width bytes without a media "
         "inference");
  expect(output.str().find("eMMC") == std::string::npos,
         "opaque flash ID output does not infer eMMC");
}

void testJsonFirmwareImageGoldenOutput() {
  zzrk::image::FirmwareLoaderInfo loader{};
  loader.version = 0x1234;
  loader.chipType = 0x3588;
  loader.idBlockSize = 2048;
  zzrk::image::RkFirmwareImage image{
      .sourcePath = "/tmp/update.img",
      .fileSize = 4096,
      .version = {.major = 1, .minor = 2, .patch = 3},
      .timestamp = {.year = 2026,
                    .month = 7,
                    .day = 28,
                    .hour = 12,
                    .minute = 34,
                    .second = 56},
      .chip = "3588",
      .model = "pudu\"rk",
      .manufacturer = "PUDU",
      .bootSignature = "BOOT",
      .bootOffset = 128,
      .bootSize = 256,
      .firmwareOffset = 384,
      .firmwareSize = 1024,
      .trailingMd5 = {'0', '1', '2', '3', '4', '5', '6', '7',
                      '8', '9', 'a', 'b', 'c', 'd', 'e', 'f',
                      '0', '1', '2', '3', '4', '5', '6', '7',
                      '8', '9', 'a', 'b', 'c', 'd', 'e', 'f', '\0'},
      .trailingMd5Verified = true,
      .entries = {{.name = "boot",
                   .path = "boot.img",
                   .fileOffset = 512,
                   .storedSize = 4,
                   .contentSize = 4,
                   .logicalSize = 4,
                   .format = zzrk::image::PayloadFormat::raw}},
      .partitions = {{.name = "boot_a",
                      .firstLba = 64,
                      .sectorCount = 8,
                      .grow = false}},
      .loader = loader,
  };
  std::ostringstream output;
  zzrk::cli::printFirmwareImageJson(image, output);
  expect(
      output.str() ==
          "{\"schemaVersion\":1,\"success\":true,\"command\":\"inspect-image\",\"result\":{\"format\":\"rkfw-rkaf\",\"sourcePath\":\"/tmp/update.img\",\"fileSize\":4096,\"version\":{\"major\":1,\"minor\":2,\"patch\":3},\"timestamp\":{\"year\":2026,\"month\":7,\"day\":28,\"hour\":12,\"minute\":34,\"second\":56},\"chip\":\"3588\",\"model\":\"pudu\\\"rk\",\"manufacturer\":\"PUDU\",\"bootSignature\":\"BOOT\",\"bootOffset\":128,\"bootSize\":256,\"firmwareOffset\":384,\"firmwareSize\":1024,\"md5\":\"0123456789abcdef0123456789abcdef\",\"md5Verified\":true,\"loader\":{\"version\":4660,\"chipType\":13704,\"idBlockSize\":2048},\"entries\":[{\"name\":\"boot\",\"path\":\"boot.img\",\"fileOffset\":512,\"storedSize\":4,\"contentSize\":4,\"logicalSize\":4,\"format\":\"raw\",\"selfEntry\":false}],\"partitions\":[{\"name\":\"boot_a\",\"firstLba\":64,\"sectorCount\":8,\"grow\":false}]},\"error\":null}\n",
      "firmware JSON uses the stable schemaVersion 1 golden contract");
}

void testJsonFlashPlanGoldenOutput() {
  const zzrk::image::RkFirmwareFlashPlan plan{
      .sourcePath = "/tmp/update.img",
      .targetChip = "RK3588",
      .targetModel = "pudu",
      .minimumDeviceSectors = 1024,
      .totalPartitionBytes = 512,
      .steps = {{.kind = zzrk::image::FirmwareFlashStepKind::writePartition,
                 .target = "boot_a",
                 .entryIndex = 2,
                 .firstLba = 64,
                 .sectorCount = 8,
                 .inputSize = 443,
                 .outputSize = 443,
                 .format = zzrk::image::PayloadFormat::raw}},
  };
  std::ostringstream output;
  zzrk::cli::printFirmwareFlashPlanJson(plan, output);
  expect(output.str() ==
             "{\"schemaVersion\":1,\"success\":true,\"command\":\"plan-image\",\"result\":{\"sourcePath\":\"/tmp/update.img\",\"targetChip\":\"RK3588\",\"targetModel\":\"pudu\",\"minimumDeviceSectors\":1024,\"totalPartitionBytes\":512,\"steps\":[{\"index\":0,\"kind\":\"write-partition\",\"target\":\"boot_a\",\"entryIndex\":2,\"firstLba\":64,\"sectorCount\":8,\"inputSize\":443,\"outputSize\":443,\"format\":\"raw\"}]},\"error\":null}\n",
         "flash plan JSON uses the stable schemaVersion 1 golden contract");
}

void testJsonFlashLinesGoldenOutput() {
  const zzrk::workflow::FirmwareUpgradeEvent progress{
      .stage = zzrk::workflow::FirmwareUpgradeStage::flash,
      .target = "boot_a",
      .completedBytes = 256,
      .totalBytes = 512,
      .flashPhase = zzrk::image::FirmwareFlashPhase::writePartition,
      .device = std::nullopt,
  };
  std::ostringstream progressOutput;
  zzrk::cli::printFirmwareUpgradeEventJson(progress, progressOutput);
  expect(progressOutput.str() ==
             "{\"schemaVersion\":1,\"type\":\"progress\",\"command\":\"flash-image\",\"stage\":\"flash\",\"target\":\"boot_a\",\"phase\":\"write-partition\",\"completedBytes\":256,\"totalBytes\":512}\n",
         "flash JSON Lines progress uses the versioned golden contract");

  auto selected = progress;
  selected.stage = zzrk::workflow::FirmwareUpgradeStage::selectDevice;
  selected.target = "1-5";
  selected.flashPhase.reset();
  selected.device = makeLoaderDevice();
  std::ostringstream selectedOutput;
  zzrk::cli::printFirmwareUpgradeEventJson(selected, selectedOutput);
  expect(selectedOutput.str().find("\"device\":{\"portPath\":\"1-5\"") !=
             std::string::npos,
         "device selection progress carries the selected USB identity");

  zzrk::workflow::FirmwareUpgradeError error{};
  error.code = zzrk::workflow::FirmwareUpgradeErrorCode::flashFailed;
  error.stage = zzrk::workflow::FirmwareUpgradeStage::flash;
  error.target = "boot_a";
  error.completedBytes = 256;
  std::ostringstream errorOutput;
  zzrk::cli::printFirmwareUpgradeErrorJson(error, "write failed", errorOutput);
  expect(errorOutput.str() ==
             "{\"schemaVersion\":1,\"type\":\"result\",\"command\":\"flash-image\",\"success\":false,\"result\":null,\"error\":{\"code\":\"flash-failed\",\"message\":\"write failed\",\"stage\":\"flash\",\"target\":\"boot_a\",\"completedBytes\":256}}\n",
         "flash JSON Lines error uses the versioned golden contract");
  error.code = zzrk::workflow::FirmwareUpgradeErrorCode::loaderDownloadFailed;
  error.loaderError = zzrk::image::MaskromLoaderError{
      .code = zzrk::image::MaskromLoaderErrorCode::transferFailed,
      .stage = "DDR code",
      .completedBytes = 0,
      .transportError = zzrk::session::TransportError{
          .code = zzrk::session::TransportErrorCode::timedOut,
          .nativeCode = -7,
          .bytesTransferred = 0}};
  std::ostringstream loaderOutput;
  zzrk::cli::printFirmwareUpgradeErrorJson(error, "download failed",
                                           loaderOutput);
  expect(loaderOutput.str().find("\"loaderTransportNativeCode\":-7") !=
             std::string::npos,
         "CLI JSON retains RAM Loader USB timeout diagnostics");
}

void testJsonInfoGoldenOutput() {
  zzrk::cli::DeviceInfoReport report{};
  report.device = makeLoaderDevice();
  report.interfaceInfo = {
      .interfaceNumber = 0,
      .alternateSetting = 0,
      .bulkInEndpoint = 0x81,
      .bulkOutEndpoint = 0x02,
      .bulkInMaxPacketSize = 512,
      .bulkOutMaxPacketSize = 512,
  };
  report.chipInfo.bytes[0] = std::byte{'8'};
  report.capability.rawBytes[0] = std::byte{0x01};
  report.flashId.rawBytes = {std::byte{'E'}, std::byte{'M'}, std::byte{'M'},
                             std::byte{'C'}, std::byte{' '}};
  report.flashInfo.rawBytes[0] = std::byte{0xaa};
  report.flashInfo.legacy.sectorCount = 1024;
  report.flashInfo.legacy.blockSizeCode = 64;
  report.flashInfo.legacy.pageSizeCode = 4;
  report.flashInfo.legacy.eccBits = 8;
  report.flashInfo.legacy.accessTime = 40;
  report.flashInfo.legacy.manufacturerCode = 10;
  report.flashInfo.legacy.flashMask = 1;
  std::ostringstream output;
  zzrk::cli::printDeviceInfoJson(report, output);
  expect(output.str() ==
             "{\"schemaVersion\":1,\"success\":true,\"command\":\"info\",\"result\":{\"device\":{\"portPath\":\"1-5\",\"vendorId\":8711,\"productId\":13579,\"bcdUsb\":513,\"bcdDevice\":547,\"busNumber\":1,\"deviceAddress\":4,\"mode\":\"Loader\"},\"interface\":{\"interfaceNumber\":0,\"alternateSetting\":0,\"bulkInEndpoint\":129,\"bulkOutEndpoint\":2,\"bulkInMaxPacketSize\":512,\"bulkOutMaxPacketSize\":512},\"chipInfo\":\"38000000000000000000000000000000\",\"capability\":\"0100000000000000\",\"flashId\":\"454d4d4320\",\"storageType\":\"emmc\",\"flashInfo\":{\"raw\":\"aa00000000000000000000\",\"sectorCount\":1024,\"capacityBytes\":524288,\"blockSizeCode\":64,\"pageSizeCode\":4,\"eccBits\":8,\"accessTime\":40,\"manufacturerCode\":10,\"flashMask\":1}},\"error\":null}\n",
         "unified info JSON uses the stable schemaVersion 1 golden contract");
}

void testJsonPartitionsGoldenOutput() {
  zzrk::storage::GptPartitionTable table{};
  table.firstUsableLba = 34;
  table.lastUsableLba = 999;
  table.backupStatus = zzrk::storage::BackupGptStatus::unavailable;
  zzrk::storage::GptPartition partition{};
  partition.name = "boot_a";
  partition.firstLba = 64;
  partition.lastLba = 127;
  partition.attributes = 4;
  table.partitions.push_back(partition);
  std::ostringstream output;
  zzrk::cli::printPartitionsJson(table, 1024, output);
  expect(output.str() ==
             "{\"schemaVersion\":1,\"success\":true,\"command\":\"partitions\",\"result\":{\"deviceSectors\":1024,\"primaryGpt\":\"verified\",\"backupGpt\":\"unavailable\",\"firstUsableLba\":34,\"lastUsableLba\":999,\"partitions\":[{\"name\":\"boot_a\",\"firstLba\":64,\"lastLba\":127,\"sectorCount\":64,\"byteSize\":32768,\"attributes\":4}]},\"error\":null}\n",
         "partition JSON keeps backup-unavailable separate from primary validity");
}

} // namespace

int main() {
  testEmptySuccess();
  testFatalErrorHelpIsCategorySpecific();
  testPartialScanReturnsTwo();
  testCompleteDeviceListSucceeds();
  testAdbDeviceIsVisibleButNotOperable();
  testJsonDeviceListHasStableEnvelope();
  testJsonScanErrorEscapesText();
  testLoaderSelectionRequiresExactUniquePath();
  testLoaderSelectionRejectsPartialScan();
  testLoaderSelectionRequiresExactIdentity();
  testFlashSelectionAcceptsOnlyValidatedLoaderAndMaskrom();
  testProbeSuccessOutputIsExplicit();
  testChipInfoOutputUsesFixedWidthHexBytes();
  testFlashInfoOutputUsesNeutralFieldsAndFixedWidthHex();
  testFlashIdOutputRecognizesOnlyEmmcMarker();
  testOpaqueFlashIdOutputKeepsStorageUnknown();
  testJsonFirmwareImageGoldenOutput();
  testJsonFlashPlanGoldenOutput();
  testJsonFlashLinesGoldenOutput();
  testJsonInfoGoldenOutput();
  testJsonPartitionsGoldenOutput();
  return failures == 0 ? 0 : 1;
}
