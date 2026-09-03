// SPDX-License-Identifier: MIT

#include "GuiTestHarness.hpp"
#include "MainWorkspaceController.hpp"

#include <ZzFluentUI/ZzTabWidget.h>
#include <ZzPureTools/ZzPageHost.h>
#include <ZzPureTools/ZzPureApplication.h>

#include <QApplication>
#include <QEventLoop>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QStatusBar>
#include <QStyle>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <optional>
#include <thread>
#include <utility>

namespace {

int failures = 0;

void expect(const bool condition, const char *description) {
  if (!condition) {
    std::cerr << "FAIL: " << description << '\n';
    ++failures;
  }
}

} // namespace

namespace {

zzrk::workflow::FirmwareUpgradeError unusedUpgradeError() {
  zzrk::workflow::FirmwareUpgradeError error;
  error.code = zzrk::workflow::FirmwareUpgradeErrorCode::imageInvalid;
  return error;
}

zzrk::workflow::ImageDownloadError unusedDownloadError() {
  zzrk::workflow::ImageDownloadError error;
  error.code = zzrk::workflow::ImageDownloadErrorCode::layoutInvalid;
  error.target = "parameter.txt";
  return error;
}

zzrk::workflow::DeviceManagementError unusedManagementError() {
  zzrk::workflow::DeviceManagementError error;
  error.code = zzrk::workflow::DeviceManagementErrorCode::chipInfoFailed;
  error.target = "1-5";
  return error;
}

bool writeFile(const QString &path, const QByteArray &contents) {
  QFile file{path};
  return file.open(QIODevice::WriteOnly | QIODevice::Truncate) &&
         file.write(contents) == contents.size();
}

} // namespace

class FrameworkFakeBackend final : public zzrk::gui::IGuiOperationBackend {
public:
  bool monitoring = false;
  int stopCalls = 0;
  std::atomic_int scans = 0;
  std::atomic_int partitionReads = 0;
  std::atomic_bool preparationFails = true;

  [[nodiscard]] zzrk::transport::ScanResult scanDevices() override {
    ++scans;
    zzrk::transport::ScanResult result;
    result.devices.push_back(zzrk::transport::UsbDeviceInfo{
        .vendorId = 0x2207U,
        .productId = 0x350bU,
        .bcdUsb = 0x0201U,
        .bcdDevice = 0x0223U,
        .busNumber = 1U,
        .portNumbers = {5U},
        .portPath = "1-5",
        .mode = zzrk::transport::DeviceMode::Loader,
        .deviceAddress = 7U,
    });
    return result;
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
      const zzrk::workflow::ImageDownloadPrepareRequest &request) override {
    if (preparationFails.load())
      return unusedDownloadError();
    zzrk::workflow::PreparedImageDownloadPlan plan{};
    zzrk::workflow::PreparedImageDownloadItem item{};
    item.kind = zzrk::workflow::ImageDownloadItemKind::partition;
    item.target = "boot_a";
    item.sourcePath = request.parameterPath;
    plan.items.push_back(std::move(item));
    return plan;
  }

  [[nodiscard]] zzrk::workflow::ImageDownloadResult executeImageDownload(
      const zzrk::workflow::ImageDownloadRequest &,
      zzrk::workflow::PreparedImageDownloadPlan,
      const zzrk::workflow::ImageDownloadProgressCallback &,
      const zzrk::workflow::ImageDownloadCancellationCallback &) override {
    zzrk::workflow::ImageDownloadError error{};
    error.code = zzrk::workflow::ImageDownloadErrorCode::flashFailed;
    error.stage = zzrk::workflow::ImageDownloadStage::write;
    error.target = "boot_a";
    error.itemIndex = 0U;
    return error;
  }

  [[nodiscard]] zzrk::workflow::DeviceInspectionResult
  inspectDevice(const std::string &) override {
    std::this_thread::sleep_for(std::chrono::milliseconds{20});
    return unusedManagementError();
  }

