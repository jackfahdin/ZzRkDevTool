// SPDX-License-Identifier: MIT

#include "GuiOperationBackend.hpp"
#include "GuiTestHarness.hpp"
#include "MainWorkspaceController.hpp"

#include <ZzPureTools/ZzApplicationWindow.h>

#include <QCheckBox>
#include <QComboBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QProgressBar>
#include <QPushButton>
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <ranges>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>

namespace {

using namespace std::chrono_literals;

zzrk::workflow::FirmwareUpgradeError unusedUpgradeError() {
  zzrk::workflow::FirmwareUpgradeError error{};
  error.code = zzrk::workflow::FirmwareUpgradeErrorCode::imageInvalid;
  error.stage = zzrk::workflow::FirmwareUpgradeStage::parseImage;
  error.target = "unused";
  return error;
}

zzrk::workflow::DeviceManagementError unusedManagementError() {
  zzrk::workflow::DeviceManagementError error{};
  error.code = zzrk::workflow::DeviceManagementErrorCode::deviceUnsupported;
  error.target = "unused";
  return error;
}

class DownloadGuiBackend final : public zzrk::gui::IGuiOperationBackend {
public:
  std::atomic_int scans{};
  std::atomic_int preparations{};
  std::atomic_int downloads{};
  std::unordered_map<std::string, std::filesystem::path> paths;
  std::unordered_map<std::string, bool> verifications;

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
  startDeviceMonitoring(
      zzrk::transport::UsbHotplugEventCallback eventCallback,
      zzrk::transport::UsbHotplugErrorCallback errorCallback) override {
    std::lock_guard lock{monitoringMutex_};
    eventCallback_ = std::move(eventCallback);
    errorCallback_ = std::move(errorCallback);
    return std::nullopt;
  }

  void stopDeviceMonitoring() noexcept override {
    std::lock_guard lock{monitoringMutex_};
    eventCallback_ = {};
    errorCallback_ = {};
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
    ++preparations;
    auto parsed =
        zzrk::image::parseRockchipParameterFile(request.parameterPath);
    if (const auto *error =
            std::get_if<zzrk::image::FirmwareImageError>(&parsed)) {
      zzrk::workflow::ImageDownloadError result{};
      result.code = zzrk::workflow::ImageDownloadErrorCode::layoutInvalid;
      result.stage = zzrk::workflow::ImageDownloadStage::inspectFiles;
      result.target = request.parameterPath.string();
      result.imageError = *error;
      return result;
    }
    zzrk::workflow::PreparedImageDownloadPlan plan{};
    plan.layoutSource = request.layoutSource;
    plan.layout =
        std::get<zzrk::image::RockchipParameterLayout>(std::move(parsed));
    plan.minimumDeviceSectors = 34U;
    plan.items.push_back(zzrk::workflow::PreparedImageDownloadItem{
        .kind = zzrk::workflow::ImageDownloadItemKind::partitionTable,
        .target = "GPT",
        .sourcePath = request.parameterPath,
        .firstLba = 0U,
        .sectorCount = 67U,
        .outputSize = 67U * 512U,
        .format = zzrk::image::PayloadFormat::rockchipParameter,
        .verify = request.verifyPartitionTable,
        .md5 = {},
        .payload = {},
        .loader = {},
    });
    for (const auto &selection : request.partitions) {
      paths.insert_or_assign(selection.target, selection.imagePath);
      verifications.insert_or_assign(selection.target, selection.verify);
      auto inspected = zzrk::image::inspectFirmwarePayload(selection.imagePath);
      const auto payload =
          std::get<zzrk::image::FirmwarePayloadSource>(std::move(inspected));
      const auto partition =
          std::ranges::find(plan.layout.partitions, selection.target,
                            &zzrk::image::FirmwarePartition::name);
      plan.items.push_back(zzrk::workflow::PreparedImageDownloadItem{
          .kind = zzrk::workflow::ImageDownloadItemKind::partition,
          .target = selection.target,
          .sourcePath = selection.imagePath,
          .firstLba = partition->firstLba,
          .sectorCount = partition->sectorCount.value_or(0U),
          .outputSize = payload.logicalSize,
          .format = payload.format,
          .verify = selection.verify,
          .md5 = {},
          .payload = payload,
          .loader = {},
      });
    }
    return plan;
  }

