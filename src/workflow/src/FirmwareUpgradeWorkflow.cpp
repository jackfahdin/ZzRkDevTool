// SPDX-License-Identifier: MIT

#include <zzrk/workflow/FirmwareUpgradeWorkflow.hpp>

#include <zzrk/session/ReadChipInfo.hpp>
#include <zzrk/session/ReadFlashId.hpp>
#include <zzrk/session/ReadFlashInfo.hpp>
#include <zzrk/session/ResetDevice.hpp>
#include <zzrk/session/TestUnitReady.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <ranges>
#include <span>
#include <utility>

namespace zzrk::workflow {
namespace {

using namespace std::chrono_literals;

constexpr std::uint16_t kRockchipVendorId = 0x2207U;
constexpr std::uint16_t kRk3588ProductId = 0x350bU;
constexpr std::uint16_t kLoaderBcdUsb = 0x0201U;
constexpr std::uint16_t kLoaderBcdDevice = 0x0223U;
constexpr std::uint16_t kMaskromBcdUsb = 0x0200U;
constexpr std::uint16_t kMaskromBcdDevice = 0x0100U;
constexpr std::uint8_t kUsbplugBulkOutEndpoint = 0x01U;
constexpr auto kReenumerationTimeout = 15s;

void report(const FirmwareUpgradeProgressCallback& progress,
            const FirmwareUpgradeStage stage, const std::string_view target,
            const std::uint64_t completed = 0,
            const std::uint64_t total = 0,
            const std::optional<image::FirmwareFlashPhase> flashPhase = {})
{
    if (progress) {
        progress(FirmwareUpgradeEvent{
            .stage = stage,
            .target = std::string{target},
            .completedBytes = completed,
            .totalBytes = total,
            .flashPhase = flashPhase,
        });
    }
}

[[nodiscard]] bool cancelled(
    const FirmwareUpgradeCancellationCallback& cancellation)
{
    return cancellation && cancellation();
}

[[nodiscard]] FirmwareUpgradeError
makeError(const FirmwareUpgradeErrorCode code,
          const FirmwareUpgradeStage stage, const std::string_view target)
{
    FirmwareUpgradeError error{};
    error.code = code;
    error.stage = stage;
    error.target = target;
    return error;
}

[[nodiscard]] bool isSupportedFlashDevice(
    const transport::UsbDeviceInfo& device) noexcept
{
    if (device.vendorId != kRockchipVendorId ||
        device.productId != kRk3588ProductId) {
        return false;
    }
    if (device.mode == transport::DeviceMode::Loader) {
        return device.bcdUsb == kLoaderBcdUsb &&
               device.bcdDevice == kLoaderBcdDevice;
    }
    return device.mode == transport::DeviceMode::Maskrom &&
           device.bcdUsb == kMaskromBcdUsb &&
           device.bcdDevice == kMaskromBcdDevice;
}

[[nodiscard]] bool isRk3588ChipInfo(
    const session::ChipInfo& chipInfo) noexcept
{
    constexpr std::array expected{std::byte{'8'}, std::byte{'8'},
                                  std::byte{'5'}, std::byte{'3'}};
    return std::ranges::equal(
        expected, std::span{chipInfo.bytes}.first(expected.size()));
}

[[nodiscard]] std::variant<transport::UsbDeviceInfo, FirmwareUpgradeError>
selectDevice(const transport::ScanResult& scan,
             const std::string_view portPath)
{
    if (scan.error.has_value()) {
        auto error = makeError(FirmwareUpgradeErrorCode::scanFailed,
                               FirmwareUpgradeStage::selectDevice, portPath);
        error.scanError = scan.error;
        return error;
    }

    const transport::UsbDeviceInfo* selected = nullptr;
    for (const auto& device : scan.devices) {
        if (device.portPath != portPath) {
            continue;
        }
        if (selected != nullptr) {
            return makeError(FirmwareUpgradeErrorCode::deviceAmbiguous,
                             FirmwareUpgradeStage::selectDevice, portPath);
        }
        selected = &device;
    }
    if (selected == nullptr) {
        return makeError(FirmwareUpgradeErrorCode::deviceNotFound,
                         FirmwareUpgradeStage::selectDevice, portPath);
    }
    if (!isSupportedFlashDevice(*selected)) {
        return makeError(FirmwareUpgradeErrorCode::deviceUnsupported,
                         FirmwareUpgradeStage::selectDevice, portPath);
    }
    return *selected;
}

[[nodiscard]] std::variant<transport::UsbDeviceInfo, FirmwareUpgradeError>
waitForDevice(IFirmwareUpgradeBackend& backend,
              const std::string_view portPath,
              const transport::DeviceMode expectedMode,
              const std::optional<std::uint8_t> previousAddress,
              const FirmwareUpgradeStage stage,
              const FirmwareUpgradeCancellationCallback& cancellation)
{
    const auto deadline = std::chrono::steady_clock::now() +
                          kReenumerationTimeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (cancelled(cancellation)) {
            return makeError(FirmwareUpgradeErrorCode::cancelled, stage,
                             portPath);
        }
        auto scan = backend.scanDevices();
        if (scan.error.has_value()) {
            auto error = makeError(FirmwareUpgradeErrorCode::scanFailed, stage,
                                   portPath);
            error.scanError = scan.error;
            return error;
        }
        const transport::UsbDeviceInfo* found = nullptr;
        for (const auto& device : scan.devices) {
            if (device.portPath != portPath) {
                continue;
            }
            if (found != nullptr) {
                return makeError(FirmwareUpgradeErrorCode::deviceAmbiguous,
                                 stage, portPath);
            }
            found = &device;
        }
        if (found != nullptr && found->mode == expectedMode &&
            (!previousAddress.has_value() ||
             found->deviceAddress != *previousAddress)) {
            if (!isSupportedFlashDevice(*found)) {
                return makeError(FirmwareUpgradeErrorCode::deviceUnsupported,
                                 stage, portPath);
            }
            return *found;
        }
        backend.wait(100ms);
    }
    return makeError(FirmwareUpgradeErrorCode::reenumerationTimedOut, stage,
                     portPath);
}

using PreparedTransport =
    std::variant<std::unique_ptr<transport::IFlashTransport>,
                 FirmwareUpgradeError>;

[[nodiscard]] PreparedTransport openDevice(
    IFirmwareUpgradeBackend& backend,
    const transport::UsbDeviceInfo& device,
    const FirmwareUpgradeStage stage)
{
    auto opened = backend.openDevice(device);
    if (const auto* error =
            std::get_if<FirmwareUpgradeOpenError>(&opened)) {
        auto result = makeError(FirmwareUpgradeErrorCode::openFailed, stage,
                                device.portPath);
        result.openError = *error;
        return result;
    }
    return std::move(
        std::get<std::unique_ptr<transport::IFlashTransport>>(opened));
}

[[nodiscard]] PreparedTransport prepareRamLoader(
    IFirmwareUpgradeBackend& backend,
    const std::string_view portPath,
    const transport::UsbDeviceInfo& initialDevice,
    const image::RkFirmwareImage& image,
    const FirmwareUpgradeProgressCallback& progress,
    const FirmwareUpgradeCancellationCallback& cancellation)
{
    transport::UsbDeviceInfo maskromDevice = initialDevice;
    if (initialDevice.mode == transport::DeviceMode::Loader) {
        report(progress, FirmwareUpgradeStage::enterMaskrom, portPath);
        auto opened = openDevice(backend, initialDevice,
                                 FirmwareUpgradeStage::enterMaskrom);
        if (const auto* error = std::get_if<FirmwareUpgradeError>(&opened)) {
            return *error;
        }
        auto loaderTransport = std::move(
            std::get<std::unique_ptr<transport::IFlashTransport>>(opened));
        session::RockusbSession loaderSession{*loaderTransport};
        const auto reset = session::resetDevice(loaderSession,
                                                session::ResetMode::maskrom);
        if (const auto* error = std::get_if<session::SessionError>(&reset)) {
            auto result = makeError(FirmwareUpgradeErrorCode::loaderResetFailed,
                                    FirmwareUpgradeStage::enterMaskrom,
                                    portPath);
            result.sessionError = *error;
            static_cast<void>(loaderTransport->close());
            return result;
        }
        static_cast<void>(loaderTransport->close());
        loaderTransport.reset();

        report(progress, FirmwareUpgradeStage::waitForMaskrom, portPath);
        auto reenumerated = waitForDevice(
            backend, portPath, transport::DeviceMode::Maskrom,
            initialDevice.deviceAddress,
            FirmwareUpgradeStage::waitForMaskrom, cancellation);
        if (const auto* error =
                std::get_if<FirmwareUpgradeError>(&reenumerated)) {
            return *error;
        }
        maskromDevice =
            std::get<transport::UsbDeviceInfo>(std::move(reenumerated));
    }

    auto opened = openDevice(backend, maskromDevice,
                             FirmwareUpgradeStage::checkRamLoader);
    if (const auto* error = std::get_if<FirmwareUpgradeError>(&opened)) {
        return *error;
    }
    auto maskromTransport = std::move(
        std::get<std::unique_ptr<transport::IFlashTransport>>(opened));

    const bool mayBeActiveUsbplug =
        maskromTransport->interfaceInfo().bulkOutEndpoint ==
        kUsbplugBulkOutEndpoint;
    if (mayBeActiveUsbplug) {
        session::SessionTimeouts timeouts{};
        timeouts.command = 1s;
        timeouts.status = 1s;
        session::RockusbSession probe{
            *maskromTransport, timeouts,
            session::StatusResiduePolicy::allowNonZeroOnSuccess};
        const auto ready = session::testUnitReady(probe);
        if (const auto* error = std::get_if<session::SessionError>(&ready)) {
            auto result = makeError(FirmwareUpgradeErrorCode::loaderNotReady,
                                    FirmwareUpgradeStage::checkRamLoader,
                                    portPath);
            result.sessionError = *error;
            static_cast<void>(maskromTransport->close());
            return result;
        }
        const auto chip = session::readChipInfo(probe);
        if (const auto* error = std::get_if<session::SessionError>(&chip)) {
            auto result = makeError(FirmwareUpgradeErrorCode::loaderNotReady,
                                    FirmwareUpgradeStage::checkRamLoader,
                                    portPath);
            result.sessionError = *error;
            static_cast<void>(maskromTransport->close());
            return result;
        }
        if (!isRk3588ChipInfo(std::get<session::ChipInfo>(chip))) {
            static_cast<void>(maskromTransport->close());
            return makeError(FirmwareUpgradeErrorCode::wrongChip,
                             FirmwareUpgradeStage::checkRamLoader, portPath);
        }
        return maskromTransport;
    }

    report(progress, FirmwareUpgradeStage::downloadLoader, "Loader", 0,
           image.loader.has_value() ? image.loader->idBlockSize : 0U);
    const auto loaderProgress = [&](const std::string_view stage,
                                    const std::uint64_t completed,
                                    const std::uint64_t total) {
        report(progress, FirmwareUpgradeStage::downloadLoader, stage,
               completed, total);
    };
    const auto downloaded = image::downloadRkMaskromLoader(
        *maskromTransport, image, loaderProgress, cancellation);
    if (const auto* error =
            std::get_if<image::MaskromLoaderError>(&downloaded)) {
        auto result = makeError(
            error->code == image::MaskromLoaderErrorCode::cancelled
                ? FirmwareUpgradeErrorCode::cancelled
                : FirmwareUpgradeErrorCode::loaderDownloadFailed,
            FirmwareUpgradeStage::downloadLoader, error->stage);
        result.completedBytes = error->completedBytes;
        result.loaderError = *error;
        static_cast<void>(maskromTransport->close());
        return result;
    }

    backend.wait(1s);
    const auto previousAddress = maskromDevice.deviceAddress;
    static_cast<void>(maskromTransport->close());
    maskromTransport.reset();

    report(progress, FirmwareUpgradeStage::waitForRamLoader, portPath);
    auto usbplugDevice = waitForDevice(
        backend, portPath, transport::DeviceMode::Maskrom, previousAddress,
        FirmwareUpgradeStage::waitForRamLoader, cancellation);
    if (const auto* error =
            std::get_if<FirmwareUpgradeError>(&usbplugDevice)) {
        return *error;
    }
    auto usbplugOpened = openDevice(
        backend, std::get<transport::UsbDeviceInfo>(usbplugDevice),
        FirmwareUpgradeStage::checkRamLoader);
    if (const auto* error =
            std::get_if<FirmwareUpgradeError>(&usbplugOpened)) {
        return *error;
    }
    auto usbplugTransport = std::move(
        std::get<std::unique_ptr<transport::IFlashTransport>>(usbplugOpened));
    if (usbplugTransport->interfaceInfo().bulkOutEndpoint !=
        kUsbplugBulkOutEndpoint) {
        static_cast<void>(usbplugTransport->close());
        return makeError(FirmwareUpgradeErrorCode::loaderNotReady,
                         FirmwareUpgradeStage::checkRamLoader, portPath);
    }

    session::SessionTimeouts timeouts{};
    timeouts.status = 120s;
    session::RockusbSession rockusb{
        *usbplugTransport, timeouts,
        session::StatusResiduePolicy::allowNonZeroOnSuccess};
    const auto ready = session::testUnitReady(rockusb);
    if (const auto* error = std::get_if<session::SessionError>(&ready)) {
        auto result = makeError(FirmwareUpgradeErrorCode::loaderNotReady,
                                FirmwareUpgradeStage::checkRamLoader,
                                portPath);
        result.sessionError = *error;
        static_cast<void>(usbplugTransport->close());
        return result;
    }
    const auto chip = session::readChipInfo(rockusb);
    if (const auto* error = std::get_if<session::SessionError>(&chip)) {
        auto result = makeError(FirmwareUpgradeErrorCode::loaderNotReady,
                                FirmwareUpgradeStage::checkRamLoader,
                                portPath);
        result.sessionError = *error;
        static_cast<void>(usbplugTransport->close());
        return result;
    }
    if (!isRk3588ChipInfo(std::get<session::ChipInfo>(chip))) {
        static_cast<void>(usbplugTransport->close());
        return makeError(FirmwareUpgradeErrorCode::wrongChip,
                         FirmwareUpgradeStage::checkRamLoader, portPath);
    }
    return usbplugTransport;
}

} // namespace

