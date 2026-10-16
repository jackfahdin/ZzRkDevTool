// SPDX-License-Identifier: MIT

#include <zzrk/ffi/zzrk_ffi.h>

#include <zzrk/image/RkFirmwareFlasher.hpp>
#include <zzrk/image/RkFirmwareImage.hpp>
#include <zzrk/image/RkMaskromLoader.hpp>
#include <zzrk/workflow/DeviceManagementWorkflow.hpp>
#include <zzrk/workflow/FirmwareMd5Verifier.hpp>
#include <zzrk/workflow/FirmwareUpgradeWorkflow.hpp>
#include <zzrk/workflow/ImageDownloadWorkflow.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {

namespace image = zzrk::image;
namespace session = zzrk::session;
namespace storage = zzrk::storage;
namespace transport = zzrk::transport;
namespace workflow = zzrk::workflow;

// ---------------------------------------------------------------------------
// Compile-time enum contract locks
// ---------------------------------------------------------------------------

static_assert(static_cast<int>(transport::DeviceMode::Unknown) ==
              ZZRK_DEVICE_MODE_UNKNOWN);
static_assert(static_cast<int>(transport::DeviceMode::Adb) ==
              ZZRK_DEVICE_MODE_ADB);
static_assert(static_cast<int>(transport::DeviceMode::Maskrom) ==
              ZZRK_DEVICE_MODE_MASKROM);
static_assert(static_cast<int>(transport::DeviceMode::Loader) ==
              ZZRK_DEVICE_MODE_LOADER);

static_assert(static_cast<int>(image::FirmwareVerifyMode::none) ==
              ZZRK_VERIFY_MODE_NONE);
static_assert(static_cast<int>(image::FirmwareVerifyMode::critical) ==
              ZZRK_VERIFY_MODE_CRITICAL);
static_assert(static_cast<int>(image::FirmwareVerifyMode::all) ==
              ZZRK_VERIFY_MODE_ALL);

static_assert(static_cast<int>(workflow::FirmwareUpgradeStage::parseImage) ==
              ZZRK_UPGRADE_STAGE_PARSE_IMAGE);
static_assert(static_cast<int>(workflow::FirmwareUpgradeStage::verifyImage) ==
              ZZRK_UPGRADE_STAGE_VERIFY_IMAGE);
static_assert(static_cast<int>(workflow::FirmwareUpgradeStage::planImage) ==
              ZZRK_UPGRADE_STAGE_PLAN_IMAGE);
static_assert(static_cast<int>(workflow::FirmwareUpgradeStage::selectDevice) ==
              ZZRK_UPGRADE_STAGE_SELECT_DEVICE);
static_assert(static_cast<int>(workflow::FirmwareUpgradeStage::enterMaskrom) ==
              ZZRK_UPGRADE_STAGE_ENTER_MASKROM);
static_assert(
    static_cast<int>(workflow::FirmwareUpgradeStage::waitForMaskrom) ==
    ZZRK_UPGRADE_STAGE_WAIT_MASKROM);
static_assert(
    static_cast<int>(workflow::FirmwareUpgradeStage::downloadLoader) ==
    ZZRK_UPGRADE_STAGE_DOWNLOAD_LOADER);
static_assert(
    static_cast<int>(workflow::FirmwareUpgradeStage::waitForRamLoader) ==
    ZZRK_UPGRADE_STAGE_WAIT_RAM_LOADER);
static_assert(
    static_cast<int>(workflow::FirmwareUpgradeStage::checkRamLoader) ==
    ZZRK_UPGRADE_STAGE_CHECK_RAM_LOADER);
static_assert(static_cast<int>(workflow::FirmwareUpgradeStage::checkStorage) ==
              ZZRK_UPGRADE_STAGE_CHECK_STORAGE);
static_assert(static_cast<int>(workflow::FirmwareUpgradeStage::flash) ==
              ZZRK_UPGRADE_STAGE_FLASH);
static_assert(static_cast<int>(workflow::FirmwareUpgradeStage::reset) ==
              ZZRK_UPGRADE_STAGE_RESET);

static_assert(static_cast<int>(image::FirmwareFlashPhase::writePartitionTable) ==
              ZZRK_FLASH_PHASE_WRITE_PARTITION_TABLE);
static_assert(
    static_cast<int>(image::FirmwareFlashPhase::verifyPartitionTable) ==
    ZZRK_FLASH_PHASE_VERIFY_PARTITION_TABLE);
static_assert(static_cast<int>(image::FirmwareFlashPhase::erasePartition) ==
              ZZRK_FLASH_PHASE_ERASE_PARTITION);
static_assert(static_cast<int>(image::FirmwareFlashPhase::writePartition) ==
              ZZRK_FLASH_PHASE_WRITE_PARTITION);
static_assert(static_cast<int>(image::FirmwareFlashPhase::verifyPartition) ==
              ZZRK_FLASH_PHASE_VERIFY_PARTITION);
static_assert(static_cast<int>(image::FirmwareFlashPhase::writeLoader) ==
              ZZRK_FLASH_PHASE_WRITE_LOADER);
static_assert(static_cast<int>(image::FirmwareFlashPhase::verifyLoader) ==
              ZZRK_FLASH_PHASE_VERIFY_LOADER);

static_assert(static_cast<int>(workflow::FirmwareUpgradeErrorCode::cancelled) ==
              ZZRK_UPGRADE_ERROR_CANCELLED);
static_assert(
    static_cast<int>(workflow::FirmwareUpgradeErrorCode::imageInvalid) ==
    ZZRK_UPGRADE_ERROR_IMAGE_INVALID);
static_assert(
    static_cast<int>(workflow::FirmwareUpgradeErrorCode::digestFailed) ==
    ZZRK_UPGRADE_ERROR_DIGEST_FAILED);
static_assert(
    static_cast<int>(workflow::FirmwareUpgradeErrorCode::planInvalid) ==
    ZZRK_UPGRADE_ERROR_PLAN_INVALID);
static_assert(
    static_cast<int>(workflow::FirmwareUpgradeErrorCode::scanFailed) ==
    ZZRK_UPGRADE_ERROR_SCAN_FAILED);
static_assert(
    static_cast<int>(workflow::FirmwareUpgradeErrorCode::deviceNotFound) ==
    ZZRK_UPGRADE_ERROR_DEVICE_NOT_FOUND);
static_assert(
    static_cast<int>(workflow::FirmwareUpgradeErrorCode::deviceAmbiguous) ==
    ZZRK_UPGRADE_ERROR_DEVICE_AMBIGUOUS);
static_assert(
    static_cast<int>(workflow::FirmwareUpgradeErrorCode::deviceUnsupported) ==
    ZZRK_UPGRADE_ERROR_DEVICE_UNSUPPORTED);
static_assert(
    static_cast<int>(workflow::FirmwareUpgradeErrorCode::openFailed) ==
    ZZRK_UPGRADE_ERROR_OPEN_FAILED);
static_assert(
    static_cast<int>(workflow::FirmwareUpgradeErrorCode::loaderResetFailed) ==
    ZZRK_UPGRADE_ERROR_LOADER_RESET_FAILED);
static_assert(static_cast<int>(
                  workflow::FirmwareUpgradeErrorCode::reenumerationTimedOut) ==
              ZZRK_UPGRADE_ERROR_REENUMERATION_TIMED_OUT);
static_assert(static_cast<int>(
                  workflow::FirmwareUpgradeErrorCode::loaderDownloadFailed) ==
              ZZRK_UPGRADE_ERROR_LOADER_DOWNLOAD_FAILED);
static_assert(
    static_cast<int>(workflow::FirmwareUpgradeErrorCode::loaderNotReady) ==
    ZZRK_UPGRADE_ERROR_LOADER_NOT_READY);
static_assert(
    static_cast<int>(workflow::FirmwareUpgradeErrorCode::wrongChip) ==
    ZZRK_UPGRADE_ERROR_WRONG_CHIP);
static_assert(
    static_cast<int>(workflow::FirmwareUpgradeErrorCode::storageCheckFailed) ==
    ZZRK_UPGRADE_ERROR_STORAGE_CHECK_FAILED);
static_assert(
    static_cast<int>(workflow::FirmwareUpgradeErrorCode::storageNotEmmc) ==
    ZZRK_UPGRADE_ERROR_STORAGE_NOT_EMMC);
static_assert(
    static_cast<int>(workflow::FirmwareUpgradeErrorCode::deviceTooSmall) ==
    ZZRK_UPGRADE_ERROR_DEVICE_TOO_SMALL);
static_assert(
    static_cast<int>(workflow::FirmwareUpgradeErrorCode::flashFailed) ==
    ZZRK_UPGRADE_ERROR_FLASH_FAILED);
static_assert(
    static_cast<int>(workflow::FirmwareUpgradeErrorCode::resetFailed) ==
    ZZRK_UPGRADE_ERROR_RESET_FAILED);
static_assert(
    static_cast<int>(workflow::FirmwareUpgradeErrorCode::closeFailed) ==
    ZZRK_UPGRADE_ERROR_CLOSE_FAILED);

static_assert(
    static_cast<int>(workflow::DeviceManagementErrorCode::cancelled) ==
    ZZRK_DEVICE_ERROR_CANCELLED);
static_assert(
    static_cast<int>(workflow::DeviceManagementErrorCode::scanFailed) ==
    ZZRK_DEVICE_ERROR_SCAN_FAILED);
static_assert(
    static_cast<int>(workflow::DeviceManagementErrorCode::deviceNotFound) ==
    ZZRK_DEVICE_ERROR_DEVICE_NOT_FOUND);
