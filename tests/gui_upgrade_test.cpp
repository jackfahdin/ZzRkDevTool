// SPDX-License-Identifier: MIT

#include "FilePickerTestSupport.hpp"
#include "GuiOperationBackend.hpp"
#include "GuiTestHarness.hpp"
#include "GuiTranslations.hpp"
#include "MainWorkspaceController.hpp"
#include "MainWorkspaceViewModel.hpp"
#include "pages/FullUpgradePage.hpp"
#include "pages/PartitionPage.hpp"

#include <ZzPureTools/ZzApplicationWindow.h>

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFileDialog>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QTableWidget>
#include <QTemporaryFile>
#include <QTimer>
#include <QToolButton>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

namespace {

using namespace std::chrono_literals;

class AutomatedGuiBackend final : public zzrk::gui::IGuiOperationBackend {
public:
  std::atomic_int scans{};
  std::atomic_int preparations{};
  std::atomic_int upgrades{};
  std::atomic_int inspections{};
  std::atomic_int partitionReads{};
  std::atomic_int partitionOperations{};
  std::atomic_int monitoringStarts{};
  std::atomic_int monitoringStops{};
  std::atomic_bool holdUpgrade{false};
  std::atomic_bool upgradeEntered{false};
  std::atomic_bool holdPartition{false};
  std::atomic_bool partitionEntered{false};

  [[nodiscard]] zzrk::transport::ScanResult scanDevices() override {
    ++scans;
    zzrk::transport::ScanResult result;
    result.devices.push_back(makeDevice());
    return result;
  }

  [[nodiscard]] std::optional<zzrk::transport::UsbHotplugError>
  startDeviceMonitoring(
      zzrk::transport::UsbHotplugEventCallback eventCallback,
      zzrk::transport::UsbHotplugErrorCallback errorCallback) override {
    std::lock_guard lock{monitoringMutex_};
    ++monitoringStarts;
    hotplugEventCallback_ = std::move(eventCallback);
    hotplugErrorCallback_ = std::move(errorCallback);
    return std::nullopt;
  }

  void stopDeviceMonitoring() noexcept override {
    std::lock_guard lock{monitoringMutex_};
    ++monitoringStops;
    hotplugEventCallback_ = {};
    hotplugErrorCallback_ = {};
  }

  void emitDeviceChange(const zzrk::transport::UsbHotplugEvent event =
                            zzrk::transport::UsbHotplugEvent::Arrived) {
    zzrk::transport::UsbHotplugEventCallback callback;
    {
      std::lock_guard lock{monitoringMutex_};
      callback = hotplugEventCallback_;
    }
    if (callback) {
      callback(event);
    }
  }

  [[nodiscard]] zzrk::workflow::PreparedFirmwareUpgradeResult
  prepareFirmware(const std::filesystem::path &imagePath) override {
    ++preparations;
    zzrk::image::RkFirmwareImage image{};
    image.sourcePath = imagePath;
    image.model = "automated-rk3588";
    image.trailingMd5Verified = true;
    zzrk::image::RkFirmwareFlashPlan plan{};
    plan.sourcePath = imagePath;
    plan.targetChip = "RK3588";
    plan.targetModel = image.model;
    plan.minimumDeviceSectors = 4096U;
    plan.totalPartitionBytes = 1024U;
    plan.steps = {
        zzrk::image::FirmwareFlashStep{
            .kind = zzrk::image::FirmwareFlashStepKind::upgradeLoader,
            .target = "eMMC IDBlock",
            .entryIndex = std::nullopt,
            .firstLba = 0U,
            .sectorCount = 0U,
            .inputSize = 0U,
            .outputSize = 512U,
            .format = zzrk::image::PayloadFormat::raw,
        },
        zzrk::image::FirmwareFlashStep{
            .kind = zzrk::image::FirmwareFlashStepKind::ensurePartitionTable,
            .target = "primary and backup GPT",
            .entryIndex = std::nullopt,
            .firstLba = 0U,
            .sectorCount = 0U,
            .inputSize = 0U,
            .outputSize = 512U,
            .format = zzrk::image::PayloadFormat::raw,
        },
        zzrk::image::FirmwareFlashStep{
            .kind = zzrk::image::FirmwareFlashStepKind::writePartition,
            .target = "boot_a",
            .entryIndex = std::nullopt,
            .firstLba = 0U,
            .sectorCount = 0U,
            .inputSize = 0U,
            .outputSize = 1024U,
            .format = zzrk::image::PayloadFormat::raw,
        },
    };
    std::array<char, 33> digest{};
    digest[0] = 'a';
    return zzrk::workflow::PreparedFirmwareUpgrade{
        .image = std::move(image),
        .plan = std::move(plan),
        .md5 = digest,
    };
  }

