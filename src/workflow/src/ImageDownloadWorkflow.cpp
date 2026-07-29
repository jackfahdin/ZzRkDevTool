// SPDX-License-Identifier: MIT

#include <zzrk/workflow/ImageDownloadWorkflow.hpp>

#include "FirmwareUpgradeWorkflowInternal.hpp"

#include <zzrk/session/ReadChipInfo.hpp>
#include <zzrk/session/ReadFlashId.hpp>
#include <zzrk/session/ReadFlashInfo.hpp>
#include <zzrk/session/ResetDevice.hpp>
#include <zzrk/session/TestUnitReady.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <limits>
#include <ranges>
#include <span>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace zzrk::workflow {
namespace {

using namespace std::chrono_literals;

constexpr std::uint32_t kRk3588LoaderChipType = 0x33353838U;
constexpr std::uint64_t kPrimaryAndBackupGptBytes = 67U * 512U;
constexpr std::uint64_t kPrimaryGptBytes = 34U * 512U;

void reportDownload(const ImageDownloadProgressCallback& progress,
                    const ImageDownloadStage stage,
                    const std::string_view target,
                    const std::uint64_t completed = 0U,
                    const std::uint64_t total = 0U,
                    const std::uint64_t completedPlan = 0U,
                    const std::uint64_t totalPlan = 0U,
                    const std::optional<std::size_t> itemIndex = {})
{
    if (progress) {
        progress(ImageDownloadEvent{
            .stage = stage,
            .target = std::string{target},
            .completedBytes = completed,
            .totalBytes = total,
            .completedPlanBytes = completedPlan,
            .totalPlanBytes = totalPlan,
            .itemIndex = itemIndex,
        });
    }
}

[[nodiscard]] ImageDownloadError makeDownloadError(
    const ImageDownloadErrorCode code, const ImageDownloadStage stage,
    const std::string_view target,
    const std::optional<std::size_t> itemIndex = {},
    const std::uint64_t completedPlan = 0U)
{
    ImageDownloadError error{};
    error.code = code;
    error.stage = stage;
    error.target = target;
    error.completedPlanBytes = completedPlan;
    error.itemIndex = itemIndex;
    return error;
}

[[nodiscard]] ImageDownloadStage downloadStage(
    const FirmwareUpgradeStage stage) noexcept
{
    switch (stage) {
    case FirmwareUpgradeStage::enterMaskrom:
        return ImageDownloadStage::enterMaskrom;
    case FirmwareUpgradeStage::waitForMaskrom:
        return ImageDownloadStage::waitForMaskrom;
    case FirmwareUpgradeStage::downloadLoader:
        return ImageDownloadStage::downloadLoader;
    case FirmwareUpgradeStage::waitForRamLoader:
        return ImageDownloadStage::waitForRamLoader;
    case FirmwareUpgradeStage::checkRamLoader:
        return ImageDownloadStage::checkRamLoader;
    case FirmwareUpgradeStage::checkStorage:
        return ImageDownloadStage::checkStorage;
    case FirmwareUpgradeStage::reset:
        return ImageDownloadStage::reset;
    case FirmwareUpgradeStage::selectDevice:
        return ImageDownloadStage::selectDevice;
    case FirmwareUpgradeStage::parseImage:
    case FirmwareUpgradeStage::verifyImage:
    case FirmwareUpgradeStage::planImage:
        return ImageDownloadStage::inspectFiles;
    case FirmwareUpgradeStage::flash:
        return ImageDownloadStage::write;
    }
    return ImageDownloadStage::inspectFiles;
}

[[nodiscard]] ImageDownloadError convertUpgradeError(
    const FirmwareUpgradeError& source)
{
    ImageDownloadErrorCode code = ImageDownloadErrorCode::invalidRequest;
    switch (source.code) {
    case FirmwareUpgradeErrorCode::cancelled:
        code = ImageDownloadErrorCode::cancelled;
        break;
    case FirmwareUpgradeErrorCode::scanFailed:
        code = ImageDownloadErrorCode::scanFailed;
        break;
    case FirmwareUpgradeErrorCode::deviceNotFound:
        code = ImageDownloadErrorCode::deviceNotFound;
        break;
    case FirmwareUpgradeErrorCode::deviceAmbiguous:
        code = ImageDownloadErrorCode::deviceAmbiguous;
        break;
    case FirmwareUpgradeErrorCode::deviceUnsupported:
        code = ImageDownloadErrorCode::deviceUnsupported;
        break;
    case FirmwareUpgradeErrorCode::openFailed:
        code = ImageDownloadErrorCode::openFailed;
        break;
    case FirmwareUpgradeErrorCode::loaderResetFailed:
        code = ImageDownloadErrorCode::loaderResetFailed;
        break;
    case FirmwareUpgradeErrorCode::reenumerationTimedOut:
        code = ImageDownloadErrorCode::reenumerationTimedOut;
        break;
    case FirmwareUpgradeErrorCode::loaderDownloadFailed:
        code = ImageDownloadErrorCode::loaderDownloadFailed;
        break;
    case FirmwareUpgradeErrorCode::loaderNotReady:
        code = ImageDownloadErrorCode::loaderNotReady;
        break;
    case FirmwareUpgradeErrorCode::wrongChip:
        code = ImageDownloadErrorCode::wrongChip;
        break;
    case FirmwareUpgradeErrorCode::storageCheckFailed:
        code = ImageDownloadErrorCode::storageCheckFailed;
        break;
    case FirmwareUpgradeErrorCode::storageNotEmmc:
        code = ImageDownloadErrorCode::storageNotEmmc;
        break;
    case FirmwareUpgradeErrorCode::deviceTooSmall:
        code = ImageDownloadErrorCode::deviceTooSmall;
        break;
    case FirmwareUpgradeErrorCode::flashFailed:
        code = ImageDownloadErrorCode::flashFailed;
        break;
    case FirmwareUpgradeErrorCode::resetFailed:
        code = ImageDownloadErrorCode::resetFailed;
        break;
    case FirmwareUpgradeErrorCode::closeFailed:
        code = ImageDownloadErrorCode::closeFailed;
        break;
    case FirmwareUpgradeErrorCode::imageInvalid:
    case FirmwareUpgradeErrorCode::digestFailed:
    case FirmwareUpgradeErrorCode::planInvalid:
        break;
    }
    auto result = makeDownloadError(code, downloadStage(source.stage),
                                    source.target);
    result.scanError = source.scanError;
    result.openError = source.openError;
    result.loaderError = source.loaderError;
    result.sessionError = source.sessionError;
    result.flashError = source.flashError;
    return result;
}

[[nodiscard]] bool validLayout(
    const std::vector<image::FirmwarePartition>& partitions,
    const std::uint64_t deviceSectors,
    std::uint64_t& minimumDeviceSectors) noexcept
{
    if (partitions.empty()) {
        return false;
    }
    minimumDeviceSectors = 68U;
    std::unordered_set<std::string> names;
    for (std::size_t index = 0; index < partitions.size(); ++index) {
        const auto& partition = partitions[index];
        if (partition.name.empty() || partition.firstLba < 34U
            || !names.insert(partition.name).second
            || (partition.grow && index + 1U != partitions.size())) {
            return false;
        }
        std::uint64_t end = partition.firstLba;
        if (partition.sectorCount.has_value()) {
            if (*partition.sectorCount == 0U
                || partition.firstLba
                       > std::numeric_limits<std::uint64_t>::max()
                             - *partition.sectorCount) {
                return false;
            }
            end = partition.firstLba + *partition.sectorCount;
        } else if (!partition.grow) {
            return false;
        }
        if (index + 1U < partitions.size()
            && (end > partitions[index + 1U].firstLba
                || !partition.sectorCount.has_value())) {
            return false;
        }
        const std::uint64_t required = partition.sectorCount.has_value()
            ? end + 33U
            : partition.firstLba + 34U;
        if (required < end ||
            (deviceSectors != 0U && required > deviceSectors)) {
            return false;
        }
        minimumDeviceSectors = std::max(minimumDeviceSectors, required);
    }
    return true;
}

[[nodiscard]] std::optional<std::uint64_t> resolvedSectorCount(
    const image::FirmwarePartition& partition,
    const std::uint64_t deviceSectors) noexcept
{
    if (partition.sectorCount.has_value()) {
        return partition.sectorCount;
    }
    if (!partition.grow || deviceSectors < 34U
        || partition.firstLba > deviceSectors - 34U) {
        return std::nullopt;
    }
    return deviceSectors - 34U - partition.firstLba + 1U;
}

[[nodiscard]] bool sameDeviceLayout(
    const image::RockchipParameterLayout& expected,
    const storage::GptPartitionTable& actual)
{
    if (expected.partitions.size() != actual.partitions.size()) {
        return false;
    }
    return std::ranges::all_of(expected.partitions, [&](const auto& partition) {
        const auto found = std::ranges::find(
            actual.partitions, partition.name, &storage::GptPartition::name);
        return found != actual.partitions.end()
            && partition.sectorCount.has_value()
            && found->firstLba == partition.firstLba
            && found->sectorCount() == *partition.sectorCount;
    });
}

} // namespace