static_assert(
    static_cast<int>(workflow::DeviceManagementErrorCode::deviceAmbiguous) ==
    ZZRK_DEVICE_ERROR_DEVICE_AMBIGUOUS);
static_assert(
    static_cast<int>(workflow::DeviceManagementErrorCode::deviceUnsupported) ==
    ZZRK_DEVICE_ERROR_DEVICE_UNSUPPORTED);
static_assert(
    static_cast<int>(workflow::DeviceManagementErrorCode::openFailed) ==
    ZZRK_DEVICE_ERROR_OPEN_FAILED);
static_assert(
    static_cast<int>(workflow::DeviceManagementErrorCode::chipInfoFailed) ==
    ZZRK_DEVICE_ERROR_CHIP_INFO_FAILED);
static_assert(
    static_cast<int>(workflow::DeviceManagementErrorCode::capabilityFailed) ==
    ZZRK_DEVICE_ERROR_CAPABILITY_FAILED);
static_assert(
    static_cast<int>(workflow::DeviceManagementErrorCode::flashInfoFailed) ==
    ZZRK_DEVICE_ERROR_FLASH_INFO_FAILED);
static_assert(
    static_cast<int>(workflow::DeviceManagementErrorCode::flashIdFailed) ==
    ZZRK_DEVICE_ERROR_FLASH_ID_FAILED);
static_assert(
    static_cast<int>(workflow::DeviceManagementErrorCode::storageNotEmmc) ==
    ZZRK_DEVICE_ERROR_STORAGE_NOT_EMMC);
static_assert(
    static_cast<int>(workflow::DeviceManagementErrorCode::gptFailed) ==
    ZZRK_DEVICE_ERROR_GPT_FAILED);
static_assert(
    static_cast<int>(workflow::DeviceManagementErrorCode::partitionNotFound) ==
    ZZRK_DEVICE_ERROR_PARTITION_NOT_FOUND);
static_assert(static_cast<int>(
                  workflow::DeviceManagementErrorCode::partitionAmbiguous) ==
              ZZRK_DEVICE_ERROR_PARTITION_AMBIGUOUS);
static_assert(
    static_cast<int>(workflow::DeviceManagementErrorCode::inputInvalid) ==
    ZZRK_DEVICE_ERROR_INPUT_INVALID);
static_assert(
    static_cast<int>(workflow::DeviceManagementErrorCode::inputTooLarge) ==
    ZZRK_DEVICE_ERROR_INPUT_TOO_LARGE);
static_assert(
    static_cast<int>(workflow::DeviceManagementErrorCode::inputOpenFailed) ==
    ZZRK_DEVICE_ERROR_INPUT_OPEN_FAILED);
static_assert(
    static_cast<int>(workflow::DeviceManagementErrorCode::inputReadFailed) ==
    ZZRK_DEVICE_ERROR_INPUT_READ_FAILED);
static_assert(
    static_cast<int>(workflow::DeviceManagementErrorCode::outputExists) ==
    ZZRK_DEVICE_ERROR_OUTPUT_EXISTS);
static_assert(
    static_cast<int>(workflow::DeviceManagementErrorCode::outputOpenFailed) ==
    ZZRK_DEVICE_ERROR_OUTPUT_OPEN_FAILED);
static_assert(
    static_cast<int>(workflow::DeviceManagementErrorCode::outputWriteFailed) ==
    ZZRK_DEVICE_ERROR_OUTPUT_WRITE_FAILED);
static_assert(static_cast<int>(
                  workflow::DeviceManagementErrorCode::outputFinalizeFailed) ==
              ZZRK_DEVICE_ERROR_OUTPUT_FINALIZE_FAILED);
static_assert(
    static_cast<int>(workflow::DeviceManagementErrorCode::readFailed) ==
    ZZRK_DEVICE_ERROR_READ_FAILED);
static_assert(
    static_cast<int>(workflow::DeviceManagementErrorCode::writeFailed) ==
    ZZRK_DEVICE_ERROR_WRITE_FAILED);
static_assert(
    static_cast<int>(workflow::DeviceManagementErrorCode::verifyFailed) ==
    ZZRK_DEVICE_ERROR_VERIFY_FAILED);
static_assert(
    static_cast<int>(workflow::DeviceManagementErrorCode::eraseFailed) ==
    ZZRK_DEVICE_ERROR_ERASE_FAILED);
static_assert(
    static_cast<int>(workflow::DeviceManagementErrorCode::closeFailed) ==
    ZZRK_DEVICE_ERROR_CLOSE_FAILED);

static_assert(static_cast<int>(workflow::PartitionOperationPhase::read) ==
              ZZRK_PARTITION_PHASE_READ);
static_assert(static_cast<int>(workflow::PartitionOperationPhase::write) ==
              ZZRK_PARTITION_PHASE_WRITE);
static_assert(static_cast<int>(workflow::PartitionOperationPhase::verify) ==
              ZZRK_PARTITION_PHASE_VERIFY);
static_assert(static_cast<int>(workflow::PartitionOperationPhase::erase) ==
              ZZRK_PARTITION_PHASE_ERASE);

static_assert(static_cast<int>(workflow::ImageDownloadLayoutSource::deviceGpt) ==
              ZZRK_DOWNLOAD_LAYOUT_DEVICE_GPT);
static_assert(
    static_cast<int>(workflow::ImageDownloadLayoutSource::parameterFile) ==
    ZZRK_DOWNLOAD_LAYOUT_PARAMETER_FILE);

static_assert(static_cast<int>(workflow::ImageDownloadStage::inspectFiles) ==
              ZZRK_DOWNLOAD_STAGE_INSPECT_FILES);
static_assert(static_cast<int>(workflow::ImageDownloadStage::verifyFiles) ==
              ZZRK_DOWNLOAD_STAGE_VERIFY_FILES);
static_assert(static_cast<int>(workflow::ImageDownloadStage::selectDevice) ==
              ZZRK_DOWNLOAD_STAGE_SELECT_DEVICE);
static_assert(static_cast<int>(workflow::ImageDownloadStage::enterMaskrom) ==
              ZZRK_DOWNLOAD_STAGE_ENTER_MASKROM);
static_assert(static_cast<int>(workflow::ImageDownloadStage::waitForMaskrom) ==
              ZZRK_DOWNLOAD_STAGE_WAIT_MASKROM);
static_assert(static_cast<int>(workflow::ImageDownloadStage::downloadLoader) ==
              ZZRK_DOWNLOAD_STAGE_DOWNLOAD_LOADER);
static_assert(
    static_cast<int>(workflow::ImageDownloadStage::waitForRamLoader) ==
    ZZRK_DOWNLOAD_STAGE_WAIT_RAM_LOADER);
static_assert(static_cast<int>(workflow::ImageDownloadStage::checkRamLoader) ==
              ZZRK_DOWNLOAD_STAGE_CHECK_RAM_LOADER);
static_assert(static_cast<int>(workflow::ImageDownloadStage::checkStorage) ==
              ZZRK_DOWNLOAD_STAGE_CHECK_STORAGE);
static_assert(static_cast<int>(workflow::ImageDownloadStage::validateLayout) ==
              ZZRK_DOWNLOAD_STAGE_VALIDATE_LAYOUT);
static_assert(static_cast<int>(workflow::ImageDownloadStage::erase) ==
              ZZRK_DOWNLOAD_STAGE_ERASE);
static_assert(static_cast<int>(workflow::ImageDownloadStage::write) ==
              ZZRK_DOWNLOAD_STAGE_WRITE);
static_assert(static_cast<int>(workflow::ImageDownloadStage::verify) ==
              ZZRK_DOWNLOAD_STAGE_VERIFY);
static_assert(static_cast<int>(workflow::ImageDownloadStage::reset) ==
              ZZRK_DOWNLOAD_STAGE_RESET);

static_assert(static_cast<int>(workflow::ImageDownloadItemKind::loader) ==
              ZZRK_DOWNLOAD_ITEM_LOADER);
static_assert(
    static_cast<int>(workflow::ImageDownloadItemKind::partitionTable) ==
    ZZRK_DOWNLOAD_ITEM_PARTITION_TABLE);
static_assert(static_cast<int>(workflow::ImageDownloadItemKind::partition) ==
              ZZRK_DOWNLOAD_ITEM_PARTITION);

static_assert(static_cast<int>(image::PayloadFormat::unknown) ==
              ZZRK_PAYLOAD_FORMAT_UNKNOWN);
static_assert(static_cast<int>(image::PayloadFormat::raw) ==
              ZZRK_PAYLOAD_FORMAT_RAW);
static_assert(static_cast<int>(image::PayloadFormat::androidSparse) ==
              ZZRK_PAYLOAD_FORMAT_ANDROID_SPARSE);
static_assert(static_cast<int>(image::PayloadFormat::rockchipParameter) ==
              ZZRK_PAYLOAD_FORMAT_ROCKCHIP_PARAMETER);

static_assert(static_cast<int>(workflow::ImageDownloadErrorCode::cancelled) ==
              ZZRK_DOWNLOAD_ERROR_CANCELLED);
static_assert(
    static_cast<int>(workflow::ImageDownloadErrorCode::invalidRequest) ==
    ZZRK_DOWNLOAD_ERROR_INVALID_REQUEST);
static_assert(
    static_cast<int>(workflow::ImageDownloadErrorCode::imageInvalid) ==
    ZZRK_DOWNLOAD_ERROR_IMAGE_INVALID);
