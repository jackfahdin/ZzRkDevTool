// SPDX-License-Identifier: MIT
#include "GuiTestHarness.hpp"
#include "driver/DriverManagementDialog.hpp"
#include "pages/DevicePage.hpp"
#include <QApplication>
#include <QDialog>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <iostream>

int main() {
  auto harness = zzrk::gui::test::GuiTestHarness::create();
  if (!harness)
    return 1;
  auto *page = zzrk::gui::DevicePage::buildUi(nullptr);
  auto *entry = page->findChild<QPushButton *>("driverManagementButton");
  if (!entry) {
    std::cerr << "FAIL: device page provides the guarded driver entry\n";
    delete page;
    return 1;
  }
#if !defined(Q_OS_WIN) || !defined(ZZRK_ENABLE_WINDOWS_DRIVER_MANAGEMENT)
  if (!entry->isHidden())
    return 1;
#endif
  page->setBusy(true, true);
  const bool blocked = !entry->isEnabled();
  delete page;
  if (!blocked)
    return 1;
  // 错选第一台设备、忽略忙状态或恢复记录缺失都会触发这些回归断言。
  const QByteArray fixture = R"({"libraryAvailable":true,"devices":[
    {"instanceId":"USB\\VID_2207&PID_350B\\FIRST","hardwareId":"USB\\VID_2207&PID_350B","service":"rockusb","infName":"oem42.inf","description":"RK3588","provider":"Rockchip","version":"1","location":"USB port","portPath":"1-5","backupPath":"","lastMessage":"","backupAvailable":false,"bindingKnown":true,"originalBound":false,"originalInfName":""},
    {"instanceId":"USB\\VID_2207&PID_350B\\SECOND","hardwareId":"USB\\VID_2207&PID_350B","service":"WinUSB","infName":"oem43.inf","description":"RK3588","provider":"libwdi","version":"2","location":"USB port","portPath":"1-6","backupPath":"C:/backup/oem42.inf","lastMessage":"verified","backupAvailable":true,"bindingKnown":true,"originalBound":false,"originalInfName":"oem42.inf"}
  ]})";
  const auto inventory = zzrk::gui::parseDriverInventory(fixture);
  const auto *selected = zzrk::gui::selectedDriverDevice(inventory, "1-6");
  if (!inventory.valid || !inventory.libraryAvailable || !selected ||
      selected->device.instanceId != "USB\\VID_2207&PID_350B\\SECOND" ||
      selected->lastMessage != "verified") {
    std::cerr << "FAIL: helper JSON selects the exact current port\n";
    return 1;
  }
  using zzrk::driver::DriverAction;
  const auto allowed = [&](const auto &data, const QString &port,
                           DriverAction action, bool busy = false) {
    return zzrk::gui::driverActionDecision(data, port, action, busy).allowed;
  };
  if (!allowed(inventory, "1-5", DriverAction::installWinUsb) ||
      allowed(inventory, "1-5", DriverAction::installWinUsb, true) ||
      allowed(inventory, "1-5", DriverAction::restoreOriginal) ||
      !allowed(inventory, "1-6", DriverAction::restoreOriginal) ||
      allowed(inventory, "missing", DriverAction::installWinUsb) ||
      allowed(inventory, "", DriverAction::installWinUsb))
    return 1;
  auto ambiguous = inventory;
  ambiguous.devices.push_back(inventory.devices.front());
  if (allowed(ambiguous, "1-5", DriverAction::installWinUsb))
    return 1;
  auto noBackup = inventory;
  noBackup.devices.back().device.backupAvailable = false;
  if (allowed(noBackup, "1-6", DriverAction::restoreOriginal))
    return 1;
  auto missingLibrary = inventory;
  missingLibrary.libraryAvailable = false;
  if (allowed(missingLibrary, "1-5", DriverAction::installWinUsb) ||
      !allowed(missingLibrary, "1-6", DriverAction::restoreOriginal))
    return 1;
  auto duplicateIdentity = inventory;
  duplicateIdentity.devices.back().device.instanceId =
      duplicateIdentity.devices.front().device.instanceId;
  if (allowed(duplicateIdentity, "1-5", DriverAction::installWinUsb))
    return 1;
  auto unknownJson = fixture;
  unknownJson.replace("\"bindingKnown\":true", "\"bindingKnown\":false");
  if (allowed(zzrk::gui::parseDriverInventory(unknownJson), "1-5",
              DriverAction::installWinUsb)) {
    std::cerr << "FAIL: unknown driver binding must block changes\n";
    return 1;
  }
  auto missingBinding = fixture;
  missingBinding.replace(",\"bindingKnown\":true", "");
  if (allowed(zzrk::gui::parseDriverInventory(missingBinding), "1-5",
              DriverAction::installWinUsb))
    return 1;
  zzrk::gui::DriverManagementDialog busyDialog("1-5", true);
  busyDialog.applyInventory(fixture);
  auto *busyInstall =
      busyDialog.widget()->findChild<QPushButton *>("installWinUsbButton");
  auto *busyRestore =
      busyDialog.widget()->findChild<QPushButton *>("restoreDriverButton");
  if (!busyInstall || !busyRestore || busyInstall->isEnabled() ||
      busyRestore->isEnabled())
    return 1;
  // 程序化信号也必须被真实启动入口拒绝，不能只依赖按钮 disabled。
  bool unexpectedConfirmation = false;
  QTimer watchdog;
  QObject::connect(&watchdog, &QTimer::timeout, [&] {
    if (auto *dialog =
            qobject_cast<QDialog *>(QApplication::activeModalWidget())) {
      unexpectedConfirmation = true;
      dialog->reject();
    }
  });
  watchdog.start(10);
  QMetaObject::invokeMethod(busyInstall, "clicked", Qt::DirectConnection);
  QMetaObject::invokeMethod(busyRestore, "clicked", Qt::DirectConnection);
  watchdog.stop();
  if (unexpectedConfirmation || busyDialog.operationRunning())
    return 1;
  zzrk::gui::DriverManagementDialog installDialog("1-5", false);
  installDialog.applyInventory(fixture);
  if (!installDialog.widget()
           ->findChild<QPushButton *>("installWinUsbButton")
           ->isEnabled() ||
      installDialog.widget()
          ->findChild<QPushButton *>("restoreDriverButton")
          ->isEnabled())
    return 1;
  installDialog.applyInventory("{}");
  if (installDialog.widget()
          ->findChild<QPushButton *>("installWinUsbButton")
          ->isEnabled())
    return 1;
  zzrk::gui::DriverManagementDialog restoreDialog("1-6", false);
  restoreDialog.applyInventory(fixture);
  if (!restoreDialog.widget()
           ->findChild<QPushButton *>("restoreDriverButton")
           ->isEnabled())
    return 1;
  if (!restoreDialog.widget()
           ->findChild<QLabel *>("driverDetails")
           ->text()
           .contains("oem42.inf"))
    return 1;
  for (const auto &malformed :
       {QByteArray("{}"), QByteArray("[]"),
        QByteArray("{\"libraryAvailable\":true,\"devices\":[{}]}"),
        QByteArray("not JSON")})
    if (zzrk::gui::parseDriverInventory(malformed).valid)
      return 1;
  return 0;
}
