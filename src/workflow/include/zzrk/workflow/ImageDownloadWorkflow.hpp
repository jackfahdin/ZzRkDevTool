// SPDX-License-Identifier: MIT

#pragma once

#include <zzrk/image/RkFirmwareFlasher.hpp>
#include <zzrk/storage/GptPartitionTable.hpp>
#include <zzrk/workflow/FirmwareUpgradeWorkflow.hpp>

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace zzrk::workflow {

class IFileDigestProvider {
  public:
    virtual ~IFileDigestProvider() = default;

    [[nodiscard]] virtual FirmwareDigestResult
    digest(const std::filesystem::path& path) noexcept = 0;
};

enum class ImageDownloadLayoutSource {
    deviceGpt,
    parameterFile,
};

enum class ImageDownloadItemKind {
    loader,
    partitionTable,
    partition,
};

struct ImageDownloadPartitionSelection {
    std::string target;
    std::filesystem::path imagePath;
    bool verify{true};
};

struct ImageDownloadPrepareRequest {
    ImageDownloadLayoutSource layoutSource{ImageDownloadLayoutSource::deviceGpt};
    std::uint64_t deviceSectors{};
    std::vector<image::FirmwarePartition> devicePartitions;
    std::filesystem::path parameterPath;
    std::filesystem::path loaderPath;
    bool verifyLoader{true};
    bool verifyPartitionTable{true};
    std::vector<ImageDownloadPartitionSelection> partitions;
};

struct PreparedImageDownloadItem {
    ImageDownloadItemKind kind{};
    std::string target;
    std::filesystem::path sourcePath;
    std::uint64_t firstLba{};
    std::uint64_t sectorCount{};
    std::uint64_t outputSize{};
    image::PayloadFormat format{image::PayloadFormat::unknown};
    bool verify{true};
    std::array<char, 33> md5{};
    std::optional<image::FirmwarePayloadSource> payload;
    std::optional<image::RkLoaderImage> loader;
};

struct PreparedImageDownloadPlan {
    ImageDownloadLayoutSource layoutSource{ImageDownloadLayoutSource::deviceGpt};
    std::uint64_t expectedDeviceSectors{};
    std::uint64_t minimumDeviceSectors{};
    image::RockchipParameterLayout layout;
    std::vector<PreparedImageDownloadItem> items;
};

enum class ImageDownloadStage {
    inspectFiles,
    verifyFiles,
    selectDevice,
    enterMaskrom,
    waitForMaskrom,
    downloadLoader,
    waitForRamLoader,
    checkRamLoader,
    checkStorage,
    validateLayout,
    erase,
    write,
    verify,
    reset,
};

struct ImageDownloadEvent {
    ImageDownloadStage stage{};
    std::string target;
    std::uint64_t completedBytes{};
    std::uint64_t totalBytes{};
    std::uint64_t completedPlanBytes{};
    std::uint64_t totalPlanBytes{};
    std::optional<std::size_t> itemIndex;
};

using ImageDownloadProgressCallback =
    std::function<void(const ImageDownloadEvent&)>;
using ImageDownloadCancellationCallback = std::function<bool()>;

enum class ImageDownloadErrorCode {
    cancelled,
    invalidRequest,
    imageInvalid,
    digestFailed,
    digestChanged,
    layoutInvalid,
    scanFailed,
    deviceNotFound,
    deviceAmbiguous,
    deviceUnsupported,
    loaderRequired,
    openFailed,
    loaderResetFailed,
    reenumerationTimedOut,
    loaderDownloadFailed,
    loaderNotReady,
    wrongChip,
    storageCheckFailed,
    storageNotEmmc,
    deviceTooSmall,
    deviceLayoutChanged,
    flashFailed,
    resetFailed,
    closeFailed,
};

struct ImageDownloadError {
    ImageDownloadErrorCode code{};
    ImageDownloadStage stage{};
    std::string target;
    std::uint64_t completedPlanBytes{};
    std::optional<std::size_t> itemIndex;
    std::optional<image::FirmwareImageError> imageError;
    std::optional<FirmwareDigestError> digestError;
    std::optional<transport::ScanError> scanError;
    std::optional<FirmwareUpgradeOpenError> openError;
    std::optional<image::MaskromLoaderError> loaderError;
    std::optional<session::SessionError> sessionError;
    std::optional<storage::GptError> gptError;
    std::optional<image::FirmwareFlashError> flashError;
};

struct ImageDownloadRequest {
    std::string portPath;
    bool reboot{true};
};

struct ImageDownloadSuccess {
    std::string portPath;
    std::uint64_t deviceSectors{};
    std::uint64_t partitionBytesWritten{};
    std::uint64_t sectorsErased{};
    std::uint32_t itemsWritten{};
    std::uint32_t itemsVerified{};
    bool loaderWritten{};
    bool partitionTableWritten{};
    bool resetAccepted{};
};

using PreparedImageDownloadResult =
    std::variant<PreparedImageDownloadPlan, ImageDownloadError>;
using ImageDownloadResult =
    std::variant<ImageDownloadSuccess, ImageDownloadError>;

class ImageDownloadWorkflow final {
  public:
    ImageDownloadWorkflow(IFirmwareUpgradeBackend& backend,
                          IFileDigestProvider& digestProvider) noexcept;

    [[nodiscard]] PreparedImageDownloadResult prepare(
        const ImageDownloadPrepareRequest& request,
        const ImageDownloadProgressCallback& progress = {});

    [[nodiscard]] ImageDownloadResult execute(
        const ImageDownloadRequest& request,
        PreparedImageDownloadPlan prepared,
        const ImageDownloadProgressCallback& progress = {},
        const ImageDownloadCancellationCallback& cancellation = {});

  private:
    IFirmwareUpgradeBackend& backend_;
    IFileDigestProvider& digestProvider_;
};

[[nodiscard]] constexpr std::string_view
toString(const ImageDownloadStage stage) noexcept
{
    switch (stage) {
    case ImageDownloadStage::inspectFiles:
        return "inspect-files";
    case ImageDownloadStage::verifyFiles:
        return "verify-files";
    case ImageDownloadStage::selectDevice:
        return "select-device";
    case ImageDownloadStage::enterMaskrom:
        return "enter-maskrom";
    case ImageDownloadStage::waitForMaskrom:
        return "wait-maskrom";
    case ImageDownloadStage::downloadLoader:
        return "download-loader";
    case ImageDownloadStage::waitForRamLoader:
        return "wait-ram-loader";
    case ImageDownloadStage::checkRamLoader:
        return "check-ram-loader";
    case ImageDownloadStage::checkStorage:
        return "check-storage";
    case ImageDownloadStage::validateLayout:
        return "validate-layout";
    case ImageDownloadStage::erase:
        return "erase";
    case ImageDownloadStage::write:
        return "write";
    case ImageDownloadStage::verify:
        return "verify";
    case ImageDownloadStage::reset:
        return "reset";
    }
    return "unknown";
}

} // namespace zzrk::workflow
