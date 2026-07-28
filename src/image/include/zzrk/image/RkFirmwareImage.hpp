// SPDX-License-Identifier: MIT

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace zzrk::image {

enum class PayloadFormat {
    unknown,
    raw,
    androidSparse,
    rockchipParameter,
};

[[nodiscard]] constexpr std::string_view
toString(const PayloadFormat format) noexcept
{
    switch (format) {
    case PayloadFormat::androidSparse:
        return "Android Sparse";
    case PayloadFormat::rockchipParameter:
        return "Rockchip parameter";
    case PayloadFormat::raw:
        return "Raw";
    case PayloadFormat::unknown:
        return "Unknown";
    }
    return "Unknown";
}

struct FirmwareVersion {
    std::uint8_t major{};
    std::uint8_t minor{};
    std::uint16_t patch{};
};

struct FirmwareTimestamp {
    std::uint16_t year{};
    std::uint8_t month{};
    std::uint8_t day{};
    std::uint8_t hour{};
    std::uint8_t minute{};
    std::uint8_t second{};
};

struct FirmwareEntry {
    std::string name;
    std::string path;
    std::uint64_t containerOffset{};
    std::uint64_t fileOffset{};
    std::uint32_t storedSize{};
    std::uint32_t contentSize{};
    std::uint64_t logicalSize{};
    PayloadFormat format{PayloadFormat::unknown};
    bool selfEntry{};
};

struct FirmwarePartition {
    std::string name;
    std::uint64_t firstLba{};
    std::optional<std::uint64_t> sectorCount;
    bool grow{};
};

struct FirmwareLoaderComponent {
    std::uint64_t relativeOffset{};
    std::uint32_t size{};
};

struct FirmwareLoaderInfo {
    std::uint32_t version{};
    std::uint32_t chipType{};
    bool rc4Disable{};
    FirmwareLoaderComponent flashHead;
    FirmwareLoaderComponent flashData;
    FirmwareLoaderComponent flashBoot;
    std::uint64_t idBlockSize{};
};

struct RkFirmwareImage {
    std::filesystem::path sourcePath;
    std::uint64_t fileSize{};
    std::uint16_t headerSize{};
    FirmwareVersion version;
    FirmwareTimestamp timestamp;
    std::string chip;
    std::string model;
    std::string manufacturer;
    std::string bootSignature;
    std::uint64_t bootOffset{};
    std::uint64_t bootSize{};
    std::uint64_t firmwareOffset{};
    std::uint64_t firmwareSize{};
    std::array<char, 33> trailingMd5{};
    bool trailingMd5Verified{};
    std::vector<FirmwareEntry> entries;
    std::vector<FirmwarePartition> partitions;
    std::optional<FirmwareLoaderInfo> loader;
};

enum class FirmwareImageErrorCode {
    fileStatFailed,
    fileOpenFailed,
    fileTooSmall,
    seekFailed,
    shortRead,
    invalidRkfwSignature,
    invalidRkfwHeader,
    invalidBootRegion,
    invalidBootSignature,
    invalidLoader,
    invalidFirmwareRegion,
    invalidRkafSignature,
    rkafSizeMismatch,
    invalidEntryCount,
    invalidEntryTable,
    invalidEntryRegion,
    invalidParameter,
    invalidPartitionLayout,
    invalidSparseImage,
    invalidTrailingMd5,
};

struct FirmwareImageError {
    FirmwareImageErrorCode code{};
    std::uint64_t offset{};
    std::size_t entryIndex{};
};

using RkFirmwareImageResult =
    std::variant<RkFirmwareImage, FirmwareImageError>;

[[nodiscard]] RkFirmwareImageResult
parseRkFirmwareImage(const std::filesystem::path& path) noexcept;

enum class FirmwareFlashStepKind {
    upgradeLoader,
    ensurePartitionTable,
    writePartition,
};

struct FirmwareFlashStep {
    FirmwareFlashStepKind kind{};
    std::string target;
    std::optional<std::size_t> entryIndex;
    std::uint64_t firstLba{};
    std::uint64_t sectorCount{};
    std::uint64_t inputSize{};
    std::uint64_t outputSize{};
    PayloadFormat format{PayloadFormat::unknown};
};

struct RkFirmwareFlashPlan {
    std::filesystem::path sourcePath;
    std::string targetChip;
    std::string targetModel;
    std::uint64_t minimumDeviceSectors{};
    std::uint64_t totalPartitionBytes{};
    std::vector<FirmwareFlashStep> steps;
};

enum class FirmwarePlanErrorCode {
    unsupportedTargetChip,
    missingLoader,
    missingParameter,
    firmwareMd5NotVerified,
    duplicatePartition,
    targetPartitionMissing,
    targetPartitionTooSmall,
    partitionRangeOverflow,
};

struct FirmwarePlanError {
    FirmwarePlanErrorCode code{};
    std::string target;
    std::size_t entryIndex{};
};

using RkFirmwareFlashPlanResult =
    std::variant<RkFirmwareFlashPlan, FirmwarePlanError>;

[[nodiscard]] RkFirmwareFlashPlanResult
buildRkFirmwareFlashPlan(const RkFirmwareImage& image) noexcept;

} // namespace zzrk::image