static_assert(
    static_cast<int>(workflow::ImageDownloadErrorCode::digestFailed) ==
    ZZRK_DOWNLOAD_ERROR_DIGEST_FAILED);
static_assert(
    static_cast<int>(workflow::ImageDownloadErrorCode::digestChanged) ==
    ZZRK_DOWNLOAD_ERROR_DIGEST_CHANGED);
static_assert(
    static_cast<int>(workflow::ImageDownloadErrorCode::layoutInvalid) ==
    ZZRK_DOWNLOAD_ERROR_LAYOUT_INVALID);
static_assert(
    static_cast<int>(workflow::ImageDownloadErrorCode::scanFailed) ==
    ZZRK_DOWNLOAD_ERROR_SCAN_FAILED);
static_assert(
    static_cast<int>(workflow::ImageDownloadErrorCode::deviceNotFound) ==
    ZZRK_DOWNLOAD_ERROR_DEVICE_NOT_FOUND);
static_assert(
    static_cast<int>(workflow::ImageDownloadErrorCode::deviceAmbiguous) ==
    ZZRK_DOWNLOAD_ERROR_DEVICE_AMBIGUOUS);
static_assert(
    static_cast<int>(workflow::ImageDownloadErrorCode::deviceUnsupported) ==
    ZZRK_DOWNLOAD_ERROR_DEVICE_UNSUPPORTED);
static_assert(
    static_cast<int>(workflow::ImageDownloadErrorCode::loaderRequired) ==
    ZZRK_DOWNLOAD_ERROR_LOADER_REQUIRED);
static_assert(
    static_cast<int>(workflow::ImageDownloadErrorCode::openFailed) ==
    ZZRK_DOWNLOAD_ERROR_OPEN_FAILED);
static_assert(
    static_cast<int>(workflow::ImageDownloadErrorCode::loaderResetFailed) ==
    ZZRK_DOWNLOAD_ERROR_LOADER_RESET_FAILED);
static_assert(static_cast<int>(
                  workflow::ImageDownloadErrorCode::reenumerationTimedOut) ==
              ZZRK_DOWNLOAD_ERROR_REENUMERATION_TIMED_OUT);
static_assert(static_cast<int>(
                  workflow::ImageDownloadErrorCode::loaderDownloadFailed) ==
              ZZRK_DOWNLOAD_ERROR_LOADER_DOWNLOAD_FAILED);
static_assert(
    static_cast<int>(workflow::ImageDownloadErrorCode::loaderNotReady) ==
    ZZRK_DOWNLOAD_ERROR_LOADER_NOT_READY);
static_assert(
    static_cast<int>(workflow::ImageDownloadErrorCode::wrongChip) ==
    ZZRK_DOWNLOAD_ERROR_WRONG_CHIP);
static_assert(
    static_cast<int>(workflow::ImageDownloadErrorCode::storageCheckFailed) ==
    ZZRK_DOWNLOAD_ERROR_STORAGE_CHECK_FAILED);
static_assert(
    static_cast<int>(workflow::ImageDownloadErrorCode::storageNotEmmc) ==
    ZZRK_DOWNLOAD_ERROR_STORAGE_NOT_EMMC);
static_assert(
    static_cast<int>(workflow::ImageDownloadErrorCode::deviceTooSmall) ==
    ZZRK_DOWNLOAD_ERROR_DEVICE_TOO_SMALL);
static_assert(static_cast<int>(
                  workflow::ImageDownloadErrorCode::deviceLayoutChanged) ==
              ZZRK_DOWNLOAD_ERROR_DEVICE_LAYOUT_CHANGED);
static_assert(
    static_cast<int>(workflow::ImageDownloadErrorCode::flashFailed) ==
    ZZRK_DOWNLOAD_ERROR_FLASH_FAILED);
static_assert(
    static_cast<int>(workflow::ImageDownloadErrorCode::resetFailed) ==
    ZZRK_DOWNLOAD_ERROR_RESET_FAILED);
static_assert(
    static_cast<int>(workflow::ImageDownloadErrorCode::closeFailed) ==
    ZZRK_DOWNLOAD_ERROR_CLOSE_FAILED);

// ---------------------------------------------------------------------------
// Thread-local error state
// ---------------------------------------------------------------------------

thread_local std::string lastError;

void clearError()
{
    lastError.clear();
}

void setError(std::string message)
{
    lastError = std::move(message);
}

// ---------------------------------------------------------------------------
// Bounded copy helpers
// ---------------------------------------------------------------------------

template <std::size_t N>
void copyString(char (&destination)[N], const std::string_view source) noexcept
{
    static_assert(N > 0);
    const auto length = std::min(source.size(), N - 1);
    if (length > 0) {
        std::memcpy(destination, source.data(), length);
    }
    destination[length] = '\0';
}

template <std::size_t N, std::size_t M>
void copyBytes(std::uint8_t (&destination)[N],
               const std::array<std::byte, M>& source) noexcept
{
    static_assert(N == M);
    std::memcpy(destination, source.data(), N);
}

// ---------------------------------------------------------------------------
// Enum descriptions for error messages
// ---------------------------------------------------------------------------

[[nodiscard]] std::string_view
describeImageError(const image::FirmwareImageErrorCode code) noexcept
{
    switch (code) {
    case image::FirmwareImageErrorCode::fileStatFailed:
        return "file stat failed";
    case image::FirmwareImageErrorCode::fileOpenFailed:
        return "file open failed";
    case image::FirmwareImageErrorCode::fileTooSmall:
        return "file too small";
    case image::FirmwareImageErrorCode::seekFailed:
        return "seek failed";
    case image::FirmwareImageErrorCode::shortRead:
        return "short read";
    case image::FirmwareImageErrorCode::invalidRkfwSignature:
        return "invalid RKFW signature";
    case image::FirmwareImageErrorCode::invalidRkfwHeader:
        return "invalid RKFW header";
    case image::FirmwareImageErrorCode::invalidBootRegion:
        return "invalid boot region";
    case image::FirmwareImageErrorCode::invalidBootSignature:
        return "invalid boot signature";
    case image::FirmwareImageErrorCode::invalidLoader:
        return "invalid loader";
    case image::FirmwareImageErrorCode::invalidFirmwareRegion:
        return "invalid firmware region";
    case image::FirmwareImageErrorCode::invalidRkafSignature:
        return "invalid RKAF signature";
    case image::FirmwareImageErrorCode::rkafSizeMismatch:
        return "RKAF size mismatch";
    case image::FirmwareImageErrorCode::invalidEntryCount:
        return "invalid entry count";
    case image::FirmwareImageErrorCode::invalidEntryTable:
        return "invalid entry table";
    case image::FirmwareImageErrorCode::invalidEntryRegion:
        return "invalid entry region";
    case image::FirmwareImageErrorCode::invalidParameter:
        return "invalid parameter";
    case image::FirmwareImageErrorCode::invalidPartitionLayout:
        return "invalid partition layout";
    case image::FirmwareImageErrorCode::invalidSparseImage:
        return "invalid sparse image";
    case image::FirmwareImageErrorCode::invalidTrailingMd5:
        return "invalid trailing MD5";
    }
    return "unknown image error";
}

[[nodiscard]] std::string_view
describePlanError(const image::FirmwarePlanErrorCode code) noexcept
{
    switch (code) {
    case image::FirmwarePlanErrorCode::unsupportedTargetChip:
        return "unsupported target chip";
    case image::FirmwarePlanErrorCode::missingLoader:
        return "missing loader";
    case image::FirmwarePlanErrorCode::missingParameter:
        return "missing parameter";
    case image::FirmwarePlanErrorCode::firmwareMd5NotVerified:
        return "firmware MD5 not verified";
    case image::FirmwarePlanErrorCode::duplicatePartition:
        return "duplicate partition";
    case image::FirmwarePlanErrorCode::targetPartitionMissing:
        return "target partition missing";
    case image::FirmwarePlanErrorCode::targetPartitionTooSmall:
        return "target partition too small";
    case image::FirmwarePlanErrorCode::partitionRangeOverflow:
        return "partition range overflow";
    }
    return "unknown plan error";
}

[[nodiscard]] std::string_view
describeOpenError(const workflow::FirmwareUpgradeOpenErrorCode code) noexcept
{
    switch (code) {
    case workflow::FirmwareUpgradeOpenErrorCode::invalidSelection:
        return "invalid selection";
    case workflow::FirmwareUpgradeOpenErrorCode::initializationFailed:
        return "initialization failed";
    case workflow::FirmwareUpgradeOpenErrorCode::enumerationFailed:
        return "enumeration failed";
    case workflow::FirmwareUpgradeOpenErrorCode::topologyReadFailed:
        return "topology read failed";
    case workflow::FirmwareUpgradeOpenErrorCode::descriptorReadFailed:
        return "descriptor read failed";
    case workflow::FirmwareUpgradeOpenErrorCode::deviceNotFound:
        return "device not found";
    case workflow::FirmwareUpgradeOpenErrorCode::deviceAmbiguous:
        return "device ambiguous";
    case workflow::FirmwareUpgradeOpenErrorCode::configurationReadFailed:
        return "configuration read failed";
    case workflow::FirmwareUpgradeOpenErrorCode::interfaceNotFound:
        return "interface not found";
    case workflow::FirmwareUpgradeOpenErrorCode::interfaceAmbiguous:
        return "interface ambiguous";
    case workflow::FirmwareUpgradeOpenErrorCode::invalidEndpointLayout:
        return "invalid endpoint layout";
    case workflow::FirmwareUpgradeOpenErrorCode::openFailed:
        return "open failed";
    case workflow::FirmwareUpgradeOpenErrorCode::driverQueryFailed:
        return "driver query failed";
    case workflow::FirmwareUpgradeOpenErrorCode::driverActive:
        return "kernel driver active";
    case workflow::FirmwareUpgradeOpenErrorCode::claimFailed:
        return "claim failed";
    case workflow::FirmwareUpgradeOpenErrorCode::resourceExhausted:
        return "resource exhausted";
    }
    return "unknown open error";
}

