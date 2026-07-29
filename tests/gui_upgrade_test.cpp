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
    });
    reportEvent({
        .stage = zzrk::workflow::FirmwareUpgradeStage::checkStorage,
        .target = "eMMC",
        .completedBytes = 0U,
        .totalBytes = 0U,
        .flashPhase = std::nullopt,
    });
    reportEvent({
        .stage = zzrk::workflow::FirmwareUpgradeStage::flash,
        .target = "MiniLoaderAll.bin -> eMMC IDBlock",
        .completedBytes = 256U,
        .totalBytes = 512U,
        .flashPhase = zzrk::image::FirmwareFlashPhase::writeLoader,
    });
    reportEvent({
        .stage = zzrk::workflow::FirmwareUpgradeStage::flash,
        .target = "boot_a",
        .completedBytes = 1024U,
        .totalBytes = 1024U,
        .flashPhase = zzrk::image::FirmwareFlashPhase::writePartition,
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
    window.loadFirmwareImage(QStringLiteral("automated-update.img"));
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
        !progressValues.empty() && monotonic && independent &&
        backend->scans.load() >= 1 && backend->preparations.load() == 1 &&
        backend->upgrades.load() == 1;
    failed = failed || !complete;
    application.exit(failed ? EXIT_FAILURE : EXIT_SUCCESS);
  });

  QTimer::singleShot(5000, &application, [&] {
    failed = true;
    application.exit(EXIT_FAILURE);
  });
  return application.exec();
}
