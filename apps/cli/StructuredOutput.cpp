// SPDX-License-Identifier: MIT

#include "StructuredOutput.hpp"

#include "JsonWriter.hpp"

#include <zzrk/core/StorageUnits.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <iomanip>
#include <ostream>
#include <span>
#include <string_view>

namespace zzrk::cli {
namespace {

void writeCommandEnvelopeStart(const std::string_view command,
                               std::ostream &output) {
  output << "{\"schemaVersion\":1,\"success\":true,\"command\":";
  writeJsonString(command, output);
  output << ",\"result\":";
}

void writeDevice(const transport::UsbDeviceInfo &device,
                 std::ostream &output) {
  output << "{\"portPath\":";
  writeJsonString(device.portPath, output);
  output << ",\"vendorId\":" << device.vendorId
         << ",\"productId\":" << device.productId
         << ",\"bcdUsb\":" << device.bcdUsb
         << ",\"bcdDevice\":" << device.bcdDevice
         << ",\"busNumber\":" << static_cast<unsigned int>(device.busNumber)
         << ",\"deviceAddress\":"
         << static_cast<unsigned int>(device.deviceAddress) << ",\"mode\":";
  writeJsonString(transport::toString(device.mode), output);
  output << '}';
}

[[nodiscard]] constexpr std::string_view
flashStepKind(const image::FirmwareFlashStepKind kind) noexcept {
  using image::FirmwareFlashStepKind;
  switch (kind) {
  case FirmwareFlashStepKind::upgradeLoader:
    return "upgrade-loader";
  case FirmwareFlashStepKind::ensurePartitionTable:
    return "ensure-partition-table";
  case FirmwareFlashStepKind::writePartition:
    return "write-partition";
  }
  return "unknown";
}

[[nodiscard]] constexpr std::string_view
payloadFormat(const image::PayloadFormat format) noexcept {
  using image::PayloadFormat;
  switch (format) {
  case PayloadFormat::unknown:
    return "unknown";
  case PayloadFormat::raw:
    return "raw";
  case PayloadFormat::androidSparse:
    return "android-sparse";
  case PayloadFormat::rockchipParameter:
    return "rockchip-parameter";
  }
  return "unknown";
}

[[nodiscard]] constexpr std::string_view
flashPhase(const image::FirmwareFlashPhase phase) noexcept {
  using image::FirmwareFlashPhase;
  switch (phase) {
  case FirmwareFlashPhase::writePartitionTable:
    return "write-partition-table";
  case FirmwareFlashPhase::verifyPartitionTable:
    return "verify-partition-table";
  case FirmwareFlashPhase::erasePartition:
    return "erase-partition";
  case FirmwareFlashPhase::writePartition:
    return "write-partition";
  case FirmwareFlashPhase::verifyPartition:
    return "verify-partition";
  case FirmwareFlashPhase::writeLoader:
    return "write-loader";
  case FirmwareFlashPhase::verifyLoader:
    return "verify-loader";
  }
  return "unknown";
}

[[nodiscard]] constexpr std::string_view
workflowErrorCode(const workflow::FirmwareUpgradeErrorCode code) noexcept {
  using workflow::FirmwareUpgradeErrorCode;
  switch (code) {
  case FirmwareUpgradeErrorCode::cancelled:
    return "cancelled";
  case FirmwareUpgradeErrorCode::imageInvalid:
    return "image-invalid";
  case FirmwareUpgradeErrorCode::digestFailed:
    return "digest-failed";
  case FirmwareUpgradeErrorCode::planInvalid:
    return "plan-invalid";
  case FirmwareUpgradeErrorCode::scanFailed:
    return "scan-failed";
  case FirmwareUpgradeErrorCode::deviceNotFound:
    return "device-not-found";
  case FirmwareUpgradeErrorCode::deviceAmbiguous:
    return "device-ambiguous";
  case FirmwareUpgradeErrorCode::deviceUnsupported:
    return "device-unsupported";
  case FirmwareUpgradeErrorCode::openFailed:
    return "open-failed";
  case FirmwareUpgradeErrorCode::loaderResetFailed:
    return "loader-reset-failed";
  case FirmwareUpgradeErrorCode::reenumerationTimedOut:
    return "reenumeration-timeout";
  case FirmwareUpgradeErrorCode::loaderDownloadFailed:
    return "loader-download-failed";
  case FirmwareUpgradeErrorCode::loaderNotReady:
    return "loader-not-ready";
  case FirmwareUpgradeErrorCode::wrongChip:
    return "wrong-chip";
  case FirmwareUpgradeErrorCode::storageCheckFailed:
    return "storage-check-failed";
  case FirmwareUpgradeErrorCode::storageNotEmmc:
    return "storage-not-emmc";
  case FirmwareUpgradeErrorCode::deviceTooSmall:
    return "device-too-small";
  case FirmwareUpgradeErrorCode::flashFailed:
    return "flash-failed";
  case FirmwareUpgradeErrorCode::resetFailed:
    return "reset-failed";
  case FirmwareUpgradeErrorCode::closeFailed:
    return "close-failed";
  }
  return "unknown";
}

void writeInterface(const transport::FlashInterfaceInfo &interfaceInfo,
                    std::ostream &output) {
  output << "{\"interfaceNumber\":"
         << static_cast<unsigned int>(interfaceInfo.interfaceNumber)
         << ",\"alternateSetting\":"
         << static_cast<unsigned int>(interfaceInfo.alternateSetting)
         << ",\"bulkInEndpoint\":"
         << static_cast<unsigned int>(interfaceInfo.bulkInEndpoint)
         << ",\"bulkOutEndpoint\":"
         << static_cast<unsigned int>(interfaceInfo.bulkOutEndpoint)
         << ",\"bulkInMaxPacketSize\":"
         << interfaceInfo.bulkInMaxPacketSize
         << ",\"bulkOutMaxPacketSize\":"
         << interfaceInfo.bulkOutMaxPacketSize << '}';
}

void writeHexBytes(const std::span<const std::byte> bytes,
                   std::ostream &output) {
  output << std::hex << std::setfill('0');
  for (std::size_t index = 0; index < bytes.size(); ++index) {
    if (index != 0U) {
      output << ' ';
    }
    output << std::setw(2) << std::to_integer<unsigned int>(bytes[index]);
  }
  output << std::dec << std::setfill(' ');
}

} // namespace

void printDeviceInfo(const DeviceInfoReport &report, std::ostream &output) {
  output << "Port: " << report.device.portPath << " ("
         << transport::toString(report.device.mode) << ", " << std::hex
         << std::setfill('0') << std::setw(4) << report.device.vendorId << ':'
         << std::setw(4) << report.device.productId << std::dec
         << std::setfill(' ') << ")\nChip info: ";
  writeHexBytes(report.chipInfo.bytes, output);
  output << "\nCapability: ";
  writeHexBytes(report.capability.rawBytes, output);
  output << "\nFlash ID: ";
  writeHexBytes(report.flashId.rawBytes, output);
  output << " ("
         << (session::classifyFlashId(report.flashId) ==
                     session::FlashIdKind::emmcMediaMarker
                 ? "eMMC"
                 : "unknown")
         << ")\nSector count: " << report.flashInfo.legacy.sectorCount;
  const auto bytes = core::toBytes(
      core::SectorCount{report.flashInfo.legacy.sectorCount});
  output << "\nCapacity bytes: "
         << (bytes.has_value() ? std::to_string(bytes->value) : "overflow")
         << '\n';
}

void printDeviceInfoJson(const DeviceInfoReport &report, std::ostream &output) {
  writeCommandEnvelopeStart("info", output);
  output << "{\"device\":";
  writeDevice(report.device, output);
  output << ",\"interface\":";
  writeInterface(report.interfaceInfo, output);
  output << ",\"chipInfo\":";
  writeJsonBytes(report.chipInfo.bytes, output);
  output << ",\"capability\":";
  writeJsonBytes(report.capability.rawBytes, output);
  output << ",\"flashId\":";
  writeJsonBytes(report.flashId.rawBytes, output);
  output << ",\"storageType\":";
  writeJsonString(session::classifyFlashId(report.flashId) ==
                          session::FlashIdKind::emmcMediaMarker
                      ? "emmc"
                      : "unknown",
                  output);
  output << ",\"flashInfo\":{\"raw\":";
  writeJsonBytes(report.flashInfo.rawBytes, output);
  output << ",\"sectorCount\":" << report.flashInfo.legacy.sectorCount
         << ",\"capacityBytes\":"
         << static_cast<std::uint64_t>(report.flashInfo.legacy.sectorCount) *
                core::kSectorSize
         << ",\"blockSizeCode\":"
         << report.flashInfo.legacy.blockSizeCode << ",\"pageSizeCode\":"
         << static_cast<unsigned int>(report.flashInfo.legacy.pageSizeCode)
         << ",\"eccBits\":"
         << static_cast<unsigned int>(report.flashInfo.legacy.eccBits)
         << ",\"accessTime\":"
         << static_cast<unsigned int>(report.flashInfo.legacy.accessTime)
         << ",\"manufacturerCode\":"
         << static_cast<unsigned int>(report.flashInfo.legacy.manufacturerCode)
         << ",\"flashMask\":"
         << static_cast<unsigned int>(report.flashInfo.legacy.flashMask)
         << "}},\"error\":null}\n";
}

void printFirmwareImageJson(const image::RkFirmwareImage &firmware,
                            std::ostream &output) {
  writeCommandEnvelopeStart("inspect-image", output);
  output << "{\"format\":\"rkfw-rkaf\",\"sourcePath\":";
  writeJsonString(firmware.sourcePath.string(), output);
  output << ",\"fileSize\":" << firmware.fileSize << ",\"version\":{\"major\":"
         << static_cast<unsigned int>(firmware.version.major)
         << ",\"minor\":" << static_cast<unsigned int>(firmware.version.minor)
         << ",\"patch\":" << firmware.version.patch
         << "},\"timestamp\":{\"year\":" << firmware.timestamp.year
         << ",\"month\":" << static_cast<unsigned int>(firmware.timestamp.month)
         << ",\"day\":" << static_cast<unsigned int>(firmware.timestamp.day)
         << ",\"hour\":" << static_cast<unsigned int>(firmware.timestamp.hour)
         << ",\"minute\":"
         << static_cast<unsigned int>(firmware.timestamp.minute)
         << ",\"second\":"
         << static_cast<unsigned int>(firmware.timestamp.second)
         << "},\"chip\":";
  writeJsonString(firmware.chip, output);
  output << ",\"model\":";
  writeJsonString(firmware.model, output);
  output << ",\"manufacturer\":";
  writeJsonString(firmware.manufacturer, output);
  output << ",\"bootSignature\":";
  writeJsonString(firmware.bootSignature, output);
  output << ",\"bootOffset\":" << firmware.bootOffset
         << ",\"bootSize\":" << firmware.bootSize
         << ",\"firmwareOffset\":" << firmware.firmwareOffset
         << ",\"firmwareSize\":" << firmware.firmwareSize
         << ",\"md5\":";
  writeJsonString(firmware.trailingMd5.data(), output);
  output << ",\"md5Verified\":"
         << (firmware.trailingMd5Verified ? "true" : "false")
         << ",\"loader\":";
  if (firmware.loader.has_value()) {
    output << "{\"version\":" << firmware.loader->version
           << ",\"chipType\":" << firmware.loader->chipType
           << ",\"idBlockSize\":" << firmware.loader->idBlockSize << '}';
  } else {
    output << "null";
  }
  output << ",\"entries\":[";
  for (std::size_t index = 0; index < firmware.entries.size(); ++index) {
    if (index != 0U) {
      output << ',';
    }
    const auto &entry = firmware.entries[index];
    output << "{\"name\":";
    writeJsonString(entry.name, output);
    output << ",\"path\":";
    writeJsonString(entry.path, output);
    output << ",\"fileOffset\":" << entry.fileOffset
           << ",\"storedSize\":" << entry.storedSize
           << ",\"contentSize\":" << entry.contentSize
           << ",\"logicalSize\":" << entry.logicalSize << ",\"format\":";
    writeJsonString(payloadFormat(entry.format), output);
    output << ",\"selfEntry\":" << (entry.selfEntry ? "true" : "false")
           << '}';
  }
  output << "],\"partitions\":[";
  for (std::size_t index = 0; index < firmware.partitions.size(); ++index) {
    if (index != 0U) {
      output << ',';
    }
    const auto &partition = firmware.partitions[index];
    output << "{\"name\":";
    writeJsonString(partition.name, output);
    output << ",\"firstLba\":" << partition.firstLba
           << ",\"sectorCount\":";
    if (partition.sectorCount.has_value()) {
      output << *partition.sectorCount;
    } else {
      output << "null";
    }
    output << ",\"grow\":" << (partition.grow ? "true" : "false") << '}';
  }
  output << "]},\"error\":null}\n";
}

void printFirmwareFlashPlanJson(const image::RkFirmwareFlashPlan &plan,
                                std::ostream &output) {
  writeCommandEnvelopeStart("plan-image", output);
  output << "{\"sourcePath\":";
  writeJsonString(plan.sourcePath.string(), output);
  output << ",\"targetChip\":";
  writeJsonString(plan.targetChip, output);
  output << ",\"targetModel\":";
  writeJsonString(plan.targetModel, output);
  output << ",\"minimumDeviceSectors\":" << plan.minimumDeviceSectors
         << ",\"totalPartitionBytes\":" << plan.totalPartitionBytes
         << ",\"steps\":[";
  for (std::size_t index = 0; index < plan.steps.size(); ++index) {
    if (index != 0U) {
      output << ',';
    }
    const auto &step = plan.steps[index];
    output << "{\"index\":" << index << ",\"kind\":";
    writeJsonString(flashStepKind(step.kind), output);
    output << ",\"target\":";
    writeJsonString(step.target, output);
    output << ",\"entryIndex\":";
    if (step.entryIndex.has_value()) {
      output << *step.entryIndex;
    } else {
      output << "null";
    }
    output << ",\"firstLba\":" << step.firstLba
           << ",\"sectorCount\":" << step.sectorCount
           << ",\"inputSize\":" << step.inputSize
           << ",\"outputSize\":" << step.outputSize << ",\"format\":";
    writeJsonString(payloadFormat(step.format), output);
    output << '}';
  }
  output << "]},\"error\":null}\n";
}

void printJsonError(const std::string_view command, const std::string_view code,
                    const std::string_view message, std::ostream &output) {
  output << "{\"schemaVersion\":1,\"success\":false,\"command\":";
  writeJsonString(command, output);
  output << ",\"result\":null,\"error\":{\"code\":";
  writeJsonString(code, output);
  output << ",\"message\":";
  writeJsonString(message, output);
  output << "}}\n";
}

void printFirmwareUpgradeEventJson(
    const workflow::FirmwareUpgradeEvent &event, std::ostream &output) {
  output << "{\"schemaVersion\":1,\"type\":\"progress\",\"command\":\"flash-image\",\"stage\":";
  writeJsonString(workflow::toString(event.stage), output);
  output << ",\"target\":";
  writeJsonString(event.target, output);
  output << ",\"phase\":";
  if (event.flashPhase.has_value()) {
    writeJsonString(flashPhase(*event.flashPhase), output);
  } else {
    output << "null";
  }
  output << ",\"completedBytes\":" << event.completedBytes
         << ",\"totalBytes\":" << event.totalBytes << "}\n";
}

void printFirmwareUpgradeSuccessJson(
    const workflow::FirmwareUpgradeSuccess &success, std::ostream &output) {
  output << "{\"schemaVersion\":1,\"type\":\"result\",\"command\":\"flash-image\",\"success\":true,\"result\":{\"portPath\":";
  writeJsonString(success.portPath, output);
  output << ",\"model\":";
  writeJsonString(success.model, output);
  output << ",\"firmwareMd5\":";
  writeJsonString(success.firmwareMd5.data(), output);
  output << ",\"deviceSectors\":" << success.deviceSectors
         << ",\"preparedOnly\":" << (success.preparedOnly ? "true" : "false")
         << ",\"partitionBytesWritten\":" << success.partitionBytesWritten
         << ",\"sectorsErased\":" << success.sectorsErased
         << ",\"partitionsVerified\":" << success.partitionsVerified
         << ",\"partitionTableWritten\":"
         << (success.partitionTableWritten ? "true" : "false")
         << ",\"partitionTableVerified\":"
         << (success.partitionTableVerified ? "true" : "false")
         << ",\"loaderWritten\":" << (success.loaderWritten ? "true" : "false")
         << ",\"loaderVerified\":"
         << (success.loaderVerified ? "true" : "false")
         << ",\"resetAccepted\":" << (success.resetAccepted ? "true" : "false")
         << "},\"error\":null}\n";
}

void printFirmwareUpgradeErrorJson(
    const workflow::FirmwareUpgradeError &error,
    const std::string_view message, std::ostream &output) {
  output << "{\"schemaVersion\":1,\"type\":\"result\",\"command\":\"flash-image\",\"success\":false,\"result\":null,\"error\":{\"code\":";
  writeJsonString(workflowErrorCode(error.code), output);
  output << ",\"message\":";
  writeJsonString(message, output);
  output << ",\"stage\":";
  writeJsonString(workflow::toString(error.stage), output);
  output << ",\"target\":";
  writeJsonString(error.target, output);
  output << ",\"completedBytes\":" << error.completedBytes << "}}\n";
}

void printPartitions(const storage::GptPartitionTable &table,
                     const std::uint64_t deviceSectors,
                     std::ostream &output) {
  output << "Primary GPT: verified\nBackup GPT: "
         << storage::toString(table.backupStatus)
         << "\nDevice sectors: " << deviceSectors << "\nPartitions: "
         << table.partitions.size() << '\n';
  for (const auto &partition : table.partitions) {
    output << "  " << partition.name << ": start=" << partition.firstLba
           << " end=" << partition.lastLba
           << " sectors=" << partition.sectorCount()
           << " bytes=" << partition.sectorCount() * core::kSectorSize
           << '\n';
  }
}

void printPartitionsJson(const storage::GptPartitionTable &table,
                         const std::uint64_t deviceSectors,
                         std::ostream &output) {
  writeCommandEnvelopeStart("partitions", output);
  output << "{\"deviceSectors\":" << deviceSectors
         << ",\"primaryGpt\":\"verified\",\"backupGpt\":";
  writeJsonString(storage::toString(table.backupStatus), output);
  output << ",\"firstUsableLba\":" << table.firstUsableLba
         << ",\"lastUsableLba\":" << table.lastUsableLba
         << ",\"partitions\":[";
  for (std::size_t index = 0; index < table.partitions.size(); ++index) {
    if (index != 0U) {
      output << ',';
    }
    const auto &partition = table.partitions[index];
    output << "{\"name\":";
    writeJsonString(partition.name, output);
    output << ",\"firstLba\":" << partition.firstLba
           << ",\"lastLba\":" << partition.lastLba
           << ",\"sectorCount\":" << partition.sectorCount()
           << ",\"byteSize\":"
           << partition.sectorCount() * core::kSectorSize
           << ",\"attributes\":" << partition.attributes << '}';
  }
  output << "]},\"error\":null}\n";
}

void printPartitionOperationJson(const std::string_view command,
                                 const std::string_view partition,
                                 const std::uint64_t bytes,
                                 const std::uint64_t sectors,
                                 const bool verified,
                                 std::ostream &output) {
  writeCommandEnvelopeStart(command, output);
  output << "{\"partition\":";
  writeJsonString(partition, output);
  output << ",\"bytes\":" << bytes << ",\"sectors\":" << sectors
         << ",\"verified\":" << (verified ? "true" : "false")
         << "},\"error\":null}\n";
}

} // namespace zzrk::cli