[[nodiscard]] std::string_view
describeSessionError(const session::SessionErrorCode code) noexcept
{
    switch (code) {
    case session::SessionErrorCode::sessionFaulted:
        return "session faulted";
    case session::SessionErrorCode::invalidTimeout:
        return "invalid timeout";
    case session::SessionErrorCode::commandEncodeFailed:
        return "command encode failed";
    case session::SessionErrorCode::emptyDataBuffer:
        return "empty data buffer";
    case session::SessionErrorCode::bufferTooLarge:
        return "buffer too large";
    case session::SessionErrorCode::invalidAddress:
        return "invalid address";
    case session::SessionErrorCode::invalidSectorCount:
        return "invalid sector count";
    case session::SessionErrorCode::bufferSizeMismatch:
        return "buffer size mismatch";
    case session::SessionErrorCode::transportFailure:
        return "transport failure";
    case session::SessionErrorCode::shortTransfer:
        return "short transfer";
    case session::SessionErrorCode::invalidTransferCount:
        return "invalid transfer count";
    case session::SessionErrorCode::deviceCommandFailed:
        return "device command failed";
    case session::SessionErrorCode::statusDecodeFailed:
        return "status decode failed";
    case session::SessionErrorCode::nonZeroResidue:
        return "non-zero residue";
    }
    return "unknown session error";
}

[[nodiscard]] std::string_view
describeLoaderError(const image::MaskromLoaderErrorCode code) noexcept
{
    switch (code) {
    case image::MaskromLoaderErrorCode::invalidImage:
        return "invalid loader image";
    case image::MaskromLoaderErrorCode::imageOpenFailed:
        return "loader image open failed";
    case image::MaskromLoaderErrorCode::imageSeekFailed:
        return "loader image seek failed";
    case image::MaskromLoaderErrorCode::imageReadFailed:
        return "loader image read failed";
    case image::MaskromLoaderErrorCode::transferFailed:
        return "loader transfer failed";
    case image::MaskromLoaderErrorCode::shortTransfer:
        return "loader short transfer";
    case image::MaskromLoaderErrorCode::cancelled:
        return "cancelled";
    }
    return "unknown loader error";
}

[[nodiscard]] std::string_view
describeFlashError(const image::FirmwareFlashErrorCode code) noexcept
{
    switch (code) {
    case image::FirmwareFlashErrorCode::invalidPlan:
        return "invalid plan";
    case image::FirmwareFlashErrorCode::deviceTooSmall:
        return "device too small";
    case image::FirmwareFlashErrorCode::addressOverflow:
        return "address overflow";
    case image::FirmwareFlashErrorCode::imageOpenFailed:
        return "image open failed";
    case image::FirmwareFlashErrorCode::imageSeekFailed:
        return "image seek failed";
    case image::FirmwareFlashErrorCode::imageReadFailed:
        return "image read failed";
    case image::FirmwareFlashErrorCode::unsupportedPayload:
        return "unsupported payload";
    case image::FirmwareFlashErrorCode::invalidLoader:
        return "invalid loader";
    case image::FirmwareFlashErrorCode::loaderUnsupported:
        return "loader unsupported";
    case image::FirmwareFlashErrorCode::partitionTableGenerationFailed:
        return "partition table generation failed";
    case image::FirmwareFlashErrorCode::eraseFailed:
        return "erase failed";
    case image::FirmwareFlashErrorCode::writeFailed:
        return "write failed";
    case image::FirmwareFlashErrorCode::verifyFailed:
        return "verify failed";
    case image::FirmwareFlashErrorCode::cancelled:
        return "cancelled";
    }
    return "unknown flash error";
}

[[nodiscard]] std::string_view describeUpgradeError(
    const workflow::FirmwareUpgradeErrorCode code) noexcept
{
    switch (code) {
    case workflow::FirmwareUpgradeErrorCode::cancelled:
        return "cancelled";
    case workflow::FirmwareUpgradeErrorCode::imageInvalid:
        return "image invalid";
    case workflow::FirmwareUpgradeErrorCode::digestFailed:
        return "digest failed";
    case workflow::FirmwareUpgradeErrorCode::planInvalid:
        return "plan invalid";
    case workflow::FirmwareUpgradeErrorCode::scanFailed:
        return "scan failed";
    case workflow::FirmwareUpgradeErrorCode::deviceNotFound:
        return "device not found";
    case workflow::FirmwareUpgradeErrorCode::deviceAmbiguous:
        return "device ambiguous";
    case workflow::FirmwareUpgradeErrorCode::deviceUnsupported:
        return "device unsupported";
    case workflow::FirmwareUpgradeErrorCode::openFailed:
        return "open failed";
    case workflow::FirmwareUpgradeErrorCode::loaderResetFailed:
        return "loader reset failed";
    case workflow::FirmwareUpgradeErrorCode::reenumerationTimedOut:
        return "reenumeration timed out";
    case workflow::FirmwareUpgradeErrorCode::loaderDownloadFailed:
        return "loader download failed";
    case workflow::FirmwareUpgradeErrorCode::loaderNotReady:
        return "loader not ready";
    case workflow::FirmwareUpgradeErrorCode::wrongChip:
        return "wrong chip";
    case workflow::FirmwareUpgradeErrorCode::storageCheckFailed:
        return "storage check failed";
    case workflow::FirmwareUpgradeErrorCode::storageNotEmmc:
        return "storage not eMMC";
    case workflow::FirmwareUpgradeErrorCode::deviceTooSmall:
        return "device too small";
    case workflow::FirmwareUpgradeErrorCode::flashFailed:
        return "flash failed";
    case workflow::FirmwareUpgradeErrorCode::resetFailed:
        return "reset failed";
    case workflow::FirmwareUpgradeErrorCode::closeFailed:
        return "close failed";
    }
    return "unknown upgrade error";
}

[[nodiscard]] std::string_view describeDeviceError(
    const workflow::DeviceManagementErrorCode code) noexcept
{
    switch (code) {
    case workflow::DeviceManagementErrorCode::cancelled:
        return "cancelled";
    case workflow::DeviceManagementErrorCode::scanFailed:
        return "scan failed";
    case workflow::DeviceManagementErrorCode::deviceNotFound:
        return "device not found";
    case workflow::DeviceManagementErrorCode::deviceAmbiguous:
        return "device ambiguous";
    case workflow::DeviceManagementErrorCode::deviceUnsupported:
        return "device unsupported";
    case workflow::DeviceManagementErrorCode::openFailed:
        return "open failed";
    case workflow::DeviceManagementErrorCode::chipInfoFailed:
        return "chip info failed";
    case workflow::DeviceManagementErrorCode::capabilityFailed:
        return "capability failed";
    case workflow::DeviceManagementErrorCode::flashInfoFailed:
        return "flash info failed";
    case workflow::DeviceManagementErrorCode::flashIdFailed:
        return "flash id failed";
    case workflow::DeviceManagementErrorCode::storageNotEmmc:
        return "storage not eMMC";
    case workflow::DeviceManagementErrorCode::gptFailed:
        return "GPT failed";
    case workflow::DeviceManagementErrorCode::partitionNotFound:
        return "partition not found";
    case workflow::DeviceManagementErrorCode::partitionAmbiguous:
        return "partition ambiguous";
    case workflow::DeviceManagementErrorCode::inputInvalid:
        return "input invalid";
    case workflow::DeviceManagementErrorCode::inputTooLarge:
        return "input too large";
    case workflow::DeviceManagementErrorCode::inputOpenFailed:
        return "input open failed";
    case workflow::DeviceManagementErrorCode::inputReadFailed:
        return "input read failed";
    case workflow::DeviceManagementErrorCode::outputExists:
        return "output exists";
    case workflow::DeviceManagementErrorCode::outputOpenFailed:
        return "output open failed";
    case workflow::DeviceManagementErrorCode::outputWriteFailed:
        return "output write failed";
    case workflow::DeviceManagementErrorCode::outputFinalizeFailed:
        return "output finalize failed";
    case workflow::DeviceManagementErrorCode::readFailed:
        return "read failed";
    case workflow::DeviceManagementErrorCode::writeFailed:
        return "write failed";
    case workflow::DeviceManagementErrorCode::verifyFailed:
        return "verify failed";
    case workflow::DeviceManagementErrorCode::eraseFailed:
        return "erase failed";
    case workflow::DeviceManagementErrorCode::closeFailed:
        return "close failed";
    }
    return "unknown device error";
}

