// SPDX-License-Identifier: MIT

#include "GuiOperationBackend.hpp"

#include <zzrk/qt/FirmwareMd5Verifier.hpp>

#include <exception>
#include <utility>

namespace zzrk::gui {
namespace {

class GuiOperationBackend final : public IGuiOperationBackend {
public:
  [[nodiscard]] transport::ScanResult scanDevices() override {
    try {
      transport::UsbDeviceScanner scanner;
      return scanner.scanRockchipDevices();
    } catch (const std::exception &error) {
      transport::ScanResult result;
      result.error = transport::ScanError{-1, error.what()};
      return result;
    } catch (...) {
      transport::ScanResult result;
      result.error = transport::ScanError{-1, "Unknown USB scan failure"};
      return result;
    }
  }

  [[nodiscard]] workflow::PreparedFirmwareUpgradeResult
  prepareFirmware(const std::filesystem::path &imagePath) override {
    auto backend = workflow::createLibusbFirmwareUpgradeBackend();
    qt::FirmwareMd5Verifier verifier;
    workflow::FirmwareUpgradeWorkflow workflow{*backend, verifier};
    return workflow.prepare(imagePath);
  }

  [[nodiscard]] workflow::FirmwareUpgradeResult
  executeUpgrade(const workflow::FirmwareUpgradeRequest &request,
                 workflow::PreparedFirmwareUpgrade prepared,
                 const workflow::FirmwareUpgradeProgressCallback &progress,
                 const workflow::FirmwareUpgradeCancellationCallback
                     &cancellation) override {
    auto backend = workflow::createLibusbFirmwareUpgradeBackend();
    qt::FirmwareMd5Verifier verifier;
    workflow::FirmwareUpgradeWorkflow workflow{*backend, verifier};
    return workflow.execute(request, std::move(prepared), progress,
                            cancellation);
  }

  [[nodiscard]] workflow::DeviceInspectionResult
  inspectDevice(const std::string &portPath) override {
    auto backend = workflow::createLibusbFirmwareUpgradeBackend();
    workflow::DeviceManagementWorkflow workflow{*backend};
    return workflow.inspect(portPath);
  }

  [[nodiscard]] workflow::DevicePartitionTableResult
  readPartitions(const std::string &portPath) override {
    auto backend = workflow::createLibusbFirmwareUpgradeBackend();
    workflow::DeviceManagementWorkflow workflow{*backend};
    return workflow.readPartitions(portPath);
  }

  [[nodiscard]] workflow::PartitionOperationResult readPartition(
      const std::string &portPath, const std::string &partition,
      const std::filesystem::path &outputPath,
      const workflow::PartitionOperationProgressCallback &progress,
      const workflow::DeviceManagementCancellationCallback &cancellation)
      override {
    auto backend = workflow::createLibusbFirmwareUpgradeBackend();
    workflow::DeviceManagementWorkflow workflow{*backend};
    return workflow.readPartition(portPath, partition, outputPath, progress,
                                  cancellation);
  }

  [[nodiscard]] workflow::PartitionOperationResult writePartition(
      const std::string &portPath, const std::string &partition,
      const std::filesystem::path &inputPath, const bool verify,
      const workflow::PartitionOperationProgressCallback &progress,
      const workflow::DeviceManagementCancellationCallback &cancellation)
      override {
    auto backend = workflow::createLibusbFirmwareUpgradeBackend();
    workflow::DeviceManagementWorkflow workflow{*backend};
    return workflow.writePartition(portPath, partition, inputPath, verify,
                                   progress, cancellation);
  }

  [[nodiscard]] workflow::PartitionOperationResult erasePartition(
      const std::string &portPath, const std::string &partition,
      const workflow::PartitionOperationProgressCallback &progress,
      const workflow::DeviceManagementCancellationCallback &cancellation)
      override {
    auto backend = workflow::createLibusbFirmwareUpgradeBackend();
    workflow::DeviceManagementWorkflow workflow{*backend};
    return workflow.erasePartition(portPath, partition, progress,
                                   cancellation);
  }
};

} // namespace

std::shared_ptr<IGuiOperationBackend> createGuiOperationBackend() {
  return std::make_shared<GuiOperationBackend>();
}

} // namespace zzrk::gui