ImageDownloadWorkflow::ImageDownloadWorkflow(
    IFirmwareUpgradeBackend& backend,
    IFileDigestProvider& digestProvider) noexcept
    : backend_(backend), digestProvider_(digestProvider)
{
}

PreparedImageDownloadResult ImageDownloadWorkflow::prepare(
    const ImageDownloadPrepareRequest& request,
    const ImageDownloadProgressCallback& progress)
{
    PreparedImageDownloadPlan plan{};
    plan.layoutSource = request.layoutSource;
    plan.expectedDeviceSectors = request.deviceSectors;
    std::unordered_map<std::filesystem::path, std::array<char, 33>> digests;
    const auto digestFile = [&](const std::filesystem::path& path) {
        if (const auto found = digests.find(path); found != digests.end()) {
            return FirmwareDigestResult{found->second};
        }
        auto result = digestProvider_.digest(path);
        if (const auto* digest = std::get_if<std::array<char, 33>>(&result)) {
            digests.emplace(path, *digest);
        }
        return result;
    };

    if (request.layoutSource == ImageDownloadLayoutSource::parameterFile) {
        if (request.parameterPath.empty()) {
            return makeDownloadError(ImageDownloadErrorCode::invalidRequest,
                                     ImageDownloadStage::inspectFiles,
                                     "parameter.txt");
        }
        reportDownload(progress, ImageDownloadStage::inspectFiles,
                       request.parameterPath.string());
        auto parsed = image::parseRockchipParameterFile(request.parameterPath);
        if (const auto* error =
                std::get_if<image::FirmwareImageError>(&parsed)) {
            auto result = makeDownloadError(
                ImageDownloadErrorCode::layoutInvalid,
                ImageDownloadStage::inspectFiles,
                request.parameterPath.string());
            result.imageError = *error;
            return result;
        }
        plan.layout = std::get<image::RockchipParameterLayout>(
            std::move(parsed));
        auto digest = digestFile(request.parameterPath);
        if (const auto* error = std::get_if<FirmwareDigestError>(&digest)) {
            auto result = makeDownloadError(
                ImageDownloadErrorCode::digestFailed,
                ImageDownloadStage::verifyFiles,
                request.parameterPath.string());
            result.digestError = *error;
            return result;
        }
        plan.items.push_back(PreparedImageDownloadItem{
            .kind = ImageDownloadItemKind::partitionTable,
            .target = "GPT",
            .sourcePath = request.parameterPath,
            .firstLba = 0U,
            .sectorCount = 67U,
            .outputSize = kPrimaryAndBackupGptBytes,
            .format = image::PayloadFormat::rockchipParameter,
            .verify = request.verifyPartitionTable,
            .md5 = std::get<std::array<char, 33>>(digest),
            .payload = std::nullopt,
            .loader = std::nullopt,
        });
    } else {
        plan.layout.sourcePath.clear();
        plan.layout.model = "device-gpt";
        plan.layout.partitions = request.devicePartitions;
    }

    if (!plan.layout.partitions.empty()) {
        std::ranges::sort(plan.layout.partitions, {},
                          &image::FirmwarePartition::firstLba);
        if (!validLayout(plan.layout.partitions,
                         request.layoutSource
                                 == ImageDownloadLayoutSource::deviceGpt
                             ? request.deviceSectors
                             : 0U,
                         plan.minimumDeviceSectors)) {
            return makeDownloadError(ImageDownloadErrorCode::layoutInvalid,
                                     ImageDownloadStage::inspectFiles, "GPT");
        }
    }

    if (!request.loaderPath.empty()) {
        reportDownload(progress, ImageDownloadStage::inspectFiles,
                       request.loaderPath.string());
        auto parsed = image::parseRkLoaderImage(request.loaderPath);
        if (const auto* error =
                std::get_if<image::FirmwareImageError>(&parsed)) {
            auto result = makeDownloadError(
                ImageDownloadErrorCode::imageInvalid,
                ImageDownloadStage::inspectFiles,
                request.loaderPath.string());
            result.imageError = *error;
            return result;
        }
        auto loader = std::get<image::RkLoaderImage>(std::move(parsed));
        if (loader.info.chipType != kRk3588LoaderChipType) {
            return makeDownloadError(ImageDownloadErrorCode::imageInvalid,
                                     ImageDownloadStage::inspectFiles,
                                     "RK3588 Loader");
        }
        auto digest = digestFile(request.loaderPath);
        if (const auto* error = std::get_if<FirmwareDigestError>(&digest)) {
            auto result = makeDownloadError(
                ImageDownloadErrorCode::digestFailed,
                ImageDownloadStage::verifyFiles,
                request.loaderPath.string());
            result.digestError = *error;
            return result;
        }
        plan.minimumDeviceSectors = std::max(
            plan.minimumDeviceSectors,
            64U + loader.info.idBlockSize / 512U);
        plan.items.insert(plan.items.begin(), PreparedImageDownloadItem{
            .kind = ImageDownloadItemKind::loader,
            .target = "Loader",
            .sourcePath = request.loaderPath,
            .firstLba = 64U,
            .sectorCount = loader.info.idBlockSize / 512U,
            .outputSize = loader.info.idBlockSize,
            .format = image::PayloadFormat::raw,
            .verify = request.verifyLoader,
            .md5 = std::get<std::array<char, 33>>(digest),
            .payload = std::nullopt,
            .loader = std::move(loader),
        });
    }

    std::unordered_set<std::string> targets;
    for (const auto& selection : request.partitions) {
        const std::size_t itemIndex = plan.items.size();
        if (selection.target.empty() || selection.imagePath.empty()
            || !targets.insert(selection.target).second) {
            return makeDownloadError(ImageDownloadErrorCode::invalidRequest,
                                     ImageDownloadStage::inspectFiles,
                                     selection.target, itemIndex);
        }
        const auto partition = std::ranges::find(
            plan.layout.partitions, selection.target,
            &image::FirmwarePartition::name);
        if (partition == plan.layout.partitions.end()) {
            return makeDownloadError(ImageDownloadErrorCode::layoutInvalid,
                                     ImageDownloadStage::inspectFiles,
                                     selection.target, itemIndex);
        }
        reportDownload(progress, ImageDownloadStage::inspectFiles,
                       selection.imagePath.string(), 0U, 0U, 0U, 0U,
                       itemIndex);
        auto inspected = image::inspectFirmwarePayload(selection.imagePath);
        if (const auto* error =
                std::get_if<image::FirmwareImageError>(&inspected)) {
            auto result = makeDownloadError(
                ImageDownloadErrorCode::imageInvalid,
                ImageDownloadStage::inspectFiles, selection.target,
                itemIndex);
            result.imageError = *error;
            return result;
        }
        auto payload = std::get<image::FirmwarePayloadSource>(
            std::move(inspected));
        if (payload.format != image::PayloadFormat::raw
            && payload.format != image::PayloadFormat::androidSparse) {
            return makeDownloadError(ImageDownloadErrorCode::imageInvalid,
                                     ImageDownloadStage::inspectFiles,
                                     selection.target, itemIndex);
        }
        if (partition->sectorCount.has_value()
            && (*partition->sectorCount
                    > std::numeric_limits<std::uint64_t>::max() / 512U
                || payload.logicalSize > *partition->sectorCount * 512U)) {
            return makeDownloadError(ImageDownloadErrorCode::deviceTooSmall,
                                     ImageDownloadStage::inspectFiles,
                                     selection.target, itemIndex);
        }
        auto digest = digestFile(selection.imagePath);
        if (const auto* error = std::get_if<FirmwareDigestError>(&digest)) {
            auto result = makeDownloadError(
                ImageDownloadErrorCode::digestFailed,
                ImageDownloadStage::verifyFiles, selection.target,
                itemIndex);
            result.digestError = *error;
            return result;
        }
        plan.items.push_back(PreparedImageDownloadItem{
            .kind = ImageDownloadItemKind::partition,
            .target = selection.target,
            .sourcePath = selection.imagePath,
            .firstLba = partition->firstLba,
            .sectorCount = partition->sectorCount.value_or(0U),
            .outputSize = payload.logicalSize,
            .format = payload.format,
            .verify = selection.verify,
            .md5 = std::get<std::array<char, 33>>(digest),
            .payload = std::move(payload),
            .loader = std::nullopt,
        });
    }
    if (plan.items.empty()) {
        return makeDownloadError(ImageDownloadErrorCode::invalidRequest,
                                 ImageDownloadStage::inspectFiles, "images");
    }
    if (request.layoutSource == ImageDownloadLayoutSource::deviceGpt
        && !request.partitions.empty()
        && (request.deviceSectors == 0U || plan.layout.partitions.empty())) {
        return makeDownloadError(ImageDownloadErrorCode::layoutInvalid,
                                 ImageDownloadStage::inspectFiles,
                                 "device GPT");
    }
    return plan;
}

