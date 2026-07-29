// SPDX-License-Identifier: MIT

#pragma once

#include <zzrk/transport/UsbDeviceScanner.hpp>
#include <zzrk/workflow/FirmwareUpgradeWorkflow.hpp>

#include <filesystem>
#include <memory>

namespace zzrk::gui {

class IGuiOperationBackend {
public:
  virtual ~IGuiOperationBackend() = default;

  [[nodiscard]] virtual transport::ScanResult scanDevices() = 0;

  [[nodiscard]] virtual workflow::PreparedFirmwareUpgradeResult
  prepareFirmware(const std::filesystem::path &imagePath) = 0;

  [[nodiscard]] virtual workflow::FirmwareUpgradeResult executeUpgrade(
      const workflow::FirmwareUpgradeRequest &request,
      workflow::PreparedFirmwareUpgrade prepared,
      const workflow::FirmwareUpgradeProgressCallback &progress,
      const workflow::FirmwareUpgradeCancellationCallback &cancellation) = 0;
};

[[nodiscard]] std::shared_ptr<IGuiOperationBackend> createGuiOperationBackend();

} // namespace zzrk::gui