[[nodiscard]] std::string_view describeDownloadError(
    const workflow::ImageDownloadErrorCode code) noexcept
{
    switch (code) {
    case workflow::ImageDownloadErrorCode::cancelled:
        return "cancelled";
    case workflow::ImageDownloadErrorCode::invalidRequest:
        return "invalid request";
    case workflow::ImageDownloadErrorCode::imageInvalid:
        return "image invalid";
    case workflow::ImageDownloadErrorCode::digestFailed:
        return "digest failed";
    case workflow::ImageDownloadErrorCode::digestChanged:
        return "digest changed";
    case workflow::ImageDownloadErrorCode::layoutInvalid:
        return "layout invalid";
    case workflow::ImageDownloadErrorCode::scanFailed:
        return "scan failed";
    case workflow::ImageDownloadErrorCode::deviceNotFound:
        return "device not found";
    case workflow::ImageDownloadErrorCode::deviceAmbiguous:
        return "device ambiguous";
    case workflow::ImageDownloadErrorCode::deviceUnsupported:
        return "device unsupported";
    case workflow::ImageDownloadErrorCode::loaderRequired:
        return "loader required";
    case workflow::ImageDownloadErrorCode::openFailed:
        return "open failed";
    case workflow::ImageDownloadErrorCode::loaderResetFailed:
        return "loader reset failed";
    case workflow::ImageDownloadErrorCode::reenumerationTimedOut:
        return "reenumeration timed out";
    case workflow::ImageDownloadErrorCode::loaderDownloadFailed:
        return "loader download failed";
    case workflow::ImageDownloadErrorCode::loaderNotReady:
        return "loader not ready";
    case workflow::ImageDownloadErrorCode::wrongChip:
        return "wrong chip";
    case workflow::ImageDownloadErrorCode::storageCheckFailed:
        return "storage check failed";
    case workflow::ImageDownloadErrorCode::storageNotEmmc:
        return "storage not eMMC";
    case workflow::ImageDownloadErrorCode::deviceTooSmall:
        return "device too small";
    case workflow::ImageDownloadErrorCode::deviceLayoutChanged:
        return "device layout changed";
    case workflow::ImageDownloadErrorCode::flashFailed:
        return "flash failed";
    case workflow::ImageDownloadErrorCode::resetFailed:
        return "reset failed";
    case workflow::ImageDownloadErrorCode::closeFailed:
        return "close failed";
    }
    return "unknown download error";
}

// ---------------------------------------------------------------------------
// Error message composition
// ---------------------------------------------------------------------------

void appendImageError(std::ostringstream& message,
                      const image::FirmwareImageError& error)
{
    message << describeImageError(error.code) << " (offset " << error.offset;
    if (error.entryIndex != 0U) {
        message << ", entry " << error.entryIndex;
    }
    message << ')';
}

void appendDigestError(std::ostringstream& message,
                       const workflow::FirmwareDigestError& error)
{
    message << workflow::describeFirmwareDigestError(error.code);
    if (error.code == workflow::FirmwareDigestErrorCode::mismatch) {
        message << " (actual " << error.actualMd5.data() << ')';
    }
}

void appendSessionError(std::ostringstream& message,
                        const session::SessionError& error)
{
    message << describeSessionError(error.code);
}

[[nodiscard]] std::string
upgradeErrorMessage(const workflow::FirmwareUpgradeError& error)
{
    std::ostringstream message;
    message << "firmware upgrade failed at "
            << workflow::toString(error.stage) << ": "
            << describeUpgradeError(error.code);
    if (!error.target.empty()) {
        message << " (target " << error.target << ')';
    }
    if (error.imageError.has_value()) {
        message << ": ";
        appendImageError(message, *error.imageError);
    } else if (error.digestError.has_value()) {
        message << ": ";
        appendDigestError(message, *error.digestError);
    } else if (error.planError.has_value()) {
        message << ": " << describePlanError(error.planError->code);
        if (!error.planError->target.empty()) {
            message << " (target " << error.planError->target << ')';
        }
    } else if (error.scanError.has_value()) {
        message << ": " << error.scanError->message;
    } else if (error.openError.has_value()) {
        message << ": " << describeOpenError(error.openError->code);
    } else if (error.loaderError.has_value()) {
        message << ": " << describeLoaderError(error.loaderError->code);
        if (!error.loaderError->stage.empty()) {
            message << " (" << error.loaderError->stage << ')';
        }
    } else if (error.sessionError.has_value()) {
        message << ": ";
        appendSessionError(message, *error.sessionError);
    } else if (error.flashError.has_value()) {
        message << ": " << describeFlashError(error.flashError->code);
        if (!error.flashError->target.empty()) {
            message << " (target " << error.flashError->target << ')';
        }
    }
    return message.str();
}

[[nodiscard]] std::string
deviceErrorMessage(const workflow::DeviceManagementError& error)
{
    std::ostringstream message;
    message << "device operation failed: " << describeDeviceError(error.code);
    if (!error.target.empty()) {
        message << " (target " << error.target << ')';
    }
    if (error.scanError.has_value()) {
        message << ": " << error.scanError->message;
    } else if (error.openError.has_value()) {
        message << ": " << describeOpenError(error.openError->code);
    } else if (error.sessionError.has_value()) {
        message << ": ";
        appendSessionError(message, *error.sessionError);
    } else if (error.gptError.has_value()) {
        message << ": " << storage::describeGptError(error.gptError->code)
                << " (lba " << error.gptError->lba << ')';
    }
    return message.str();
}

[[nodiscard]] std::string
downloadErrorMessage(const workflow::ImageDownloadError& error)
{
    std::ostringstream message;
    message << "image download failed at "
            << workflow::toString(error.stage) << ": "
            << describeDownloadError(error.code);
    if (!error.target.empty()) {
        message << " (target " << error.target << ')';
    }
    if (error.imageError.has_value()) {
        message << ": ";
        appendImageError(message, *error.imageError);
    } else if (error.digestError.has_value()) {
        message << ": ";
        appendDigestError(message, *error.digestError);
    } else if (error.scanError.has_value()) {
        message << ": " << error.scanError->message;
    } else if (error.openError.has_value()) {
        message << ": " << describeOpenError(error.openError->code);
    } else if (error.loaderError.has_value()) {
        message << ": " << describeLoaderError(error.loaderError->code);
    } else if (error.sessionError.has_value()) {
        message << ": ";
        appendSessionError(message, *error.sessionError);
    } else if (error.gptError.has_value()) {
        message << ": " << storage::describeGptError(error.gptError->code)
                << " (lba " << error.gptError->lba << ')';
    } else if (error.flashError.has_value()) {
        message << ": " << describeFlashError(error.flashError->code);
        if (!error.flashError->target.empty()) {
            message << " (target " << error.flashError->target << ')';
        }
    }
    return message.str();
}

// ---------------------------------------------------------------------------
// Struct mapping helpers
// ---------------------------------------------------------------------------

void fillDeviceInfo(zzrk_usb_device_info& out,
                    const transport::UsbDeviceInfo& device) noexcept
{
    out.vendor_id = device.vendorId;
    out.product_id = device.productId;
    out.bcd_usb = device.bcdUsb;
    out.bcd_device = device.bcdDevice;
    out.bus_number = device.busNumber;
    out.device_address = device.deviceAddress;
    out.mode = static_cast<int32_t>(device.mode);
    copyString(out.port_path, device.portPath);
    const auto count =
        std::min(device.portNumbers.size(), sizeof(out.port_numbers));
    for (std::size_t index = 0; index < count; ++index) {
        out.port_numbers[index] = device.portNumbers[index];
    }
    out.port_number_count = static_cast<std::uint8_t>(count);
}

[[nodiscard]] zzrk_status statusForErrorCode(const int32_t errorCode) noexcept
{
    return errorCode == 0 ? ZZRK_ERR_CANCELLED : ZZRK_ERR_FAILED;
}

} // namespace

struct zzrk_cancellation {
    std::shared_ptr<std::atomic<bool>> flag;
};

