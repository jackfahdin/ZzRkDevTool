// SPDX-License-Identifier: MIT
#include "GuiSettings.hpp"
#include "GuiTestHarness.hpp"
#include "SettingsLocationDialog.hpp"
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QTemporaryDir>
#include <QTimer>
#include <ZzFluentUI/ZzContentDialog.h>
#include <ZzPureTools/ZzApplicationWindow.h>
#include <iostream>
int main() {
  // 弹窗确认必须真实保存成功；失败留在当前弹窗，取消不得创建配置。
  auto harness = zzrk::gui::test::GuiTestHarness::create();
  if (!harness)
    return 1;
  harness->window()->show();
  QTemporaryDir fixture;
  zzrk::gui::GuiSettingsStore store(fixture.filePath("user"),
                                    fixture.filePath("app"));
  bool observedFailure = false;
  const auto blocked = fixture.filePath("blocked");
  QFile blocker(blocked);
  if (!blocker.open(QIODevice::WriteOnly) || blocker.write("file") != 4)
    return 1;
  blocker.close();
  QTimer::singleShot(0, harness->window(), [&] {
    auto *dialog = qobject_cast<ZzFluentUI::ZzContentDialog *>(
        QApplication::activeModalWidget());
    if (!dialog)
      return;
    QTimer::singleShot(3000, dialog, &QDialog::reject);
    const auto screenshots = qEnvironmentVariable("ZZRK_GUI_SCREENSHOT_DIR");
    if (!screenshots.isEmpty()) {
      if (QDir{}.mkpath(screenshots))
        dialog->grab().save(QDir(screenshots).filePath("first-launch.png"));
    }
    auto *custom = dialog->findChild<QRadioButton *>("settingsLocationOption2");
    auto *path = dialog->findChild<QLineEdit *>("customSettingsDirectory");
    auto *confirm =
        dialog->findChild<QPushButton *>("confirmSettingsLocationButton");
    auto *error = dialog->findChild<QLabel *>("settingsLocationError");
    if (!custom || !path || !confirm || !error) {
      dialog->reject();
      return;
    }
    custom->setChecked(true);
    path->setText(blocked + "/invalid");
    confirm->click();
    observedFailure = dialog->isVisible() && !error->text().isEmpty() &&
                      store.directory().isEmpty();
    path->setText(fixture.filePath("配置 文件夹"));
    confirm->click();
  });
  const bool selected =
      zzrk::gui::chooseSettingsLocation(store, harness->window());
  // 配置存储会解析已有祖先目录，包括 macOS 的 /var -> /private/var。
  const auto customDirectory =
      QFileInfo(fixture.filePath("配置 文件夹")).canonicalFilePath();
  if (!selected || !observedFailure || !QFileInfo(store.filePath()).isFile() ||
      customDirectory.isEmpty() || store.directory() != customDirectory) {
    std::cerr << "FAIL: first launch validates and saves custom directory\n";
    return 1;
  }
  zzrk::gui::GuiSettingsStore canceled(fixture.filePath("canceled"),
                                       fixture.filePath("app2"));
  QTimer::singleShot(0, harness->window(), [] {
    if (auto *modal =
            qobject_cast<QDialog *>(QApplication::activeModalWidget()))
      modal->reject();
  });
  if (zzrk::gui::chooseSettingsLocation(canceled, harness->window()) ||
      QDir(fixture.filePath("canceled")).exists())
    return 1;
  std::cout << "First launch directory selection and cancel passed\n";
  return 0;
}