  [[nodiscard]] zzrk::workflow::FirmwareUpgradeResult executeUpgrade(
      const zzrk::workflow::FirmwareUpgradeRequest &request,
      zzrk::workflow::PreparedFirmwareUpgrade,
      const zzrk::workflow::FirmwareUpgradeProgressCallback &progress,
      const zzrk::workflow::FirmwareUpgradeCancellationCallback &cancellation)
      override {
    ++upgrades;
    upgradeEntered.store(true);
    while (holdUpgrade.load()) {
      std::this_thread::sleep_for(2ms);
    }
    if (!cancellation || !cancellation())
      emitDeviceChange(zzrk::transport::UsbHotplugEvent::Left);
    std::this_thread::sleep_for(20ms);
    const auto reportEvent = [&](zzrk::workflow::FirmwareUpgradeEvent event) {
      if (progress) {
        progress(event);
      }
      std::this_thread::sleep_for(10ms);
    };
    reportEvent({
        .stage = zzrk::workflow::FirmwareUpgradeStage::selectDevice,
        .target = request.portPath,
        .completedBytes = 0U,
        .totalBytes = 0U,
        .flashPhase = std::nullopt,
        .device = std::nullopt,
    });
    reportEvent({
        .stage = zzrk::workflow::FirmwareUpgradeStage::checkStorage,
        .target = "eMMC",
        .completedBytes = 0U,
        .totalBytes = 0U,
        .flashPhase = std::nullopt,
        .device = std::nullopt,
    });
    reportEvent({
        .stage = zzrk::workflow::FirmwareUpgradeStage::flash,
        .target = "MiniLoaderAll.bin -> eMMC IDBlock",
        .completedBytes = 256U,
        .totalBytes = 512U,
        .flashPhase = zzrk::image::FirmwareFlashPhase::writeLoader,
        .device = std::nullopt,
    });
    reportEvent({
        .stage = zzrk::workflow::FirmwareUpgradeStage::flash,
        .target = "boot_a",
        .completedBytes = 1024U,
        .totalBytes = 1024U,
        .flashPhase = zzrk::image::FirmwareFlashPhase::writePartition,
        .device = std::nullopt,
    });
    if (cancellation && cancellation()) {
      zzrk::workflow::FirmwareUpgradeError error{};
      error.code = zzrk::workflow::FirmwareUpgradeErrorCode::cancelled;
      error.stage = zzrk::workflow::FirmwareUpgradeStage::flash;
      error.target = "boot_a";
      return error;
    }
    return zzrk::workflow::FirmwareUpgradeSuccess{
        .portPath = request.portPath,
        .model = "automated-rk3588",
        .firmwareMd5 = {},
        .deviceSectors = 124'190'720U,
        .partitionBytesWritten = 1024U,
        .sectorsErased = 0U,
        .partitionsVerified = 1U,
        .preparedOnly = false,
        .partitionTableWritten = true,
        .partitionTableVerified = true,
        .loaderWritten = true,
        .loaderVerified = true,
        .resetAccepted = true,
    };
  }

