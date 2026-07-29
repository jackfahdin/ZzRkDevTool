// SPDX-License-Identifier: MIT

#pragma once

#include <zzrk/transport/LibusbHotplugMonitor.hpp>
#include <zzrk/transport/UsbDeviceScanner.hpp>
#include <zzrk/workflow/DeviceManagementWorkflow.hpp>
#include <zzrk/workflow/FirmwareUpgradeWorkflow.hpp>

#include <filesystem>
#include <memory>
#include <optional>

namespace zzrk::gui {

class IGuiOperationBackend {
public:
  virtual ~IGuiOperationBackend() = default;

  [[nodiscard]] virtual transport::ScanResult scanDevices() = 0;

  [[nodiscard]] virtual std::optional<transport::UsbHotplugError>
  startDeviceMonitoring(
      transport::UsbHotplugEventCallback eventCallback,
      transport::UsbHotplugErrorCallback errorCallback) = 0;
  virtual void stopDeviceMonitoring() noexcept = 0;

  [[nodiscard]] virtual workflow::PreparedFirmwareUpgradeResult
  prepareFirmware(const std::filesystem::path &imagePath) = 0;

  [[nodiscard]] virtual workflow::FirmwareUpgradeResult executeUpgrade(
      const workflow::FirmwareUpgradeRequest &request,
      workflow::PreparedFirmwareUpgrade prepared,
      const workflow::FirmwareUpgradeProgressCallback &progress,
      const workflow::FirmwareUpgradeCancellationCallback &cancellation) = 0;

  [[nodiscard]] virtual workflow::DeviceInspectionResult
  inspectDevice(const std::string &portPath) = 0;

  [[nodiscard]] virtual workflow::DevicePartitionTableResult
  readPartitions(const std::string &portPath) = 0;

  [[nodiscard]] virtual workflow::PartitionOperationResult readPartition(
      const std::string &portPath, const std::string &partition,
      const std::filesystem::path &outputPath,
      const workflow::PartitionOperationProgressCallback &progress,
      const workflow::DeviceManagementCancellationCallback &cancellation) = 0;

  [[nodiscard]] virtual workflow::PartitionOperationResult writePartition(
      const std::string &portPath, const std::string &partition,
      const std::filesystem::path &inputPath, bool verify,
      const workflow::PartitionOperationProgressCallback &progress,
      const workflow::DeviceManagementCancellationCallback &cancellation) = 0;

  [[nodiscard]] virtual workflow::PartitionOperationResult erasePartition(
      const std::string &portPath, const std::string &partition,
      const workflow::PartitionOperationProgressCallback &progress,
      const workflow::DeviceManagementCancellationCallback &cancellation) = 0;
};

[[nodiscard]] std::shared_ptr<IGuiOperationBackend> createGuiOperationBackend();

} // namespace zzrk::gui