  [[nodiscard]] zzrk::workflow::DevicePartitionTableResult
  readPartitions(const std::string &portPath) override {
    ++partitionReads;
    zzrk::transport::UsbDeviceInfo device{
        .vendorId = 0x2207U,
        .productId = 0x350bU,
        .bcdUsb = 0x0201U,
        .bcdDevice = 0x0223U,
        .busNumber = 1U,
        .portNumbers = {5U},
        .portPath = portPath,
        .mode = zzrk::transport::DeviceMode::Loader,
        .deviceAddress = 7U,
    };
    zzrk::storage::GptPartitionTable table{};
    table.revision = 0x00010000U;
    table.backupStatus = zzrk::storage::BackupGptStatus::verified;
    table.partitions = {
        zzrk::storage::GptPartition{
            .name = "boot_a",
            .firstLba = 32'768U,
            .lastLba = 196'607U,
            .attributes = 0U,
        },
        zzrk::storage::GptPartition{
            .name = "userdata",
            .firstLba = 20'971'520U,
            .lastLba = 124'190'686U,
            .attributes = 0U,
        },
    };
    return zzrk::workflow::DevicePartitionTable{
        .device = std::move(device),
        .deviceSectors = 124'190'720U,
        .table = std::move(table),
    };
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

int main() {
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
  ZzPureTools::ZzPageHost pageHost;
  int selectedDeviceChanges = 0;
  int scanCompletions = 0;
  QObject::connect(&controller,
                   &zzrk::gui::MainWorkspaceController::selectedDeviceChanged,
                   [&] { ++selectedDeviceChanges; });
  QObject::connect(&controller,
                   &zzrk::gui::MainWorkspaceController::scanCompleted,
                   [&] { ++scanCompletions; });
  auto downloadPage = controller.createInitialDownloadPage(&pageHost);
  expect(downloadPage.hasValue(),
         "controller creates the initial download page");
  if (downloadPage) {
    auto *downloadTable =
        downloadPage.value()->view()->findChild<QTableWidget *>(
            QStringLiteral("downloadImageTable"));
    expect(downloadTable != nullptr && downloadTable->columnCount() == 9,
           "initial page exposes the nine-column download plan");
    expect(downloadPage.value()->view()->findChild<QProgressBar *>(
               QStringLiteral("overallImageDownloadProgress")) != nullptr,
           "initial page exposes overall download progress");
    expect(downloadPage.value()->view()->findChild<QProgressBar *>(
               QStringLiteral("operationImageDownloadProgress")) != nullptr,
           "initial page exposes operation download progress");
    expect(tabs.findChild<QTableWidget *>(QStringLiteral("deviceTable")) !=
               nullptr,
           "controller adds the device page to the workspace tabs");

    bool scanFinished = false;
    QEventLoop scanLoop;
    QObject::connect(&controller,
                     &zzrk::gui::MainWorkspaceController::scanCompleted,
                     &scanLoop, [&] {
                       scanFinished = true;
                       scanLoop.quit();
                     });
    QTimer::singleShot(2000, &scanLoop, &QEventLoop::quit);
    scanLoop.exec();
    expect(scanFinished, "controller completes the initial device scan");
    expect(selectedDeviceChanges == 1,
           "initial device selection emits one change");
    QMetaObject::invokeMethod(&controller, "handleDeviceSelectionChanged",
                              Qt::DirectConnection);
    expect(selectedDeviceChanges == 1,
           "unchanged device selection does not emit another change");

    auto *readGptButton =
        downloadPage.value()->view()->findChild<QToolButton *>(
            QStringLiteral("readDownloadGptButton"));
    expect(readGptButton != nullptr && readGptButton->isEnabled(),
           "selected Loader device can read its GPT");
    if (readGptButton != nullptr)
      readGptButton->click();
    QEventLoop gptLoop;
    QTimer::singleShot(100, &gptLoop, &QEventLoop::quit);
    gptLoop.exec();
    const auto *layoutPath =
        downloadPage.value()->view()->findChild<QLineEdit *>(
            QStringLiteral("downloadParameterPath"));
    expect(backend->partitionReads.load() == 1 && layoutPath != nullptr &&
               layoutPath->text() == QStringLiteral("1-5 / 设备 GPT") &&
               downloadTable->rowCount() == 3,
           "controller reads and applies device GPT without MainWindow");

    bool inspectionFinished = false;
    QEventLoop inspectionLoop;
    QObject::connect(
        &controller,
        &zzrk::gui::MainWorkspaceController::deviceInspectionCompleted,
        &inspectionLoop, [&](bool) {
          inspectionFinished = true;
          inspectionLoop.quit();
        });
    auto *inspectButton =
        tabs.findChild<QPushButton *>(QStringLiteral("inspectDeviceButton"));
    expect(inspectButton != nullptr && inspectButton->isEnabled(),
           "selected Loader device can be inspected");
    if (inspectButton != nullptr)
      inspectButton->click();
    controller.handleDeviceChange();
    QTimer::singleShot(2000, &inspectionLoop, &QEventLoop::quit);
    inspectionLoop.exec();
    const auto *inspectionStatus =
        tabs.findChild<QLabel *>(QStringLiteral("deviceDetailStatus"));
    expect(inspectionFinished && inspectionStatus != nullptr &&
               inspectionStatus->text() ==
                   QStringLiteral("读取 ChipInfo 失败（1-5）"),
           "device inspection preserves the specific error message");
    if (scanCompletions < 2) {
      QEventLoop pendingScanLoop;
      QObject::connect(&controller,
                       &zzrk::gui::MainWorkspaceController::scanCompleted,
                       &pendingScanLoop, &QEventLoop::quit);
      QTimer::singleShot(2000, &pendingScanLoop, &QEventLoop::quit);
      pendingScanLoop.exec();
    }
    expect(scanCompletions == 2 && backend->scans.load() == 2,
           "hotplug during inspection triggers one deferred scan");

    QTemporaryDir directory;
    const QString parameterPath = directory.filePath("parameter.txt");
    expect(
        directory.isValid() &&
            writeFile(parameterPath, QByteArray{"MACHINE_MODEL: gui-rk3588\n"
                                                "TYPE: GPT\n"
                                                "CMDLINE:mtdparts=rk29xxnand:"
                                                "0x100@0x8000(boot_a)\n"}) &&
            writeFile(directory.filePath("boot_a.img"), QByteArray{"raw"}),
        "download error fixture is created");
    controller.loadDownloadParameter(parameterPath);
    controller.loadDownloadDirectory(directory.path());
    bool downloadFinished = false;
    QEventLoop downloadLoop;
    QObject::connect(
        &controller,
        &zzrk::gui::MainWorkspaceController::imageDownloadCompleted,
        &downloadLoop, [&](bool) {
          downloadFinished = true;
          downloadLoop.quit();
        });
    auto *startButton = downloadPage.value()->view()->findChild<QPushButton *>(
        QStringLiteral("startImageDownloadButton"));
    expect(startButton != nullptr && startButton->isEnabled(),
           "valid download inputs enable image preparation");
    QString confirmationText;
    controller.setShowModalDialogs(true);
    QTimer::singleShot(0, application, [&] {
      auto *confirmation =
          qobject_cast<QMessageBox *>(application->activeModalWidget());
      if (confirmation == nullptr)
        return;
      confirmationText = confirmation->text();
      confirmation->done(QMessageBox::Cancel);
    });
    if (startButton != nullptr)
      startButton->click();
    expect(confirmationText.contains(QStringLiteral("端口：1-5")) &&
               confirmationText.contains(
                   QStringLiteral("布局：%1").arg(parameterPath)) &&
               confirmationText.contains(
                   QStringLiteral("目标：主/备 GPT、boot_a")) &&
               confirmationText.contains(QStringLiteral("完成后重启：是")),
           "image confirmation summarizes the destructive request");
    controller.setShowModalDialogs(false);
    if (startButton != nullptr)
      startButton->click();
    QTimer::singleShot(2000, &downloadLoop, &QEventLoop::quit);
    downloadLoop.exec();
    const auto *downloadStatus =
        downloadPage.value()->view()->findChild<QLabel *>(
            QStringLiteral("imageDownloadStatus"));
    expect(downloadFinished && downloadStatus != nullptr &&
               downloadStatus->text() ==
                   QStringLiteral("分区布局无效（parameter.txt）"),
           "image preparation preserves the specific error message");

    backend->preparationFails.store(false);
    downloadFinished = false;
    if (startButton != nullptr)
      startButton->click();
    QTimer::singleShot(2000, &downloadLoop, &QEventLoop::quit);
    downloadLoop.exec();
    const auto *overallProgress =
        downloadPage.value()->view()->findChild<QProgressBar *>(
            QStringLiteral("overallImageDownloadProgress"));
    const auto *operationProgress =
        downloadPage.value()->view()->findChild<QProgressBar *>(
            QStringLiteral("operationImageDownloadProgress"));
    QString bootStatus;
    if (downloadTable != nullptr) {
      for (int row = 0; row < downloadTable->rowCount(); ++row) {
        const auto *target = downloadTable->item(row, 1);
        if (target != nullptr && target->text() == QStringLiteral("boot_a")) {
          bootStatus = downloadTable->item(row, 8)->text();
          break;
        }
      }
    }
    expect(downloadFinished && overallProgress != nullptr &&
               overallProgress->maximum() == 100 &&
               operationProgress != nullptr &&
               operationProgress->maximum() == 100,
           "failed image execution restores determinate progress ranges");
    expect(downloadStatus != nullptr &&
               downloadStatus->text() ==
                   QStringLiteral("镜像写入或回读校验失败（boot_a）") &&
               bootStatus == QStringLiteral("失败"),
           "failed image execution marks the matching plan row");
  }
  controller.shutdown();
  expect(!backend->monitoring && backend->stopCalls == 1,
         "controller shutdown stops device monitoring");
  expect(controller.viewModel()->upgradeCancellation()->load(),
         "shutdown cancels upgrade operation");
  expect(controller.viewModel()->downloadCancellation()->load(),
         "shutdown cancels image download operation");
  expect(controller.viewModel()->partitionCancellation()->load(),
         "shutdown cancels partition operation");
  if (downloadPage) {
    downloadPage.value().reset();
    expect(controller.viewModel() == nullptr,
           "controller observes page-owned view model destruction");
  }
  controller.shutdown();
  expect(backend->stopCalls == 1, "controller shutdown is idempotent");

  application->beginShutdown();
  application->beginShutdown();
  return failures;
}