  [[nodiscard]] zzrk::workflow::PreparedImageDownloadResult
  prepareImageDownload(
      const zzrk::workflow::ImageDownloadPrepareRequest &) override {
    return zzrk::workflow::ImageDownloadError{
        .code = zzrk::workflow::ImageDownloadErrorCode::invalidRequest,
        .stage = zzrk::workflow::ImageDownloadStage::inspectFiles,
        .target = "unused",
        .completedPlanBytes = 0U,
        .itemIndex = {},
        .imageError = {},
        .digestError = {},
        .scanError = {},
        .openError = {},
        .loaderError = {},
        .sessionError = {},
        .gptError = {},
        .flashError = {},
    };
  }

  [[nodiscard]] zzrk::workflow::ImageDownloadResult executeImageDownload(
      const zzrk::workflow::ImageDownloadRequest &,
      zzrk::workflow::PreparedImageDownloadPlan,
      const zzrk::workflow::ImageDownloadProgressCallback &,
      const zzrk::workflow::ImageDownloadCancellationCallback &) override {
    return zzrk::workflow::ImageDownloadError{
        .code = zzrk::workflow::ImageDownloadErrorCode::invalidRequest,
        .stage = zzrk::workflow::ImageDownloadStage::write,
        .target = "unused",
        .completedPlanBytes = 0U,
        .itemIndex = {},
        .imageError = {},
        .digestError = {},
        .scanError = {},
        .openError = {},
        .loaderError = {},
        .sessionError = {},
        .gptError = {},
        .flashError = {},
    };
  }

  [[nodiscard]] zzrk::workflow::DeviceInspectionResult
  inspectDevice(const std::string &portPath) override {
    ++inspections;
    auto device = makeDevice();
    device.portPath = portPath;
    zzrk::session::ChipInfo chipInfo{};
    chipInfo.bytes[0] = std::byte{'8'};
    chipInfo.bytes[1] = std::byte{'8'};
    chipInfo.bytes[2] = std::byte{'5'};
    chipInfo.bytes[3] = std::byte{'3'};
    zzrk::session::Capability capability{};
    capability.rawBytes[0] = std::byte{0x01};
    zzrk::session::FlashInfo flashInfo{};
    flashInfo.legacy.sectorCount = 124'190'720U;
    zzrk::session::FlashId flashId{};
    flashId.rawBytes = {
        std::byte{'E'}, std::byte{'M'}, std::byte{'M'},
        std::byte{'C'}, std::byte{' '},
    };
    return zzrk::workflow::DeviceInspection{
        .device = std::move(device),
        .interfaceInfo =
            zzrk::transport::FlashInterfaceInfo{
                .interfaceNumber = 0U,
                .alternateSetting = 0U,
                .bulkInEndpoint = 0x81U,
                .bulkOutEndpoint = 0x01U,
                .bulkInMaxPacketSize = 512U,
                .bulkOutMaxPacketSize = 512U,
            },
        .chipInfo = chipInfo,
        .capability = capability,
        .flashInfo = flashInfo,
        .flashId = flashId,
    };
  }

  [[nodiscard]] zzrk::workflow::DevicePartitionTableResult
  readPartitions(const std::string &portPath) override {
    ++partitionReads;
    auto device = makeDevice();
    device.portPath = portPath;
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
      const std::string &, const std::string &partition,
      const std::filesystem::path &,
      const zzrk::workflow::PartitionOperationProgressCallback &progress,
      const zzrk::workflow::DeviceManagementCancellationCallback &cancellation)
      override {
    return partitionOperation(zzrk::workflow::PartitionOperationPhase::read,
                              partition, false, progress, cancellation);
  }

  [[nodiscard]] zzrk::workflow::PartitionOperationResult writePartition(
      const std::string &, const std::string &partition,
      const std::filesystem::path &, const bool verify,
      const zzrk::workflow::PartitionOperationProgressCallback &progress,
      const zzrk::workflow::DeviceManagementCancellationCallback &cancellation)
      override {
    return partitionOperation(zzrk::workflow::PartitionOperationPhase::write,
                              partition, verify, progress, cancellation);
  }

