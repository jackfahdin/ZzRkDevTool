// SPDX-License-Identifier: MIT

#include <zzrk/workflow/DeviceManagementWorkflow.hpp>

#include <zzrk/core/StorageUnits.hpp>
#include <zzrk/session/EraseLba.hpp>
#include <zzrk/session/ReadCapability.hpp>
#include <zzrk/session/ReadChipInfo.hpp>
#include <zzrk/session/ReadFlashId.hpp>
#include <zzrk/session/ReadFlashInfo.hpp>
#include <zzrk/session/ReadLba.hpp>
#include <zzrk/session/WriteLba.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <memory>
#include <ranges>
#include <span>
#include <utility>
#include <variant>
#include <vector>

namespace zzrk::workflow {
namespace {

constexpr std::uint16_t kRockchipVendorId = 0x2207U;
constexpr std::uint16_t kRk3588ProductId = 0x350bU;
constexpr std::uint16_t kLoaderBcdUsb = 0x0201U;
constexpr std::uint16_t kLoaderBcdDevice = 0x0223U;

struct EmmcLoaderContext {
    transport::UsbDeviceInfo device;
    std::unique_ptr<transport::IFlashTransport> transport;
    std::uint64_t deviceSectors{};
};

struct NamedPartitionContext {
    EmmcLoaderContext loader;
    storage::GptPartitionTable table;
    std::size_t partitionIndex{};
};

using LoaderResult = std::variant<transport::UsbDeviceInfo,
                                  DeviceManagementError>;
using EmmcLoaderResult =
    std::variant<EmmcLoaderContext, DeviceManagementError>;
using NamedPartitionResult =
    std::variant<NamedPartitionContext, DeviceManagementError>;

[[nodiscard]] DeviceManagementError makeError(
    const DeviceManagementErrorCode code, const std::string_view target)
{
    return DeviceManagementError{
        .code = code,
        .target = std::string{target},
        .completedBytes = 0U,
        .totalBytes = 0U,
        .scanError = {},
        .openError = {},
        .sessionError = {},
        .gptError = {},
    };
}

[[nodiscard]] bool isSupportedLoader(
    const transport::UsbDeviceInfo& device) noexcept
{
    return device.vendorId == kRockchipVendorId &&
           device.productId == kRk3588ProductId &&
           device.bcdUsb == kLoaderBcdUsb &&
           device.bcdDevice == kLoaderBcdDevice &&
           device.mode == transport::DeviceMode::Loader;
}

[[nodiscard]] LoaderResult selectLoader(IFirmwareUpgradeBackend& backend,
                                        const std::string_view portPath)
{
    const auto scan = backend.scanDevices();
    if (scan.error.has_value()) {
        auto error = makeError(DeviceManagementErrorCode::scanFailed,
                               portPath);
        error.scanError = scan.error;
        return error;
    }

    const transport::UsbDeviceInfo* selected = nullptr;
    for (const auto& device : scan.devices) {
        if (device.portPath != portPath) {
            continue;
        }
        if (selected != nullptr) {
            return makeError(DeviceManagementErrorCode::deviceAmbiguous,
                             portPath);
        }
        selected = &device;
    }
    if (selected == nullptr) {
        return makeError(DeviceManagementErrorCode::deviceNotFound, portPath);
    }
    if (!isSupportedLoader(*selected)) {
        return makeError(DeviceManagementErrorCode::deviceUnsupported,
                         portPath);
    }
    return *selected;
}

[[nodiscard]] std::variant<std::unique_ptr<transport::IFlashTransport>,
                           DeviceManagementError>
openLoader(IFirmwareUpgradeBackend& backend,
           const transport::UsbDeviceInfo& device)
{
    auto opened = backend.openDevice(device);
    if (const auto* error =
            std::get_if<FirmwareUpgradeOpenError>(&opened)) {
        auto result = makeError(DeviceManagementErrorCode::openFailed,
                                device.portPath);
        result.openError = *error;
        return result;
    }
    return std::move(
        std::get<std::unique_ptr<transport::IFlashTransport>>(opened));
}

[[nodiscard]] EmmcLoaderResult openEmmcLoader(
    IFirmwareUpgradeBackend& backend, const std::string_view portPath)
{
    auto selected = selectLoader(backend, portPath);
    if (const auto* error =
            std::get_if<DeviceManagementError>(&selected)) {
        return *error;
    }
    const auto device = std::get<transport::UsbDeviceInfo>(selected);
    auto opened = openLoader(backend, device);
    if (const auto* error =
            std::get_if<DeviceManagementError>(&opened)) {
        return *error;
    }
    auto transport = std::move(
        std::get<std::unique_ptr<transport::IFlashTransport>>(opened));
    session::SessionTimeouts timeouts{};
    timeouts.status = std::chrono::seconds{120};
    session::RockusbSession rockusb{
        *transport, timeouts,
        session::StatusResiduePolicy::allowNonZeroOnSuccess};

    const auto flashId = session::readFlashId(rockusb);
    if (const auto* error = std::get_if<session::SessionError>(&flashId)) {
        auto result = makeError(DeviceManagementErrorCode::flashIdFailed,
                                portPath);
        result.sessionError = *error;
        static_cast<void>(transport->close());
        return result;
    }
    if (session::classifyFlashId(std::get<session::FlashId>(flashId)) !=
        session::FlashIdKind::emmcMediaMarker) {
        static_cast<void>(transport->close());
        return makeError(DeviceManagementErrorCode::storageNotEmmc,
                         portPath);
    }

    const auto flashInfo = session::readFlashInfo(rockusb);
    if (const auto* error =
            std::get_if<session::SessionError>(&flashInfo)) {
        auto result = makeError(DeviceManagementErrorCode::flashInfoFailed,
                                portPath);
        result.sessionError = *error;
        static_cast<void>(transport->close());
        return result;
    }
    return EmmcLoaderContext{
        .device = device,
        .transport = std::move(transport),
        .deviceSectors =
            std::get<session::FlashInfo>(flashInfo).legacy.sectorCount,
    };
}

[[nodiscard]] NamedPartitionResult openNamedPartition(
    IFirmwareUpgradeBackend& backend, const std::string_view portPath,
    const std::string_view partitionName)
{
    auto opened = openEmmcLoader(backend, portPath);
    if (const auto* error =
            std::get_if<DeviceManagementError>(&opened)) {
        return *error;
    }
    auto loader = std::get<EmmcLoaderContext>(std::move(opened));
    session::SessionTimeouts timeouts{};
    timeouts.status = std::chrono::seconds{120};
    session::RockusbSession rockusb{
        *loader.transport, timeouts,
        session::StatusResiduePolicy::allowNonZeroOnSuccess};
    auto parsed = storage::readDeviceGpt(rockusb, loader.deviceSectors, false);
    if (const auto* error = std::get_if<storage::GptError>(&parsed)) {
        auto result = makeError(DeviceManagementErrorCode::gptFailed,
                                partitionName);
        result.gptError = *error;
        if (error->sessionError.has_value()) {
            result.sessionError = error->sessionError;
        }
        static_cast<void>(loader.transport->close());
        return result;
    }

    auto table =
        std::get<storage::GptPartitionTable>(std::move(parsed));
    std::optional<std::size_t> match;
    for (std::size_t index = 0; index < table.partitions.size(); ++index) {
        if (table.partitions[index].name != partitionName) {
            continue;
        }
        if (match.has_value()) {
            static_cast<void>(loader.transport->close());
            return makeError(DeviceManagementErrorCode::partitionAmbiguous,
                             partitionName);
        }
        match = index;
    }
    if (!match.has_value()) {
        static_cast<void>(loader.transport->close());
        return makeError(DeviceManagementErrorCode::partitionNotFound,
                         partitionName);
    }
    return NamedPartitionContext{
        .loader = std::move(loader),
        .table = std::move(table),
        .partitionIndex = *match,
    };
}

void reportProgress(const PartitionOperationProgressCallback& progress,
                    const PartitionOperationPhase phase,
                    const std::string_view partition,
                    const std::uint64_t completedBytes,
                    const std::uint64_t totalBytes)
{
    if (progress) {
        progress(PartitionOperationProgress{
            .phase = phase,
            .partition = std::string{partition},
            .completedBytes = completedBytes,
            .totalBytes = totalBytes,
        });
    }
}

[[nodiscard]] bool cancelled(
    const DeviceManagementCancellationCallback& cancellation)
{
    return cancellation && cancellation();
}

[[nodiscard]] DeviceManagementError transferError(
    const DeviceManagementErrorCode code, const std::string_view partition,
    const std::uint64_t completedBytes, const std::uint64_t totalBytes,
    const std::optional<session::SessionError>& sessionError = {})
{
    auto error = makeError(code, partition);
    error.completedBytes = completedBytes;
    error.totalBytes = totalBytes;
    error.sessionError = sessionError;
    return error;
}

} // namespace

DeviceManagementWorkflow::DeviceManagementWorkflow(
    IFirmwareUpgradeBackend& backend) noexcept
    : backend_(backend)
{
}

DeviceInspectionResult DeviceManagementWorkflow::inspect(
    const std::string& portPath)
{
    auto selected = selectLoader(backend_, portPath);
    if (const auto* error =
            std::get_if<DeviceManagementError>(&selected)) {
        return *error;
    }
    const auto device = std::get<transport::UsbDeviceInfo>(selected);
    auto opened = openLoader(backend_, device);
    if (const auto* error =
            std::get_if<DeviceManagementError>(&opened)) {
        return *error;
    }
    auto transport = std::move(
        std::get<std::unique_ptr<transport::IFlashTransport>>(opened));
    const auto interfaceInfo = transport->interfaceInfo();
    session::RockusbSession rockusb{
        *transport, {},
        session::StatusResiduePolicy::allowNonZeroOnSuccess};

    const auto readError = [&](const DeviceManagementErrorCode code,
                               const session::SessionError& error) {
        auto result = makeError(code, portPath);
        result.sessionError = error;
        static_cast<void>(transport->close());
        return DeviceInspectionResult{result};
    };
    const auto chipInfo = session::readChipInfo(rockusb);
    if (const auto* error =
            std::get_if<session::SessionError>(&chipInfo)) {
        return readError(DeviceManagementErrorCode::chipInfoFailed, *error);
    }
    const auto capability = session::readCapability(rockusb);
    if (const auto* error =
            std::get_if<session::SessionError>(&capability)) {
        return readError(DeviceManagementErrorCode::capabilityFailed, *error);
    }
    const auto flashInfo = session::readFlashInfo(rockusb);
    if (const auto* error =
            std::get_if<session::SessionError>(&flashInfo)) {
        return readError(DeviceManagementErrorCode::flashInfoFailed, *error);
    }
    const auto flashId = session::readFlashId(rockusb);
    if (const auto* error = std::get_if<session::SessionError>(&flashId)) {
        return readError(DeviceManagementErrorCode::flashIdFailed, *error);
    }
    if (transport->close() < 0) {
        return makeError(DeviceManagementErrorCode::closeFailed, portPath);
    }
    return DeviceInspection{
        .device = device,
        .interfaceInfo = interfaceInfo,
        .chipInfo = std::get<session::ChipInfo>(chipInfo),
        .capability = std::get<session::Capability>(capability),
        .flashInfo = std::get<session::FlashInfo>(flashInfo),
        .flashId = std::get<session::FlashId>(flashId),
    };
}

DevicePartitionTableResult DeviceManagementWorkflow::readPartitions(
    const std::string& portPath, const bool checkBackup)
{
    auto opened = openEmmcLoader(backend_, portPath);
    if (const auto* error =
            std::get_if<DeviceManagementError>(&opened)) {
        return *error;
    }
    auto loader = std::get<EmmcLoaderContext>(std::move(opened));
    session::SessionTimeouts timeouts{};
    timeouts.status = std::chrono::seconds{120};
    session::RockusbSession rockusb{
        *loader.transport, timeouts,
        session::StatusResiduePolicy::allowNonZeroOnSuccess};
    auto parsed = storage::readDeviceGpt(rockusb, loader.deviceSectors,
                                         checkBackup);
    if (const auto* error = std::get_if<storage::GptError>(&parsed)) {
        auto result = makeError(DeviceManagementErrorCode::gptFailed,
                                portPath);
        result.gptError = *error;
        if (error->sessionError.has_value()) {
            result.sessionError = error->sessionError;
        }
        static_cast<void>(loader.transport->close());
        return result;
    }
    if (loader.transport->close() < 0) {
        return makeError(DeviceManagementErrorCode::closeFailed, portPath);
    }
    return DevicePartitionTable{
        .device = loader.device,
        .deviceSectors = loader.deviceSectors,
        .table =
            std::get<storage::GptPartitionTable>(std::move(parsed)),
    };
}

PartitionOperationResult DeviceManagementWorkflow::readPartition(
    const std::string& portPath, const std::string& partitionName,
    const std::filesystem::path& outputPath,
    const PartitionOperationProgressCallback& progress,
    const DeviceManagementCancellationCallback& cancellation)
{
    std::error_code existsError;
    if (std::filesystem::exists(outputPath, existsError) || existsError) {
        return makeError(DeviceManagementErrorCode::outputExists,
                         partitionName);
    }
    auto opened = openNamedPartition(backend_, portPath, partitionName);
    if (const auto* error =
            std::get_if<DeviceManagementError>(&opened)) {
        return *error;
    }
    auto context = std::get<NamedPartitionContext>(std::move(opened));
    const auto partition = context.table.partitions[context.partitionIndex];
    const auto totalBytes = partition.sectorCount() * core::kSectorSize;
    auto partialPath = outputPath;
    partialPath += ".partial";
    std::ofstream output{partialPath, std::ios::binary | std::ios::trunc};
    if (!output) {
        static_cast<void>(context.loader.transport->close());
        return makeError(DeviceManagementErrorCode::outputOpenFailed,
                         partitionName);
    }

    session::SessionTimeouts timeouts{};
    timeouts.status = std::chrono::seconds{120};
    session::RockusbSession rockusb{
        *context.loader.transport, timeouts,
        session::StatusResiduePolicy::allowNonZeroOnSuccess};
    std::vector<std::byte> buffer(static_cast<std::size_t>(
        session::kMaximumLbaSectorsPerCommand * core::kSectorSize));
    std::uint64_t completedSectors = 0U;
    std::optional<DeviceManagementError> failure;
    reportProgress(progress, PartitionOperationPhase::read, partition.name,
                   0U, totalBytes);
    while (completedSectors < partition.sectorCount()) {
        const auto completedBytes = completedSectors * core::kSectorSize;
        if (cancelled(cancellation)) {
            failure = transferError(DeviceManagementErrorCode::cancelled,
                                    partition.name, completedBytes,
                                    totalBytes);
            break;
        }
        const auto sectors = std::min<std::uint64_t>(
            session::kMaximumLbaSectorsPerCommand,
            partition.sectorCount() - completedSectors);
        const auto bytes =
            static_cast<std::size_t>(sectors * core::kSectorSize);
        const auto read = session::readLba(
            rockusb, core::Lba{partition.firstLba + completedSectors},
            core::SectorCount{sectors}, std::span{buffer}.first(bytes));
        if (const auto* error = std::get_if<session::SessionError>(&read)) {
            failure = transferError(DeviceManagementErrorCode::readFailed,
                                    partition.name, completedBytes,
                                    totalBytes, *error);
            break;
        }
        output.write(reinterpret_cast<const char*>(buffer.data()),
                     static_cast<std::streamsize>(bytes));
        if (!output) {
            failure = transferError(
                DeviceManagementErrorCode::outputWriteFailed,
                partition.name, completedBytes, totalBytes);
            break;
        }
        completedSectors += sectors;
        reportProgress(progress, PartitionOperationPhase::read,
                       partition.name,
                       completedSectors * core::kSectorSize, totalBytes);
    }
    output.flush();
    if (!output && !failure.has_value()) {
        failure = transferError(
            DeviceManagementErrorCode::outputWriteFailed,
            partition.name, completedSectors * core::kSectorSize,
            totalBytes);
    }
    output.close();
    const int closeResult = context.loader.transport->close();
    if (failure.has_value()) {
        std::error_code removeError;
        std::filesystem::remove(partialPath, removeError);
        return *failure;
    }
    if (closeResult < 0) {
        std::error_code removeError;
        std::filesystem::remove(partialPath, removeError);
        return makeError(DeviceManagementErrorCode::closeFailed,
                         partition.name);
    }
    std::error_code renameError;
    std::filesystem::rename(partialPath, outputPath, renameError);
    if (renameError) {
        return makeError(DeviceManagementErrorCode::outputFinalizeFailed,
                         partition.name);
    }
    return PartitionOperationSuccess{
        .phase = PartitionOperationPhase::read,
        .partition = partition.name,
        .bytesProcessed = totalBytes,
        .sectorsProcessed = partition.sectorCount(),
        .verified = false,
    };
}

PartitionOperationResult DeviceManagementWorkflow::writePartition(
    const std::string& portPath, const std::string& partitionName,
    const std::filesystem::path& inputPath, const bool verify,
    const PartitionOperationProgressCallback& progress,
    const DeviceManagementCancellationCallback& cancellation)
{
    std::error_code sizeError;
    const auto inputSize = std::filesystem::file_size(inputPath, sizeError);
    if (sizeError || inputSize == 0U) {
        return makeError(DeviceManagementErrorCode::inputInvalid,
                         partitionName);
    }
    auto opened = openNamedPartition(backend_, portPath, partitionName);
    if (const auto* error =
            std::get_if<DeviceManagementError>(&opened)) {
        return *error;
    }
    auto context = std::get<NamedPartitionContext>(std::move(opened));
    const auto partition = context.table.partitions[context.partitionIndex];
    const auto partitionBytes = partition.sectorCount() * core::kSectorSize;
    if (inputSize > partitionBytes) {
        static_cast<void>(context.loader.transport->close());
        return makeError(DeviceManagementErrorCode::inputTooLarge,
                         partitionName);
    }
    std::ifstream input{inputPath, std::ios::binary};
    if (!input) {
        static_cast<void>(context.loader.transport->close());
        return makeError(DeviceManagementErrorCode::inputOpenFailed,
                         partitionName);
    }

    session::SessionTimeouts timeouts{};
    timeouts.status = std::chrono::seconds{120};
    session::RockusbSession rockusb{
        *context.loader.transport, timeouts,
        session::StatusResiduePolicy::allowNonZeroOnSuccess};
    const auto sectorsToWrite =
        (inputSize + core::kSectorSize - 1U) / core::kSectorSize;
    const auto transferBytes = sectorsToWrite * core::kSectorSize;
    std::vector<std::byte> buffer(static_cast<std::size_t>(
        session::kMaximumWriteLbaSectorsPerCommand * core::kSectorSize));
    std::vector<std::byte> readback(buffer.size());
    std::uint64_t completedSectors = 0U;
    std::uint64_t completedInputBytes = 0U;
    std::optional<DeviceManagementError> failure;
    reportProgress(progress, PartitionOperationPhase::write, partition.name,
                   0U, transferBytes);
    while (completedSectors < sectorsToWrite) {
        const auto completedBytes = completedSectors * core::kSectorSize;
        if (cancelled(cancellation)) {
            failure = transferError(DeviceManagementErrorCode::cancelled,
                                    partition.name, completedBytes,
                                    transferBytes);
            break;
        }
        const auto sectors = std::min<std::uint64_t>(
            session::kMaximumWriteLbaSectorsPerCommand,
            sectorsToWrite - completedSectors);
        const auto bytes =
            static_cast<std::size_t>(sectors * core::kSectorSize);
        const auto sourceBytes = static_cast<std::size_t>(
            std::min<std::uint64_t>(inputSize - completedInputBytes, bytes));
        std::fill_n(buffer.begin(), bytes, std::byte{});
        input.read(reinterpret_cast<char*>(buffer.data()),
                   static_cast<std::streamsize>(sourceBytes));
        if (input.gcount() != static_cast<std::streamsize>(sourceBytes)) {
            failure = transferError(
                DeviceManagementErrorCode::inputReadFailed,
                partition.name, completedBytes, transferBytes);
            break;
        }
        const auto written = session::writeLba(
            rockusb, core::Lba{partition.firstLba + completedSectors},
            core::SectorCount{sectors}, std::span{buffer}.first(bytes));
        if (const auto* error =
                std::get_if<session::SessionError>(&written)) {
            failure = transferError(DeviceManagementErrorCode::writeFailed,
                                    partition.name, completedBytes,
                                    transferBytes, *error);
            break;
        }
        reportProgress(progress, PartitionOperationPhase::write,
                       partition.name, completedBytes + bytes, transferBytes);
        if (cancelled(cancellation)) {
            failure = transferError(DeviceManagementErrorCode::cancelled,
                                    partition.name, completedBytes + bytes,
                                    transferBytes);
            break;
        }
        if (verify) {
            const auto read = session::readLba(
                rockusb, core::Lba{partition.firstLba + completedSectors},
                core::SectorCount{sectors},
                std::span{readback}.first(bytes));
            if (const auto* error =
                    std::get_if<session::SessionError>(&read)) {
                failure = transferError(
                    DeviceManagementErrorCode::verifyFailed,
                    partition.name, completedBytes, transferBytes, *error);
                break;
            }
            if (!std::ranges::equal(std::span{buffer}.first(bytes),
                                    std::span{readback}.first(bytes))) {
                failure = transferError(
                    DeviceManagementErrorCode::verifyFailed,
                    partition.name, completedBytes, transferBytes);
                break;
            }
            reportProgress(progress, PartitionOperationPhase::verify,
                           partition.name, completedBytes + bytes,
                           transferBytes);
        }
        completedSectors += sectors;
        completedInputBytes += sourceBytes;
    }
    const int closeResult = context.loader.transport->close();
    if (failure.has_value()) {
        return *failure;
    }
    if (closeResult < 0) {
        return makeError(DeviceManagementErrorCode::closeFailed,
                         partition.name);
    }
    return PartitionOperationSuccess{
        .phase = PartitionOperationPhase::write,
        .partition = partition.name,
        .bytesProcessed = inputSize,
        .sectorsProcessed = sectorsToWrite,
        .verified = verify,
    };
}

PartitionOperationResult DeviceManagementWorkflow::erasePartition(
    const std::string& portPath, const std::string& partitionName,
    const PartitionOperationProgressCallback& progress,
    const DeviceManagementCancellationCallback& cancellation)
{
    auto opened = openNamedPartition(backend_, portPath, partitionName);
    if (const auto* error =
            std::get_if<DeviceManagementError>(&opened)) {
        return *error;
    }
    auto context = std::get<NamedPartitionContext>(std::move(opened));
    const auto partition = context.table.partitions[context.partitionIndex];
    const auto totalBytes = partition.sectorCount() * core::kSectorSize;
    session::SessionTimeouts timeouts{};
    timeouts.status = std::chrono::seconds{120};
    session::RockusbSession rockusb{
        *context.loader.transport, timeouts,
        session::StatusResiduePolicy::allowNonZeroOnSuccess};
    std::uint64_t completedSectors = 0U;
    std::optional<DeviceManagementError> failure;
    reportProgress(progress, PartitionOperationPhase::erase, partition.name,
                   0U, totalBytes);
    while (completedSectors < partition.sectorCount()) {
        const auto completedBytes = completedSectors * core::kSectorSize;
        if (cancelled(cancellation)) {
            failure = transferError(DeviceManagementErrorCode::cancelled,
                                    partition.name, completedBytes,
                                    totalBytes);
            break;
        }
        const auto sectors = std::min<std::uint64_t>(
            session::kMaximumEraseLbaSectorsPerCommand,
            partition.sectorCount() - completedSectors);
        const auto erased = session::eraseLba(
            rockusb, core::Lba{partition.firstLba + completedSectors},
            core::SectorCount{sectors});
        if (const auto* error =
                std::get_if<session::SessionError>(&erased)) {
            failure = transferError(DeviceManagementErrorCode::eraseFailed,
                                    partition.name, completedBytes,
                                    totalBytes, *error);
            break;
        }
        completedSectors += sectors;
        reportProgress(progress, PartitionOperationPhase::erase,
                       partition.name,
                       completedSectors * core::kSectorSize, totalBytes);
    }
    const int closeResult = context.loader.transport->close();
    if (failure.has_value()) {
        return *failure;
    }
    if (closeResult < 0) {
        return makeError(DeviceManagementErrorCode::closeFailed,
                         partition.name);
    }
    return PartitionOperationSuccess{
        .phase = PartitionOperationPhase::erase,
        .partition = partition.name,
        .bytesProcessed = totalBytes,
        .sectorsProcessed = partition.sectorCount(),
        .verified = false,
    };
}

} // namespace zzrk::workflow