  [[nodiscard]] zzrk::workflow::ImageDownloadResult executeImageDownload(
      const zzrk::workflow::ImageDownloadRequest &request,
      zzrk::workflow::PreparedImageDownloadPlan prepared,
      const zzrk::workflow::ImageDownloadProgressCallback &progress,
      const zzrk::workflow::ImageDownloadCancellationCallback &cancellation)
      override {
    ++downloads;
    emitDeviceChange();
    std::this_thread::sleep_for(20ms);
    if (progress) {
      progress(zzrk::workflow::ImageDownloadEvent{
          .stage = zzrk::workflow::ImageDownloadStage::write,
          .target = "boot_a",
          .completedBytes = 50U,
          .totalBytes = 100U,
          .completedPlanBytes = 100U,
          .totalPlanBytes = 1000U,
          .itemIndex = 1U,
      });
    }
    std::this_thread::sleep_for(20ms);
    if (cancellation && cancellation()) {
      zzrk::workflow::ImageDownloadError error{};
      error.code = zzrk::workflow::ImageDownloadErrorCode::cancelled;
      error.stage = zzrk::workflow::ImageDownloadStage::write;
      error.target = "boot_a";
      error.itemIndex = 1U;
      return error;
    }
    if (progress) {
      progress(zzrk::workflow::ImageDownloadEvent{
          .stage = zzrk::workflow::ImageDownloadStage::verify,
          .target = "boot_b",
          .completedBytes = 100U,
          .totalBytes = 100U,
          .completedPlanBytes = 700U,
          .totalPlanBytes = 1000U,
          .itemIndex = 2U,
      });
    }
    std::uint32_t verified = 0U;
    for (const auto &item : prepared.items) {
      verified += item.verify ? 1U : 0U;
    }
    return zzrk::workflow::ImageDownloadSuccess{
        .portPath = request.portPath,
        .deviceSectors = 124'190'720U,
        .partitionBytesWritten = 12U,
        .sectorsErased = 0U,
        .itemsWritten = static_cast<std::uint32_t>(prepared.items.size()),
        .itemsVerified = verified,
        .loaderWritten = false,
        .partitionTableWritten = true,
        .resetAccepted = request.reboot,
    };
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
  void emitDeviceChange() {
    zzrk::transport::UsbHotplugEventCallback callback;
    {
      std::lock_guard lock{monitoringMutex_};
      callback = eventCallback_;
    }
    if (callback) {
      callback(zzrk::transport::UsbHotplugEvent::Left);
    }
  }

  std::mutex monitoringMutex_;
  zzrk::transport::UsbHotplugEventCallback eventCallback_;
  zzrk::transport::UsbHotplugErrorCallback errorCallback_;
};

bool writeFile(const QString &path, const QByteArray &contents) {
  QFile file{path};
  return file.open(QIODevice::WriteOnly | QIODevice::Truncate) &&
         file.write(contents) == contents.size();
}

int findTargetRow(const QTableWidget &table, const QString &target) {
  for (int row = 0; row < table.rowCount(); ++row) {
    const auto *item = table.item(row, 1);
    if (item != nullptr && item->text() == target) {
      return row;
    }
  }
  return -1;
}

} // namespace

int main() {
  auto backend = std::make_shared<DownloadGuiBackend>();
  auto harness = zzrk::gui::test::GuiTestHarness::create(backend);
  if (harness == nullptr || harness->application() == nullptr ||
      harness->window() == nullptr || harness->controller() == nullptr) {
    return EXIT_FAILURE;
  }
  auto &application = *harness->application();
  auto *window = harness->window();
  auto *controller = harness->controller();
  QTemporaryDir directory;
  if (!directory.isValid()) {
    return EXIT_FAILURE;
  }
  const QString parameterPath = directory.filePath("parameter.txt");
  const QString genericBootPath = directory.filePath("boot.img");
  const QString exactBootBPath = directory.filePath("boot_b.img");
  const QByteArray parameter{"MACHINE_MODEL: gui-rk3588\n"
                             "TYPE: GPT\n"
                             "CMDLINE:mtdparts=rk29xxnand:0x100@0x8000(boot_a),"
                             "0x100@0x8100(boot_b),-@0x8200(userdata:grow)\n"};
  if (!writeFile(parameterPath, parameter) ||
      !writeFile(genericBootPath, QByteArray{"generic"}) ||
      !writeFile(exactBootBPath, QByteArray{"exact"})) {
    return EXIT_FAILURE;
  }

  window->show();
  bool started = false;
  bool completed = false;
  bool failed = false;
  bool observedIndependentProgress = false;
  bool observedUsbBusy = false;

  const auto finishWhenRefreshed = [&] {
    if (completed && backend->scans.load() >= 2) {
      application.exit(failed ? EXIT_FAILURE : EXIT_SUCCESS);
    }
  };

  QObject::connect(controller,
                   &zzrk::gui::MainWorkspaceController::scanCompleted,
                   &application, [&] {
    if (started) {
      finishWhenRefreshed();
      return;
    }
    started = true;
    auto *tabs = window->findChild<QTabWidget *>(QStringLiteral("mainTabs"));
    auto *table =
        window->findChild<QTableWidget *>(QStringLiteral("downloadImageTable"));
    auto *device =
        window->findChild<QComboBox *>(QStringLiteral("downloadDeviceCombo"));
    if (tabs == nullptr || tabs->tabText(0) != QStringLiteral("下载镜像") ||
        table == nullptr || device == nullptr ||
        device->currentData().toString() != QStringLiteral("1-5")) {
      failed = true;
      application.exit(EXIT_FAILURE);
      return;
    }

    harness->loadDownloadParameter(parameterPath);
    harness->loadDownloadDirectory(directory.path());
    const int bootA = findTargetRow(*table, QStringLiteral("boot_a"));
    const int bootB = findTargetRow(*table, QStringLiteral("boot_b"));
    const int userdata = findTargetRow(*table, QStringLiteral("userdata"));
    const bool matches =
        bootA >= 0 && bootB >= 0 && userdata >= 0 &&
        QFileInfo{table->item(bootA, 4)->text()}.fileName() ==
            QStringLiteral("boot.img") &&
        QFileInfo{table->item(bootB, 4)->text()}.fileName() ==
            QStringLiteral("boot_b.img") &&
        table->item(bootA, 0)->checkState() == Qt::Checked &&
        table->item(bootB, 0)->checkState() == Qt::Checked &&
        table->item(userdata, 0)->checkState() == Qt::Unchecked &&
        table->item(bootA, 7)->checkState() == Qt::Checked &&
        table->item(bootB, 7)->checkState() == Qt::Checked;
    if (!matches) {
      failed = true;
      application.exit(EXIT_FAILURE);
      return;
    }
    table->item(bootA, 0)->setCheckState(Qt::Unchecked);
    table->selectRow(userdata);
    auto *clear = window->findChild<QPushButton *>(
        QStringLiteral("clearDownloadImageButton"));
    if (clear == nullptr) {
      failed = true;
      application.exit(EXIT_FAILURE);
      return;
    }
    clear->click();
    const int rebuiltBootA = findTargetRow(*table, QStringLiteral("boot_a"));
    if (rebuiltBootA < 0 ||
        table->item(rebuiltBootA, 0)->checkState() != Qt::Unchecked) {
      failed = true;
      application.exit(EXIT_FAILURE);
      return;
    }
    table->item(rebuiltBootA, 0)->setCheckState(Qt::Checked);
    table->item(rebuiltBootA, 7)->setCheckState(Qt::Unchecked);
    const int rebuiltBootB = findTargetRow(*table, QStringLiteral("boot_b"));
    table->selectRow(rebuiltBootB);
    auto *choose = window->findChild<QPushButton *>(
        QStringLiteral("chooseDownloadImageButton"));
    QTimer::singleShot(0, &application, [&] {
      auto *dialog =
          qobject_cast<QFileDialog *>(application.activeModalWidget());
      if (dialog == nullptr) {
        std::cerr << "FAIL: image chooser dialog did not open\n";
        failed = true;
        application.exit(EXIT_FAILURE);
        return;
      }
      dialog->selectFile(exactBootBPath);
      QMetaObject::invokeMethod(dialog, "accept", Qt::DirectConnection);
    });
    if (choose == nullptr) {
      failed = true;
      application.exit(EXIT_FAILURE);
      return;
    }
    choose->click();
    const int preservedBootA = findTargetRow(*table, QStringLiteral("boot_a"));
    const int preservedBootB = findTargetRow(*table, QStringLiteral("boot_b"));
    if (preservedBootA < 0 || preservedBootB < 0 ||
        table->item(preservedBootA, 0)->checkState() != Qt::Checked ||
        table->item(preservedBootA, 7)->checkState() != Qt::Unchecked) {
      std::cerr << "FAIL: download row choices were reset while rebuilding\n";
      failed = true;
      application.exit(EXIT_FAILURE);
      return;
    }
    table->item(preservedBootB, 7)->setCheckState(Qt::Unchecked);
    auto *start = window->findChild<QPushButton *>(
        QStringLiteral("startImageDownloadButton"));
    if (start == nullptr || !start->isEnabled()) {
      failed = true;
      application.exit(EXIT_FAILURE);
      return;
    }
    start->click();
  });

  QObject::connect(
      controller,
      &zzrk::gui::MainWorkspaceController::imageDownloadProgressChanged,
      &application,
      [&](const int overall, const int operation) {
        observedIndependentProgress =
            observedIndependentProgress || overall != operation;
        const auto *device = window->findChild<QComboBox *>(
            QStringLiteral("downloadDeviceCombo"));
        const auto *refresh =
            window->findChild<QToolButton *>(QStringLiteral("refreshButton"));
        observedUsbBusy =
            observedUsbBusy || (device != nullptr && !device->isEnabled() &&
                                refresh != nullptr && !refresh->isEnabled());
      });

  QObject::connect(
      controller, &zzrk::gui::MainWorkspaceController::imageDownloadCompleted,
      &application,
      [&](const bool success) {
        auto *table = window->findChild<QTableWidget *>(
            QStringLiteral("downloadImageTable"));
        const auto *overall = window->findChild<QProgressBar *>(
            QStringLiteral("overallImageDownloadProgress"));
        const auto *operation = window->findChild<QProgressBar *>(
            QStringLiteral("operationImageDownloadProgress"));
        const int bootA = findTargetRow(*table, QStringLiteral("boot_a"));
        const int bootB = findTargetRow(*table, QStringLiteral("boot_b"));
        failed = failed || !success || backend->preparations.load() != 1 ||
                 backend->downloads.load() != 1 ||
                 backend->paths["boot_a"].filename() != "boot.img" ||
                 backend->paths["boot_b"].filename() != "boot_b.img" ||
                 backend->verifications["boot_a"] ||
                 backend->verifications["boot_b"] || overall == nullptr ||
                 overall->value() != 100 || operation == nullptr ||
                 operation->value() != 100 || bootA < 0 || bootB < 0 ||
                 table->item(bootA, 8)->text() != QStringLiteral("完成") ||
                 table->item(bootB, 8)->text() != QStringLiteral("完成") ||
                 !observedIndependentProgress || !observedUsbBusy;
        completed = true;
        finishWhenRefreshed();
      });

  QTimer::singleShot(5000, &application, [&] {
    failed = true;
    application.exit(EXIT_FAILURE);
  });
  return application.exec();
}
