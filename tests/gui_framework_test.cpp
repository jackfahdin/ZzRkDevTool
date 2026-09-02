// SPDX-License-Identifier: MIT

#include "GuiTestHarness.hpp"
#include "MainWorkspaceController.hpp"

#include <ZzPureTools/ZzPureApplication.h>
#include <ZzFluentUI/ZzTabWidget.h>

#include <QApplication>
#include <QStatusBar>
#include <QStyle>

#include <atomic>
#include <filesystem>
#include <optional>
#include <utility>
#include <iostream>

namespace {

int failures = 0;

void expect(const bool condition, const char *description)
{
  if (!condition) {
    std::cerr << "FAIL: " << description << '\n';
    ++failures;
  }
}

} // namespace

namespace {

zzrk::workflow::FirmwareUpgradeError unusedUpgradeError()
{
  zzrk::workflow::FirmwareUpgradeError error;
  error.code = zzrk::workflow::FirmwareUpgradeErrorCode::imageInvalid;
  return error;
}

zzrk::workflow::ImageDownloadError unusedDownloadError()
{
  zzrk::workflow::ImageDownloadError error;
  error.code = zzrk::workflow::ImageDownloadErrorCode::invalidRequest;
  return error;
}

zzrk::workflow::DeviceManagementError unusedManagementError()
{
  zzrk::workflow::DeviceManagementError error;
  error.code = zzrk::workflow::DeviceManagementErrorCode::deviceUnsupported;
  return error;
}

} // namespace

class FrameworkFakeBackend final : public zzrk::gui::IGuiOperationBackend {
public:
  bool monitoring = false;
  int stopCalls = 0;

  [[nodiscard]] zzrk::transport::ScanResult scanDevices() override {
    return {};
  }

  [[nodiscard]] std::optional<zzrk::transport::UsbHotplugError>
  startDeviceMonitoring(zzrk::transport::UsbHotplugEventCallback,
                        zzrk::transport::UsbHotplugErrorCallback) override {
    monitoring = true;
    return std::nullopt;
  }

  void stopDeviceMonitoring() noexcept override {
    monitoring = false;
    ++stopCalls;
  }

  [[nodiscard]] zzrk::workflow::PreparedFirmwareUpgradeResult
  prepareFirmware(const std::filesystem::path &) override {
    return unusedUpgradeError();
  }

  [[nodiscard]] zzrk::workflow::FirmwareUpgradeResult executeUpgrade(
      const zzrk::workflow::FirmwareUpgradeRequest &,
      zzrk::workflow::PreparedFirmwareUpgrade,
      const zzrk::workflow::FirmwareUpgradeProgressCallback &,
      const zzrk::workflow::FirmwareUpgradeCancellationCallback &) override {
    return unusedUpgradeError();
  }

  [[nodiscard]] zzrk::workflow::PreparedImageDownloadResult
  prepareImageDownload(
      const zzrk::workflow::ImageDownloadPrepareRequest &) override {
    return unusedDownloadError();
  }

  [[nodiscard]] zzrk::workflow::ImageDownloadResult executeImageDownload(
      const zzrk::workflow::ImageDownloadRequest &,
      zzrk::workflow::PreparedImageDownloadPlan,
      const zzrk::workflow::ImageDownloadProgressCallback &,
      const zzrk::workflow::ImageDownloadCancellationCallback &) override {
    return unusedDownloadError();
  }

  [[nodiscard]] zzrk::workflow::DeviceInspectionResult
  inspectDevice(const std::string &) override {
    return unusedManagementError();
  }

  [[nodiscard]] zzrk::workflow::DevicePartitionTableResult
  readPartitions(const std::string &) override {
    return unusedManagementError();
  }

  [[nodiscard]] zzrk::workflow::PartitionOperationResult readPartition(
      const std::string &, const std::string &, const std::filesystem::path &,
      const zzrk::workflow::PartitionOperationProgressCallback &,
      const zzrk::workflow::DeviceManagementCancellationCallback &) override {
    return unusedManagementError();
  }

  [[nodiscard]] zzrk::workflow::PartitionOperationResult writePartition(
      const std::string &, const std::string &, const std::filesystem::path &,
      bool, const zzrk::workflow::PartitionOperationProgressCallback &,
      const zzrk::workflow::DeviceManagementCancellationCallback &) override {
    return unusedManagementError();
  }

  [[nodiscard]] zzrk::workflow::PartitionOperationResult erasePartition(
      const std::string &, const std::string &,
      const zzrk::workflow::PartitionOperationProgressCallback &,
      const zzrk::workflow::DeviceManagementCancellationCallback &) override {
    return unusedManagementError();
  }

private:
};

int main()
{
  auto harness = zzrk::gui::test::GuiTestHarness::create();
  expect(harness != nullptr, "GUI test harness creates an application");
  if (harness == nullptr) {
    return 1;
  }

  auto *application = harness->application();
  expect(application != nullptr, "harness exposes the application");
  expect(qApp == application, "qApp is the PureApplication instance");
  expect(qobject_cast<ZzPureTools::ZzPureApplication *>(qApp) == application,
         "qApp can be cast to ZzPureApplication");
  expect(application->themeController() != nullptr,
         "application exposes a theme controller");
  expect(QApplication::style() != nullptr, "Fluent application style is set");

  auto backend = std::make_shared<FrameworkFakeBackend>();
  zzrk::gui::MainWorkspaceController controller{backend, false};
  expect(controller.backend() == backend.get(),
         "controller accepts an injectable backend");
  expect(!controller.showModalDialogs(),
         "controller preserves modal dialog configuration");
  ZzFluentUI::ZzTabWidget tabs;
  QStatusBar statusBar;
  controller.attachWorkspace(&tabs, &statusBar);
  expect(controller.mainTabs() == &tabs, "controller stores main tabs");
  expect(controller.connectionStatusText() != nullptr,
         "controller creates shared connection status");
  expect(controller.refreshButton() != nullptr,
         "controller creates shared refresh button");
  expect(controller.lastScanText() != nullptr,
         "controller creates shared last scan status");
  controller.shutdown();
  expect(!backend->monitoring && backend->stopCalls == 1,
         "controller shutdown stops device monitoring");
  expect(controller.viewModel()->upgradeCancellation()->load(),
         "shutdown cancels upgrade operation");
  expect(controller.viewModel()->downloadCancellation()->load(),
         "shutdown cancels image download operation");
  expect(controller.viewModel()->partitionCancellation()->load(),
         "shutdown cancels partition operation");
  controller.shutdown();
  expect(backend->stopCalls == 1, "controller shutdown is idempotent");

  application->beginShutdown();
  application->beginShutdown();
  return failures;
}
