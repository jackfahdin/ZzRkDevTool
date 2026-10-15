// SPDX-License-Identifier: MIT
#include "FileSelectionDialog.hpp"
#include "GuiAppearanceSettings.hpp"
#include "GuiSettings.hpp"
#include "GuiTestHarness.hpp"
#include "GuiTranslations.hpp"
#include "GuiWindowShell.hpp"
#include "MainWorkspaceController.hpp"
#include "pages/SettingsPage.hpp"
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QEventLoop>
#include <QFileDialog>
#include <QFontDatabase>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QProcess>
#include <QPushButton>
#include <QSettings>
#include <QScrollArea>
#include <QScrollBar>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <ZzFluentUI/ZzColorPickerButton.h>
#include <ZzFluentUI/ZzFluentTitleBar.h>
#include <ZzFluentUI/ZzTabWidget.h>
#include <ZzFluentUI/ZzThemeController.h>
#include <ZzFluentUI/ZzThemeSnapshot.h>
#include <ZzFluentUI/ZzTypographyToken.h>
#include <ZzPureTools/ZzApplicationWindow.h>
#include <iostream>

namespace {
int failures = 0;
void expect(bool ok, const char *description) {
  if (!ok) {
    ++failures;
    std::cerr << "FAIL: " << description << '\n';
  }
}
} // namespace

