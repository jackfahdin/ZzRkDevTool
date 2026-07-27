// SPDX-License-Identifier: MIT

#include "FirmwareImagePrinter.hpp"

#include <iomanip>
#include <ostream>

namespace zzrk::cli {

std::string_view describeFirmwareImageError(
    const image::FirmwareImageErrorCode code) noexcept
{
    using image::FirmwareImageErrorCode;
    switch (code) {
    case FirmwareImageErrorCode::fileStatFailed:
        return "cannot stat the image file";
    case FirmwareImageErrorCode::fileOpenFailed:
        return "cannot open the image file";
    case FirmwareImageErrorCode::fileTooSmall:
        return "the image file is too small";
    case FirmwareImageErrorCode::seekFailed:
        return "seeking in the image failed";
    case FirmwareImageErrorCode::shortRead:
        return "the image ended before the declared data";
    case FirmwareImageErrorCode::invalidRkfwSignature:
        return "the image is not an RKFW container";
    case FirmwareImageErrorCode::invalidRkfwHeader:
        return "the RKFW header is invalid";
    case FirmwareImageErrorCode::invalidBootRegion:
        return "the RKFW boot region is out of bounds";
    case FirmwareImageErrorCode::invalidBootSignature:
        return "the embedded Loader has an unsupported signature";
    case FirmwareImageErrorCode::invalidFirmwareRegion:
        return "the RKFW firmware region is out of bounds";
    case FirmwareImageErrorCode::invalidRkafSignature:
        return "the embedded firmware is not an RKAF container";
    case FirmwareImageErrorCode::rkafSizeMismatch:
        return "the RKFW and RKAF sizes disagree";
    case FirmwareImageErrorCode::invalidEntryCount:
        return "the RKAF entry count is invalid";
    case FirmwareImageErrorCode::invalidEntryTable:
        return "the RKAF entry table is out of bounds";
    case FirmwareImageErrorCode::invalidEntryRegion:
        return "an RKAF entry is out of bounds";
    case FirmwareImageErrorCode::invalidParameter:
        return "the embedded parameter payload is invalid";
    case FirmwareImageErrorCode::invalidPartitionLayout:
        return "the embedded partition layout is invalid";
    case FirmwareImageErrorCode::invalidSparseImage:
        return "an embedded Android Sparse image is invalid";
    case FirmwareImageErrorCode::invalidTrailingMd5:
        return "the RKFW trailing MD5 field is invalid";
    }
    return "unknown firmware image error";
}

void printFirmwareImage(const image::RkFirmwareImage& firmware,
                        std::ostream& output)
{
    output << "Format: Rockchip RKFW with embedded RKAF\n"
           << "File: " << firmware.sourcePath.string() << '\n'
           << "File size: " << firmware.fileSize << " bytes\n"
           << "Version: "
           << static_cast<unsigned int>(firmware.version.major) << '.'
           << static_cast<unsigned int>(firmware.version.minor) << '.'
           << firmware.version.patch << '\n'
           << "Build time: " << firmware.timestamp.year << '-'
           << std::setfill('0') << std::setw(2)
           << static_cast<unsigned int>(firmware.timestamp.month) << '-'
           << std::setw(2)
           << static_cast<unsigned int>(firmware.timestamp.day) << ' '
           << std::setw(2)
           << static_cast<unsigned int>(firmware.timestamp.hour) << ':'
           << std::setw(2)
           << static_cast<unsigned int>(firmware.timestamp.minute) << ':'
           << std::setw(2)
           << static_cast<unsigned int>(firmware.timestamp.second)
           << std::setfill(' ') << '\n'
           << "Target chip: RK" << firmware.chip << '\n'
           << "Model: " << firmware.model << '\n'
           << "Manufacturer: " << firmware.manufacturer << '\n'
           << "Loader: " << firmware.bootSignature << " at "
           << firmware.bootOffset << ", " << firmware.bootSize << " bytes\n"
           << "RKAF: " << firmware.firmwareOffset << ", "
           << firmware.firmwareSize << " bytes\n"
           << "Trailing MD5: " << firmware.trailingMd5.data() << '\n'
           << "Entries: " << firmware.entries.size() << '\n';

    for (std::size_t index = 0; index < firmware.entries.size(); ++index) {
        const auto& entry = firmware.entries[index];
        output << "  [" << index << "] " << entry.name << " -> "
               << entry.path << "\n"
               << "       offset=" << entry.fileOffset
               << " content=" << entry.contentSize
               << " logical=" << entry.logicalSize
               << " stored=" << entry.storedSize
               << " format=" << image::toString(entry.format);
        if (entry.selfEntry) {
            output << " (metadata only)";
        }
        output << '\n';
    }
    output << "Partitions: " << firmware.partitions.size() << '\n';
    for (const auto& partition : firmware.partitions) {
        output << "  " << partition.name << ": start=0x" << std::hex
               << partition.firstLba << std::dec << " sectors=";
        if (partition.sectorCount.has_value()) {
            output << "0x" << std::hex << *partition.sectorCount << std::dec;
        } else {
            output << "grow";
        }
        output << '\n';
    }
}

std::string_view describeFirmwarePlanError(
    const image::FirmwarePlanErrorCode code) noexcept
{
    using image::FirmwarePlanErrorCode;
    switch (code) {
    case FirmwarePlanErrorCode::unsupportedTargetChip:
        return "the firmware does not target RK3588";
    case FirmwarePlanErrorCode::missingLoader:
        return "the firmware has no embedded Loader";
    case FirmwarePlanErrorCode::missingParameter:
        return "the firmware has no usable parameter partition layout";
    case FirmwarePlanErrorCode::duplicatePartition:
        return "the partition layout contains duplicate names";
    case FirmwarePlanErrorCode::targetPartitionMissing:
        return "an image entry has no target partition";
    case FirmwarePlanErrorCode::targetPartitionHasNoFixedSize:
        return "an image entry targets a grow-only partition";
    case FirmwarePlanErrorCode::targetPartitionTooSmall:
        return "an image is larger than its target partition";
    case FirmwarePlanErrorCode::partitionRangeOverflow:
        return "a partition range or total size overflows";
    }
    return "unknown firmware plan error";
}

void printFirmwareFlashPlan(const image::RkFirmwareFlashPlan& plan,
                            std::ostream& output)
{
    output << "Flash plan: " << plan.targetChip << " / " << plan.targetModel
           << '\n'
           << "Minimum eMMC sectors: " << plan.minimumDeviceSectors << '\n'
           << "Partition bytes to write: " << plan.totalPartitionBytes << '\n'
           << "Steps: " << plan.steps.size() << '\n';
    for (std::size_t index = 0; index < plan.steps.size(); ++index) {
        const auto& step = plan.steps[index];
        output << "  " << index + 1U << ". ";
        switch (step.kind) {
        case image::FirmwareFlashStepKind::upgradeLoader:
            output << "Upgrade Loader";
            break;
        case image::FirmwareFlashStepKind::ensurePartitionTable:
            output << "Ensure GPT";
            break;
        case image::FirmwareFlashStepKind::writePartition:
            output << "Write " << step.target << " at LBA 0x" << std::hex
                   << step.firstLba << std::dec << " (capacity "
                   << step.sectorCount << " sectors)";
            break;
        }
        output << ", input=" << step.inputSize
               << ", output=" << step.outputSize
               << ", format=" << image::toString(step.format) << '\n';
    }
}

} // namespace zzrk::cli
