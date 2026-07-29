// SPDX-License-Identifier: MIT

#pragma once

#include <zzrk/storage/GptPartitionTable.hpp>
#include <zzrk/session/ReadCapability.hpp>
#include <zzrk/session/ReadChipInfo.hpp>
#include <zzrk/session/ReadFlashId.hpp>
#include <zzrk/session/ReadFlashInfo.hpp>
#include <zzrk/workflow/FirmwareUpgradeWorkflow.hpp>

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

namespace zzrk::workflow {

enum class DeviceManagementErrorCode {
    cancelled,
    scanFailed,
    deviceNotFound,
    deviceAmbiguous,
    deviceUnsupported,
    openFailed,
    chipInfoFailed,
    capabilityFailed,
    flashInfoFailed,
    flashIdFailed,
    storageNotEmmc,
    gptFailed,
    partitionNotFound,
    partitionAmbiguous,
    inputInvalid,
    inputTooLarge,
    inputOpenFailed,
    inputReadFailed,
    outputExists,
    outputOpenFailed,
    outputWriteFailed,
    outputFinalizeFailed,
    readFailed,
    writeFailed,
    verifyFailed,
    eraseFailed,
    closeFailed,
};

struct DeviceManagementError {
    DeviceManagementErrorCode code{};
    std::string target;
    std::uint64_t completedBytes{};
    std::uint64_t totalBytes{};
    std::optional<transport::ScanError> scanError;
    std::optional<FirmwareUpgradeOpenError> openError;
    std::optional<session::SessionError> sessionError;
    std::optional<storage::GptError> gptError;
};

struct DeviceInspection {
    transport::UsbDeviceInfo device;
    transport::FlashInterfaceInfo interfaceInfo;
    session::ChipInfo chipInfo;
    session::Capability capability;
    session::FlashInfo flashInfo;
    session::FlashId flashId;
};

struct DevicePartitionTable {
    transport::UsbDeviceInfo device;
    std::uint64_t deviceSectors{};
    storage::GptPartitionTable table;
};

enum class PartitionOperationPhase {
    read,
    write,
    verify,
    erase,
};

struct PartitionOperationProgress {
    PartitionOperationPhase phase{};
    std::string partition;
    std::uint64_t completedBytes{};
    std::uint64_t totalBytes{};
};

using PartitionOperationProgressCallback =
    std::function<void(const PartitionOperationProgress&)>;
using DeviceManagementCancellationCallback = std::function<bool()>;

struct PartitionOperationSuccess {
    PartitionOperationPhase phase{};
    std::string partition;
    std::uint64_t bytesProcessed{};
    std::uint64_t sectorsProcessed{};
    bool verified{};
};

using DeviceInspectionResult =
    std::variant<DeviceInspection, DeviceManagementError>;
using DevicePartitionTableResult =
    std::variant<DevicePartitionTable, DeviceManagementError>;
using PartitionOperationResult =
    std::variant<PartitionOperationSuccess, DeviceManagementError>;

class DeviceManagementWorkflow final {
  public:
    explicit DeviceManagementWorkflow(IFirmwareUpgradeBackend& backend) noexcept;

    [[nodiscard]] DeviceInspectionResult
    inspect(const std::string& portPath);

    [[nodiscard]] DevicePartitionTableResult
    readPartitions(const std::string& portPath, bool checkBackup = true);

    [[nodiscard]] PartitionOperationResult readPartition(
        const std::string& portPath, const std::string& partition,
        const std::filesystem::path& outputPath,
        const PartitionOperationProgressCallback& progress = {},
        const DeviceManagementCancellationCallback& cancellation = {});

    [[nodiscard]] PartitionOperationResult writePartition(
        const std::string& portPath, const std::string& partition,
        const std::filesystem::path& inputPath, bool verify = true,
        const PartitionOperationProgressCallback& progress = {},
        const DeviceManagementCancellationCallback& cancellation = {});

    [[nodiscard]] PartitionOperationResult erasePartition(
        const std::string& portPath, const std::string& partition,
        const PartitionOperationProgressCallback& progress = {},
        const DeviceManagementCancellationCallback& cancellation = {});

  private:
    IFirmwareUpgradeBackend& backend_;
};

[[nodiscard]] constexpr std::string_view
toString(const PartitionOperationPhase phase) noexcept
{
    switch (phase) {
    case PartitionOperationPhase::read:
        return "read";
    case PartitionOperationPhase::write:
        return "write";
    case PartitionOperationPhase::verify:
        return "verify";
    case PartitionOperationPhase::erase:
        return "erase";
    }
    return "unknown";
}

} // namespace zzrk::workflow
