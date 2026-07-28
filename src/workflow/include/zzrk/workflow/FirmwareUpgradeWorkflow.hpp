// SPDX-License-Identifier: MIT

#pragma once

#include <zzrk/image/RkFirmwareFlasher.hpp>
#include <zzrk/image/RkFirmwareImage.hpp>
#include <zzrk/image/RkMaskromLoader.hpp>
#include <zzrk/session/RockusbSession.hpp>
#include <zzrk/transport/IFlashTransport.hpp>
#include <zzrk/transport/UsbDeviceScanner.hpp>

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

namespace zzrk::workflow {

enum class FirmwareDigestErrorCode {
    openFailed,
    readFailed,
    mismatch,
};

struct FirmwareDigestError {
    FirmwareDigestErrorCode code{};
    std::array<char, 33> actualMd5{};
};

using FirmwareDigestResult =
    std::variant<std::array<char, 33>, FirmwareDigestError>;

class IFirmwareDigestVerifier {
  public:
    virtual ~IFirmwareDigestVerifier() = default;

    [[nodiscard]] virtual FirmwareDigestResult
    verify(image::RkFirmwareImage& firmware) noexcept = 0;
};

enum class FirmwareUpgradeOpenErrorCode {
    invalidSelection,
    initializationFailed,
    enumerationFailed,
    topologyReadFailed,
    descriptorReadFailed,
    deviceNotFound,
    deviceAmbiguous,
    configurationReadFailed,
    interfaceNotFound,
    interfaceAmbiguous,
    invalidEndpointLayout,
    openFailed,
    driverQueryFailed,
    driverActive,
    claimFailed,
    resourceExhausted,
};

struct FirmwareUpgradeOpenError {
    FirmwareUpgradeOpenErrorCode code{};
    int nativeCode{};
    bool accessDenied{};
};

using FirmwareUpgradeOpenResult =
    std::variant<std::unique_ptr<transport::IFlashTransport>,
                 FirmwareUpgradeOpenError>;

class IFirmwareUpgradeBackend {
  public:
    virtual ~IFirmwareUpgradeBackend() = default;

    [[nodiscard]] virtual transport::ScanResult scanDevices() = 0;

    [[nodiscard]] virtual FirmwareUpgradeOpenResult
    openDevice(const transport::UsbDeviceInfo& device) = 0;

