// SPDX-License-Identifier: MIT

#include "GuiTestHarness.hpp"
#include "FilePickerTestSupport.hpp"
#include "FileSelectionDialog.hpp"
#include "GuiDialogs.hpp"
#include "GuiTranslations.hpp"
#include "GuiWindowShell.hpp"
#include "MainWorkspaceController.hpp"
#include "pages/DevicePage.hpp"
#include "pages/FullUpgradePage.hpp"
#include "pages/ImageDownloadPage.hpp"
#include "pages/PartitionPage.hpp"

#include <ZzFluentUI/ZzActivityBar.h>
#include <ZzFluentUI/ZzContentDialog.h>
#include <ZzFluentUI/ZzFluentTitleBar.h>
#include <ZzFluentUI/ZzInfoBar.h>
#include <ZzFluentUI/ZzNavigationPane.h>
#include <ZzFluentUI/ZzSidePane.h>
#include <ZzFluentUI/ZzTabBar.h>
#include <ZzFluentUI/ZzTabWidget.h>
#include <ZzFluentUI/ZzThemeController.h>
#include <ZzFluentUI/ZzThemeMode.h>
#include <ZzPureTools/ZzApplicationBuilder.h>
#include <ZzPureTools/ZzApplicationWindow.h>
#include <ZzPureTools/ZzNavigationController.h>
#include <ZzPureTools/ZzNavigationNode.h>
#include <ZzPureTools/ZzPageHost.h>
#include <ZzPureTools/ZzPageInstance.h>
#include <ZzPureTools/ZzPageLifetimePolicy.h>
#include <ZzPureTools/ZzPageRegistration.h>
#include <ZzPureTools/ZzPureApplication.h>
#include <ZzPureTools/ZzRouteId.h>
#include <ZzWindowKit/ZzWindowKitBootstrap.h>

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QDir>
#include <QDialogButtonBox>
#include <QElapsedTimer>
#include <QEvent>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QFileDialog>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QKeyEvent>
#include <QMessageBox>
#include <QPersistentModelIndex>
#include <QPointer>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QScreen>
#include <QStatusBar>
#include <QStyle>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QToolButton>
#include <QTreeView>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
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

bool waitFor(const std::function<bool()> &condition,
             const int timeoutMilliseconds = 2000) {
  QElapsedTimer timer;
  timer.start();
  while (!condition() && timer.elapsed() < timeoutMilliseconds) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    QThread::msleep(1);
  }
  return condition();
}

void checkFilePickers(QWidget *window, ZzFluentUI::ZzThemeController *theme) {
  // 这些检查捕获丢失中文完整路径、确认/取消信号接错及目录模式被当作文件的回归。
  QTemporaryDir fixture;
  expect(fixture.isValid(), "file picker fixture has a temporary directory");
  const QString directory = fixture.filePath(QStringLiteral("镜像 文件夹"));
  expect(QDir{}.mkpath(directory), "file picker fixture creates a Unicode directory");
  const QString imagePath = QDir(directory).filePath(QStringLiteral("测试 固件.img"));
  QFile image(imagePath);
  expect(image.open(QIODevice::WriteOnly) && image.write("firmware") == 8,
         "file picker fixture creates an existing image");
  image.close();
  const auto originalMode = theme->mode();
  const auto respond = [&](const QString &selectedPath, const QString &capture,
                           bool cancel = false) {
    QTimer::singleShot(0, window, [=] {
      auto *dialog = qobject_cast<ZzFluentUI::ZzContentDialog *>(
          QApplication::activeModalWidget());
      expect(dialog != nullptr, "all file picker modes use the Fluent host");
      auto *browser =
          dialog != nullptr ? dialog->findChild<QFileDialog *>() : nullptr;
      expect(browser != nullptr, "Fluent file picker retains Qt file browsing");
      if (browser == nullptr) {
        if (auto *modal =
                qobject_cast<QDialog *>(QApplication::activeModalWidget()))
          modal->reject();
        return;
      }
      const bool selectionReady =
          zzrk::gui::test::enterFilePickerPath(browser, selectedPath);
      expect(selectionReady,
             "file picker selection is ready in the real file model");
      if (!selectionReady) {
        dialog->reject();
        return;
      }
      expect(browser->testOption(QFileDialog::DontUseNativeDialog),
             "file picker stays within the framework on every platform");
      expect(browser->palette().color(QPalette::Base) ==
                 QApplication::palette().color(QPalette::Base),
             "file browsing follows the application theme");
      const auto screenshotDirectory =
          qEnvironmentVariable("ZZRK_GUI_SCREENSHOT_DIR");
      if (!screenshotDirectory.isEmpty() && !capture.isEmpty()) {
        if (auto *tree = browser->findChild<QTreeView *>(QStringLiteral("treeView")))
          waitFor([tree] { return tree->model()->rowCount(tree->rootIndex()) > 0; });
        for (int frame = 0; frame < 3; ++frame) {
          QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
          QApplication::processEvents();
        }
        expect(QDir{}.mkpath(screenshotDirectory) &&
                   dialog->grab().save(QDir(screenshotDirectory).filePath(capture)),
               "file picker visual evidence is saved");
      }
      QTimer::singleShot(2000, dialog, &QDialog::reject); // 验证失败也不能挂住测试。
      if (cancel)
        dialog->reject();
      else if (auto *buttons = browser->findChild<QDialogButtonBox *>()) {
        bool confirmed = false;
        for (auto *button : buttons->findChildren<QPushButton *>())
          if (buttons->buttonRole(button) == QDialogButtonBox::AcceptRole) {
            confirmed = button->isEnabled();
            button->click();
            break;
          }
        expect(confirmed, "selected file or directory can be confirmed through its button");
      } else {
        expect(false, "file picker has a confirmation button");
        dialog->reject();
      }
    });
  };
  theme->setMode(ZzFluentUI::ZzThemeMode::Light);
  respond(imagePath, QStringLiteral("file-picker-light.png"));
  expect(zzrk::gui::FileSelectionDialog::getOpenFileName(
             window, QStringLiteral("选择镜像"), directory,
             QStringLiteral("分区镜像 (*.img *.bin);;所有文件 (*)")) == imagePath,
         "file picker returns an existing Unicode image path intact");
  theme->setMode(ZzFluentUI::ZzThemeMode::Dark);
  respond(imagePath, QStringLiteral("file-picker-dark.png"), true);
  expect(zzrk::gui::FileSelectionDialog::getOpenFileName(
             window, QStringLiteral("选择镜像"), directory).isEmpty(),
         "cancel returns no path even when a file is preselected");
  respond(directory, QStringLiteral("directory-picker-dark.png"));
  expect(zzrk::gui::FileSelectionDialog::getExistingDirectory(
             window, QStringLiteral("选择镜像目录"), fixture.path()) == directory,
         "directory picker returns a directory rather than a file");
  const QString savePath = QDir(directory).filePath(QStringLiteral("备份 分区.img"));
  respond(savePath, QStringLiteral("save-picker-dark.png"));
  expect(zzrk::gui::FileSelectionDialog::getSaveFileName(
             window, QStringLiteral("备份分区"), savePath,
             QStringLiteral("分区镜像 (*.img);;所有文件 (*)")) == savePath &&
             !QFileInfo::exists(savePath),
         "save picker returns a new path without writing the file");
  theme->setMode(originalMode);
}