  [[nodiscard]] zzrk::workflow::PartitionOperationResult erasePartition(
      const std::string &, const std::string &partition,
      const zzrk::workflow::PartitionOperationProgressCallback &progress,
      const zzrk::workflow::DeviceManagementCancellationCallback &cancellation)
      override {
    return partitionOperation(zzrk::workflow::PartitionOperationPhase::erase,
                              partition, false, progress, cancellation);
  }

private:
  [[nodiscard]] static zzrk::transport::UsbDeviceInfo makeDevice() {
    return zzrk::transport::UsbDeviceInfo{
        .vendorId = 0x2207U,
        .productId = 0x350bU,
        .bcdUsb = 0x0201U,
        .bcdDevice = 0x0223U,
        .busNumber = 1U,
        .portNumbers = {5U},
        .portPath = "1-5",
        .mode = zzrk::transport::DeviceMode::Loader,
        .deviceAddress = 7U,
    };
  }

  std::mutex monitoringMutex_;
  zzrk::transport::UsbHotplugEventCallback hotplugEventCallback_;
  zzrk::transport::UsbHotplugErrorCallback hotplugErrorCallback_;

  [[nodiscard]] zzrk::workflow::PartitionOperationResult partitionOperation(
      const zzrk::workflow::PartitionOperationPhase phase,
      const std::string &partition, const bool verified,
      const zzrk::workflow::PartitionOperationProgressCallback &progress,
      const zzrk::workflow::DeviceManagementCancellationCallback
          &cancellation) {
    ++partitionOperations;
    partitionEntered.store(true);
    while (holdPartition.load()) {
      std::this_thread::sleep_for(2ms);
    }
    constexpr std::uint64_t bytes = 1024U;
    if (progress) {
      progress({phase, partition, bytes / 2U, bytes});
      progress({phase, partition, bytes, bytes});
    }
    if (cancellation && cancellation()) {
      return zzrk::workflow::DeviceManagementError{
          .code = zzrk::workflow::DeviceManagementErrorCode::cancelled,
          .target = partition,
          .completedBytes = bytes,
          .totalBytes = bytes,
          .scanError = {},
          .openError = {},
          .sessionError = {},
          .gptError = {},
      };
    }
    return zzrk::workflow::PartitionOperationSuccess{
        .phase = phase,
        .partition = partition,
        .bytesProcessed = bytes,
        .sectorsProcessed = 2U,
        .verified = verified,
    };
  }
};

} // namespace

