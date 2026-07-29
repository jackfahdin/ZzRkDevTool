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
};

} // namespace

std::shared_ptr<IGuiOperationBackend> createGuiOperationBackend() {
  return std::make_shared<GuiOperationBackend>();
}

} // namespace zzrk::gui
