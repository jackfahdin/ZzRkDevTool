// SPDX-License-Identifier: MIT

#include "GuiOperationBackend.hpp"
#include "MainWindow.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QTableWidget>
#include <QTimer>
#include <QToolButton>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <memory>
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

  [[nodiscard]] zzrk::workflow::DeviceInspectionResult
  inspectDevice(const std::string &portPath) override {
    ++inspections;
    auto device = scanDevices().devices.front();
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
        std::byte{'E'}, std::byte{'M'}, std::byte{'M'}, std::byte{'C'},
        std::byte{' '},
    };
    return zzrk::workflow::DeviceInspection{
        .device = std::move(device),
        .interfaceInfo = zzrk::transport::FlashInterfaceInfo{
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
    auto device = scanDevices().devices.front();
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
  [[nodiscard]] zzrk::workflow::PartitionOperationResult partitionOperation(
      const zzrk::workflow::PartitionOperationPhase phase,
      const std::string &partition, const bool verified,
      const zzrk::workflow::PartitionOperationProgressCallback &progress,
      const zzrk::workflow::DeviceManagementCancellationCallback &cancellation) {
    ++partitionOperations;
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

int main(int argc, char *argv[]) {
  QApplication application(argc, argv);
  auto backend = std::make_shared<AutomatedGuiBackend>();
  MainWindow window{backend, false};
  window.show();

  if (auto *autoRefresh = window.findChild<QCheckBox *>(
          QStringLiteral("autoRefreshCheckBox"))) {
    autoRefresh->setChecked(false);
  }

  bool started = false;
  bool inspectionCompleted = false;
  bool partitionsCompleted = false;
  bool partitionOperationFinished = false;
  bool failed = false;
  std::vector<std::pair<int, int>> progressValues;

  QObject::connect(&window, &MainWindow::scanCompleted, &application, [&] {
    if (started) {
      return;
    }
    started = true;
    auto *devices =
        window.findChild<QTableWidget *>(QStringLiteral("deviceTable"));
    if (devices == nullptr || devices->rowCount() != 1) {
      failed = true;
      application.exit(EXIT_FAILURE);
      return;
    }
    devices->selectRow(0);
    auto *inspect = window.findChild<QPushButton *>(
        QStringLiteral("inspectDeviceButton"));
    if (inspect == nullptr || !inspect->isEnabled()) {
      failed = true;
      application.exit(EXIT_FAILURE);
      return;
    }
    inspect->click();
  });

  QObject::connect(&window, &MainWindow::deviceInspectionCompleted,
                   &application, [&](const bool success) {
                     const auto *chipInfo = window.findChild<QLabel *>(
                         QStringLiteral("deviceDetailStatus"));
                     auto *readPartitions = window.findChild<QToolButton *>(
                         QStringLiteral("readPartitionsButton"));
                     inspectionCompleted = success && chipInfo != nullptr &&
                         chipInfo->text().contains(QStringLiteral("读取完成")) &&
                         readPartitions != nullptr &&
                         readPartitions->isEnabled();
                     if (!inspectionCompleted) {
                       failed = true;
                       application.exit(EXIT_FAILURE);
                       return;
                     }
                     readPartitions->click();
                   });

  QObject::connect(&window, &MainWindow::partitionTableCompleted,
                   &application, [&](const bool success) {
                     auto *partitions = window.findChild<QTableWidget *>(
                         QStringLiteral("partitionTable"));
                     if (!success || partitions == nullptr ||
                         partitions->rowCount() != 2 ||
                         partitions->item(0, 0) == nullptr) {
                       failed = true;
                       application.exit(EXIT_FAILURE);
                       return;
                     }
                     partitions->selectRow(0);
                     const auto *backup = window.findChild<QPushButton *>(
                         QStringLiteral("backupPartitionButton"));
                     const auto *write = window.findChild<QPushButton *>(
                         QStringLiteral("writePartitionButton"));
                     auto *erase = window.findChild<QPushButton *>(
                         QStringLiteral("erasePartitionButton"));
                     const auto *verify = window.findChild<QCheckBox *>(
                         QStringLiteral("verifyPartitionWriteCheckBox"));
                     partitionsCompleted = backup != nullptr &&
                         backup->isEnabled() && write != nullptr &&
                         write->isEnabled() && erase != nullptr &&
                         erase->isEnabled() && verify != nullptr &&
                         verify->isChecked();
                     if (!partitionsCompleted) {
                       failed = true;
                       application.exit(EXIT_FAILURE);
                       return;
                     }
                     erase->click();
                   });

  QObject::connect(&window, &MainWindow::partitionOperationCompleted,
                   &application, [&](const bool success) {
                     const auto *progress = window.findChild<QProgressBar *>(
                         QStringLiteral("partitionProgress"));
                     const auto *status = window.findChild<QLabel *>(
                         QStringLiteral("partitionStatus"));
                     partitionOperationFinished = success &&
                         progress != nullptr && progress->value() == 100 &&
                         status != nullptr &&
                         status->text().startsWith(QStringLiteral("擦除完成"));
                     if (!partitionOperationFinished) {
                       failed = true;
                       application.exit(EXIT_FAILURE);
                       return;
                     }
                     window.loadFirmwareImage(
                         QStringLiteral("automated-update.img"));
                   });

  QObject::connect(&window, &MainWindow::firmwarePreparationCompleted,
                   &application, [&](const bool success) {
                     auto *start = window.findChild<QPushButton *>(
                         QStringLiteral("startUpgradeButton"));
                     if (!success || start == nullptr || !start->isEnabled()) {
                       failed = true;
                       application.exit(EXIT_FAILURE);
                       return;
                     }
                     start->click();
                   });

  QObject::connect(&window, &MainWindow::upgradeProgressChanged, &application,
                   [&](const int overall, const int operation) {
                     progressValues.emplace_back(overall, operation);
                   });

  QObject::connect(&window, &MainWindow::upgradeCompleted, &application, [&] {
    const auto *overall = window.findChild<QProgressBar *>(
        QStringLiteral("overallUpgradeProgress"));
    const auto *operation = window.findChild<QProgressBar *>(
        QStringLiteral("operationUpgradeProgress"));
    const auto *history =
        window.findChild<QTableWidget *>(QStringLiteral("upgradeHistoryTable"));
    const auto *status =
        window.findChild<QLabel *>(QStringLiteral("upgradeStatus"));
    const auto *exportReport = window.findChild<QToolButton *>(
        QStringLiteral("exportReportButton"));
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
        operation->value() == 100 && history != nullptr &&
        history->rowCount() == 1 && history->item(0, 3) != nullptr &&
        history->item(0, 3)->text() == QStringLiteral("已完成") &&
        status != nullptr &&
        status->text().startsWith(QStringLiteral("升级完成")) &&
        exportReport != nullptr && exportReport->isEnabled() &&
        !progressValues.empty() && monotonic && independent &&
        inspectionCompleted && partitionsCompleted &&
        partitionOperationFinished &&
        backend->scans.load() >= 1 && backend->preparations.load() == 1 &&
        backend->upgrades.load() == 1 && backend->inspections.load() == 1 &&
        backend->partitionReads.load() == 1 &&
        backend->partitionOperations.load() == 1;
    failed = failed || !complete;
    application.exit(failed ? EXIT_FAILURE : EXIT_SUCCESS);
  });

  QTimer::singleShot(5000, &application, [&] {
    failed = true;
    application.exit(EXIT_FAILURE);
  });
  return application.exec();
}