    virtual void wait(std::chrono::milliseconds duration) = 0;
};

[[nodiscard]] std::unique_ptr<IFirmwareUpgradeBackend>
createLibusbFirmwareUpgradeBackend();

enum class FirmwareUpgradeStage {
    parseImage,
    verifyImage,
    planImage,
    selectDevice,
    enterMaskrom,
    waitForMaskrom,
    downloadLoader,
    waitForRamLoader,
    checkRamLoader,
    checkStorage,
    flash,
    reset,
};

struct FirmwareUpgradeEvent {
    FirmwareUpgradeStage stage{};
    std::string target;
    std::uint64_t completedBytes{};
    std::uint64_t totalBytes{};
    std::optional<image::FirmwareFlashPhase> flashPhase;
};

using FirmwareUpgradeProgressCallback =
    std::function<void(const FirmwareUpgradeEvent&)>;
using FirmwareUpgradeCancellationCallback = std::function<bool()>;

struct PreparedFirmwareUpgrade {
    image::RkFirmwareImage image;
    image::RkFirmwareFlashPlan plan;
    std::array<char, 33> md5{};
};

enum class FirmwareUpgradeErrorCode {
    cancelled,
    imageInvalid,
    digestFailed,
    planInvalid,
    scanFailed,
    deviceNotFound,
    deviceAmbiguous,
    deviceUnsupported,
    openFailed,
    loaderResetFailed,
    reenumerationTimedOut,
    loaderDownloadFailed,
    loaderNotReady,
    wrongChip,
    storageCheckFailed,
    storageNotEmmc,
    deviceTooSmall,
    flashFailed,
    resetFailed,
    closeFailed,
};

struct FirmwareUpgradeError {
    FirmwareUpgradeErrorCode code{};
    FirmwareUpgradeStage stage{};
    std::string target;
    std::uint64_t completedBytes{};
    std::optional<image::FirmwareImageError> imageError;
    std::optional<FirmwareDigestError> digestError;
    std::optional<image::FirmwarePlanError> planError;
    std::optional<transport::ScanError> scanError;
    std::optional<FirmwareUpgradeOpenError> openError;
    std::optional<image::MaskromLoaderError> loaderError;
    std::optional<session::SessionError> sessionError;
    std::optional<image::FirmwareFlashError> flashError;
};

struct FirmwareUpgradeRequest {
    std::string portPath;
    std::filesystem::path imagePath;
    image::FirmwareVerifyMode verifyMode{image::FirmwareVerifyMode::critical};
    bool reboot{true};
    bool prepareOnly{};
};

struct FirmwareUpgradeSuccess {
    std::string portPath;
    std::string model;
    std::array<char, 33> firmwareMd5{};
    std::uint64_t deviceSectors{};
    std::uint64_t partitionBytesWritten{};
    std::uint64_t sectorsErased{};
    std::uint32_t partitionsVerified{};
    bool preparedOnly{};
    bool partitionTableWritten{};
    bool partitionTableVerified{};
    bool loaderWritten{};
    bool loaderVerified{};
    bool resetAccepted{};
};

using PreparedFirmwareUpgradeResult =
    std::variant<PreparedFirmwareUpgrade, FirmwareUpgradeError>;
using FirmwareUpgradeResult =
    std::variant<FirmwareUpgradeSuccess, FirmwareUpgradeError>;

class FirmwareUpgradeWorkflow final {
  public:
    FirmwareUpgradeWorkflow(IFirmwareUpgradeBackend& backend,
                            IFirmwareDigestVerifier& digestVerifier) noexcept;

    [[nodiscard]] PreparedFirmwareUpgradeResult prepare(
        const std::filesystem::path& imagePath,
        const FirmwareUpgradeProgressCallback& progress = {});

    [[nodiscard]] FirmwareUpgradeResult execute(
        const FirmwareUpgradeRequest& request,
        PreparedFirmwareUpgrade prepared,
        const FirmwareUpgradeProgressCallback& progress = {},
        const FirmwareUpgradeCancellationCallback& cancellation = {});

    [[nodiscard]] FirmwareUpgradeResult run(
        const FirmwareUpgradeRequest& request,
        const FirmwareUpgradeProgressCallback& progress = {},
        const FirmwareUpgradeCancellationCallback& cancellation = {});

  private:
    IFirmwareUpgradeBackend& backend_;
    IFirmwareDigestVerifier& digestVerifier_;
};

[[nodiscard]] constexpr std::string_view
toString(FirmwareUpgradeStage stage) noexcept
{
    switch (stage) {
    case FirmwareUpgradeStage::parseImage:
        return "parse-image";
    case FirmwareUpgradeStage::verifyImage:
        return "verify-image";
    case FirmwareUpgradeStage::planImage:
        return "plan-image";
    case FirmwareUpgradeStage::selectDevice:
        return "select-device";
    case FirmwareUpgradeStage::enterMaskrom:
        return "enter-maskrom";
    case FirmwareUpgradeStage::waitForMaskrom:
        return "wait-maskrom";
    case FirmwareUpgradeStage::downloadLoader:
        return "download-loader";
    case FirmwareUpgradeStage::waitForRamLoader:
        return "wait-ram-loader";
    case FirmwareUpgradeStage::checkRamLoader:
        return "check-ram-loader";
    case FirmwareUpgradeStage::checkStorage:
        return "check-storage";
    case FirmwareUpgradeStage::flash:
        return "flash";
    case FirmwareUpgradeStage::reset:
        return "reset";
    }
    return "unknown";
}

} // namespace zzrk::workflow