FirmwareUpgradeWorkflow::FirmwareUpgradeWorkflow(
    IFirmwareUpgradeBackend& backend,
    IFirmwareDigestVerifier& digestVerifier) noexcept
    : backend_(backend), digestVerifier_(digestVerifier)
{
}

PreparedFirmwareUpgradeResult FirmwareUpgradeWorkflow::prepare(
    const std::filesystem::path& imagePath,
    const FirmwareUpgradeProgressCallback& progress)
{
    report(progress, FirmwareUpgradeStage::parseImage, imagePath.string());
    auto parsed = image::parseRkFirmwareImage(imagePath);
    if (const auto* error = std::get_if<image::FirmwareImageError>(&parsed)) {
        auto result = makeError(FirmwareUpgradeErrorCode::imageInvalid,
                                FirmwareUpgradeStage::parseImage,
                                imagePath.string());
        result.imageError = *error;
        return result;
    }
    auto firmware = std::get<image::RkFirmwareImage>(std::move(parsed));

    report(progress, FirmwareUpgradeStage::verifyImage, imagePath.string());
    const auto digest = digestVerifier_.verify(firmware);
    if (const auto* error = std::get_if<FirmwareDigestError>(&digest)) {
        auto result = makeError(FirmwareUpgradeErrorCode::digestFailed,
                                FirmwareUpgradeStage::verifyImage,
                                imagePath.string());
        result.digestError = *error;
        return result;
    }

    report(progress, FirmwareUpgradeStage::planImage, imagePath.string());
    auto planned = image::buildRkFirmwareFlashPlan(firmware);
    if (const auto* error = std::get_if<image::FirmwarePlanError>(&planned)) {
        auto result = makeError(FirmwareUpgradeErrorCode::planInvalid,
                                FirmwareUpgradeStage::planImage,
                                error->target);
        result.planError = *error;
        return result;
    }
    return PreparedFirmwareUpgrade{
        .image = std::move(firmware),
        .plan = std::get<image::RkFirmwareFlashPlan>(std::move(planned)),
        .md5 = std::get<std::array<char, 33>>(digest),
    };
}