ImageDownloadResult ImageDownloadWorkflow::execute(
    const ImageDownloadRequest& request,
    PreparedImageDownloadPlan prepared,
    const ImageDownloadProgressCallback& progress,
    const ImageDownloadCancellationCallback& cancellation)
{
    const auto isCancelled = [&] { return cancellation && cancellation(); };
    if (request.portPath.empty() || isCancelled()) {
        return makeDownloadError(
            isCancelled() ? ImageDownloadErrorCode::cancelled
                          : ImageDownloadErrorCode::invalidRequest,
            ImageDownloadStage::selectDevice, request.portPath);
    }

    std::unordered_map<std::filesystem::path, std::array<char, 33>> digests;
    for (std::size_t index = 0; index < prepared.items.size(); ++index) {
        const auto& item = prepared.items[index];
        reportDownload(progress, ImageDownloadStage::verifyFiles,
                       item.sourcePath.string(), 0U, 0U, 0U, 0U, index);
        FirmwareDigestResult digest;
        if (const auto found = digests.find(item.sourcePath);
            found != digests.end()) {
            digest = found->second;
        } else {
            digest = digestProvider_.digest(item.sourcePath);
            if (const auto* value =
                    std::get_if<std::array<char, 33>>(&digest)) {
                digests.emplace(item.sourcePath, *value);
            }
        }
        if (const auto* error = std::get_if<FirmwareDigestError>(&digest)) {
            auto result = makeDownloadError(
                ImageDownloadErrorCode::digestFailed,
                ImageDownloadStage::verifyFiles, item.target, index);
            result.digestError = *error;
            return result;
        }
        if (std::get<std::array<char, 33>>(digest) != item.md5) {
            return makeDownloadError(ImageDownloadErrorCode::digestChanged,
                                     ImageDownloadStage::verifyFiles,
                                     item.target, index);
        }
    }

    reportDownload(progress, ImageDownloadStage::selectDevice,
                   request.portPath);
    auto selected = detail::selectDevice(backend_.scanDevices(),
                                         request.portPath);
    if (const auto* error = std::get_if<FirmwareUpgradeError>(&selected)) {
        return convertUpgradeError(*error);
    }
    const auto selectedDevice = std::get<transport::UsbDeviceInfo>(selected);
    const auto loaderItem = std::ranges::find(
        prepared.items, ImageDownloadItemKind::loader,
        &PreparedImageDownloadItem::kind);
    if (selectedDevice.mode == transport::DeviceMode::Maskrom
        && loaderItem == prepared.items.end()) {
        return makeDownloadError(ImageDownloadErrorCode::loaderRequired,
                                 ImageDownloadStage::downloadLoader,
                                 request.portPath);
    }

    std::unique_ptr<transport::IFlashTransport> transport;
    if (loaderItem != prepared.items.end()) {
        if (!loaderItem->loader.has_value()) {
            return makeDownloadError(ImageDownloadErrorCode::invalidRequest,
                                     ImageDownloadStage::inspectFiles,
                                     "Loader");
        }
        const FirmwareUpgradeProgressCallback loaderProgress =
            [&](const FirmwareUpgradeEvent& event) {
                reportDownload(progress, downloadStage(event.stage),
                               event.target, event.completedBytes,
                               event.totalBytes);
            };
        auto ready = detail::prepareRamLoader(
            backend_, request.portPath, selectedDevice, &*loaderItem->loader,
            loaderProgress, cancellation);
        if (const auto* error = std::get_if<FirmwareUpgradeError>(&ready)) {
            return convertUpgradeError(*error);
        }
        transport = std::move(
            std::get<std::unique_ptr<transport::IFlashTransport>>(ready));
    } else {
        auto opened = detail::openDevice(
            backend_, selectedDevice, FirmwareUpgradeStage::checkRamLoader);
        if (const auto* error = std::get_if<FirmwareUpgradeError>(&opened)) {
            return convertUpgradeError(*error);
        }
        transport = std::move(
            std::get<std::unique_ptr<transport::IFlashTransport>>(opened));
    }

    session::SessionTimeouts timeouts{};
    timeouts.status = 120s;
    session::RockusbSession rockusb{
        *transport, timeouts,
        session::StatusResiduePolicy::allowNonZeroOnSuccess};
    if (loaderItem == prepared.items.end()) {
        const auto ready = session::testUnitReady(rockusb);
        if (const auto* error = std::get_if<session::SessionError>(&ready)) {
            auto result = makeDownloadError(
                ImageDownloadErrorCode::loaderNotReady,
                ImageDownloadStage::checkRamLoader, request.portPath);
            result.sessionError = *error;
            static_cast<void>(transport->close());
            return result;
        }
        const auto chip = session::readChipInfo(rockusb);
        if (const auto* error = std::get_if<session::SessionError>(&chip)) {
            auto result = makeDownloadError(
                ImageDownloadErrorCode::loaderNotReady,
                ImageDownloadStage::checkRamLoader, request.portPath);
            result.sessionError = *error;
            static_cast<void>(transport->close());
            return result;
        }
        if (!detail::isRk3588ChipInfo(
                std::get<session::ChipInfo>(chip))) {
            static_cast<void>(transport->close());
            return makeDownloadError(ImageDownloadErrorCode::wrongChip,
                                     ImageDownloadStage::checkRamLoader,
                                     request.portPath);
        }
    }

    reportDownload(progress, ImageDownloadStage::checkStorage, "eMMC");
    const auto flashId = session::readFlashId(rockusb);
    if (const auto* error = std::get_if<session::SessionError>(&flashId)) {
        auto result = makeDownloadError(
            ImageDownloadErrorCode::storageCheckFailed,
            ImageDownloadStage::checkStorage, "eMMC");
        result.sessionError = *error;
        static_cast<void>(transport->close());
        return result;
    }
    if (session::classifyFlashId(std::get<session::FlashId>(flashId)) !=
        session::FlashIdKind::emmcMediaMarker) {
        static_cast<void>(transport->close());
        return makeDownloadError(ImageDownloadErrorCode::storageNotEmmc,
                                 ImageDownloadStage::checkStorage, "eMMC");
    }
    const auto flashInfo = session::readFlashInfo(rockusb);
    if (const auto* error = std::get_if<session::SessionError>(&flashInfo)) {
        auto result = makeDownloadError(
            ImageDownloadErrorCode::storageCheckFailed,
            ImageDownloadStage::checkStorage, "eMMC");
        result.sessionError = *error;
        static_cast<void>(transport->close());
        return result;
    }
    const std::uint64_t deviceSectors =
        std::get<session::FlashInfo>(flashInfo).legacy.sectorCount;
    if (deviceSectors < prepared.minimumDeviceSectors
        || deviceSectors > std::numeric_limits<std::uint32_t>::max()) {
        static_cast<void>(transport->close());
        return makeDownloadError(ImageDownloadErrorCode::deviceTooSmall,
                                 ImageDownloadStage::checkStorage, "eMMC");
    }

    reportDownload(progress, ImageDownloadStage::validateLayout, "GPT");
    if (prepared.layoutSource == ImageDownloadLayoutSource::deviceGpt) {
        if (prepared.expectedDeviceSectors != 0U
            && prepared.expectedDeviceSectors != deviceSectors) {
            static_cast<void>(transport->close());
            return makeDownloadError(
                ImageDownloadErrorCode::deviceLayoutChanged,
                ImageDownloadStage::validateLayout, "eMMC capacity");
        }
        if (!prepared.layout.partitions.empty()) {
            const auto table = storage::readDeviceGpt(rockusb, deviceSectors,
                                                      false);
            if (const auto* error = std::get_if<storage::GptError>(&table)) {
                auto result = makeDownloadError(
                    ImageDownloadErrorCode::deviceLayoutChanged,
                    ImageDownloadStage::validateLayout, "GPT");
                result.gptError = *error;
                static_cast<void>(transport->close());
                return result;
            }
            if (!sameDeviceLayout(
                    prepared.layout,
                    std::get<storage::GptPartitionTable>(table))) {
                static_cast<void>(transport->close());
                return makeDownloadError(
                    ImageDownloadErrorCode::deviceLayoutChanged,
                    ImageDownloadStage::validateLayout, "GPT");
            }
        }
    }

    for (auto& item : prepared.items) {
        if (item.kind != ImageDownloadItemKind::partition) {
            continue;
        }
        const auto partition = std::ranges::find(
            prepared.layout.partitions, item.target,
            &image::FirmwarePartition::name);
        if (partition == prepared.layout.partitions.end()) {
            static_cast<void>(transport->close());
            return makeDownloadError(ImageDownloadErrorCode::layoutInvalid,
                                     ImageDownloadStage::validateLayout,
                                     item.target);
        }
        const auto sectors = resolvedSectorCount(*partition, deviceSectors);
        if (!sectors.has_value() || *sectors > deviceSectors
            || item.firstLba > deviceSectors - *sectors
            || *sectors > std::numeric_limits<std::uint64_t>::max() / 512U
            || item.outputSize > *sectors * 512U) {
            static_cast<void>(transport->close());
            return makeDownloadError(ImageDownloadErrorCode::deviceTooSmall,
                                     ImageDownloadStage::validateLayout,
                                     item.target);
        }
        item.sectorCount = *sectors;
    }

    std::uint64_t totalPlan = 0U;
    const auto addWork = [&](const std::uint64_t bytes) {
        if (totalPlan <= std::numeric_limits<std::uint64_t>::max() - bytes) {
            totalPlan += bytes;
            return true;
        }
        return false;
    };
    for (const auto& item : prepared.items) {
        std::uint64_t writeBytes = item.outputSize;
        if (item.kind == ImageDownloadItemKind::partition
            && item.format == image::PayloadFormat::androidSparse) {
            if (item.sectorCount
                > std::numeric_limits<std::uint64_t>::max() / 512U
                || !addWork(item.sectorCount * 512U)) {
                static_cast<void>(transport->close());
                return makeDownloadError(
                    ImageDownloadErrorCode::invalidRequest,
                    ImageDownloadStage::validateLayout, item.target);
            }
        }
        if (!addWork(writeBytes)
            || (item.verify
                && !addWork(item.kind
                                == ImageDownloadItemKind::partitionTable
                            ? kPrimaryGptBytes
                            : item.outputSize))) {
            static_cast<void>(transport->close());
            return makeDownloadError(ImageDownloadErrorCode::invalidRequest,
                                     ImageDownloadStage::validateLayout,
                                     item.target);
        }
    }

    ImageDownloadSuccess success{
        .portPath = request.portPath,
        .deviceSectors = deviceSectors,
    };
    std::uint64_t completedPlan = 0U;
    for (std::size_t index = 0; index < prepared.items.size(); ++index) {
        const auto& item = prepared.items[index];
        const std::uint64_t eraseBytes =
            item.kind == ImageDownloadItemKind::partition
                && item.format == image::PayloadFormat::androidSparse
            ? item.sectorCount * 512U
            : 0U;
        const image::FirmwareFlashProgressCallback flashProgress =
            [&](const image::FirmwareFlashProgress& event) {
                const bool erasing =
                    event.phase == image::FirmwareFlashPhase::erasePartition;
                const std::uint64_t prefix = erasing ? 0U : eraseBytes;
                reportDownload(
                    progress,
                    erasing ? ImageDownloadStage::erase
                            : ImageDownloadStage::write,
                    event.target, event.completedBytes, event.totalBytes,
                    completedPlan + prefix + event.completedBytes,
                    totalPlan, index);
            };
        image::FirmwarePayloadOperationResult operation;
        if (item.kind == ImageDownloadItemKind::loader) {
            operation = image::writeRkLoaderIdBlock(
                rockusb, *item.loader, flashProgress, cancellation);
        } else if (item.kind == ImageDownloadItemKind::partitionTable) {
            operation = image::writeRkPartitionTable(
                rockusb, prepared.layout, deviceSectors, flashProgress,
                cancellation);
        } else {
            operation = image::writeFirmwarePayload(
                rockusb, *item.payload, item.target, item.firstLba,
                item.sectorCount, true, flashProgress, cancellation);
        }
        if (const auto* error =
                std::get_if<image::FirmwareFlashError>(&operation)) {
            auto result = makeDownloadError(
                error->code == image::FirmwareFlashErrorCode::cancelled
                    ? ImageDownloadErrorCode::cancelled
                    : ImageDownloadErrorCode::flashFailed,
                ImageDownloadStage::write, item.target, index,
                completedPlan);
            result.flashError = *error;
            result.sessionError = error->sessionError;
            static_cast<void>(transport->close());
            return result;
        }
        completedPlan += eraseBytes + item.outputSize;
        ++success.itemsWritten;
        if (item.kind == ImageDownloadItemKind::loader) {
            success.loaderWritten = true;
        } else if (item.kind == ImageDownloadItemKind::partitionTable) {
            success.partitionTableWritten = true;
        } else {
            success.partitionBytesWritten += item.outputSize;
            success.sectorsErased +=
                std::get<image::FirmwarePayloadOperationSuccess>(operation)
                    .sectorsErased;
        }
    }

    for (std::size_t index = 0; index < prepared.items.size(); ++index) {
        const auto& item = prepared.items[index];
        if (!item.verify) {
            continue;
        }
        const image::FirmwareFlashProgressCallback verifyProgress =
            [&](const image::FirmwareFlashProgress& event) {
                reportDownload(progress, ImageDownloadStage::verify,
                               event.target, event.completedBytes,
                               event.totalBytes,
                               completedPlan + event.completedBytes,
                               totalPlan, index);
            };
        image::FirmwarePayloadOperationResult operation;
        if (item.kind == ImageDownloadItemKind::loader) {
            operation = image::verifyRkLoaderIdBlock(
                rockusb, *item.loader, verifyProgress, cancellation);
        } else if (item.kind == ImageDownloadItemKind::partitionTable) {
            operation = image::verifyRkPartitionTable(
                rockusb, prepared.layout, deviceSectors, verifyProgress,
                cancellation);
        } else {
            operation = image::verifyFirmwarePayload(
                rockusb, *item.payload, item.target, item.firstLba,
                item.sectorCount, verifyProgress, cancellation);
        }
        if (const auto* error =
                std::get_if<image::FirmwareFlashError>(&operation)) {
            auto result = makeDownloadError(
                error->code == image::FirmwareFlashErrorCode::cancelled
                    ? ImageDownloadErrorCode::cancelled
                    : ImageDownloadErrorCode::flashFailed,
                ImageDownloadStage::verify, item.target, index,
                completedPlan);
            result.flashError = *error;
            result.sessionError = error->sessionError;
            static_cast<void>(transport->close());
            return result;
        }
        completedPlan += item.kind == ImageDownloadItemKind::partitionTable
            ? kPrimaryGptBytes
            : item.outputSize;
        ++success.itemsVerified;
    }

    std::optional<session::SessionError> resetError;
    if (request.reboot && !isCancelled()) {
        reportDownload(progress, ImageDownloadStage::reset,
                       request.portPath, 0U, 0U, completedPlan, totalPlan);
        const auto reset = session::resetDevice(rockusb);
        if (const auto* error = std::get_if<session::SessionError>(&reset)) {
            resetError = *error;
        } else {
            success.resetAccepted = true;
        }
    }
    const int closeResult = transport->close();
    if (resetError.has_value()) {
        auto result = makeDownloadError(ImageDownloadErrorCode::resetFailed,
                                        ImageDownloadStage::reset,
                                        request.portPath);
        result.sessionError = resetError;
        return result;
    }
    if (isCancelled() && !success.resetAccepted) {
        return makeDownloadError(ImageDownloadErrorCode::cancelled,
                                 ImageDownloadStage::reset,
                                 request.portPath, {}, completedPlan);
    }
    if (closeResult < 0 && !success.resetAccepted) {
        return makeDownloadError(ImageDownloadErrorCode::closeFailed,
                                 ImageDownloadStage::reset,
                                 request.portPath, {}, completedPlan);
    }
    return success;
}

} // namespace zzrk::workflow
