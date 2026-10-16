// SPDX-License-Identifier: MIT
#include "GuiTestHarness.hpp"
#include "FilePickerTestSupport.hpp"
#include "GuiTranslations.hpp"
#include "MainWorkspaceController.hpp"
#include "MainWorkspaceViewModel.hpp"
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileDialog>
#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStatusBar>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QToolButton>
#include <ZzPureTools/ZzApplicationWindow.h>
#include <iostream>
#include <mutex>

using namespace zzrk;
namespace {
int failures = 0;
void expect(bool ok, const char *description) {
  if (!ok) {
    ++failures;
    std::cerr << "FAIL: " << description << '\n';
  }
}
bool waitFor(const std::function<bool()> &condition) {
  QElapsedTimer timer;
  timer.start();
  while (!condition() && timer.elapsed() < 3000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    QThread::msleep(1);
  }
  return condition();
}
transport::UsbDeviceInfo device(std::string port) {
  return {.vendorId = 0x2207,
          .productId = 0x350b,
          .bcdUsb = 0x0201,
          .bcdDevice = 0x0223,
          .busNumber = 1,
          .portNumbers = {5},
          .portPath = std::move(port),
          .mode = transport::DeviceMode::Loader,
          .deviceAddress = 7};
}
// 只替换外部 USB 边界；选择、缓存失效、控件和异步完成均运行真实 GUI 代码。
class Backend final : public gui::IGuiOperationBackend {
public:
  std::mutex mutex;
  transport::ScanResult next{{device("1-5"), device("1-6")}, {}};
  std::string requestedPort;
  transport::UsbHotplugErrorCallback monitoringError;
  transport::ScanResult scanDevices() override {
    std::lock_guard lock(mutex);
    return next;
  }
  std::optional<transport::UsbHotplugError>
  startDeviceMonitoring(transport::UsbHotplugEventCallback,
                        transport::UsbHotplugErrorCallback error) override {
    monitoringError = std::move(error);
    return {};
  }
  void stopDeviceMonitoring() noexcept override {}
  workflow::PreparedFirmwareUpgradeResult
  prepareFirmware(const std::filesystem::path &) override {
    return workflow::FirmwareUpgradeError{};
  }
  workflow::FirmwareUpgradeResult executeUpgrade(
      const workflow::FirmwareUpgradeRequest &,
      workflow::PreparedFirmwareUpgrade,
      const workflow::FirmwareUpgradeProgressCallback &,
      const workflow::FirmwareUpgradeCancellationCallback &) override {
    return workflow::FirmwareUpgradeError{};
  }
  workflow::PreparedImageDownloadResult
  prepareImageDownload(const workflow::ImageDownloadPrepareRequest &) override {
    return workflow::ImageDownloadError{};
  }
  workflow::ImageDownloadResult executeImageDownload(
      const workflow::ImageDownloadRequest &,
      workflow::PreparedImageDownloadPlan,
      const workflow::ImageDownloadProgressCallback &,
      const workflow::ImageDownloadCancellationCallback &) override {
    return workflow::ImageDownloadError{};
  }
  workflow::DeviceInspectionResult inspectDevice(const std::string &) override {
    return workflow::DeviceManagementError{};
  }
  workflow::DevicePartitionTableResult
  readPartitions(const std::string &port) override {
    {
      std::lock_guard lock(mutex);
      requestedPort = port;
    }
    storage::GptPartitionTable table{};
    table.partitions = {{.name = "boot_a",
                         .firstLba = 32768,
                         .lastLba = 196607,
                         .attributes = 0}};
    return workflow::DevicePartitionTable{.device = device(port),
                                          .deviceSectors = 124190720,
                                          .table = std::move(table)};
  }
  workflow::PartitionOperationResult readPartition(
      const std::string &, const std::string &, const std::filesystem::path &,
      const workflow::PartitionOperationProgressCallback &,
      const workflow::DeviceManagementCancellationCallback &) override {
    return workflow::DeviceManagementError{};
  }
  workflow::PartitionOperationResult writePartition(
      const std::string &, const std::string &, const std::filesystem::path &,
      bool, const workflow::PartitionOperationProgressCallback &,
      const workflow::DeviceManagementCancellationCallback &) override {
    return workflow::DeviceManagementError{};
  }
  workflow::PartitionOperationResult erasePartition(
      const std::string &, const std::string &,
      const workflow::PartitionOperationProgressCallback &,
      const workflow::DeviceManagementCancellationCallback &) override {
    return workflow::DeviceManagementError{};
  }
};
} // namespace
int main(int argc, char **argv) {
  auto backend = std::make_shared<Backend>();
  const bool single = argc > 1 && std::string_view(argv[1]) == "--single";
  if (single)
    backend->next.devices.clear();
  auto harness = gui::test::GuiTestHarness::create(backend);
  if (!harness)
    return 1;
  auto *controller = harness->controller();
  auto *window = harness->window();
  int scans = 0;
  QObject::connect(controller, &gui::MainWorkspaceController::scanCompleted,
                   window, [&] { ++scans; });
  expect(waitFor([&] { return scans == 1; }), "initial scan finishes");
  auto *selector = window->findChild<QComboBox *>("statusDeviceCombo");
  expect(selector != nullptr,
         "global status bar exposes target device selection");
  if (!selector)
    return 1;
  const auto refresh = [&] {
    const int previous = scans;
    controller->refreshDevices();
    return waitFor([&] { return scans > previous; });
  };
  if (single) {
    expect(controller->selectedPortPath().isEmpty() && !selector->isEnabled(),
           "empty initial scan has no selectable target");
    {
      std::lock_guard lock(backend->mutex);
      backend->next.devices = {device("1-5")};
    }
    expect(refresh() && controller->selectedPortPath() == "1-5",
           "first single device is automatically selected");
    {
      std::lock_guard lock(backend->mutex);
      backend->next.devices.push_back(device("1-6"));
    }
    expect(refresh() && controller->selectedPortPath() == "1-5",
           "adding another device preserves current selection");
    controller->selectDevice({});
  }
  expect(controller->selectedPortPath().isEmpty(),
         "multiple devices require explicit selection");
  selector->setCurrentIndex(selector->findData("1-6"));
  expect(controller->selectedPortPath() == "1-6",
         "status selection updates global target");
  controller->readSelectedDevicePartitions();
  expect(!selector->isEnabled(), "USB operation locks global selection");
  // 程序化信号同样不能改变进行中任务的目标，控制器必须验证忙碌状态。
  selector->setCurrentIndex(selector->findData("1-5"));
  expect(controller->selectedPortPath() == "1-6",
         "busy guard rejects target change");
  expect(waitFor([&] {
           return controller->devicePartitionTable().has_value() &&
                  !controller->usbOperationRunning();
         }),
         "selected target returns a real displayed GPT");
  {
    std::lock_guard lock(backend->mutex);
    expect(backend->requestedPort == "1-6", "GPT request uses global target");
  }
  expect(refresh() && controller->selectedPortPath() == "1-6" &&
             controller->devicePartitionTable().has_value(),
         "unchanged scan preserves selection and GPT");
  const auto lockObserver = QObject::connect(
      controller->viewModel(), &gui::MainWorkspaceViewModel::busyChanged,
      window,
      [&](bool busy) {
        if (busy)
          expect(controller->usbOperationRunning(),
                 "busy notification publishes an already locked controller");
      },
      Qt::DirectConnection);
  expect(refresh(), "scan safely publishes busy state");
  QObject::disconnect(lockObserver);
  {
    std::lock_guard lock(backend->mutex);
    backend->next.devices = {device("1-5")};
  }
  expect(refresh() && controller->selectedPortPath().isEmpty() &&
             !controller->devicePartitionTable().has_value(),
         "unplug clears target and GPT without selecting the remaining board");
  expect(!window->findChild<QPushButton *>("inspectDeviceButton")->isEnabled(),
         "no target disables device operation");
  {
    std::lock_guard lock(backend->mutex);
    backend->next.devices.push_back(device("1-6"));
  }
  expect(refresh() && controller->selectedPortPath().isEmpty(),
         "reconnecting the old board still requires explicit selection");
  selector->setCurrentIndex(selector->findData("1-5"));
  expect(controller->selectedPortPath() == "1-5",
         "user can explicitly select remaining board");
  controller->readSelectedDevicePartitions();
  expect(waitFor([&] {
           return controller->devicePartitionTable().has_value() &&
                  !controller->usbOperationRunning();
         }),
         "read GPT before mode change");
  {
    std::lock_guard lock(backend->mutex);
    ++backend->next.devices.front().deviceAddress;
  }
  expect(refresh() && controller->selectedPortPath() == "1-5" &&
             !controller->devicePartitionTable().has_value(),
         "same-port USB re-enumeration invalidates cached GPT");
  controller->readSelectedDevicePartitions();
  expect(waitFor([&] {
           return controller->devicePartitionTable().has_value() &&
                  !controller->usbOperationRunning();
         }),
         "reread GPT after address change");

  // 设备缓存清除时，本地 parameter 和镜像匹配属于用户草稿，必须保留。
  QTemporaryDir fixture;
  const QString parameterPath = fixture.filePath("parameter.txt");
  QFile parameter(parameterPath);
  expect(parameter.open(QIODevice::WriteOnly) &&
             parameter.write(
                 "MACHINE_MODEL: gui-rk3588\nTYPE: "
                 "GPT\nCMDLINE:mtdparts=rk29xxnand:0x100@0x8000(boot_a)\n") > 0,
         "create local partition layout");
  parameter.close();
  QFile image(fixture.filePath("boot.img"));
  expect(image.open(QIODevice::WriteOnly) && image.write("image") == 5,
         "create draft image");
  image.close();
  harness->loadDownloadParameter(parameterPath);
  auto *plan = window->findChild<QTableWidget *>("downloadImageTable");
  int bootRow = -1;
  for (int row = 0; row < plan->rowCount(); ++row)
    if (plan->item(row, 1)->text() == "boot_a")
      bootRow = row;
  expect(bootRow >= 0, "local parameter creates boot row");
  const QString draft =
      bootRow >= 0 ? plan->item(bootRow, 4)->text() : QString{};
  expect(!draft.isEmpty(), "image matching creates a local draft");
  {
    std::lock_guard lock(backend->mutex);
    auto &board = backend->next.devices.front();
    board.mode = transport::DeviceMode::Maskrom;
    board.bcdUsb = 0x0200;
    board.bcdDevice = 0x0100;
  }
  expect(refresh() && controller->selectedPortPath() == "1-5" &&
             !controller->devicePartitionTable().has_value() &&
             !controller->selectedDeviceManageable(),
         "same-port mode change invalidates GPT and management capability");
  expect(window->findChild<QLineEdit *>("downloadParameterPath")->text() ==
                 parameterPath &&
             bootRow >= 0 && plan->item(bootRow, 4)->text() == draft,
         "device identity change retains local layout and image mapping");
  {
    std::lock_guard lock(backend->mutex);
    backend->next.devices.front() = device("1-5");
  }
  expect(refresh(), "restore Loader mode for draft reread test");
  window->findChild<QComboBox *>("downloadLayoutCombo")->setCurrentIndex(0);
  controller->readSelectedDevicePartitions();
  expect(waitFor([&] {
           return controller->devicePartitionTable().has_value() &&
                  !controller->usbOperationRunning();
         }),
         "load device GPT for manual mapping");
  QFile custom(fixture.filePath("custom.img"));
  expect(custom.open(QIODevice::WriteOnly) && custom.write("custom") == 6,
         "create manual image");
  custom.close();
  window->show();
  plan->selectRow(1);
  QTimer::singleShot(0, window, [&] {
    auto *modal = QApplication::activeModalWidget();
    auto *browser = modal ? modal->findChild<QFileDialog *>() : nullptr;
    expect(browser != nullptr, "manual image chooser opens");
    if (browser) {
      const bool ready =
          gui::test::enterFilePickerPath(browser, custom.fileName());
      expect(ready, "manual image is ready in the real file model");
      if (ready)
        gui::test::filePickerAcceptButton(browser)->click();
      // 确认失败也必须退出模态循环，输出断言而不是一直等待 CTest 超时。
      if (modal->isVisible())
        qobject_cast<QDialog *>(modal)->reject();
    } else if (modal)
      modal->close();
  });
  window->findChild<QPushButton *>("chooseDownloadImageButton")->click();
  expect(plan->item(1, 4)->toolTip() == custom.fileName(),
         "manual chooser updates the full image path");
  plan->item(1, 0)->setCheckState(Qt::Unchecked);
  plan->item(1, 7)->setCheckState(Qt::Unchecked);
  {
    std::lock_guard lock(backend->mutex);
    ++backend->next.devices.front().deviceAddress;
  }
  expect(refresh() && !controller->devicePartitionTable(),
         "identity change clears only device GPT");
  controller->readSelectedDevicePartitions();
  expect(waitFor([&] {
           return controller->devicePartitionTable().has_value() &&
                  !controller->usbOperationRunning();
         }),
         "reread new session GPT");
  expect(plan->item(1, 4)->toolTip() == custom.fileName() &&
             plan->item(1, 0)->checkState() == Qt::Unchecked &&
             plan->item(1, 7)->checkState() == Qt::Unchecked,
         "GPT reread preserves manual image, selection and verification draft");
  {
    std::lock_guard lock(backend->mutex);
    backend->next.devices.front().productId = 0x9999;
  }
  expect(refresh() && controller->selectedPortPath() == "1-5" &&
             !controller->selectedDeviceFlashable(),
         "unsupported devices remain visible but cannot flash");
  auto *state = window->findChild<QLabel *>("connectionStatusText");
  expect(selector->toolTip().contains("2207:9999"),
         "device tooltip retains full USB identity");
  {
    std::lock_guard lock(backend->mutex);
    backend->next.error = transport::ScanError{-1, "scan error detail"};
  }
  expect(refresh() && state->text() == "扫描结果不完整" &&
             state->toolTip().contains("scan error detail"),
         "partial scan clearly exposes failure details");
  backend->monitoringError({-1, "hotplug error detail"});
  expect(
      waitFor([&] { return !state->property("hotplugAvailable").toBool(); }) &&
          state->text().contains("热插拔不可用") &&
          controller->refreshButton()->isEnabled(),
      "hotplug failure exposes manual refresh without polling");
  {
    std::lock_guard lock(backend->mutex);
    backend->next = {{device("1-5"), device("1-6")}, {}};
  }
  expect(refresh() && controller->selectedPortPath() == "1-5",
         "manual refresh works after monitor failure");
  window->show();
  gui::setGuiStatus(window->statusBar(), gui::guiText("正在读取设备详情"));
  QCoreApplication::processEvents();
  expect(selector->isVisible() &&
             window->findChild<QLabel *>("statusOperationText")->text() ==
                 "正在读取设备详情",
         "operation messages retain visible device selector");
  const auto screenshots = qEnvironmentVariable("ZZRK_GUI_SCREENSHOT_DIR");
  const auto capture = [&](const QString &name) {
    for (int frame = 0; frame < 3; ++frame) {
      QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
      QCoreApplication::processEvents();
    }
    if (!screenshots.isEmpty())
      expect(QDir{}.mkpath(screenshots) &&
                 window->grab().save(QDir(screenshots).filePath(name)),
             "save status bar screenshot");
    auto *bar = window->findChild<QWidget *>("globalDeviceStatusBar");
    expect(bar->width() <= window->statusBar()->width() &&
               selector->isVisible() &&
               controller->refreshButton()->isVisible(),
           "status controls fit the visible bar");
  };
  capture("device-status-chinese.png");
  int selectionChanges = 0;
  QObject::connect(controller,
                   &gui::MainWorkspaceController::selectedDeviceChanged, window,
                   [&] { ++selectionChanges; });
  expect(gui::GuiTranslations::active()->install("en"),
         "install English translations");
  QCoreApplication::sendPostedEvents(nullptr, QEvent::LanguageChange);
  QCoreApplication::processEvents();
  expect(selector->placeholderText() == "Select device" &&
             state->text().contains("Hotplug unavailable") &&
             window->findChild<QLabel *>("statusOperationText")->text() ==
                 "Reading device details" &&
             controller->selectedPortPath() == "1-5" && selectionChanges == 0,
         "live language updates status text without changing the target");
  capture("device-status-english.png");
  QFont large = QApplication::font();
  large.setPointSize(24);
  QApplication::setFont(large);
  window->resize(900, 640);
  capture("device-status-large.png");
  return failures ? 1 : 0;
}