FirmwareUpgradeResult FirmwareUpgradeWorkflow::execute(
    const FirmwareUpgradeRequest& request, PreparedFirmwareUpgrade prepared,
    const FirmwareUpgradeProgressCallback& progress,
    const FirmwareUpgradeCancellationCallback& cancellation)
{
    if (cancelled(cancellation)) {
        return makeError(FirmwareUpgradeErrorCode::cancelled,
                         FirmwareUpgradeStage::selectDevice,
                         request.portPath);
    }
    if (!request.imagePath.empty() &&
        request.imagePath != prepared.image.sourcePath) {
        return makeError(FirmwareUpgradeErrorCode::imageInvalid,
                         FirmwareUpgradeStage::verifyImage,
                         request.imagePath.string());
    }

    // A prepared plan can remain visible in the UI before execution. Rehash
    // immediately before device access so a changed source can never be used.
    report(progress, FirmwareUpgradeStage::verifyImage,
           prepared.image.sourcePath.string());
    const auto digest = digestVerifier_.verify(prepared.image);
    if (const auto* error = std::get_if<FirmwareDigestError>(&digest)) {
        auto result = makeError(FirmwareUpgradeErrorCode::digestFailed,
                                FirmwareUpgradeStage::verifyImage,
                                prepared.image.sourcePath.string());
        result.digestError = *error;
        return result;
    }
    if (std::get<std::array<char, 33>>(digest) != prepared.md5) {
        auto result = makeError(FirmwareUpgradeErrorCode::digestFailed,
                                FirmwareUpgradeStage::verifyImage,
                                prepared.image.sourcePath.string());
        result.digestError = FirmwareDigestError{
            .code = FirmwareDigestErrorCode::mismatch,
            .actualMd5 = std::get<std::array<char, 33>>(digest),
        };
        return result;
    }

    report(progress, FirmwareUpgradeStage::selectDevice, request.portPath);
    auto selected = selectDevice(backend_.scanDevices(), request.portPath);
    if (const auto* error = std::get_if<FirmwareUpgradeError>(&selected)) {
        return *error;
    }

    auto preparedTransport = prepareRamLoader(
        backend_, request.portPath,
        std::get<transport::UsbDeviceInfo>(selected), prepared.image, progress,
        cancellation);
    if (const auto* error =
            std::get_if<FirmwareUpgradeError>(&preparedTransport)) {
        return *error;
    }
    auto transport = std::move(
        std::get<std::unique_ptr<transport::IFlashTransport>>(
            preparedTransport));

    session::SessionTimeouts timeouts{};
    timeouts.status = 120s;
    session::RockusbSession rockusb{
        *transport, timeouts,
        session::StatusResiduePolicy::allowNonZeroOnSuccess};

    report(progress, FirmwareUpgradeStage::checkStorage, "eMMC");
    const auto flashId = session::readFlashId(rockusb);
    if (const auto* error = std::get_if<session::SessionError>(&flashId)) {
        auto result = makeError(FirmwareUpgradeErrorCode::storageCheckFailed,
                                FirmwareUpgradeStage::checkStorage, "eMMC");
        result.sessionError = *error;
        static_cast<void>(transport->close());
        return result;
    }
    if (session::classifyFlashId(std::get<session::FlashId>(flashId)) !=
        session::FlashIdKind::emmcMediaMarker) {
        static_cast<void>(transport->close());
        return makeError(FirmwareUpgradeErrorCode::storageNotEmmc,
                         FirmwareUpgradeStage::checkStorage, "eMMC");
    }
    const auto flashInfo = session::readFlashInfo(rockusb);
    if (const auto* error = std::get_if<session::SessionError>(&flashInfo)) {
        auto result = makeError(FirmwareUpgradeErrorCode::storageCheckFailed,
                                FirmwareUpgradeStage::checkStorage, "eMMC");
        result.sessionError = *error;
        static_cast<void>(transport->close());
        return result;
    }
    const auto deviceSectors =
        std::get<session::FlashInfo>(flashInfo).legacy.sectorCount;
    if (deviceSectors < prepared.plan.minimumDeviceSectors) {
        static_cast<void>(transport->close());
        return makeError(FirmwareUpgradeErrorCode::deviceTooSmall,
                         FirmwareUpgradeStage::checkStorage, "eMMC");
    }

    FirmwareUpgradeSuccess success{
        .portPath = request.portPath,
        .model = prepared.image.model,
        .firmwareMd5 = prepared.md5,
        .deviceSectors = deviceSectors,
        .preparedOnly = request.prepareOnly,
    };
    if (request.prepareOnly) {
        if (transport->close() < 0) {
            return makeError(FirmwareUpgradeErrorCode::closeFailed,
                             FirmwareUpgradeStage::checkStorage,
                             request.portPath);
        }
        return success;
    }

    const auto flashProgress = [&](const image::FirmwareFlashProgress& event) {
        report(progress, FirmwareUpgradeStage::flash, event.target,
               event.completedBytes, event.totalBytes, event.phase);
    };
    const image::FirmwareFlashOptions options{
        .verifyMode = request.verifyMode,
    };
    const auto flashed = image::flashRkFirmwareImage(
        rockusb, prepared.image, prepared.plan, deviceSectors, options,
        flashProgress, cancellation);
    if (const auto* error =
            std::get_if<image::FirmwareFlashError>(&flashed)) {
        auto result = makeError(
            error->code == image::FirmwareFlashErrorCode::cancelled
                ? FirmwareUpgradeErrorCode::cancelled
                : FirmwareUpgradeErrorCode::flashFailed,
            FirmwareUpgradeStage::flash, error->target);
        result.completedBytes = error->completedBytes;
        result.flashError = *error;
        if (error->sessionError.has_value()) {
            result.sessionError = error->sessionError;
        }
        static_cast<void>(transport->close());
        return result;
    }
    const auto& flashSuccess =
        std::get<image::FirmwareFlashSuccess>(flashed);
    success.partitionBytesWritten = flashSuccess.partitionBytesWritten;
    success.sectorsErased = flashSuccess.sectorsErased;
    success.partitionsVerified = flashSuccess.partitionsVerified;
    success.partitionTableWritten = flashSuccess.partitionTableWritten;
    success.partitionTableVerified = flashSuccess.partitionTableVerified;
    success.loaderWritten = flashSuccess.loaderWritten;
    success.loaderVerified = flashSuccess.loaderVerified;

    std::optional<session::SessionError> resetError;
    if (request.reboot && !cancelled(cancellation)) {
        report(progress, FirmwareUpgradeStage::reset, request.portPath);
        const auto reset = session::resetDevice(rockusb);
        if (const auto* error = std::get_if<session::SessionError>(&reset)) {
            resetError = *error;
        } else {
            success.resetAccepted = true;
        }
    }

    const int closeResult = transport->close();
    if (resetError.has_value()) {
        auto result = makeError(FirmwareUpgradeErrorCode::resetFailed,
                                FirmwareUpgradeStage::reset,
                                request.portPath);
        result.sessionError = resetError;
        return result;
    }
    if (cancelled(cancellation) && !success.resetAccepted) {
        return makeError(FirmwareUpgradeErrorCode::cancelled,
                         FirmwareUpgradeStage::reset, request.portPath);
    }
    if (closeResult < 0 && !success.resetAccepted) {
        return makeError(FirmwareUpgradeErrorCode::closeFailed,
                         FirmwareUpgradeStage::reset, request.portPath);
    }
    return success;
}

FirmwareUpgradeResult FirmwareUpgradeWorkflow::run(
    const FirmwareUpgradeRequest& request,
    const FirmwareUpgradeProgressCallback& progress,
    const FirmwareUpgradeCancellationCallback& cancellation)
{
    auto prepared = prepare(request.imagePath, progress);
    if (const auto* error = std::get_if<FirmwareUpgradeError>(&prepared)) {
        return *error;
    }
    return execute(request,
                   std::get<PreparedFirmwareUpgrade>(std::move(prepared)),
                   progress, cancellation);
}

} // namespace zzrk::workflow