extern "C" {

const char* zzrk_last_error_message(void)
{
    return lastError.c_str();
}

const char* zzrk_version_string(void)
{
    clearError();
    return "zzrkdevtool " ZZRK_VERSION;
}

zzrk_status zzrk_scan_devices(zzrk_usb_device_info* devices,
                              const uint32_t capacity, uint32_t* out_count)
{
    clearError();
    if (out_count == nullptr) {
        setError("device scan failed: out_count must not be null");
        return ZZRK_ERR_INVALID_ARGUMENT;
    }
    *out_count = 0;
    if (devices == nullptr && capacity != 0) {
        setError("device scan failed: devices must not be null when capacity "
                 "is nonzero");
        return ZZRK_ERR_INVALID_ARGUMENT;
    }
    transport::UsbDeviceScanner scanner;
    const auto result = scanner.scanRockchipDevices();
    if (result.error.has_value()) {
        setError("device scan failed: " + result.error->message);
        return ZZRK_ERR_FAILED;
    }
    *out_count = static_cast<std::uint32_t>(result.devices.size());
    const auto count =
        std::min<std::size_t>(result.devices.size(), capacity);
    for (std::size_t index = 0; index < count; ++index) {
        devices[index] = zzrk_usb_device_info{};
        fillDeviceInfo(devices[index], result.devices[index]);
    }
    return ZZRK_OK;
}

zzrk_cancellation* zzrk_cancellation_new(void)
{
    clearError();
    return new zzrk_cancellation{
        .flag = std::make_shared<std::atomic<bool>>(false)};
}

void zzrk_cancellation_cancel(zzrk_cancellation* token)
{
    clearError();
    if (token == nullptr) {
        return;
    }
    token->flag->store(true, std::memory_order_relaxed);
}

void zzrk_cancellation_free(zzrk_cancellation* token)
{
    clearError();
    delete token;
}

zzrk_status zzrk_inspect_firmware(const char* image_path,
                                  zzrk_firmware_info* out_info)
{
    clearError();
    if (image_path == nullptr) {
        setError("firmware inspection failed: image_path must not be null");
        return ZZRK_ERR_INVALID_ARGUMENT;
    }
    if (out_info == nullptr) {
        setError("firmware inspection failed: out_info must not be null");
        return ZZRK_ERR_INVALID_ARGUMENT;
    }
    *out_info = zzrk_firmware_info{};
    auto parsed = image::parseRkFirmwareImage(image_path);
    if (const auto* error = std::get_if<image::FirmwareImageError>(&parsed)) {
        std::ostringstream message;
        message << "firmware inspection failed: ";
        appendImageError(message, *error);
        setError(message.str());
        return ZZRK_ERR_FAILED;
    }
    auto image = std::get<image::RkFirmwareImage>(std::move(parsed));
    workflow::FirmwareMd5Verifier verifier;
    const auto verified = verifier.verify(image);
    if (const auto* error =
            std::get_if<workflow::FirmwareDigestError>(&verified)) {
        std::ostringstream message;
        message << "firmware inspection failed: ";
        appendDigestError(message, *error);
        if (error->code == workflow::FirmwareDigestErrorCode::mismatch) {
            message << " (expected " << image.trailingMd5.data() << ')';
        }
        setError(message.str());
        return ZZRK_ERR_FAILED;
    }
    copyString(out_info->chip, image.chip);
    copyString(out_info->model, image.model);
    copyString(out_info->manufacturer, image.manufacturer);
    copyString(out_info->md5, image.trailingMd5.data());
    out_info->md5_verified = image.trailingMd5Verified;
    out_info->file_size = image.fileSize;
    out_info->partition_count =
        static_cast<std::uint32_t>(image.partitions.size());
    return ZZRK_OK;
}

zzrk_status zzrk_run_firmware_upgrade(
    const char* port_path, const char* image_path, const int32_t verify_mode,
    const bool reboot, const bool prepare_only,
    const zzrk_upgrade_progress_fn progress_fn, void* user_data,
    const zzrk_cancellation* cancellation,
    zzrk_upgrade_outcome* out_outcome)
{
    clearError();
    if (port_path == nullptr) {
        setError("firmware upgrade failed: port_path must not be null");
        return ZZRK_ERR_INVALID_ARGUMENT;
    }
    if (image_path == nullptr) {
        setError("firmware upgrade failed: image_path must not be null");
        return ZZRK_ERR_INVALID_ARGUMENT;
    }
    if (verify_mode < ZZRK_VERIFY_MODE_NONE ||
        verify_mode > ZZRK_VERIFY_MODE_ALL) {
        setError("firmware upgrade failed: verify_mode is out of range");
        return ZZRK_ERR_INVALID_ARGUMENT;
    }
    if (out_outcome == nullptr) {
        setError("firmware upgrade failed: out_outcome must not be null");
        return ZZRK_ERR_INVALID_ARGUMENT;
    }
    *out_outcome = zzrk_upgrade_outcome{};

    auto backend = workflow::createLibusbFirmwareUpgradeBackend();
    workflow::FirmwareMd5Verifier verifier;
    workflow::FirmwareUpgradeWorkflow upgrade{*backend, verifier};

    workflow::FirmwareUpgradeRequest request{};
    request.portPath = port_path;
    request.imagePath = image_path;
    request.verifyMode = static_cast<image::FirmwareVerifyMode>(verify_mode);
    request.reboot = reboot;
    request.prepareOnly = prepare_only;

    workflow::FirmwareUpgradeProgressCallback progress;
    if (progress_fn != nullptr) {
        progress = [progress_fn,
                    user_data](const workflow::FirmwareUpgradeEvent& event) {
            zzrk_upgrade_progress_event cEvent{};
            cEvent.stage = static_cast<int32_t>(event.stage);
            cEvent.target =
                event.target.empty() ? nullptr : event.target.c_str();
            cEvent.completed_bytes = event.completedBytes;
            cEvent.total_bytes = event.totalBytes;
            cEvent.flash_phase =
                event.flashPhase.has_value()
                    ? static_cast<int32_t>(*event.flashPhase)
                    : ZZRK_FLASH_PHASE_NONE;
            progress_fn(&cEvent, user_data);
        };
    }
    workflow::FirmwareUpgradeCancellationCallback cancelled;
    if (cancellation != nullptr) {
        const auto flag = cancellation->flag;
        cancelled = [flag] {
            return flag->load(std::memory_order_relaxed);
        };
    }

    auto result = upgrade.run(request, progress, cancelled);
    if (const auto* error =
            std::get_if<workflow::FirmwareUpgradeError>(&result)) {
        out_outcome->error_code = static_cast<int32_t>(error->code);
        out_outcome->error_stage = static_cast<int32_t>(error->stage);
        copyString(out_outcome->error_target, error->target);
        setError(upgradeErrorMessage(*error));
        return statusForErrorCode(out_outcome->error_code);
    }
    const auto& success = std::get<workflow::FirmwareUpgradeSuccess>(result);
    out_outcome->error_code = ZZRK_UPGRADE_ERROR_NONE;
    copyString(out_outcome->model, success.model);
    copyString(out_outcome->firmware_md5, success.firmwareMd5.data());
    out_outcome->device_sectors = success.deviceSectors;
    out_outcome->partition_bytes_written = success.partitionBytesWritten;
    out_outcome->sectors_erased = success.sectorsErased;
    out_outcome->partitions_verified = success.partitionsVerified;
    out_outcome->prepared_only = success.preparedOnly;
    out_outcome->partition_table_written = success.partitionTableWritten;
    out_outcome->partition_table_verified = success.partitionTableVerified;
    out_outcome->loader_written = success.loaderWritten;
    out_outcome->loader_verified = success.loaderVerified;
    out_outcome->reset_accepted = success.resetAccepted;
    return ZZRK_OK;
}

zzrk_status zzrk_inspect_device(const char* port_path,
                                zzrk_device_inspection* out_inspection)
{
    clearError();
    if (port_path == nullptr) {
        setError("device inspection failed: port_path must not be null");
        return ZZRK_ERR_INVALID_ARGUMENT;
    }
    if (out_inspection == nullptr) {
        setError("device inspection failed: out_inspection must not be null");
        return ZZRK_ERR_INVALID_ARGUMENT;
    }
    *out_inspection = zzrk_device_inspection{};
    auto backend = workflow::createLibusbFirmwareUpgradeBackend();
    workflow::DeviceManagementWorkflow management{*backend};
    auto result = management.inspect(port_path);
    if (const auto* error =
            std::get_if<workflow::DeviceManagementError>(&result)) {
        setError(deviceErrorMessage(*error));
        return statusForErrorCode(static_cast<int32_t>(error->code));
    }
    const auto& inspection = std::get<workflow::DeviceInspection>(result);
    fillDeviceInfo(out_inspection->device, inspection.device);
    copyBytes(out_inspection->chip_info, inspection.chipInfo.bytes);
    copyBytes(out_inspection->capability, inspection.capability.rawBytes);
    copyBytes(out_inspection->flash_info, inspection.flashInfo.rawBytes);
    copyBytes(out_inspection->flash_id, inspection.flashId.rawBytes);
    out_inspection->flash_id_is_emmc =
        session::classifyFlashId(inspection.flashId) ==
        session::FlashIdKind::emmcMediaMarker;
    out_inspection->legacy_flash_sector_count =
        inspection.flashInfo.legacy.sectorCount;
    out_inspection->legacy_flash_block_size_code =
        inspection.flashInfo.legacy.blockSizeCode;
    out_inspection->legacy_flash_page_size_code =
        inspection.flashInfo.legacy.pageSizeCode;
    out_inspection->legacy_flash_ecc_bits =
        inspection.flashInfo.legacy.eccBits;
    out_inspection->legacy_flash_access_time =
        inspection.flashInfo.legacy.accessTime;
    out_inspection->legacy_flash_manufacturer_code =
        inspection.flashInfo.legacy.manufacturerCode;
    out_inspection->legacy_flash_mask = inspection.flashInfo.legacy.flashMask;
    return ZZRK_OK;
}

zzrk_status zzrk_read_partitions(const char* port_path,
                                 zzrk_partition_entry* entries,
                                 const uint32_t capacity, uint32_t* out_count,
                                 uint64_t* out_device_sectors)
{
    clearError();
    if (port_path == nullptr) {
        setError("partition table read failed: port_path must not be null");
        return ZZRK_ERR_INVALID_ARGUMENT;
    }
    if (entries == nullptr && capacity != 0) {
        setError("partition table read failed: entries must not be null when "
                 "capacity is nonzero");
        return ZZRK_ERR_INVALID_ARGUMENT;
    }
    if (out_count == nullptr) {
        setError("partition table read failed: out_count must not be null");
        return ZZRK_ERR_INVALID_ARGUMENT;
    }
    if (out_device_sectors == nullptr) {
        setError(
            "partition table read failed: out_device_sectors must not be "
            "null");
        return ZZRK_ERR_INVALID_ARGUMENT;
    }
    *out_count = 0;
    *out_device_sectors = 0;
    auto backend = workflow::createLibusbFirmwareUpgradeBackend();
    workflow::DeviceManagementWorkflow management{*backend};
    auto result = management.readPartitions(port_path);
    if (const auto* error =
            std::get_if<workflow::DeviceManagementError>(&result)) {
        setError(deviceErrorMessage(*error));
        return statusForErrorCode(static_cast<int32_t>(error->code));
    }
    const auto& table = std::get<workflow::DevicePartitionTable>(result);
    *out_device_sectors = table.deviceSectors;
    *out_count = static_cast<std::uint32_t>(table.table.partitions.size());
    const auto count =
        std::min<std::size_t>(table.table.partitions.size(), capacity);
    for (std::size_t index = 0; index < count; ++index) {
        const auto& partition = table.table.partitions[index];
        auto& entry = entries[index];
        entry = zzrk_partition_entry{};
        copyString(entry.name, partition.name);
        copyBytes(entry.type_guid, partition.typeGuid);
        copyBytes(entry.unique_guid, partition.uniqueGuid);
        entry.first_lba = partition.firstLba;
        entry.sector_count = partition.sectorCount();
        entry.attributes = partition.attributes;
    }
    return ZZRK_OK;
}

} // extern "C"

