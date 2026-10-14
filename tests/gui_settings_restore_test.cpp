// SPDX-License-Identifier: MIT
#include "GuiSettings.hpp"
#include "GuiTestHarness.hpp"
#include "GuiTranslations.hpp"
#include "GuiWindowShell.hpp"
#include "MainWorkspaceController.hpp"
#include "pages/FullUpgradePage.hpp"
#include "pages/ImageDownloadPage.hpp"
#include "pages/SettingsPage.hpp"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QProgressBar>
#include <QPushButton>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <ZzFluentUI/ZzContentDialog.h>
#include <ZzFluentUI/ZzContentDialogButton.h>
#include <ZzFluentUI/ZzTabWidget.h>
#include <ZzFluentUI/ZzThemeController.h>
#include <ZzFluentUI/ZzThemeMode.h>
#include <ZzPureTools/ZzApplicationWindow.h>
#include <iostream>
#include <string_view>

namespace {

int checkLiveLanguage() {
  QTemporaryDir fixture;
  auto store = std::make_shared<zzrk::gui::GuiSettingsStore>(
      fixture.filePath("user"), fixture.filePath("app"));
  if (!store->initialize(zzrk::gui::GuiSettingsLocation::User))
    return 1;
  store->setValue("appearance/language", "zh_CN");
  auto harness = zzrk::gui::test::GuiTestHarness::create({}, store);
  if (!harness)
    return 1;
  // 先等待正常启动扫描收敛，再设置草稿；异步扫描与语言刷新是两件不同的事。
  QEventLoop startup;
  bool scanned = false;
  QObject::connect(harness->controller(),
                   &zzrk::gui::MainWorkspaceController::scanCompleted, &startup,
                   [&] {
                     scanned = true;
                     startup.quit();
                   });
  QTimer::singleShot(3000, &startup, &QEventLoop::quit);
  startup.exec();
  if (!scanned)
    return 1;
  auto *window = harness->window();
  auto *tabs = window->findChild<ZzFluentUI::ZzTabWidget *>("mainTabs");
  auto *language = window->findChild<QComboBox *>("settingsLanguageCombo");
  auto *verify = window->findChild<QComboBox *>("verifyModeCombo");
  auto *path = window->findChild<QLineEdit *>("firmwareImagePath");
  auto *start = window->findChild<QPushButton *>("startUpgradeButton");
  auto *download =
      window->findChild<zzrk::gui::ImageDownloadPage *>("downloadImagePage");
  auto *upgrade =
      window->findChild<zzrk::gui::FullUpgradePage *>("fullUpgradePage");
  auto *table = window->findChild<QTableWidget *>("downloadImageTable");
  auto *progress = window->findChild<QProgressBar *>("overallUpgradeProgress");
  if (!tabs || !language || !verify || !path || !start || !download ||
      !upgrade || !table || !progress)
    return 1;
  upgrade->setFirmwarePath(fixture.filePath("手工镜像.img"));
  verify->setCurrentIndex(1);
  zzrk::gui::ImageDownloadPage::PlanRow row;
  row.target = "boot_a";
  row.file = fixture.filePath("boot.img");
  row.status = zzrk::gui::guiText("就绪");
  row.selected = true;
  row.verify = false;
  download->setPlan({row});
  upgrade->setOverallProgress(37);
  upgrade->setStatus(zzrk::gui::guiText("正在等待当前 USB 分块完成..."));
  upgrade->setBusy(true, false, true);
  auto *settingsPage =
      window->findChild<zzrk::gui::SettingsPage *>("settingsPage");
  settingsPage->setBusy(true);
  tabs->setCurrentIndex(4);
  int planChanges = 0;
  QObject::connect(download, &zzrk::gui::ImageDownloadPage::planChanged, window,
                   [&] { ++planChanges; });
  for (const auto &code :
       {QStringLiteral("en"), QStringLiteral("zh_CN"), QStringLiteral("en")}) {
    language->setCurrentIndex(language->findData(code));
    QCoreApplication::sendPostedEvents(nullptr, QEvent::LanguageChange);
    QCoreApplication::processEvents();
    const bool english = code == "en";
    if (tabs->tabText(4) != (english ? "Settings" : "设置") ||
        tabs->tabText(0) != (english ? "Download images" : "下载镜像") ||
        tabs->tabText(1) != (english ? "Device" : "设备") ||
        tabs->tabText(2) != (english ? "Full upgrade" : "完整升级") ||
        tabs->tabText(3) != (english ? "Partitions" : "分区") ||
        window->windowTitle() !=
            (english ? "ZzRkDevTool-Settings" : "ZzRkDevTool-设置") ||
        start->text() != (english ? "Start upgrade" : "开始升级") ||
        table->horizontalHeaderItem(4)->text() !=
            (english ? "Image file" : "镜像文件") ||
        path->placeholderText() !=
            (english ? "Choose update.img" : "选择 update.img")) {
      std::cerr << "FAIL: switching settings language must update existing "
                   "pages immediately\n";
      return 1;
    }
    auto *status = window->findChild<QLabel *>("upgradeStatus");
    if (table->item(0, zzrk::gui::ImageDownloadPage::StatusColumn)->text() !=
            (english ? "Ready" : "就绪") ||
        status->text() !=
            (english ? "Waiting for the current USB block to finish..."
                     : "正在等待当前 USB 分块完成...") ||
        verify->itemText(1) != (english ? "All partitions" : "全部分区") ||
        !language->isEnabled()) {
      std::cerr
          << "FAIL: dynamic states and options must translate while busy\n";
      return 1;
    }
    auto *close = window->findChild<QToolButton *>("zzTitleBarCloseButton");
    if (!close || close->toolTip() != (english ? "Close" : "关闭")) {
      std::cerr << "FAIL: framework system button tooltips must follow "
                   "application language\n";
      return 1;
    }
    if (window->findChild<zzrk::gui::ImageDownloadPage *>(
            "downloadImagePage") != download ||
        path->text() != fixture.filePath("手工镜像.img") ||
        verify->currentIndex() != 1 || progress->value() != 37 ||
        start->isEnabled() || tabs->currentIndex() != 4 ||
        !download->planChoices()[0].selected ||
        download->planChoices()[0].verify || planChanges != 0 ||
        store->value("appearance/language").toString() != code) {
      std::cerr << "FAIL: language update must preserve task and draft state "
                   "without business signals\n";
      std::cerr << "path=" << path->text().toStdString()
                << " verify=" << verify->currentIndex()
                << " progress=" << progress->value()
                << " enabled=" << start->isEnabled()
                << " tab=" << tabs->currentIndex()
                << " selected=" << download->planChoices()[0].selected
                << " verifyRow=" << download->planChoices()[0].verify
                << " changes=" << planChanges << " language="
                << store->value("appearance/language").toString().toStdString()
                << '\n';
      return 1;
    }
  }
  const auto screenshots = qEnvironmentVariable("ZZRK_GUI_SCREENSHOT_DIR");
  if (!screenshots.isEmpty()) {
    window->show();
    QCoreApplication::processEvents();
    if (!QDir{}.mkpath(screenshots) ||
        !window->grab().save(
            QDir(screenshots).filePath("settings-live-english.png")))
      return 1;
  }
  settingsPage->setBusy(false);
  auto *reset = window->findChild<QPushButton *>("resetSettingsButton");
  QTimer::singleShot(0, window, [] {
    if (auto *dialog = QApplication::activeModalWidget())
      if (auto *confirm =
              dialog->findChild<QPushButton *>("zzContentDialogPrimaryButton"))
        confirm->click();
  });
  reset->click();
  QCoreApplication::sendPostedEvents(nullptr, QEvent::LanguageChange);
  const bool systemEnglish = QLocale::system().language() == QLocale::English;
  if (store->value("appearance/language").toString() != "system" ||
      tabs->tabText(4) != (systemEnglish ? "Settings" : "设置")) {
    std::cerr
        << "FAIL: resetting defaults must apply system language immediately\n";
    return 1;
  }
  std::cout << "GUI live language changes preserve task and draft state\n";
  return 0;
}

int checkEnglishStartup() {
  // 每个进程只创建一次 QApplication，保持框架字体资源的真实启动生命周期。
  QTemporaryDir fixture;
  zzrk::gui::GuiSettingsStore original(fixture.filePath("user"),
                                       fixture.filePath("app"));
  if (!original.initialize(zzrk::gui::GuiSettingsLocation::User))
    return 1;
  original.setValue("appearance/language", "en");
  if (!original.sync())
    return 1;
  auto english = std::make_shared<zzrk::gui::GuiSettingsStore>(
      fixture.filePath("user"), fixture.filePath("app"));
  auto located = english->locate();
  if (!located || !located.value())
    return 1;
  auto harness = zzrk::gui::test::GuiTestHarness::create({}, english);
  if (!harness)
    return 1;
  auto *tabs =
      harness->window()->findChild<ZzFluentUI::ZzTabWidget *>("mainTabs");
  auto *verify = harness->window()->findChild<QComboBox *>("verifyModeCombo");
  if (!tabs || !verify || tabs->tabText(4) != "Settings" ||
      verify->itemText(0) != "Critical partitions") {
    std::cerr << "FAIL: persisted English language applies before page "
                 "construction\n";
    return 1;
  }
  const auto screenshots = qEnvironmentVariable("ZZRK_GUI_SCREENSHOT_DIR");
  if (!screenshots.isEmpty()) {
    if (!QDir{}.mkpath(screenshots))
      return 1;
    harness->window()->show();
    tabs->setCurrentIndex(4);
    QCoreApplication::processEvents();
    if (!harness->window()->grab().save(
            QDir(screenshots).filePath("settings-english.png")))
      return 1;
  }
  std::cout << "GUI English startup from persisted language passed\n";
  return 0;
}

} // namespace