void checkFileOperationDialogs(QWidget *window) {
  expect(QCoreApplication::testAttribute(Qt::AA_DontUseNativeDialogs),
         "application disables native dialogs before Qt can show a file message");
  // 只操作测试临时文件，验证外观桥接不能把取消映射成删除/覆盖。
  QTemporaryDir fixture;
  const QString path = fixture.filePath(QStringLiteral("待删除 固件.img"));
  QFile image(path);
  expect(image.open(QIODevice::WriteOnly) && image.write("firmware") == 8,
         "file operation fixture creates an image");
  image.close();
  QTimer::singleShot(0, window, [&] {
    auto *picker = qobject_cast<QDialog *>(QApplication::activeModalWidget());
    auto *browser = picker != nullptr ? picker->findChild<QFileDialog *>() : nullptr;
    auto *tree = browser != nullptr ? browser->findChild<QTreeView *>("treeView") : nullptr;
    auto *remove = browser != nullptr ? browser->findChild<QAction *>("qt_delete_action") : nullptr;
    expect(tree != nullptr && remove != nullptr, "file browser offers its delete operation");
    if (!tree || !remove) {
      if (picker)
        picker->reject();
      return;
    }
    const bool selectionReady =
        zzrk::gui::test::enterFilePickerPath(browser, path);
    expect(selectionReady, "delete fixture directory and file model are ready");
    if (!selectionReady) {
      std::cerr << "Delete selection: directory="
                << browser->directory().absolutePath().toStdString()
                << " root=" << tree->rootIndex().data().toString().toStdString()
                << " filters=" << browser->nameFilters().join(';').toStdString()
                << '\n';
      picker->reject();
      return;
    }
    QPersistentModelIndex selected;
    waitFor([&] {
      for (int row = 0; row < tree->model()->rowCount(tree->rootIndex()); ++row) {
        const auto index = tree->model()->index(row, 0, tree->rootIndex());
        if (index.data().toString() == QFileInfo(path).fileName()) {
          selected = index;
          return true;
        }
      }
      return false;
    });
    expect(selected.isValid(), "delete fixture is selected in the real file model");
    if (!selected.isValid()) {
      picker->reject();
      return;
    }
    tree->selectionModel()->setCurrentIndex(
        selected, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    remove->setEnabled(true); // 与 Qt 打开右键菜单后的操作状态一致。
    const auto respond = [&](bool confirm) {
      // 桥接异步进入 Fluent 弹窗，因此等待实际可见的确认窗口。
      auto *poll = new QTimer(picker);
      poll->setInterval(10);
      QObject::connect(poll, &QTimer::timeout, picker, [=] {
        auto *modal = QApplication::activeModalWidget();
        auto *content = qobject_cast<ZzFluentUI::ZzContentDialog *>(modal);
        if (content == picker || modal == nullptr ||
            (qobject_cast<QMessageBox *>(modal) &&
             modal->property("fluentMessageQueued").toBool()))
          return;
        poll->stop();
        expect(content != nullptr, "Qt file delete confirmation uses the Fluent host");
        if (content) {
          expect(content->text().contains(QFileInfo(path).fileName()),
                 "delete confirmation retains the target file name");
          expect(content->defaultButton() == ZzFluentUI::ZzContentDialogButton::Close,
                 "delete confirmation defaults to cancellation");
          const auto screenshots = qEnvironmentVariable("ZZRK_GUI_SCREENSHOT_DIR");
          if (!screenshots.isEmpty() && !confirm) {
            QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
            expect(content->grab().save(QDir(screenshots).filePath("file-delete-confirmation.png")),
                   "delete confirmation screenshot is saved");
          }
          if (confirm)
            content->done(static_cast<int>(ZzFluentUI::ZzContentDialogResult::Primary));
          else {
            QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
            QApplication::sendEvent(content, &escape);
          }
        } else if (auto *legacy = qobject_cast<QMessageBox *>(modal)) {
          legacy->button(QMessageBox::No)->click();
        }
      });
      poll->start();
      QTimer::singleShot(2000, poll, [poll] { poll->stop(); });
    };
    respond(false);
    remove->trigger();
    expect(QFileInfo::exists(path), "Escape cancels file deletion");
    respond(true);
    remove->trigger();
    expect(waitFor([&] { return !QFileInfo::exists(path); }),
           "only explicit confirmation deletes the temporary file");
    picker->reject();
  });
  expect(zzrk::gui::FileSelectionDialog::getOpenFileName(
             window, QStringLiteral("选择镜像"), fixture.path()).isEmpty(),
         "closing the browser after deletion returns no selection");

  expect(image.open(QIODevice::WriteOnly) && image.write("original") == 8,
         "overwrite fixture recreates an existing image");
  image.close();
  for (const bool confirm : {false, true}) {
    QTimer::singleShot(0, window, [&, confirm] {
      auto *picker = qobject_cast<QDialog *>(QApplication::activeModalWidget());
      auto *browser = picker ? picker->findChild<QFileDialog *>() : nullptr;
      expect(browser != nullptr, "save browser is available for overwrite testing");
      if (!browser)
        return;
      const bool selectionReady =
          zzrk::gui::test::enterFilePickerPath(browser, path);
      expect(selectionReady,
             "overwrite selection is ready in the real file model");
      if (!selectionReady) {
        picker->reject();
        return;
      }
      auto *poll = new QTimer(picker);
      poll->setInterval(10);
      QObject::connect(poll, &QTimer::timeout, picker, [=] {
        auto *modal = QApplication::activeModalWidget();
        if (modal == picker || !modal ||
            (qobject_cast<QMessageBox *>(modal) &&
             modal->property("fluentMessageQueued").toBool()))
          return;
        poll->stop();
        auto *content = qobject_cast<ZzFluentUI::ZzContentDialog *>(modal);
        expect(content != nullptr, "overwrite confirmation uses the Fluent host");
        if (content) {
          expect(content->text().contains(QFileInfo(path).fileName()),
                 "overwrite confirmation retains the target file");
          if (confirm)
            content->done(static_cast<int>(ZzFluentUI::ZzContentDialogResult::Primary));
          else {
            QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
            QApplication::sendEvent(content, &enter);
          }
        } else if (auto *legacy = qobject_cast<QMessageBox *>(modal)) {
          legacy->button(QMessageBox::No)->click();
        }
      });
      poll->start();
      QMetaObject::invokeMethod(browser, "accept", Qt::DirectConnection);
      if (picker->isVisible())
        picker->reject();
    });
    const auto result = zzrk::gui::FileSelectionDialog::getSaveFileName(
        window, QStringLiteral("保存镜像"), path);
    expect(result == (confirm ? path : QString{}),
           "overwrite only returns the existing path after explicit confirmation");
    expect(image.open(QIODevice::ReadOnly) && image.readAll() == "original",
           "choosing a save path never modifies the existing file");
    image.close();
  }
}

void checkMessageDialogs(QWidget *window) {
  using namespace zzrk::gui;
  // Escape/Enter 必须取消；按钮明确确认才返回 true。
  for (const auto key : {Qt::Key_Escape, Qt::Key_Return, Qt::Key_unknown}) {
    QTimer::singleShot(0, window, [key] {
      auto *dialog = qobject_cast<ZzFluentUI::ZzContentDialog *>(QApplication::activeModalWidget());
      expect(dialog != nullptr, "business confirmation uses the framework");
      if (!dialog)
        return;
      if (key == Qt::Key_unknown)
        dialog->findChild<QPushButton *>("zzContentDialogPrimaryButton")->click();
      else {
        QKeyEvent event(QEvent::KeyPress, key, Qt::NoModifier);
        QApplication::sendEvent(dialog, &event);
      }
    });
    expect(dialogs::confirm(window, guiText("确认写入分区"), guiText("继续")) ==
               (key == Qt::Key_unknown),
           "business confirmation requires an explicit positive action");
  }
  QTimer::singleShot(0, window, [] {
    auto *dialog = qobject_cast<ZzFluentUI::ZzContentDialog *>(QApplication::activeModalWidget());
    expect(dialog != nullptr && dialog->text().contains("<固件>.img"),
           "notification retains literal file names");
    if (!dialog)
      return;
    const auto *label = dialog->findChild<QLabel *>("zzContentDialogText");
    expect(label && label->textFormat() == Qt::PlainText,
           "notification cannot interpret file names as markup");
    auto *translations = GuiTranslations::active();
    translations->install("en");
    QCoreApplication::sendPostedEvents(nullptr, QEvent::LanguageChange);
    expect(dialog->title() == "Operation failed" && dialog->closeButtonText() == "OK",
           "visible notification updates with the current language");
    translations->install("zh_CN");
    QCoreApplication::sendPostedEvents(nullptr, QEvent::LanguageChange);
    dialog->reject();
  });
  dialogs::notify(window, guiText("操作失败"), QStringLiteral("<固件>.img"));
}

bool saveVisualEvidence(ZzPureTools::ZzPureApplication &application,
                        ZzPureTools::ZzApplicationWindow &window) {
  const QString outputDirectory =
      qEnvironmentVariable("ZZRK_GUI_SCREENSHOT_DIR");
  if (outputDirectory.isEmpty())
    return true;
  if (!QDir{}.mkpath(outputDirectory))
    return false;

  auto *themeController = application.themeController();
  if (themeController == nullptr)
    return false;
  const auto originalMode = themeController->mode();
  const QSize originalSize = window.size();
  const auto capture = [&](const QSize size, const ZzFluentUI::ZzThemeMode mode,
                           const QString &fileName) {
    themeController->setMode(mode);
    window.resize(size);
    // 主题、紧凑布局和通知的高度会依次发出 LayoutRequest；等布局完成再抓图。
    for (int frame = 0; frame < 3; ++frame) {
      QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
      QApplication::processEvents();
    }
    // 主题切换不能让表格保留局部样式表缓存的旧背景色。
    for (auto *table : window.findChildren<QTableWidget *>())
      expect(table->palette().color(QPalette::Base) ==
                 window.palette().color(QPalette::Base),
             "data tables inherit the current framework theme after switching");
    for (auto *notice : window.findChildren<ZzFluentUI::ZzInfoBar *>()) {
      if (notice->isVisible()) {
        expect(notice->height() >= notice->heightForWidth(notice->width()),
               "visible error notices retain enough height at minimum window size");
        if (const auto *next = notice->parentWidget()->findChild<QLabel *>(
                QStringLiteral("overallImageDownloadProgressPercent"))) {
          const bool separated = next->y() > notice->geometry().bottom();
          if (!separated)
            std::cerr << "Notice layout: height=" << notice->height()
                      << " required=" << notice->heightForWidth(notice->width())
                      << " bottom=" << notice->geometry().bottom()
                      << " next=" << next->y() << '\n';
          expect(separated, "error notice does not overlap progress at minimum size");
        }
      }
    }
    for (const auto &name : {QStringLiteral("startImageDownloadButton"),
                             QStringLiteral("cancelImageDownloadButton"),
                             QStringLiteral("startUpgradeButton"),
                             QStringLiteral("cancelUpgradeButton"),
                             QStringLiteral("writePartitionButton"),
                             QStringLiteral("cancelPartitionButton")}) {
      if (auto *button = window.findChild<QAbstractButton *>(name);
          button != nullptr && button->isVisible()) {
        auto *page = button->parentWidget();
        while (page != nullptr && page->objectName() != QStringLiteral("downloadImagePage") &&
               page->objectName() != QStringLiteral("fullUpgradePage") &&
               page->objectName() != QStringLiteral("partitionPage"))
          page = page->parentWidget();
        expect(page != nullptr && page->rect().contains(
                   QRect(button->mapTo(page, QPoint{}), button->size())),
               "task buttons stay fully inside the visible page at minimum size");
      }
    }
    const QPixmap image = window.grab();
    return image.size() == size &&
           image.save(QDir(outputDirectory).filePath(fileName), "PNG");
  };

  bool saved =
      capture(QSize(980, 820), ZzFluentUI::ZzThemeMode::Light,
              QStringLiteral("zzrk-gui-980x820-light.png")) &&
      capture(window.minimumSize(), ZzFluentUI::ZzThemeMode::Light,
              QStringLiteral("zzrk-gui-minimum-800x700-light.png")) &&
      capture(QSize(980, 820), ZzFluentUI::ZzThemeMode::Dark,
              QStringLiteral("zzrk-gui-980x820-dark.png"));
  // 假后端截图覆盖全部页面和空态，不连接真实硬件，也不执行额外烧录。
  if (auto *tabs = window.findChild<ZzFluentUI::ZzTabWidget *>(QStringLiteral("mainTabs"))) {
    const int originalIndex = tabs->currentIndex();
    for (int index = 0; index < tabs->count(); ++index) {
      tabs->setCurrentIndex(index);
      saved = capture(QSize(980, 820), ZzFluentUI::ZzThemeMode::Light,
                      QStringLiteral("page-%1-light.png").arg(index)) && saved;
      saved = capture(QSize(980, 820), ZzFluentUI::ZzThemeMode::Dark,
                      QStringLiteral("page-%1-dark.png").arg(index)) && saved;
      saved = capture(QSize(800, 700), ZzFluentUI::ZzThemeMode::Light,
                      QStringLiteral("page-%1-minimum.png").arg(index)) && saved;
    }
    tabs->setCurrentIndex(originalIndex);
    if (auto *download = window.findChild<zzrk::gui::ImageDownloadPage *>(
            QStringLiteral("downloadImagePage"))) {
      const auto *status = download->findChild<QLabel *>(QStringLiteral("imageDownloadStatus"));
      const auto *notice = download->findChild<ZzFluentUI::ZzInfoBar *>(
          QStringLiteral("imageDownloadErrorNotice"));
      const QString originalStatus = status->text();
      const bool wasError = notice->isOpen();
      tabs->setCurrentIndex(0);
      const QString path = QStringLiteral("/home/zz/firmware/") +
                           QStringLiteral("rk3588s-android12-validation-").repeated(4) +
                           QStringLiteral("<b>update</b>.img");
      download->setError(QStringLiteral("镜像写入或回读校验失败（%1）").arg(path));
      saved = capture(QSize(980, 820), ZzFluentUI::ZzThemeMode::Light,
                      QStringLiteral("download-long-error-wide.png")) && saved;
      saved = capture(QSize(800, 700), ZzFluentUI::ZzThemeMode::Light,
                      QStringLiteral("download-long-error-minimum.png")) && saved;
      for (const auto *text : notice->findChildren<QLabel *>())
        expect(text->textFormat() == Qt::PlainText,
               "raw paths in error notices are rendered as plain text");
      if (wasError)
        download->setError(originalStatus);
      else
        download->setStatus(originalStatus);
      tabs->setCurrentIndex(originalIndex);
    }
    // 使用独立展示页捕获空态，不清空或重建真实工作区已有的清单。
    themeController->setMode(ZzFluentUI::ZzThemeMode::Light);
    auto empty = std::unique_ptr<zzrk::gui::ImageDownloadPage>(
        zzrk::gui::ImageDownloadPage::buildUi(nullptr));
    empty->setBusy(false, false, false, false);
    empty->resize(980, 740);
    empty->show();
    QApplication::processEvents();
    saved = empty->grab().save(
        QDir(outputDirectory).filePath(QStringLiteral("download-empty-light.png")),
        "PNG") && saved;
  }
  themeController->setMode(originalMode);
  window.resize(originalSize);
  QApplication::processEvents();
  return saved;
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
  std::atomic_int completedScans = 0;
  std::atomic_int scanDelayMilliseconds = 0;
  std::atomic_int partitionReads = 0;
  std::atomic_bool preparationFails = true;

  [[nodiscard]] zzrk::transport::ScanResult scanDevices() override {
    ++scans;
    if (const int delay = scanDelayMilliseconds.load(); delay > 0)
      std::this_thread::sleep_for(std::chrono::milliseconds{delay});
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
    ++completedScans;
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

namespace {

ZzPureTools::ZzApplicationWindow *
buildBareFrameworkWindow(ZzPureTools::ZzPureApplication &application) {
  const ZzPureTools::ZzRouteId route{QStringLiteral("rollback-test")};
  QPointer<ZzPureTools::ZzApplicationWindow> window;

  ZzPureTools::ZzApplicationBuilder builder;
  ZzPureTools::ZzPageRegistration registration;
  registration.routeId = route;
  registration.lifetime = ZzPureTools::ZzPageLifetimePolicy::WhileActive;
  registration.factory = [](QWidget *parent) {
    return ZzPureTools::ZzPageInstance::create(parent, new QWidget(parent),
                                               std::make_unique<QObject>(),
                                               std::make_unique<QObject>());
  };
  if (!builder.addPage(std::move(registration)) ||
      !builder.addNavigationNode({route,
                                  QStringLiteral("gui_framework_test"),
                                  QStringLiteral("Rollback test"),
                                  {}}) ||
      !builder.setInitialRoute(route) ||
      !builder.setWindowSetupCallback(
          [&window](ZzPureTools::ZzApplicationWindow &created) {
            window = &created;
            return ZzCore::ZzResult<void>::success();
          }) ||
      !builder.build(application)) {
    return nullptr;
  }
  return window.data();
}

class WorkspaceCollapseFailureInjector final : public QObject {
public:
  explicit WorkspaceCollapseFailureInjector(
      ZzPureTools::ZzApplicationWindow &window)
      : window_(&window) {}

  [[nodiscard]] bool connected() const noexcept {
    return !connectedPanes_.isEmpty();
  }
  [[nodiscard]] bool triggered() const noexcept { return triggered_; }

protected:
  bool eventFilter(QObject *watched, QEvent *event) override {
    Q_UNUSED(event);
    if (auto *pane = qobject_cast<ZzFluentUI::ZzSidePane *>(watched))
      connectToPane(pane);
    return false;
  }

private:
  void connectToPane(ZzFluentUI::ZzSidePane *pane) {
    if (pane == nullptr || connectedPanes_.contains(pane))
      return;
    QObject::connect(
        pane, &ZzFluentUI::ZzSidePane::collapsedChanged, this,
        [this, pane](const bool collapsed) {
          if (!collapsed || pane->pageCount() != 1)
            return;
          triggered_ = true;
          pane->setCollapsed(false);
        },
        Qt::DirectConnection);
    connectedPanes_.append(pane);
  }

  QPointer<ZzPureTools::ZzApplicationWindow> window_;
  QList<QPointer<ZzFluentUI::ZzSidePane>> connectedPanes_;
  bool triggered_ = false;
};

void expectWorkspaceAttachFailureRollsBack() {
  static char applicationName[] = "gui_framework_rollback_test";
  static char *applicationArguments[] = {applicationName, nullptr};
  int argumentCount = 1;
  ZzPureTools::ZzPureApplication application(argumentCount,
                                             applicationArguments);
  auto *window = buildBareFrameworkWindow(application);
  expect(window != nullptr, "rollback fixture creates a framework window");
  if (window == nullptr) {
    application.beginShutdown();
    return;
  }

  QPointer<QWidget> originalCentral(window->centralWidget());
  QPointer<ZzFluentUI::ZzNavigationPane> navigation(window->navigationPane());
  QPointer<ZzPureTools::ZzPageHost> pageHost(window->pageHost());
  const ZzPureTools::ZzRouteId originalRoute = pageHost->currentRoute();
  WorkspaceCollapseFailureInjector injector(*window);
  application.installEventFilter(&injector);
  const auto attached = zzrk::gui::GuiWindowShell::attach(
      *window, std::make_shared<FrameworkFakeBackend>(), false);
  application.removeEventFilter(&injector);

  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  expect(injector.connected() && injector.triggered(),
         "rollback fixture triggers a post-integration panel failure");
  expect(!attached &&
             attached.error().technicalMessage() ==
                 QStringLiteral("Side panel state was interrupted") &&
             attached.error().context() ==
                 QStringLiteral("zzrk-internal-navigation"),
         "workspace attach preserves the injected panel failure");
  expect(originalCentral != nullptr &&
             window->centralWidget() == originalCentral,
         "failed workspace attach restores the original central widget");
  expect(window->findChild<QWidget *>(QStringLiteral("zzWorkspaceRoot")) ==
             nullptr,
         "failed workspace attach removes the workspace root");
  expect(navigation != nullptr &&
             navigation->parentWidget() == originalCentral &&
             originalCentral->isAncestorOf(navigation),
         "failed workspace attach restores the navigation pane");
  expect(pageHost != nullptr && pageHost->parentWidget() == originalCentral &&
             originalCentral->isAncestorOf(pageHost) &&
             originalCentral->layout() != nullptr &&
             originalCentral->layout()->count() == 2 &&
             pageHost->currentRoute() == originalRoute &&
             window->navigationController()->currentRoute() == originalRoute,
         "failed workspace attach restores the page host and body layout");
  expect(zzrk::gui::GuiWindowShell::attachedTo(*window) == nullptr,
         "failed workspace attach removes its shell object");
  application.beginShutdown();
}

int runSynchronousShutdownScenario(const QString &scenario) {
  auto backend = std::make_shared<FrameworkFakeBackend>();
  auto harness = zzrk::gui::test::GuiTestHarness::create(backend);
  expect(harness != nullptr,
         "synchronous shutdown fixture creates an application");
  if (harness == nullptr)
    return failures;

  auto *application = harness->application();
  auto *controller = harness->controller();
  QPointer<ZzPureTools::ZzApplicationWindow> window{harness->window()};
  QPointer<zzrk::gui::MainWorkspaceController> controllerGuard{controller};
  expect(application != nullptr && controller != nullptr && window != nullptr,
         "synchronous shutdown fixture exposes framework objects");
  if (application == nullptr || controller == nullptr)
    return failures;

  int partitionTableCompletions = 0;
  int expectedCompletedScans = 0;
  std::shared_ptr<const zzrk::gui::MainWorkspaceLifecycle> shutdownLifecycle;
  const auto waitForInitialSelection = [&] {
    controller->refreshDevices();
    return waitFor([&] {
      return controllerGuard != nullptr &&
             controllerGuard->viewModel()->selectedPortPath() ==
                 QStringLiteral("1-5");
    });
  };
  if (scenario == QStringLiteral("viewModelDeviceSelectionChanged")) {
    QObject::connect(
        controller->viewModel(),
        &zzrk::gui::MainWorkspaceViewModel::deviceSelectionChanged, application,
        [application] { application->beginShutdown(); }, Qt::DirectConnection);
    controller->viewModel()->setSelectedPortPath(QStringLiteral("1-5"));
  } else if (scenario == QStringLiteral("viewModelBusyChanged")) {
    QObject::connect(
        controller->viewModel(),
        &zzrk::gui::MainWorkspaceViewModel::busyChanged, application,
        [application](bool) { application->beginShutdown(); },
        Qt::DirectConnection);
    controller->viewModel()->setBusy(true);
  } else if (scenario == QStringLiteral("viewModelProgressChanged")) {
    QObject::connect(
        controller->viewModel(),
        &zzrk::gui::MainWorkspaceViewModel::progressChanged, application,
        [application](int, int) { application->beginShutdown(); },
        Qt::DirectConnection);
    controller->viewModel()->setProgress(1, 1);
  } else if (scenario == QStringLiteral("refreshDevicesBusyChanged")) {
    QObject::connect(
        controller->viewModel(),
        &zzrk::gui::MainWorkspaceViewModel::busyChanged, application,
        [application](bool) { application->beginShutdown(); },
        Qt::DirectConnection);
    controller->refreshDevices();
  } else if (scenario == QStringLiteral("shutdownBusyChanged")) {
    controller->viewModel()->setBusy(true);
    backend->scanDelayMilliseconds.store(250);
    expectedCompletedScans = backend->scans.load() + 1;
    controller->refreshDevices();
    expect(waitFor(
               [&] { return backend->scans.load() >= expectedCompletedScans; }),
           "shutdown fixture starts a controlled scan watcher");
    shutdownLifecycle = controller->lifecycle();
    QObject::connect(
        controller->viewModel(),
        &zzrk::gui::MainWorkspaceViewModel::busyChanged, application,
        [application](bool) { application->beginShutdown(); },
        Qt::DirectConnection);
    controller->shutdown();
  } else if (scenario == QStringLiteral("pendingRefreshAfterInspection")) {
    expect(waitForInitialSelection(),
           "pending refresh fixture completes its initial scan");
    QMetaObject::invokeMethod(controller, "inspectSelectedDevice",
                              Qt::DirectConnection);
    expect(controllerGuard != nullptr && controller->usbOperationRunning(),
           "pending refresh fixture starts device inspection");
    controller->handleDeviceChange();
    QObject::connect(
        controller->viewModel(),
        &zzrk::gui::MainWorkspaceViewModel::busyChanged, application,
        [application](const bool busy) {
          if (busy)
            application->beginShutdown();
        },
        Qt::DirectConnection);
  } else if (scenario == QStringLiteral("attachPagesRebuildBusyChanged")) {
    auto *downloadPage = window->findChild<zzrk::gui::ImageDownloadPage *>(
        QStringLiteral("downloadImagePage"));
    auto *devicePage = window->findChild<zzrk::gui::DevicePage *>(
        QStringLiteral("devicePage"));
    auto *upgradePage = window->findChild<zzrk::gui::FullUpgradePage *>(
        QStringLiteral("fullUpgradePage"));
    auto *partitionPage = window->findChild<zzrk::gui::PartitionPage *>(
        QStringLiteral("partitionPage"));
    expect(downloadPage != nullptr && devicePage != nullptr &&
               upgradePage != nullptr && partitionPage != nullptr,
           "page attachment fixture finds all workspace pages");
    controller->attachPages(nullptr, nullptr, nullptr, nullptr);
    controller->viewModel()->setBusy(true);
    QObject::connect(
        controller->viewModel(),
        &zzrk::gui::MainWorkspaceViewModel::busyChanged, application,
        [application](bool) { application->beginShutdown(); },
        Qt::DirectConnection);
    controller->attachPages(downloadPage, devicePage, upgradePage,
                            partitionPage);
  } else if (scenario == QStringLiteral("clearImagePlanBusyChanged")) {
    QTemporaryDir directory;
    const QString parameterPath = directory.filePath("parameter.txt");
    expect(
        directory.isValid() &&
            writeFile(parameterPath, QByteArray{"MACHINE_MODEL: gui-rk3588\n"
                                                "TYPE: GPT\n"
                                                "CMDLINE:mtdparts=rk29xxnand:"
                                                "0x100@0x8000(boot_a)\n"}),
        "clear image fixture creates parameter.txt");
    controller->loadDownloadParameter(parameterPath);
    auto *table =
        window->findChild<QTableWidget *>(QStringLiteral("downloadImageTable"));
    expect(table != nullptr && table->rowCount() > 0,
           "clear image fixture creates a selectable plan row");
    if (table != nullptr && table->rowCount() > 0)
      table->setCurrentCell(0, 0);
    controller->viewModel()->setBusy(true);
    QObject::connect(
        controller->viewModel(),
        &zzrk::gui::MainWorkspaceViewModel::busyChanged, application,
        [application](bool) { application->beginShutdown(); },
        Qt::DirectConnection);
    QMetaObject::invokeMethod(controller, "clearSelectedDownloadImage",
                              Qt::DirectConnection);
  } else if (scenario == QStringLiteral("loadDownloadParameterLayoutChanged")) {
    QTemporaryDir directory;
    const QString parameterPath = directory.filePath("parameter.txt");
    expect(
        directory.isValid() &&
            writeFile(parameterPath, QByteArray{"MACHINE_MODEL: gui-rk3588\n"
                                                "TYPE: GPT\n"
                                                "CMDLINE:mtdparts=rk29xxnand:"
                                                "0x100@0x8000(boot_a)\n"}),
        "download layout shutdown fixture creates parameter.txt");
    controller->viewModel()->setDownloadLayoutPort(QStringLiteral("1-5"));
    QObject::connect(
        controller->viewModel(),
        &zzrk::gui::MainWorkspaceViewModel::deviceSelectionChanged, application,
        [application] { application->beginShutdown(); }, Qt::DirectConnection);
    controller->loadDownloadParameter(parameterPath);
  } else if (scenario == QStringLiteral("selectionClearsInspection")) {
    expect(waitForInitialSelection(),
           "selection inspection shutdown fixture completes its initial scan");
    controller->viewModel()->setSelectedPortPath(QStringLiteral("stale"));
    controller->viewModel()->setInspectedPort(QStringLiteral("1-5"));
    int selectionNotifications = 0;
    QObject::connect(
        controller->viewModel(),
        &zzrk::gui::MainWorkspaceViewModel::deviceSelectionChanged, application,
        [application, &selectionNotifications] {
          ++selectionNotifications;
          if (selectionNotifications == 2)
            application->beginShutdown();
        },
        Qt::DirectConnection);
    controller->selectDevice({});
  } else if (scenario == QStringLiteral("selectionClearsDeviceGpt")) {
    expect(waitForInitialSelection(),
           "selection GPT shutdown fixture completes its initial scan");
    controller->viewModel()->setSelectedPortPath(QStringLiteral("stale"));
    controller->viewModel()->setDownloadLayoutPort(QStringLiteral("1-5"));
    int selectionNotifications = 0;
    QObject::connect(
        controller->viewModel(),
        &zzrk::gui::MainWorkspaceViewModel::deviceSelectionChanged, application,
        [application, &selectionNotifications] {
          ++selectionNotifications;
          if (selectionNotifications == 2)
            application->beginShutdown();
        },
        Qt::DirectConnection);
    controller->selectDevice({});
  } else if (scenario == QStringLiteral("selectionClearsPartitionTable")) {
    expect(waitForInitialSelection(),
           "selection partition shutdown fixture completes its initial scan");
    controller->viewModel()->setSelectedPortPath(QStringLiteral("stale"));
    controller->viewModel()->setPartitionPort(QStringLiteral("1-5"));
    int selectionNotifications = 0;
    QObject::connect(
        controller->viewModel(),
        &zzrk::gui::MainWorkspaceViewModel::deviceSelectionChanged, application,
        [application, &selectionNotifications] {
          ++selectionNotifications;
          if (selectionNotifications == 2)
            application->beginShutdown();
        },
        Qt::DirectConnection);
    controller->selectDevice({});
  } else if (scenario == QStringLiteral("devicesChanged")) {
    QObject::connect(
        controller, &zzrk::gui::MainWorkspaceController::devicesChanged,
        application,
        [application](const zzrk::transport::ScanResult &) {
          application->beginShutdown();
        },
        Qt::DirectConnection);
    controller->refreshDevices();
  } else if (scenario == QStringLiteral("selectedDeviceChanged")) {
    QObject::connect(
        controller, &zzrk::gui::MainWorkspaceController::selectedDeviceChanged,
        application, [application] { application->beginShutdown(); },
        Qt::DirectConnection);
    controller->refreshDevices();
  } else if (scenario == QStringLiteral("devicePartitionTableChanged")) {
    controller->refreshDevices();
    expect(waitFor([&] {
             return controllerGuard != nullptr &&
                    controllerGuard->viewModel()->selectedPortPath() ==
                        QStringLiteral("1-5");
           }),
           "partition table fixture completes its initial scan");
    QObject::connect(
        controller,
        &zzrk::gui::MainWorkspaceController::partitionTableCompleted,
        application, [&](bool) { ++partitionTableCompletions; },
        Qt::DirectConnection);
    QObject::connect(
        controller,
        &zzrk::gui::MainWorkspaceController::devicePartitionTableChanged,
        application, [application] { application->beginShutdown(); },
        Qt::DirectConnection);
    controller->readSelectedDevicePartitions();
  } else if (scenario == QStringLiteral("partitionTableAppliesDeviceGpt")) {
    expect(
        waitForInitialSelection(),
        "partition table layout shutdown fixture completes its initial scan");
    QObject::connect(
        controller->viewModel(),
        &zzrk::gui::MainWorkspaceViewModel::deviceSelectionChanged, application,
        [application] { application->beginShutdown(); }, Qt::DirectConnection);
    controller->readSelectedDevicePartitions();
  } else if (scenario == QStringLiteral("scanCompleted")) {
    QObject::connect(
        controller, &zzrk::gui::MainWorkspaceController::scanCompleted,
        application, [application] { application->beginShutdown(); },
        Qt::DirectConnection);
    controller->refreshDevices();
  } else {
    expect(false, "synchronous shutdown fixture receives a known scenario");
    return failures;
  }

  expect(waitFor([&] { return controllerGuard == nullptr; }),
         "terminal signal can synchronously destroy its controller");
  expect(window == nullptr,
         "terminal signal shutdown destroys the framework window");
  if (scenario == QStringLiteral("devicePartitionTableChanged")) {
    expect(partitionTableCompletions == 0,
           "partition table shutdown suppresses the second terminal signal");
  } else if (scenario == QStringLiteral("shutdownBusyChanged")) {
    expect(backend->completedScans.load() >= expectedCompletedScans,
           "reentrant shutdown waits for the controlled scan watcher");
    expect(shutdownLifecycle != nullptr &&
               shutdownLifecycle->monitoringStopped() &&
               shutdownLifecycle->allWatchersStopped() &&
               shutdownLifecycle->shutdownComplete(),
           "reentrant shutdown commits all lifecycle completion flags");
  }
  return failures;
}

} // namespace

int main(int argc, char **argv) {
  if (argc == 2 && QString::fromLocal8Bit(argv[1]) ==
                       QStringLiteral("--workspace-rollback")) {
    const auto bootstrap = ZzWindowKit::ZzWindowKitBootstrap::prepare();
    expect(static_cast<bool>(bootstrap),
           "WindowKit bootstrap supports rollback fixture");
    if (!bootstrap)
      return failures;
    expectWorkspaceAttachFailureRollsBack();
    return failures ? 1 : 0;
  }
  if (argc == 3 && QString::fromLocal8Bit(argv[1]) ==
                       QStringLiteral("--synchronous-shutdown")) {
    return runSynchronousShutdownScenario(QString::fromLocal8Bit(argv[2]));
  }
  const QString executable =
      QFileInfo{QString::fromLocal8Bit(argv[0])}.absoluteFilePath();
  // 每个进程只创建一个 QApplication，与 Cocoa 应用的实际生命周期一致。
  // 回滚场景由独立 CTest 模式覆盖，避免重启进程级原生单例和窗口钩子。
  auto backend = std::make_shared<FrameworkFakeBackend>();
  auto harness = zzrk::gui::test::GuiTestHarness::create(backend);
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

  auto *controller = harness->controller();
  expect(controller != nullptr, "harness exposes the workspace controller");
  if (controller == nullptr) {
    return 1;
  }
  expect(controller->backend() == backend.get(),
         "controller accepts an injectable backend");
  expect(!controller->showModalDialogs(),
         "controller preserves modal dialog configuration");
  const auto lifecycle = controller->lifecycle();
  expect(lifecycle != nullptr,
         "controller exposes a persistent shutdown lifecycle observer");
  int selectedDeviceChanges = 0;
  int scanCompletions = 0;
  QObject::connect(controller,
                   &zzrk::gui::MainWorkspaceController::selectedDeviceChanged,
                   [&] { ++selectedDeviceChanges; });
  QObject::connect(controller,
                   &zzrk::gui::MainWorkspaceController::scanCompleted,
                   [&] { ++scanCompletions; });

  auto *window = harness->window();
  int applicationWindowCount = 0;
  for (QWidget *topLevel : QApplication::topLevelWidgets()) {
    if (auto *candidate =
            qobject_cast<ZzPureTools::ZzApplicationWindow *>(topLevel)) {
      ++applicationWindowCount;
      expect(candidate == window,
             "harness exposes the unique framework application window");
    }
  }
  expect(application->windowCount() == 1 && applicationWindowCount == 1,
         "builder creates exactly one framework application window");
  if (window != nullptr) {
    window->show();
    QApplication::processEvents();
    expect(window->isWindow(), "framework application window is top-level");
    expect(window->windowTitle() == QStringLiteral("ZzRkDevTool-下载镜像"),
           "framework window initially includes the download page title");
    expect(!window->windowIcon().isNull(),
           "framework window preserves its application icon");
    const bool requiredWindowSize = window->minimumSize() == QSize(800, 700) &&
                                    window->size() == QSize(980, 820);
    if (!requiredWindowSize) {
      const auto sizeText = [](const QSize &size) {
        return QStringLiteral("%1x%2").arg(size.width()).arg(size.height());
      };
      // 构建器已显示过窗口；保留原断言并记录原生窗口和屏幕的实际约束。
      std::cerr << "Window sizing: actual="
                << sizeText(window->size()).toStdString()
                << " minimum=" << sizeText(window->minimumSize()).toStdString()
                << " normal="
                << sizeText(window->normalGeometry().size()).toStdString()
                << " frame="
                << sizeText(window->frameGeometry().size()).toStdString()
                << " screen="
                << sizeText(window->screen()->availableGeometry().size())
                       .toStdString()
                << '\n';
    }
    expect(requiredWindowSize,
           "framework window uses the required minimum and initial sizes");
    expect(qobject_cast<ZzFluentUI::ZzFluentTitleBar *>(window->menuWidget()) ==
               window->titleBar(),
           "framework window installs a Fluent title bar");
    expect(window->windowAgent() != nullptr,
           "framework window exposes its WindowKit agent");
    expect(window->navigationPane() != nullptr &&
               !window->navigationPane()->isVisible(),
           "framework navigation pane is hidden");
    for (auto *activityBar :
         window->findChildren<ZzFluentUI::ZzActivityBar *>())
      expect(activityBar->isHidden(), "workspace activity bars are hidden");
    for (auto *sidePane : window->findChildren<ZzFluentUI::ZzSidePane *>())
      expect(sidePane->isHidden(), "workspace side panes are hidden");
    auto *mainTabs = window->findChild<ZzFluentUI::ZzTabWidget *>(
        QStringLiteral("mainTabs"));
    expect(mainTabs != nullptr, "window exposes Fluent main tabs");
    if (mainTabs != nullptr) {
      const QStringList expectedTabs{
          QStringLiteral("下载镜像"), QStringLiteral("设备"),
          QStringLiteral("完整升级"), QStringLiteral("分区"), QStringLiteral("设置")};
      QStringList actualTabs;
      for (int index = 0; index < mainTabs->count(); ++index)
        actualTabs.append(mainTabs->tabText(index));
      expect(actualTabs == expectedTabs,
             "main tabs preserve the required fixed order");
      const int originalTab = mainTabs->currentIndex();
      for (int index = 0; index < mainTabs->count(); ++index) {
        mainTabs->setCurrentIndex(index);
        const QString expectedTitle =
            QStringLiteral("ZzRkDevTool-") + expectedTabs.at(index);
        expect(window->windowTitle() == expectedTitle &&
                   window->titleBar() != nullptr &&
                   window->titleBar()->title() == expectedTitle,
               "window and framework title bar follow the active page");
      }
      mainTabs->setCurrentIndex(originalTab);
      expect(!mainTabs->tabsClosable(), "main tabs cannot be closed");
      expect(!mainTabs->isMovable(), "main tabs cannot be reordered");
      auto *bar = mainTabs->fluentTabBar();
      expect(bar != nullptr, "main tabs expose a Fluent tab bar");
      if (bar != nullptr) {
        expect(!bar->isTearOffEnabled(), "main tabs cannot be detached");
        expect(!bar->isTabTransferEnabled(), "main tabs cannot be transferred");
        expect(bar->newTabButton() != nullptr,
               "main tabs expose the new-tab control for policy enforcement");
        if (bar->newTabButton() != nullptr) {
          expect(!bar->newTabButton()->isVisible(),
                 "main tabs hide the new-tab action");
        }
      }

      const int selectedTab = mainTabs->currentIndex();
      mainTabs->setTabPinned(0, true);
      expect(
          mainTabs->count() == expectedTabs.size() &&
              mainTabs->currentIndex() == selectedTab,
          "pin decoration does not create, remove, or navigate business tabs");
      mainTabs->setTabModified(1, true);
      expect(mainTabs->count() == expectedTabs.size() &&
                 mainTabs->currentIndex() == selectedTab,
             "modified decoration does not create, remove, or navigate "
             "business tabs");
      mainTabs->setTabAttention(2, true);
      expect(mainTabs->count() == expectedTabs.size() &&
                 mainTabs->currentIndex() == selectedTab,
             "attention decoration does not create, remove, or navigate "
             "business tabs");
    }
  }

  if (window == nullptr) {
    return 1;
  }
  auto *tabs =
      window->findChild<ZzFluentUI::ZzTabWidget *>(QStringLiteral("mainTabs"));
  auto *downloadView =
      window->findChild<zzrk::gui::ImageDownloadPage *>(QStringLiteral("downloadImagePage"));
  expect(tabs != nullptr && controller->mainTabs() == tabs,
         "controller stores the framework main tabs");
  expect(controller->connectionStatusText() != nullptr,
         "controller creates shared connection status");
  expect(controller->refreshButton() != nullptr,
         "controller creates shared refresh button");
  expect(controller->lastScanText() != nullptr,
         "controller creates shared last scan status");
  expect(downloadView != nullptr,
         "initial route creates the download page in the framework host");
  if (downloadView != nullptr && tabs != nullptr) {
    auto *downloadTable = downloadView->findChild<QTableWidget *>(
        QStringLiteral("downloadImageTable"));
    expect(downloadTable != nullptr && downloadTable->columnCount() == 9,
           "initial page exposes the nine-column download plan");
    expect(downloadView->findChild<QProgressBar *>(
               QStringLiteral("overallImageDownloadProgress")) != nullptr,
           "initial page exposes overall download progress");
    expect(downloadView->findChild<QProgressBar *>(
               QStringLiteral("operationImageDownloadProgress")) != nullptr,
           "initial page exposes operation download progress");
    expect(tabs->findChild<QPushButton *>(QStringLiteral("inspectDeviceButton")) !=
               nullptr,
           "controller adds the device page to the workspace tabs");

    bool scanFinished = false;
    QEventLoop scanLoop;
    QObject::connect(controller,
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
    controller->selectDevice(controller->selectedPortPath());
    expect(selectedDeviceChanges == 1,
           "unchanged device selection does not emit another change");
    // 初始扫描结束后再开模态窗口，避免提前消费扫描完成信号。
    // 捕获退回系统/旧 Qt 文件弹窗的回归；取消选择不得启动固件解析。
    bool pickerObserved = false;
    QTimer::singleShot(0, window, [&] {
      auto *modal = QApplication::activeModalWidget();
      pickerObserved = modal != nullptr;
      expect(qobject_cast<ZzFluentUI::ZzContentDialog *>(modal) != nullptr,
             "firmware file selection uses the framework Fluent dialog");
      if (modal != nullptr)
        modal->close();
    });
    QMetaObject::invokeMethod(controller, "chooseFirmwareImage",
                              Qt::DirectConnection);
    expect(pickerObserved, "firmware chooser opens a modal selection window");

    auto *themeController = application->themeController();
    auto *titleBar = window->titleBar();
    expect(themeController != nullptr && titleBar != nullptr,
           "framework window exposes theme controller and title bar");
    if (themeController != nullptr && titleBar != nullptr) {
      const auto originalThemeMode = themeController->mode();
      expect(originalThemeMode == ZzFluentUI::ZzThemeMode::System ||
                 originalThemeMode == ZzFluentUI::ZzThemeMode::Light ||
                 originalThemeMode == ZzFluentUI::ZzThemeMode::Dark ||
                 originalThemeMode == ZzFluentUI::ZzThemeMode::HighContrast,
             "theme controller exposes a readable initial mode");
      auto *const originalStyle = QApplication::style();
      int snapshotChanges = 0;
      const auto snapshotConnection = QObject::connect(
          themeController, &ZzFluentUI::ZzThemeController::snapshotChanged,
          [&snapshotChanges] { ++snapshotChanges; });

      themeController->setMode(ZzFluentUI::ZzThemeMode::Light);
      expect(waitFor([themeController, titleBar, tabs, window] {
               const QColor surface =
                   QApplication::palette().color(QPalette::Window);
               return themeController->mode() == ZzFluentUI::ZzThemeMode::Light &&
                      titleBar->palette().color(QPalette::Window) == surface &&
                      tabs->palette().color(QPalette::Window) == surface &&
                      window->statusBar()->palette().color(QPalette::Window) ==
                          surface;
             }),
             "framework propagates the light baseline to window surfaces");
      const QColor lightTitleSurface =
          titleBar->palette().color(QPalette::Window);
      const QColor lightTabsSurface = tabs->palette().color(QPalette::Window);
      const QColor lightStatusSurface =
          window->statusBar()->palette().color(QPalette::Window);

      const int snapshotsBeforeDark = snapshotChanges;
      themeController->setMode(ZzFluentUI::ZzThemeMode::Dark);
      expect(waitFor([&] { return snapshotChanges > snapshotsBeforeDark; }),
             "dark theme publishes a new snapshot");
      const QColor darkSurface =
          QApplication::palette().color(QPalette::Window);
      expect(titleBar->themeMode() == ZzFluentUI::ZzThemeMode::Dark,
             "dark snapshot refreshes the title-bar theme state");
      expect(waitFor([titleBar, darkSurface, lightTitleSurface] {
               const QColor surface =
                   titleBar->palette().color(QPalette::Window);
               return surface == darkSurface && surface != lightTitleSurface;
             }),
             "dark snapshot refreshes the title-bar palette");
      expect(waitFor([tabs, darkSurface, lightTabsSurface] {
               const QColor surface = tabs->palette().color(QPalette::Window);
               return surface == darkSurface && surface != lightTabsSurface;
             }),
             "dark snapshot refreshes the main-tabs palette");
      expect(waitFor([window, darkSurface, lightStatusSurface] {
               const QColor surface =
                   window->statusBar()->palette().color(QPalette::Window);
               return surface == darkSurface && surface != lightStatusSurface;
             }),
             "dark snapshot refreshes the status-bar palette");

      auto *themeButton = titleBar->findChild<QToolButton *>(
          QStringLiteral("zzTitleBarThemeButton"));
      expect(themeButton != nullptr && themeButton->menu() == nullptr,
             "framework title bar exposes its theme toggle button");
      if (themeButton != nullptr) {
        int toggleNotifications = 0;
        std::optional<ZzFluentUI::ZzThemeMode> reportedThemeMode;
        const auto toggleConnection = QObject::connect(
            window, &ZzPureTools::ZzApplicationWindow::themeToggled,
            [&toggleNotifications, &reportedThemeMode](const auto mode) {
              ++toggleNotifications;
              reportedThemeMode = mode;
            });
        for (const auto baselineMode : {ZzFluentUI::ZzThemeMode::Light,
                                       ZzFluentUI::ZzThemeMode::Dark,
                                       ZzFluentUI::ZzThemeMode::System,
                                       ZzFluentUI::ZzThemeMode::HighContrast}) {
          const int notificationsBeforeToggle = toggleNotifications;
          themeController->setMode(baselineMode);
          expect(waitFor([titleBar, baselineMode] {
                   return titleBar->themeMode() == baselineMode;
                 }),
                 "external theme mode synchronizes the framework title bar");
          expect(toggleNotifications == notificationsBeforeToggle,
                 "external theme synchronization emits no window toggle");
          const auto expectedMode =
              themeController->resolvedMode() == ZzFluentUI::ZzThemeMode::Dark
                  ? ZzFluentUI::ZzThemeMode::Light
                  : ZzFluentUI::ZzThemeMode::Dark;
          themeButton->click();
          expect(waitFor([themeController, titleBar, themeButton, expectedMode] {
                   return themeController->mode() == expectedMode &&
                          titleBar->themeMode() == expectedMode &&
                          themeButton->isChecked();
                 }),
                 "framework theme button switches the application theme");
          expect(toggleNotifications == notificationsBeforeToggle + 1 &&
                     reportedThemeMode == expectedMode,
                 "each button click reports exactly one framework toggle");
        }
        QObject::disconnect(toggleConnection);
      }

      const QPalette originalPalette = QApplication::palette();
      QPalette updatedPalette = originalPalette;
      const QColor inheritedSurface{QStringLiteral("#204060")};
      updatedPalette.setColor(QPalette::Window, inheritedSurface);
      QApplication::setPalette(updatedPalette);
      expect(waitFor([titleBar, inheritedSurface] {
               return titleBar->palette().color(QPalette::Window) ==
                      inheritedSurface;
             }),
             "title bar inherits application palette changes");
      expect(tabs->palette().color(QPalette::Window) == inheritedSurface,
             "main tabs inherit application palette changes");
      expect(window->statusBar()->palette().color(QPalette::Window) ==
                 inheritedSurface,
             "status bar inherits application palette changes");
      QApplication::setPalette(originalPalette);

      themeController->setMode(originalThemeMode);
      expect(
          waitFor([&] { return themeController->mode() == originalThemeMode; }),
          "theme controller restores the original mode");
      expect(QApplication::style() == originalStyle,
             "theme switching preserves the application style object");
      QObject::disconnect(snapshotConnection);
      checkFilePickers(window, themeController);
      checkFileOperationDialogs(window);
      checkMessageDialogs(window);
    }

    auto *buttonTitleBar = window->titleBar();
    auto *pinButton = window->findChild<QAbstractButton *>(
        QStringLiteral("zzTitleBarAlwaysOnTopButton"));
    expect(pinButton != nullptr && buttonTitleBar != nullptr,
           "framework title bar exposes the always-on-top button");
    if (pinButton != nullptr && buttonTitleBar != nullptr) {
      const bool originallyPinned =
          window->windowFlags().testFlag(Qt::WindowStaysOnTopHint);
      const auto originalWindowState = window->windowState();
      for (const bool pinned : {!originallyPinned, originallyPinned}) {
        pinButton->click();
        expect(waitFor([window, buttonTitleBar, pinButton, pinned] {
                 return window->windowFlags().testFlag(
                            Qt::WindowStaysOnTopHint) == pinned &&
                        buttonTitleBar->isAlwaysOnTop() == pinned &&
                        pinButton->isChecked() == pinned;
               }),
               "framework pin button synchronizes window and title bar");
        expect(window->isVisible() &&
                   window->windowState() == originalWindowState,
               "framework pinning preserves visibility and window state");
      }
    }
    auto *minimizeButton =
        buttonTitleBar != nullptr
            ? qobject_cast<QAbstractButton *>(buttonTitleBar->minimizeButton())
            : nullptr;
    auto *maximizeButton =
        buttonTitleBar != nullptr
            ? qobject_cast<QAbstractButton *>(buttonTitleBar->maximizeButton())
            : nullptr;
    expect(minimizeButton != nullptr,
           "framework title bar exposes a minimize intent button");
    expect(maximizeButton != nullptr,
           "framework title bar exposes a maximize intent button");
    if (minimizeButton != nullptr)
      minimizeButton->click();
    expect(waitFor([window] { return window->isMinimized(); }),
           "title-bar minimize intent minimizes the framework window");
    window->showNormal();
    expect(waitFor([window] {
             return !window->isMinimized() && !window->isMaximized();
           }),
           "framework window returns to normal state for maximize testing");
    if (maximizeButton != nullptr)
      maximizeButton->click();
    expect(waitFor([window] { return window->isMaximized(); }),
           "title-bar maximize intent maximizes the framework window");
    if (maximizeButton != nullptr)
      maximizeButton->click();
    expect(waitFor([window] {
             return !window->isMinimized() && !window->isMaximized();
           }),
           "title-bar restore intent returns the framework window to normal");

    auto *readGptButton = downloadView->findChild<QToolButton *>(
        QStringLiteral("readDownloadGptButton"));
    expect(readGptButton != nullptr && readGptButton->isEnabled(),
           "selected Loader device can read its GPT");
    if (readGptButton != nullptr)
      readGptButton->click();
    QEventLoop gptLoop;
    QTimer::singleShot(100, &gptLoop, &QEventLoop::quit);
    gptLoop.exec();
    const auto *layoutPath = downloadView->findChild<QLineEdit *>(
        QStringLiteral("downloadParameterPath"));
    expect(backend->partitionReads.load() == 1 && layoutPath != nullptr &&
               layoutPath->text() == QStringLiteral("1-5 / 设备 GPT") &&
               downloadTable->rowCount() == 3,
           "controller reads and applies device GPT in the framework window");

    bool inspectionFinished = false;
    QEventLoop inspectionLoop;
    QObject::connect(
        controller,
        &zzrk::gui::MainWorkspaceController::deviceInspectionCompleted,
        &inspectionLoop, [&](bool) {
          inspectionFinished = true;
          inspectionLoop.quit();
        });
    auto *inspectButton =
        tabs->findChild<QPushButton *>(QStringLiteral("inspectDeviceButton"));
    expect(inspectButton != nullptr && inspectButton->isEnabled(),
           "selected Loader device can be inspected");
    if (inspectButton != nullptr)
      inspectButton->click();
    controller->handleDeviceChange();
    QTimer::singleShot(2000, &inspectionLoop, &QEventLoop::quit);
    inspectionLoop.exec();
    const auto *inspectionStatus =
        tabs->findChild<QLabel *>(QStringLiteral("deviceDetailStatus"));
    expect(inspectionFinished && inspectionStatus != nullptr &&
               inspectionStatus->text() ==
                   QStringLiteral("读取 ChipInfo 失败（1-5）"),
           "device inspection preserves the specific error message");
    if (scanCompletions < 2) {
      QEventLoop pendingScanLoop;
      QObject::connect(controller,
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
    harness->loadDownloadParameter(parameterPath);
    harness->loadDownloadDirectory(directory.path());
    bool downloadFinished = false;
    QEventLoop downloadLoop;
    QObject::connect(
        controller, &zzrk::gui::MainWorkspaceController::imageDownloadCompleted,
        &downloadLoop, [&](bool) {
          downloadFinished = true;
          downloadLoop.quit();
        });
    auto *startButton = downloadView->findChild<QPushButton *>(
        QStringLiteral("startImageDownloadButton"));
    expect(startButton != nullptr && startButton->isEnabled(),
           "valid download inputs enable image preparation");
    QString confirmationText;
    controller->setShowModalDialogs(true);
    QTimer::singleShot(0, application, [&] {
      auto *confirmation = qobject_cast<ZzFluentUI::ZzContentDialog *>(
          application->activeModalWidget());
      expect(confirmation != nullptr, "destructive download confirmation uses the Fluent host");
      if (confirmation == nullptr) {
        if (auto *legacy = qobject_cast<QMessageBox *>(application->activeModalWidget()))
          legacy->done(QMessageBox::Cancel);
        return;
      }
      confirmationText = confirmation->text();
      confirmation->reject();
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
    controller->setShowModalDialogs(false);
    if (startButton != nullptr)
      startButton->click();
    QTimer::singleShot(2000, &downloadLoop, &QEventLoop::quit);
    downloadLoop.exec();
    const auto *downloadStatus = downloadView->findChild<QLabel *>(
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
    const auto *overallProgress = downloadView->findChild<QProgressBar *>(
        QStringLiteral("overallImageDownloadProgress"));
    const auto *operationProgress = downloadView->findChild<QProgressBar *>(
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
    const auto *notice = downloadView->findChild<ZzFluentUI::ZzInfoBar *>(
        QStringLiteral("imageDownloadErrorNotice"));
    expect(notice != nullptr && downloadStatus != nullptr && notice->isOpen() &&
               notice->severity() == ZzFluentUI::ZzInfoBar::Error &&
               notice->message() == downloadStatus->text(),
           "typed download failure exposes its exact message in the framework notice");
    // 未知总量与正常进度之间切换时，两个百分比都必须正确更新。
    downloadView->setProgress(0, 0, true, true);
    const auto *overallText = downloadView->findChild<QLabel *>(
        QStringLiteral("overallImageDownloadProgressPercent"));
    const auto *operationText = downloadView->findChild<QLabel *>(
        QStringLiteral("operationImageDownloadProgressPercent"));
    expect(overallText != nullptr && operationText != nullptr &&
               overallText->text() == QStringLiteral("进行中") &&
               operationText->text() == QStringLiteral("进行中"),
           "unknown progress totals show activity for both scopes");
    downloadView->setProgress(42, 76);
    expect(overallText != nullptr && operationText != nullptr &&
               overallText->text() == QStringLiteral("42%") &&
               operationText->text() == QStringLiteral("76%"),
           "overall and current partition percentages remain independently visible");
    downloadView->setProgress(0, 0);
  }
  const auto upgradeCancellation =
      controller->viewModel()->upgradeCancellation();
  const auto downloadCancellation =
      controller->viewModel()->downloadCancellation();
  const auto partitionCancellation =
      controller->viewModel()->partitionCancellation();
  expect(saveVisualEvidence(*application, *window),
         "visual smoke saves light, minimum, and dark screenshots");
  auto *titleBar = window->titleBar();
  auto *closeButton =
      titleBar != nullptr
          ? qobject_cast<QAbstractButton *>(titleBar->closeButton())
          : nullptr;
  expect(closeButton != nullptr,
         "framework title bar exposes a close intent button");
  QPointer<ZzPureTools::ZzApplicationWindow> closedWindow{window};
  QPointer<zzrk::gui::MainWorkspaceController> closedController{controller};
  if (closeButton != nullptr)
    closeButton->click();
  expect(waitFor([application] { return application->windowCount() == 0; }),
         "title-bar close removes the framework window from application "
         "ownership");
  expect(closedWindow == nullptr,
         "title-bar close destroys the unique framework window");
  expect(closedController == nullptr,
         "title-bar close destroys the workspace controller");
  expect(backend->stopCalls == 1 && !backend->monitoring,
         "window close stops device monitoring exactly once");
  expect(upgradeCancellation != nullptr && upgradeCancellation->load(),
         "window close cancels upgrade work");
  expect(downloadCancellation != nullptr && downloadCancellation->load(),
         "window close cancels image download work");
  expect(partitionCancellation != nullptr && partitionCancellation->load(),
         "window close cancels partition work");
  expect(lifecycle != nullptr && lifecycle->shutdownComplete(),
         "window close completes controller shutdown before destruction");
  expect(lifecycle != nullptr && lifecycle->monitoringStopped(),
         "window close records stopped device monitoring");
  expect(lifecycle != nullptr && lifecycle->allWatchersStopped(),
         "window close waits for every controller watcher");

  harness.reset();
  const QStringList shutdownScenarios{
      QStringLiteral("viewModelDeviceSelectionChanged"),
      QStringLiteral("viewModelBusyChanged"),
      QStringLiteral("viewModelProgressChanged"),
      QStringLiteral("refreshDevicesBusyChanged"),
      QStringLiteral("shutdownBusyChanged"),
      QStringLiteral("pendingRefreshAfterInspection"),
      QStringLiteral("attachPagesRebuildBusyChanged"),
      QStringLiteral("clearImagePlanBusyChanged"),
      QStringLiteral("loadDownloadParameterLayoutChanged"),
      QStringLiteral("selectionClearsInspection"),
      QStringLiteral("selectionClearsDeviceGpt"),
      QStringLiteral("selectionClearsPartitionTable"),
      QStringLiteral("devicesChanged"),
      QStringLiteral("selectedDeviceChanged"),
      QStringLiteral("devicePartitionTableChanged"),
      QStringLiteral("partitionTableAppliesDeviceGpt"),
      QStringLiteral("scanCompleted")};
  for (const QString &scenario : shutdownScenarios) {
    const int exitCode = QProcess::execute(
        executable, {QStringLiteral("--synchronous-shutdown"), scenario});
    expect(exitCode == 0,
           "isolated synchronous shutdown scenario exits successfully");
  }
  return failures;
}