// 内部回调适配器返回 C++ 类型，必须使用 C++ 链接，不能包含在 C ABI 中。
namespace {

zzrk_status runPartitionOperation(
    workflow::PartitionOperationResult result,
    zzrk_partition_outcome* out_outcome)
{
    if (const auto* error =
            std::get_if<workflow::DeviceManagementError>(&result)) {
        out_outcome->error_code = static_cast<int32_t>(error->code);
        copyString(out_outcome->error_target, error->target);
        setError(deviceErrorMessage(*error));
        return statusForErrorCode(out_outcome->error_code);
    }
    const auto& success = std::get<workflow::PartitionOperationSuccess>(result);
    out_outcome->error_code = ZZRK_DEVICE_ERROR_NONE;
    out_outcome->phase = static_cast<int32_t>(success.phase);
    out_outcome->bytes_processed = success.bytesProcessed;
    out_outcome->sectors_processed = success.sectorsProcessed;
    out_outcome->verified = success.verified;
    return ZZRK_OK;
}

[[nodiscard]] workflow::PartitionOperationProgressCallback
makePartitionProgress(const zzrk_partition_progress_fn progress_fn,
                      void* user_data)
{
    if (progress_fn == nullptr) {
        return {};
    }
    return [progress_fn,
            user_data](const workflow::PartitionOperationProgress& event) {
        zzrk_partition_progress_event cEvent{};
        cEvent.phase = static_cast<int32_t>(event.phase);
        cEvent.partition = event.partition.c_str();
        cEvent.completed_bytes = event.completedBytes;
        cEvent.total_bytes = event.totalBytes;
        progress_fn(&cEvent, user_data);
    };
}

[[nodiscard]] workflow::DeviceManagementCancellationCallback
makePartitionCancellation(const zzrk_cancellation* cancellation)
{
    if (cancellation == nullptr) {
        return {};
    }
    const auto flag = cancellation->flag;
    return [flag] { return flag->load(std::memory_order_relaxed); };
}

[[nodiscard]] bool validPartitionArgs(const char* port_path,
                                      const char* partition,
                                      zzrk_partition_outcome* out_outcome,
                                      const char* operation)
{
    if (port_path == nullptr) {
        setError(std::string{operation} +
                 " failed: port_path must not be null");
        return false;
    }
    if (partition == nullptr) {
        setError(std::string{operation} +
                 " failed: partition must not be null");
        return false;
    }
    if (out_outcome == nullptr) {
        setError(std::string{operation} +
                 " failed: out_outcome must not be null");
        return false;
    }
    return true;
}

} // namespace