int main(int argc, char **argv) {
  if (argc == 2 && std::string_view(argv[1]) == "--live")
    return checkLiveLanguage();
  if (argc == 2 && std::string_view(argv[1]) == "--english")
    return checkEnglishStartup();
  // 真实保存后重建窗口，捕获漏掉恢复、关窗未保存以及复用上次设备状态的问题。
  QTemporaryDir fixture;
  auto store = std::make_shared<zzrk::gui::GuiSettingsStore>(
      fixture.filePath("user"), fixture.filePath("app"));
  if (!store->initialize(zzrk::gui::GuiSettingsLocation::User))
    return 1;
  store->setValue("appearance/theme", "dark");
  store->setValue("upgrade/verify", "all");
  store->setValue("upgrade/reboot", false);
  store->setValue("partition/verify", false);
  store->setValue("window/tab", 2);
  store->setValue("upgrade/image", fixture.filePath("missing.img"));
  const auto parameterPath = fixture.filePath("parameter.txt");
  const auto bootPath = fixture.filePath("boot_custom.img");
  QFile parameterFile(parameterPath), bootFile(bootPath);
  const QByteArray parameter("MACHINE_MODEL: gui-rk3588\nTYPE: "
                             "GPT\nCMDLINE:mtdparts=rk29xxnand:0x100@0x8000("
                             "boot_a),0x100@0x8100(boot_b)\n");
  if (!parameterFile.open(QIODevice::WriteOnly) ||
      parameterFile.write(parameter) != parameter.size() ||
      !bootFile.open(QIODevice::WriteOnly) || bootFile.write("bootdata") != 8)
    return 1;
  parameterFile.close();
  bootFile.close();
  store->setValue("download/layout", "parameter");
  store->setValue("download/parameter", parameterPath);
  store->setValue("download/directory", fixture.path());
  store->setValue("download/images", QVariantMap{{"boot_a", bootPath}});
  store->setValue("download/selections", QVariantMap{{"boot_a", false}});
  store->setValue("download/verifications", QVariantMap{{"boot_a", false}});
  if (!store->sync())
    return 1;
  auto harness = zzrk::gui::test::GuiTestHarness::create({}, store);
  if (!harness)
    return 1;
  auto *window = harness->window();
  auto *verify = window->findChild<QComboBox *>("verifyModeCombo");
  auto *reboot = window->findChild<QCheckBox *>("rebootAfterFlashCheckBox");
  auto *partition =
      window->findChild<QCheckBox *>("verifyPartitionWriteCheckBox");
  auto *tabs = window->findChild<ZzFluentUI::ZzTabWidget *>("mainTabs");
  auto *path = window->findChild<QLineEdit *>("firmwareImagePath");
  if (!verify || !reboot || !partition || !tabs || !path)
    return 1;
  auto *download =
      window->findChild<zzrk::gui::ImageDownloadPage *>("downloadImagePage");
  if (!download || download->layoutPathEdit()->text() != parameterPath)
    return 1;
  bool foundBoot = false;
  for (const auto &choice : download->planChoices())
    if (choice.target == "boot_a") {
      foundBoot = !choice.selected && !choice.verify;
    }
  if (!foundBoot || !harness->controller()->selectedPortPath().isEmpty() ||
      harness->controller()->devicePartitionTable().has_value()) {
    std::cerr << "FAIL: restored draft retains choices without trusted device "
                 "state\n";
    return 1;
  }
  if (verify->currentIndex() != 1 || reboot->isChecked() ||
      partition->isChecked() || tabs->currentIndex() != 2 ||
      path->text() != fixture.filePath("missing.img") ||
      harness->application()->themeController()->mode() !=
          ZzFluentUI::ZzThemeMode::Dark) {
    std::cerr << "FAIL: stored preferences and missing-file draft restored\n";
    return 1;
  }
  auto *settingsPage =
      window->findChild<zzrk::gui::SettingsPage *>("settingsPage");
  auto *reset = window->findChild<QPushButton *>("resetSettingsButton");
  if (!settingsPage || !reset)
    return 1;
  QTimer::singleShot(0, window, [settingsPage] {
    auto *dialog = qobject_cast<ZzFluentUI::ZzContentDialog *>(
        QApplication::activeModalWidget());
    if (!dialog)
      return;
    settingsPage->setBusy(true); // 模态确认期间也可能收到异步设备忙碌通知。
    if (auto *confirm =
            dialog->findChild<QPushButton *>("zzContentDialogPrimaryButton"))
      confirm->click();
    else
      dialog->reject();
  });
  reset->click();
  if (store->value("upgrade/image").toString() !=
      fixture.filePath("missing.img")) {
    std::cerr
        << "FAIL: reset must recheck busy state after modal confirmation\n";
    return 1;
  }
  settingsPage->setBusy(false);
  verify->setCurrentIndex(0);
  reboot->setChecked(true);
  tabs->setCurrentIndex(1);
  window->show();
  const auto screenshots = qEnvironmentVariable("ZZRK_GUI_SCREENSHOT_DIR");
  if (!screenshots.isEmpty()) {
    if (!QDir{}.mkpath(screenshots))
      return 1;
    tabs->setCurrentIndex(4);
    QCoreApplication::processEvents();
    if (!window->grab().save(QDir(screenshots).filePath("settings-dark.png")))
      return 1;
    auto *themeCombo = window->findChild<QComboBox *>("settingsThemeCombo");
    if (!themeCombo)
      return 1;
    themeCombo->setCurrentIndex(1);
    QCoreApplication::processEvents();
    if (!window->grab().save(QDir(screenshots).filePath("settings-light.png")))
      return 1;
    tabs->setCurrentIndex(1);
  }
  window->close();
  harness.reset();
  zzrk::gui::GuiSettingsStore reopened(fixture.filePath("user"),
                                       fixture.filePath("app"));
  auto located = reopened.locate();
  if (!located || !located.value() ||
      reopened.value("upgrade/verify").toString() != "critical" ||
      !reopened.value("upgrade/reboot").toBool() ||
      reopened.value("window/tab").toInt() != 1) {
    std::cerr << "FAIL: changed preferences saved on close\n";
    return 1;
  }
  std::cout << "GUI preferences restore and close persistence passed\n";
  return 0;
}