int main() {
  auto backend = std::make_shared<AutomatedGuiBackend>();
  auto harness = zzrk::gui::test::GuiTestHarness::create(backend);
  if (harness == nullptr || harness->application() == nullptr ||
      harness->window() == nullptr || harness->controller() == nullptr) {
    return EXIT_FAILURE;
  }
  auto &application = *harness->application();
  auto *window = harness->window();
  auto *controller = harness->controller();
  QTemporaryFile partitionImage(
      QDir::temp().filePath(QStringLiteral("zzrk-partition-XXXXXX.img")));
  if (!partitionImage.open() || partitionImage.write("partition") != 9)
    return EXIT_FAILURE;
  partitionImage.flush();
  window->show();
  const char *stage = "initial scan";

  const auto cancellationButtonsStayDisabled = [&]() {
    QEventLoop scanLoop;
    bool scanFinished = false;
    QObject::connect(controller,
                     &zzrk::gui::MainWorkspaceController::scanCompleted,
                     &scanLoop, [&] {
                       scanFinished = true;
                       scanLoop.quit();
                     });
    QTimer::singleShot(2000, &scanLoop, &QEventLoop::quit);
    scanLoop.exec();
    if (!scanFinished)
      return false;

    QEventLoop preparationLoop;
    bool preparationFinished = false;
    QObject::connect(
        controller,
        &zzrk::gui::MainWorkspaceController::firmwarePreparationCompleted,
        &preparationLoop, [&](const bool success) {
          preparationFinished = success;
          preparationLoop.quit();
        });
    stage = "prepare cancellation fixture";
    harness->loadFirmwareImage(QStringLiteral("cancel-test.img"));
    QTimer::singleShot(2000, &preparationLoop, &QEventLoop::quit);
    preparationLoop.exec();
    auto *startUpgrade = window->findChild<QPushButton *>(
        QStringLiteral("startUpgradeButton"));
    auto *cancelUpgrade = window->findChild<QPushButton *>(
        QStringLiteral("cancelUpgradeButton"));
    if (!preparationFinished || startUpgrade == nullptr ||
        cancelUpgrade == nullptr || !startUpgrade->isEnabled())
      return false;

    backend->holdUpgrade.store(true);
    stage = "start cancellable upgrade";
    startUpgrade->click();
    QEventLoop upgradeStartedLoop;
    QTimer pollUpgrade;
    pollUpgrade.setInterval(2);
    QObject::connect(&pollUpgrade, &QTimer::timeout, &upgradeStartedLoop, [&] {
      if (backend->upgradeEntered.load())
        upgradeStartedLoop.quit();
    });
    pollUpgrade.start();
    QTimer::singleShot(2000, &upgradeStartedLoop, &QEventLoop::quit);
    upgradeStartedLoop.exec();
    if (!backend->upgradeEntered.load())
      return false;
    stage = "cancel upgrade";
    cancelUpgrade->click();
    QCoreApplication::processEvents();
    const bool upgradeCancellationRequested =
        controller->viewModel()->upgradeCancellation()->load();
    const bool upgradeCancelDisabled =
        upgradeCancellationRequested && !cancelUpgrade->isEnabled();
    QEventLoop upgradeFinishedLoop;
    QObject::connect(controller,
                     &zzrk::gui::MainWorkspaceController::upgradeCompleted,
                     &upgradeFinishedLoop, &QEventLoop::quit);
    backend->holdUpgrade.store(false);
    QTimer::singleShot(2000, &upgradeFinishedLoop, &QEventLoop::quit);
    upgradeFinishedLoop.exec();

    auto *readPartitions = window->findChild<QToolButton *>(
        QStringLiteral("readPartitionsButton"));
    if (readPartitions == nullptr || !readPartitions->isEnabled())
      return false;
    QEventLoop tableLoop;
    bool tableFinished = false;
    QObject::connect(
        controller, &zzrk::gui::MainWorkspaceController::partitionTableCompleted,
        &tableLoop, [&](const bool success) {
          tableFinished = success;
          tableLoop.quit();
        });
    stage = "read cancellation fixture partitions";
    readPartitions->click();
    QTimer::singleShot(2000, &tableLoop, &QEventLoop::quit);
    tableLoop.exec();
    auto *partitions = window->findChild<QTableWidget *>(
        QStringLiteral("partitionTable"));
    auto *erasePartition = window->findChild<QPushButton *>(
        QStringLiteral("erasePartitionButton"));
    auto *cancelPartition = window->findChild<QPushButton *>(
        QStringLiteral("cancelPartitionButton"));
    if (!tableFinished || partitions == nullptr || erasePartition == nullptr ||
        cancelPartition == nullptr || partitions->rowCount() == 0)
      return false;
    partitions->selectRow(0);
    backend->holdPartition.store(true);
    stage = "start cancellable partition erase";
    erasePartition->click();
    QEventLoop partitionStartedLoop;
    QTimer pollPartition;
    pollPartition.setInterval(2);
    QObject::connect(&pollPartition, &QTimer::timeout, &partitionStartedLoop,
                     [&] {
                       if (backend->partitionEntered.load())
                         partitionStartedLoop.quit();
                     });
    pollPartition.start();
    QTimer::singleShot(2000, &partitionStartedLoop, &QEventLoop::quit);
    partitionStartedLoop.exec();
    if (!backend->partitionEntered.load())
      return false;
    stage = "cancel partition erase";
    cancelPartition->click();
    QCoreApplication::processEvents();
    const bool partitionCancellationRequested =
        controller->viewModel()->partitionCancellation()->load();
    const bool partitionCancelDisabled =
        partitionCancellationRequested && !cancelPartition->isEnabled();
    QEventLoop partitionFinishedLoop;
    QObject::connect(
        controller,
        &zzrk::gui::MainWorkspaceController::partitionOperationCompleted,
        &partitionFinishedLoop, &QEventLoop::quit);
    backend->holdPartition.store(false);
    QTimer::singleShot(2000, &partitionFinishedLoop, &QEventLoop::quit);
    partitionFinishedLoop.exec();
    return upgradeCancelDisabled && partitionCancelDisabled;
  };
  if (!cancellationButtonsStayDisabled()) {
    std::cerr << "FAIL: cancellation controls at " << stage << '\n';
    return EXIT_FAILURE;
  }

  backend->scans.store(0);
  backend->preparations.store(0);
  backend->upgrades.store(0);
  backend->inspections.store(0);
  backend->partitionReads.store(0);
  backend->partitionOperations.store(0);
  backend->upgradeEntered.store(false);
  backend->partitionEntered.store(false);

  bool started = false;
  bool inspectionCompleted = false;
  bool partitionsCompleted = false;
  bool partitionOperationFinished = false;
  bool verifiedWriteFinished = false;
  bool upgradeUiCompleted = false;
  bool observedWorkspaceBusy = false;
  bool failed = false;
  int completedScans = 0;
  std::vector<std::pair<int, int>> progressValues;

  QObject::connect(controller->viewModel(),
                   &zzrk::gui::MainWorkspaceViewModel::busyChanged,
                   &application, [&](const bool busy) {
                     observedWorkspaceBusy = observedWorkspaceBusy || busy;
                   });

  QObject::connect(controller,
                   &zzrk::gui::MainWorkspaceController::scanCompleted,
                   &application, [&] {
    ++completedScans;
    stage = "scan completion";
    if (completedScans == 1) {
      if (backend->monitoringStarts.load() != 1) {
        failed = true;
        application.exit(EXIT_FAILURE);
        return;
      }
      backend->emitDeviceChange(zzrk::transport::UsbHotplugEvent::Arrived);
      backend->emitDeviceChange(zzrk::transport::UsbHotplugEvent::Left);
      backend->emitDeviceChange(zzrk::transport::UsbHotplugEvent::Arrived);
      return;
    }
    if (upgradeUiCompleted) {
      failed = failed || completedScans != 3 || backend->scans.load() != 3;
      application.exit(failed ? EXIT_FAILURE : EXIT_SUCCESS);
      return;
    }
    if (started) {
      return;
    }
    if (completedScans != 2 || backend->scans.load() != 2) {
      failed = true;
      application.exit(EXIT_FAILURE);
      return;
    }
    started = true;
    const auto *upgradePage = window->findChild<zzrk::gui::FullUpgradePage *>(
        QStringLiteral("fullUpgradePage"));
    const auto *partitionPage = window->findChild<zzrk::gui::PartitionPage *>(
        QStringLiteral("partitionPage"));
    if (upgradePage == nullptr || partitionPage == nullptr) {
      failed = true;
      application.exit(EXIT_FAILURE);
      return;
    }
    auto *devices =
        window->findChild<QComboBox *>(QStringLiteral("statusDeviceCombo"));
    if (devices == nullptr || devices->count() != 1 ||
        devices->currentData().toString() != QStringLiteral("1-5")) {
      failed = true;
      application.exit(EXIT_FAILURE);
      return;
    }
    auto *inspect =
        window->findChild<QPushButton *>(QStringLiteral("inspectDeviceButton"));
    if (inspect == nullptr || !inspect->isEnabled()) {
      failed = true;
      application.exit(EXIT_FAILURE);
      return;
    }
    stage = "device inspection";
    inspect->click();
  });

  QObject::connect(
      controller,
      &zzrk::gui::MainWorkspaceController::deviceInspectionCompleted,
      &application,
      [&](const bool success) {
        const auto *chipInfo =
            window->findChild<QLabel *>(QStringLiteral("deviceDetailStatus"));
        auto *readPartitions = window->findChild<QToolButton *>(
            QStringLiteral("readPartitionsButton"));
        inspectionCompleted =
            success && chipInfo != nullptr &&
            chipInfo->text().contains(QStringLiteral("读取完成")) &&
            readPartitions != nullptr && readPartitions->isEnabled();
        if (!inspectionCompleted) {
          failed = true;
          application.exit(EXIT_FAILURE);
          return;
        }
        stage = "partition table";
        readPartitions->click();
      });

  QObject::connect(
      controller, &zzrk::gui::MainWorkspaceController::partitionTableCompleted,
      &application,
      [&](const bool success) {
        auto *partitions =
            window->findChild<QTableWidget *>(QStringLiteral("partitionTable"));
        if (!success || partitions == nullptr || partitions->rowCount() != 2 ||
            partitions->item(0, 0) == nullptr) {
          failed = true;
          application.exit(EXIT_FAILURE);
          return;
        }
        partitions->selectRow(0);
        const auto *backup = window->findChild<QPushButton *>(
            QStringLiteral("backupPartitionButton"));
        const auto *write = window->findChild<QPushButton *>(
            QStringLiteral("writePartitionButton"));
        auto *erase = window->findChild<QPushButton *>(
            QStringLiteral("erasePartitionButton"));
        const auto *verify = window->findChild<QCheckBox *>(
            QStringLiteral("verifyPartitionWriteCheckBox"));
        partitionsCompleted = backup != nullptr && backup->isEnabled() &&
                              write != nullptr && write->isEnabled() &&
                              erase != nullptr && erase->isEnabled() &&
                              verify != nullptr && verify->isChecked();
        if (!partitionsCompleted) {
          failed = true;
          application.exit(EXIT_FAILURE);
          return;
        }
        stage = "partition erase";
        erase->click();
      });

  QObject::connect(
      controller,
      &zzrk::gui::MainWorkspaceController::partitionOperationCompleted,
      &application, [&](const bool success) {
        const auto *progress = window->findChild<QProgressBar *>(
            QStringLiteral("partitionProgress"));
        const auto *status =
            window->findChild<QLabel *>(QStringLiteral("partitionStatus"));
        if (partitionOperationFinished) {
          // 完成真实控制器的模拟写入后切换语言，连同校验后缀一起验证。
          auto *translations = zzrk::gui::GuiTranslations::active();
          verifiedWriteFinished = success && status != nullptr &&
              status->text().contains(QStringLiteral("回读校验通过")) &&
              translations != nullptr && translations->install("en");
          QCoreApplication::sendPostedEvents(nullptr, QEvent::LanguageChange);
          verifiedWriteFinished = verifiedWriteFinished &&
              status->text().startsWith(QStringLiteral("Write complete")) &&
              status->text().contains(QStringLiteral("readback verification passed"));
          if (!verifiedWriteFinished || !translations->install("zh_CN")) {
            failed = true;
            application.exit(EXIT_FAILURE);
            return;
          }
          QCoreApplication::sendPostedEvents(nullptr, QEvent::LanguageChange);
          stage = "firmware preparation";
          harness->loadFirmwareImage(QStringLiteral("automated-update.img"));
          return;
        }
        partitionOperationFinished =
            success && progress != nullptr && progress->value() == 100 &&
            status != nullptr &&
            status->text().startsWith(QStringLiteral("擦除完成"));
        if (!partitionOperationFinished) {
          failed = true;
          application.exit(EXIT_FAILURE);
          return;
        }
        QTimer::singleShot(0, window, [&] {
          auto *dialog = window->findChild<QDialog *>(
              QStringLiteral("fileSelectionDialog"));
          auto *browser =
              dialog != nullptr ? dialog->findChild<QFileDialog *>() : nullptr;
          if (browser == nullptr) {
            failed = true;
            application.exit(EXIT_FAILURE);
            return;
          }
          // 对话框可见且文件名框获得焦点，模拟用户实际编辑文件名的状态。
          auto *fileName = browser->findChild<QLineEdit *>("fileNameEdit");
          if (fileName) {
            fileName->setFocus();
            QCoreApplication::processEvents();
          }
          if (!zzrk::gui::test::enterFilePickerPath(
                  browser, partitionImage.fileName())) {
            std::cerr << "FAIL: partition image selection is not ready\n";
            failed = true;
            dialog->reject();
            application.exit(EXIT_FAILURE);
            return;
          }
          QTimer::singleShot(2000, dialog, &QDialog::reject);
          zzrk::gui::test::filePickerAcceptButton(browser)->click();
        });
        stage = "partition image picker";
        window->findChild<QPushButton *>(QStringLiteral("writePartitionButton"))
            ->click();
      });

  QObject::connect(
      controller,
      &zzrk::gui::MainWorkspaceController::firmwarePreparationCompleted,
      &application, [&](const bool success) {
        auto *start = window->findChild<QPushButton *>(
            QStringLiteral("startUpgradeButton"));
        if (!success || start == nullptr || !start->isEnabled()) {
          failed = true;
          application.exit(EXIT_FAILURE);
          return;
        }
        stage = "firmware upgrade";
        start->click();
      });

  QObject::connect(
      controller, &zzrk::gui::MainWorkspaceController::upgradeProgressChanged,
      &application, [&](const int overall, const int operation) {
        progressValues.emplace_back(overall, operation);
      });

  QObject::connect(controller,
                   &zzrk::gui::MainWorkspaceController::upgradeCompleted,
                   &application, [&] {
    const auto *overall = window->findChild<QProgressBar *>(
        QStringLiteral("overallUpgradeProgress"));
    const auto *operation = window->findChild<QProgressBar *>(
        QStringLiteral("operationUpgradeProgress"));
    const auto *history =
        window->findChild<QTableWidget *>(QStringLiteral("upgradeHistoryTable"));
    const auto *status =
        window->findChild<QLabel *>(QStringLiteral("upgradeStatus"));
    const auto *exportReport =
        window->findChild<QToolButton *>(QStringLiteral("exportReportButton"));
    bool monotonic = true;
    bool independent = false;
    int previous = 0;
    for (const auto &[total, current] : progressValues) {
      monotonic = monotonic && total >= previous;
      independent = independent || total != current;
      previous = total;
    }
    const bool complete =
        overall != nullptr && overall->value() == 100 && operation != nullptr &&
        operation->value() == 100 && history == nullptr &&
        status != nullptr &&
        status->text().startsWith(QStringLiteral("升级完成")) &&
        exportReport == nullptr &&
        !progressValues.empty() && monotonic && independent &&
        observedWorkspaceBusy &&
        controller->viewModel()->overallProgress() == 100 &&
        controller->viewModel()->operationProgress() == 100 &&
        inspectionCompleted && partitionsCompleted &&
        partitionOperationFinished && verifiedWriteFinished && backend->scans.load() >= 1 &&
        backend->preparations.load() == 1 && backend->upgrades.load() == 1 &&
        backend->inspections.load() == 1 &&
        backend->partitionReads.load() == 1 &&
        backend->partitionOperations.load() == 2;
    upgradeUiCompleted = complete;
    failed = failed || !complete;
    if (failed) {
      application.exit(EXIT_FAILURE);
    }
  });

  controller->refreshDevices();

  QTimer::singleShot(5000, &application, [&] {
    std::cerr << "FAIL: upgrade workflow timed out at " << stage
              << " scans=" << completedScans
              << " preparations=" << backend->preparations.load()
              << " upgrades=" << backend->upgrades.load()
              << " partitionOperations=" << backend->partitionOperations.load()
              << '\n';
    failed = true;
    application.exit(EXIT_FAILURE);
  });
  const int result = application.exec();
  if (result != EXIT_SUCCESS)
    std::cerr << "FAIL: upgrade workflow at " << stage << '\n';
  return result;
}