int main(int argc, char **argv) {
  const bool restart =
      argc == 4 && QString::fromLocal8Bit(argv[1]) == "--restart";
  QTemporaryDir fixture;
  auto settings = std::make_shared<zzrk::gui::GuiSettingsStore>(
      restart ? QString::fromLocal8Bit(argv[2]) : fixture.filePath("user"),
      restart ? QString::fromLocal8Bit(argv[3]) : fixture.filePath("app"));
  if (restart) {
    auto located = settings->locate();
    if (!located || !located.value())
      return 1;
  } else if (!settings->initialize(zzrk::gui::GuiSettingsLocation::User)) {
    return 1;
  }
  auto harness = zzrk::gui::test::GuiTestHarness::create({}, settings);
  if (!harness)
    return 1;
  auto *window = harness->window();
  auto *theme = harness->application()->themeController();
  auto *family = window->findChild<QComboBox *>("settingsFontFamilyCombo");
  auto *size = window->findChild<QDoubleSpinBox *>("settingsFontSizeSpin");
  auto *color = window->findChild<ZzFluentUI::ZzColorPickerButton *>(
      "settingsAccentButton");
  expect(family && size && color,
         "settings expose font, point size and framework accent picker");
  if (!family || !size || !color)
    return 1;
  if (restart) {
    expect(QApplication::font().family() ==
               settings->value("appearance/fontFamily").toString(),
           "new process restores saved font family");
    expect(QApplication::font().pointSizeF() == 14.0 && size->value() == 14.0,
           "new process restores saved point size");
    expect(theme->accentColor() == QColor("#a43bb7") &&
               color->selectedColor() == QColor("#a43bb7"),
           "new process restores saved accent and picker");
    return failures ? 1 : 0;
  }
  // 外观允许在设备忙碌时修改；整体恢复需要先等待真实启动扫描结束。
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
  expect(scanned, "startup scan finishes before global reset test");
  const QFont original = QApplication::font();
  const QColor originalAccent = theme->accentColor();
  QString alternate;
  for (const auto &candidate : QFontDatabase::families())
    if (candidate != original.family()) {
      alternate = candidate;
      break;
    }
  expect(!alternate.isEmpty(), "an installed alternate font is available");
  family->setCurrentIndex(family->findData(alternate));
  size->setValue(14.0);
  color->setSelectedColor(QColor("#a43bb7"));
  QCoreApplication::processEvents();
  expect(QApplication::font().family() == alternate &&
             QApplication::font().pointSizeF() == 14.0,
         "settings immediately change application typography");
  expect(theme->accentColor() == QColor("#a43bb7"),
         "picker immediately changes framework accent");
  auto *page = window->findChild<QWidget *>("downloadImagePage");
  auto *table = window->findChild<QTableWidget *>("downloadImageTable");
  expect(page && table && page->font().family() == alternate &&
             page->font().pointSizeF() == 14.0 &&
             table->font().pointSizeF() == 14.0,
         "existing pages and tables follow the application font");
  expect(theme->snapshot()
                 ->font(ZzFluentUI::ZzTypographyToken::Body)
                 .pointSizeF() == 14.0,
         "framework typography snapshot updates with application font");
  QEventLoop autosave;
  QTimer poll;
  bool saved = false;
  QObject::connect(&poll, &QTimer::timeout, &autosave, [&] {
    QSettings disk(settings->filePath(), QSettings::IniFormat);
    if (disk.value("appearance/fontFamily").toString() == alternate &&
        disk.value("appearance/fontPointSize").toDouble() == 14 &&
        disk.value("appearance/accentColor").toString() == "#a43bb7") {
      saved = true;
      autosave.quit();
    }
  });
  poll.start(25);
  QTimer::singleShot(3000, &autosave, &QEventLoop::quit);
  autosave.exec();
  poll.stop();
  expect(saved, "appearance automatically saves without an explicit flush");
  QProcess child;
  child.start(QCoreApplication::applicationFilePath(),
              {"--restart", settings->userDirectory(),
               settings->applicationDirectory()});
  expect(child.waitForFinished(15000) &&
             child.exitStatus() == QProcess::NormalExit &&
             child.exitCode() == 0,
         "saved appearance survives a real application restart");
  if (child.exitCode() != 0)
    std::cerr << child.readAllStandardError().toStdString();
  auto *resetFont = window->findChild<QPushButton *>("resetFontButton");
  auto *resetColor = window->findChild<QPushButton *>("resetAccentButton");
  expect(resetFont && resetColor, "appearance offers local reset actions");
  if (resetFont && resetColor) {
    resetFont->click();
    resetColor->click();
    expect(QApplication::font() == original &&
               theme->accentColor() == originalAccent,
           "local resets restore original system font and framework accent");
    expect(
        settings->value("appearance/fontFamily").toString().isEmpty() &&
            settings->value("appearance/fontPointSize").toDouble() == 0,
        "font reset stores system defaults instead of a machine-specific font");
  }
  family->setCurrentIndex(family->findData(alternate));
  size->setValue(16);
  color->setSelectedColor(QColor("#0f8048"));
  QTimer::singleShot(0, window, [] {
    if (auto *modal = QApplication::activeModalWidget())
      if (auto *confirm =
              modal->findChild<QPushButton *>("zzContentDialogPrimaryButton"))
        confirm->click();
  });
  window->findChild<QPushButton *>("resetSettingsButton")->click();
  expect(QApplication::font() == original &&
             theme->accentColor() == originalAccent,
         "global defaults reset immediately restores typography and accent");
  auto *appearance = window->findChild<zzrk::gui::GuiAppearanceSettings *>();
  settings->setValue("appearance/fontFamily", "zzrk-font-that-does-not-exist");
  settings->setValue("appearance/fontPointSize", "not-a-size");
  settings->setValue("appearance/accentColor", "not-a-color");
  appearance->applyStored();
  expect(QApplication::font() == original &&
             theme->accentColor() == originalAccent,
         "invalid appearance and missing font fall back to defaults");
  settings->setValue("appearance/fontPointSize", 100);
  settings->setValue("appearance/accentColor", "#55a43bb7");
  appearance->applyStored();
  expect(QApplication::font() == original &&
             theme->accentColor() == QColor("#a43bb7"),
         "out of range size falls back and accent is always opaque");
  // 修改外观和语言只影响展示，已输入的固件路径与页面实例不应重建。
  auto *draft = window->findChild<QLineEdit *>("firmwareImagePath");
  draft->setText("/tmp/appearance-draft.img");
  auto *settingsPage = window->findChild<zzrk::gui::SettingsPage *>();
  settingsPage->setBusy(true);
  size->setValue(24);
  family->setCurrentIndex(family->findData(alternate));
  expect(QApplication::font().pointSizeF() == 24 &&
             table == window->findChild<QTableWidget *>("downloadImageTable") &&
             draft->text() == "/tmp/appearance-draft.img",
         "appearance remains usable while busy and preserves page state");
  QCoreApplication::processEvents();
  expect(table->verticalHeader()->defaultSectionSize() >=
             table->fontMetrics().height() + 16,
         "large fonts increase table row height");
  expect(window->titleBar()->font().pointSizeF() == original.pointSizeF() &&
             window->titleBar()->font().family() == original.family(),
         "fixed-height framework title bar retains system decoration font");
  settingsPage->setBusy(false);
  appearance->resetFont();
  size->setValue(14);
  expect(zzrk::gui::GuiTranslations::active()->install("en"),
         "switch to English");
  QCoreApplication::processEvents();
  expect(resetFont->text() == "Reset font" &&
             resetColor->text() == "Reset color" &&
             family->itemText(0) == "System default" &&
             family->accessibleName() == "Font" &&
             size->accessibleName() == "Font size" &&
             color->accessibleName() == "Choose color",
         "appearance controls and framework picker translate live");
  QTimer::singleShot(0, window, [] {
    auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
    auto *browser = dialog ? dialog->findChild<QFileDialog *>() : nullptr;
    expect(browser && browser->font().pointSizeF() == 14,
           "file picker inherits configured application font");
    if (dialog)
      dialog->reject();
  });
  const auto selected = zzrk::gui::FileSelectionDialog::getOpenFileName(
      window, zzrk::gui::guiText("选择镜像"), fixture.path());
  expect(selected.isEmpty(), "font check cancels without selecting a file");
  const auto screenshots = qEnvironmentVariable("ZZRK_GUI_SCREENSHOT_DIR");
  if (!screenshots.isEmpty()) {
    window->findChild<ZzFluentUI::ZzTabWidget *>("mainTabs")
        ->setCurrentIndex(4);
    window->show();
    QCoreApplication::processEvents();
    expect(QDir{}.mkpath(screenshots) &&
               window->grab().save(
                   QDir(screenshots).filePath("settings-appearance.png")),
           "save settings screenshot");
    size->setValue(24);
    window->resize(980, 650);
    for (int frame = 0; frame < 3; ++frame) {
      QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
      QCoreApplication::processEvents();
    }
    auto *scroll = settingsPage->findChild<QScrollArea *>();
    expect(scroll && scroll->verticalScrollBar()->maximum() > 0,
           "large-font settings remain reachable by scrolling");
    expect(window->grab().save(
               QDir(screenshots).filePath("settings-appearance-large.png")),
           "save large font settings screenshot");
  }
  return failures ? 1 : 0;
}