extern "C" {

zzrk_status zzrk_read_partition(
    const char* port_path, const char* partition, const char* output_path,
    const zzrk_partition_progress_fn progress_fn, void* user_data,
    const zzrk_cancellation* cancellation,
    zzrk_partition_outcome* out_outcome)
{
    clearError();
    if (!validPartitionArgs(port_path, partition, out_outcome,
                            "partition read")) {
        return ZZRK_ERR_INVALID_ARGUMENT;
    }
    if (output_path == nullptr) {
        setError("partition read failed: output_path must not be null");
        return ZZRK_ERR_INVALID_ARGUMENT;
    }
    *out_outcome = zzrk_partition_outcome{};
    auto backend = workflow::createLibusbFirmwareUpgradeBackend();
    workflow::DeviceManagementWorkflow management{*backend};
    return runPartitionOperation(
        management.readPartition(port_path, partition, output_path,
                                 makePartitionProgress(progress_fn, user_data),
                                 makePartitionCancellation(cancellation)),
        out_outcome);
}

zzrk_status zzrk_write_partition(
    const char* port_path, const char* partition, const char* input_path,
    const bool verify, const zzrk_partition_progress_fn progress_fn,
    void* user_data, const zzrk_cancellation* cancellation,
    zzrk_partition_outcome* out_outcome)
{
    clearError();
    if (!validPartitionArgs(port_path, partition, out_outcome,
                            "partition write")) {
        return ZZRK_ERR_INVALID_ARGUMENT;
    }
    if (input_path == nullptr) {
        setError("partition write failed: input_path must not be null");
        return ZZRK_ERR_INVALID_ARGUMENT;
    }
    *out_outcome = zzrk_partition_outcome{};
    auto backend = workflow::createLibusbFirmwareUpgradeBackend();
    workflow::DeviceManagementWorkflow management{*backend};
    return runPartitionOperation(
        management.writePartition(port_path, partition, input_path, verify,
                                  makePartitionProgress(progress_fn,
                                                        user_data),
                                  makePartitionCancellation(cancellation)),
        out_outcome);
}

zzrk_status zzrk_erase_partition(
    const char* port_path, const char* partition,
    const zzrk_partition_progress_fn progress_fn, void* user_data,
    const zzrk_cancellation* cancellation,
    zzrk_partition_outcome* out_outcome)
{
    clearError();
    if (!validPartitionArgs(port_path, partition, out_outcome,
                            "partition erase")) {
        return ZZRK_ERR_INVALID_ARGUMENT;
    }
    *out_outcome = zzrk_partition_outcome{};
    auto backend = workflow::createLibusbFirmwareUpgradeBackend();
    workflow::DeviceManagementWorkflow management{*backend};
    return runPartitionOperation(
        management.erasePartition(port_path, partition,
                                  makePartitionProgress(progress_fn,
                                                        user_data),
                                  makePartitionCancellation(cancellation)),
        out_outcome);
}

zzrk_status zzrk_run_image_download(
    const char* port_path, const int32_t layout_source,
    const char* parameter_path, const char* loader_path,
    const bool verify_loader, const bool verify_partition_table,
    const zzrk_download_partition* partitions, const uint32_t partition_count,
    const bool reboot, const zzrk_download_progress_fn progress_fn,
    void* user_data, const zzrk_cancellation* cancellation,
    zzrk_download_outcome* out_outcome)
{
    clearError();
    if (port_path == nullptr) {
        setError("image download failed: port_path must not be null");
        return ZZRK_ERR_INVALID_ARGUMENT;
    }
    if (layout_source != ZZRK_DOWNLOAD_LAYOUT_DEVICE_GPT &&
        layout_source != ZZRK_DOWNLOAD_LAYOUT_PARAMETER_FILE) {
        setError("image download failed: layout_source is out of range");
        return ZZRK_ERR_INVALID_ARGUMENT;
    }
    if (layout_source == ZZRK_DOWNLOAD_LAYOUT_PARAMETER_FILE &&
        parameter_path == nullptr) {
        setError("image download failed: parameter_path must not be null "
                 "when the layout source is a parameter file");
        return ZZRK_ERR_INVALID_ARGUMENT;
    }
    if (partitions == nullptr && partition_count != 0) {
        setError("image download failed: partitions must not be null when "
                 "partition_count is nonzero");
        return ZZRK_ERR_INVALID_ARGUMENT;
    }
    for (uint32_t index = 0; index < partition_count; ++index) {
        if (partitions[index].target == nullptr ||
            partitions[index].image_path == nullptr) {
            setError("image download failed: partition entries require "
                     "non-null target and image_path");
            return ZZRK_ERR_INVALID_ARGUMENT;
        }
    }
    if (out_outcome == nullptr) {
        setError("image download failed: out_outcome must not be null");
        return ZZRK_ERR_INVALID_ARGUMENT;
    }
    *out_outcome = zzrk_download_outcome{};
    out_outcome->error_item_index = -1;

    auto backend = workflow::createLibusbFirmwareUpgradeBackend();
    workflow::FirmwareMd5Verifier verifier;
    workflow::ImageDownloadWorkflow download{*backend, verifier};

    workflow::ImageDownloadPrepareRequest prepare{};
    prepare.layoutSource =
        layout_source == ZZRK_DOWNLOAD_LAYOUT_DEVICE_GPT
            ? workflow::ImageDownloadLayoutSource::deviceGpt
            : workflow::ImageDownloadLayoutSource::parameterFile;
    prepare.verifyLoader = verify_loader;
    prepare.verifyPartitionTable = verify_partition_table;
    if (loader_path != nullptr) {
        prepare.loaderPath = loader_path;
    }
    if (prepare.layoutSource ==
        workflow::ImageDownloadLayoutSource::parameterFile) {
        prepare.parameterPath = parameter_path;
    } else {
        workflow::DeviceManagementWorkflow management{*backend};
        auto table = management.readPartitions(port_path, false);
        if (const auto* error =
                std::get_if<workflow::DeviceManagementError>(&table)) {
            out_outcome->error_code = ZZRK_DOWNLOAD_ERROR_LAYOUT_INVALID;
            out_outcome->error_stage = ZZRK_DOWNLOAD_STAGE_VALIDATE_LAYOUT;
            copyString(out_outcome->error_target, error->target);
            setError(deviceErrorMessage(*error));
            return statusForErrorCode(static_cast<int32_t>(error->code));
        }
        const auto& deviceTable =
            std::get<workflow::DevicePartitionTable>(table);
        prepare.deviceSectors = deviceTable.deviceSectors;
        for (const auto& partition : deviceTable.table.partitions) {
            prepare.devicePartitions.push_back(
                {.name = partition.name,
                 .firstLba = partition.firstLba,
                 .sectorCount = partition.sectorCount(),
                 .grow = false});
        }
    }
    for (uint32_t index = 0; index < partition_count; ++index) {
        prepare.partitions.push_back(
            {.target = partitions[index].target,
             .imagePath = partitions[index].image_path,
             .verify = partitions[index].verify});
    }

    workflow::ImageDownloadProgressCallback progress;
    if (progress_fn != nullptr) {
        progress = [progress_fn,
                    user_data](const workflow::ImageDownloadEvent& event) {
            zzrk_download_progress_event cEvent{};
            cEvent.stage = static_cast<int32_t>(event.stage);
            cEvent.target =
                event.target.empty() ? nullptr : event.target.c_str();
            cEvent.completed_bytes = event.completedBytes;
            cEvent.total_bytes = event.totalBytes;
            cEvent.completed_plan_bytes = event.completedPlanBytes;
            cEvent.total_plan_bytes = event.totalPlanBytes;
            cEvent.item_index =
                event.itemIndex.has_value()
                    ? static_cast<int32_t>(*event.itemIndex)
                    : -1;
            progress_fn(&cEvent, user_data);
        };
    }
    workflow::ImageDownloadCancellationCallback cancelled;
    if (cancellation != nullptr) {
        const auto flag = cancellation->flag;
        cancelled = [flag] {
            return flag->load(std::memory_order_relaxed);
        };
    }

    auto fillError = [out_outcome](const workflow::ImageDownloadError& error) {
        out_outcome->error_code = static_cast<int32_t>(error.code);
        out_outcome->error_stage = static_cast<int32_t>(error.stage);
        copyString(out_outcome->error_target, error.target);
        out_outcome->error_item_index =
            error.itemIndex.has_value()
                ? static_cast<int32_t>(*error.itemIndex)
                : -1;
        setError(downloadErrorMessage(error));
        return statusForErrorCode(out_outcome->error_code);
    };

    auto prepared = download.prepare(prepare, progress);
    if (const auto* error =
            std::get_if<workflow::ImageDownloadError>(&prepared)) {
        return fillError(*error);
    }
    workflow::ImageDownloadRequest request{};
    request.portPath = port_path;
    request.reboot = reboot;
    auto result = download.execute(
        request,
        std::get<workflow::PreparedImageDownloadPlan>(std::move(prepared)),
        progress, cancelled);
    if (const auto* error =
            std::get_if<workflow::ImageDownloadError>(&result)) {
        return fillError(*error);
    }
    const auto& success = std::get<workflow::ImageDownloadSuccess>(result);
    out_outcome->error_code = ZZRK_DOWNLOAD_ERROR_NONE;
    out_outcome->device_sectors = success.deviceSectors;
    out_outcome->partition_bytes_written = success.partitionBytesWritten;
    out_outcome->sectors_erased = success.sectorsErased;
    out_outcome->items_written = success.itemsWritten;
    out_outcome->items_verified = success.itemsVerified;
    out_outcome->loader_written = success.loaderWritten;
    out_outcome->partition_table_written = success.partitionTableWritten;
    out_outcome->reset_accepted = success.resetAccepted;
    return ZZRK_OK;
}

zzrk_status zzrk_prepare_image_download(
    const char* port_path, const int32_t layout_source,
    const char* parameter_path, const char* loader_path,
    const bool verify_loader, const bool verify_partition_table,
    const zzrk_download_partition* partitions, const uint32_t partition_count,
    zzrk_download_plan_item* items, const uint32_t capacity,
    uint32_t* out_count, zzrk_partition_entry* layout,
    const uint32_t layout_capacity, uint32_t* out_layout_count)
{
    clearError();
    if (layout_source != ZZRK_DOWNLOAD_LAYOUT_DEVICE_GPT &&
        layout_source != ZZRK_DOWNLOAD_LAYOUT_PARAMETER_FILE) {
        setError("download prepare failed: layout_source is out of range");
        return ZZRK_ERR_INVALID_ARGUMENT;
    }
    if (layout_source == ZZRK_DOWNLOAD_LAYOUT_PARAMETER_FILE &&
        parameter_path == nullptr) {
        setError("download prepare failed: parameter_path must not be null "
                 "when the layout source is a parameter file");
        return ZZRK_ERR_INVALID_ARGUMENT;
    }
    if (layout_source == ZZRK_DOWNLOAD_LAYOUT_DEVICE_GPT &&
        port_path == nullptr) {
        setError("download prepare failed: port_path must not be null when "
                 "the layout source is the device GPT");
        return ZZRK_ERR_INVALID_ARGUMENT;
    }
    if (partitions == nullptr && partition_count != 0) {
        setError("download prepare failed: partitions must not be null when "
                 "partition_count is nonzero");
        return ZZRK_ERR_INVALID_ARGUMENT;
    }
    for (uint32_t index = 0; index < partition_count; ++index) {
        if (partitions[index].target == nullptr ||
            partitions[index].image_path == nullptr) {
            setError("download prepare failed: partition entries require "
                     "non-null target and image_path");
            return ZZRK_ERR_INVALID_ARGUMENT;
        }
    }
    if (items == nullptr && capacity != 0) {
        setError("download prepare failed: items must not be null when "
                 "capacity is nonzero");
        return ZZRK_ERR_INVALID_ARGUMENT;
    }
    if (out_count == nullptr) {
        setError("download prepare failed: out_count must not be null");
        return ZZRK_ERR_INVALID_ARGUMENT;
    }
    *out_count = 0;
    if (layout != nullptr && out_layout_count == nullptr) {
        setError("download prepare failed: out_layout_count must not be null "
                 "when layout is provided");
        return ZZRK_ERR_INVALID_ARGUMENT;
    }
    if (out_layout_count != nullptr) {
        *out_layout_count = 0;
    }

    auto backend = workflow::createLibusbFirmwareUpgradeBackend();
    workflow::FirmwareMd5Verifier verifier;
    workflow::ImageDownloadWorkflow download{*backend, verifier};

    workflow::ImageDownloadPrepareRequest prepare{};
    prepare.layoutSource =
        layout_source == ZZRK_DOWNLOAD_LAYOUT_DEVICE_GPT
            ? workflow::ImageDownloadLayoutSource::deviceGpt
            : workflow::ImageDownloadLayoutSource::parameterFile;
    prepare.verifyLoader = verify_loader;
    prepare.verifyPartitionTable = verify_partition_table;
    if (loader_path != nullptr) {
        prepare.loaderPath = loader_path;
    }
    if (prepare.layoutSource ==
        workflow::ImageDownloadLayoutSource::parameterFile) {
        prepare.parameterPath = parameter_path;
    } else {
        workflow::DeviceManagementWorkflow management{*backend};
        auto table = management.readPartitions(port_path, false);
        if (const auto* error =
                std::get_if<workflow::DeviceManagementError>(&table)) {
            setError(deviceErrorMessage(*error));
            return statusForErrorCode(static_cast<int32_t>(error->code));
        }
        const auto& deviceTable =
            std::get<workflow::DevicePartitionTable>(table);
        prepare.deviceSectors = deviceTable.deviceSectors;
        for (const auto& partition : deviceTable.table.partitions) {
            prepare.devicePartitions.push_back(
                {.name = partition.name,
                 .firstLba = partition.firstLba,
                 .sectorCount = partition.sectorCount(),
                 .grow = false});
        }
    }
    for (uint32_t index = 0; index < partition_count; ++index) {
        prepare.partitions.push_back(
            {.target = partitions[index].target,
             .imagePath = partitions[index].image_path,
             .verify = partitions[index].verify});
    }

    auto prepared = download.prepare(prepare, {});
    if (const auto* error =
            std::get_if<workflow::ImageDownloadError>(&prepared)) {
        setError(downloadErrorMessage(*error));
        return statusForErrorCode(static_cast<int32_t>(error->code));
    }
    const auto& plan =
        std::get<workflow::PreparedImageDownloadPlan>(prepared);
    *out_count = static_cast<uint32_t>(plan.items.size());
    const auto count = std::min<std::size_t>(plan.items.size(), capacity);
    for (std::size_t index = 0; index < count; ++index) {
        const auto& item = plan.items[index];
        auto& out = items[index];
        out = zzrk_download_plan_item{};
        out.kind = static_cast<int32_t>(item.kind);
        out.format = static_cast<int32_t>(item.format);
        copyString(out.target, item.target);
        copyString(out.source_path, item.sourcePath.string());
        out.first_lba = item.firstLba;
        out.sector_count = item.sectorCount;
        out.output_size = item.outputSize;
        out.verify = item.verify;
        copyString(out.md5, item.md5.data());
    }
    if (out_layout_count != nullptr) {
        *out_layout_count =
            static_cast<uint32_t>(plan.layout.partitions.size());
        if (layout != nullptr) {
            const auto layoutCount =
                std::min<std::size_t>(plan.layout.partitions.size(),
                                      layout_capacity);
            for (std::size_t index = 0; index < layoutCount; ++index) {
                const auto& partition = plan.layout.partitions[index];
                auto& entry = layout[index];
                entry = zzrk_partition_entry{};
                copyString(entry.name, partition.name);
                entry.first_lba = partition.firstLba;
                entry.sector_count = partition.sectorCount.value_or(0);
                entry.attributes = 0;
            }
        }
    }
    return ZZRK_OK;
}

} // extern "C"
