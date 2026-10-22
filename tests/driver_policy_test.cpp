// SPDX-License-Identifier: MIT
#include <iostream>
#include <zzrk/driver/DriverPolicy.hpp>

using namespace zzrk::driver;
int main() {
  int failures = 0;
  const auto check = [&](bool condition, const char *message) {
    if (!condition) {
      std::cerr << message << '\n';
      ++failures;
    }
  };
  DriverDevice board;
  board.bindingKnown = true;
  board.instanceId = "USB\\VID_2207&PID_350B\\6&123&0&3";
  board.hardwareId = "USB\\VID_2207&PID_350B";
  board.service = "rockusb";
  board.infName = "oem42.inf";
  auto decision = evaluateDriverAction(board, DriverAction::installWinUsb, {});
  check(decision.allowed && decision.backupRequired,
        "original driver must be backed up before replacement");
  check(
      !evaluateDriverAction(board, DriverAction::installWinUsb, {.busy = true})
           .allowed,
      "device operations must exclude driver changes");
  auto alien = board;
  alien.hardwareId = "USB\\VID_046D&PID_C52B";
  check(!evaluateDriverAction(alien, DriverAction::installWinUsb, {}).allowed,
        "never replace a non-Rockchip driver");
  alien = board;
  alien.instanceId = "USB\\VID_046D&PID_C52B\\fake";
  check(!evaluateDriverAction(alien, DriverAction::installWinUsb, {}).allowed,
        "instance and hardware identity must agree");
  alien = board;
  alien.service = "usbccgp";
  check(!evaluateDriverAction(alien, DriverAction::installWinUsb, {}).allowed,
        "never replace a composite parent");
  alien = board;
  alien.hardwareId += "&MI_00";
  check(!evaluateDriverAction(alien, DriverAction::installWinUsb, {}).allowed,
        "composite interface must match the instance interface");
  alien.instanceId = "USB\\VID_2207&PID_350B&MI_00\\6&123&0&3";
  check(evaluateDriverAction(alien, DriverAction::installWinUsb, {}).allowed,
        "matching RockUSB interface may be managed");
  board.service = "WINUSB";
  check(!evaluateDriverAction(board, DriverAction::installWinUsb, {}).allowed,
        "WinUSB installation is idempotent");
  check(!evaluateDriverAction(board, DriverAction::restoreOriginal, {}).allowed,
        "restore requires a recorded original driver");
  check(evaluateDriverAction(board, DriverAction::restoreOriginal,
                             {.originalBackupAvailable = true})
            .allowed,
        "a recorded original driver may be restored");
  board.service.clear();
  check(evaluateDriverAction(board, DriverAction::installWinUsb, {})
            .backupRequired,
        "a package without a service still needs backup");
  board.infName.clear();
  check(evaluateDriverAction(board, DriverAction::installWinUsb, {}).allowed &&
            !evaluateDriverAction(board, DriverAction::installWinUsb, {})
                 .backupRequired,
        "a genuinely driverless device has no original package to export");
  check(evaluateDriverAction(board, DriverAction::restoreOriginal,
                             {.originalBackupAvailable = true})
            .allowed,
        "a trusted backup must recover an interrupted driverless installation");
  board.bindingKnown = false;
  check(!evaluateDriverAction(board, DriverAction::installWinUsb, {}).allowed,
        "an unreadable binding must never be treated as driverless");
  board.bindingKnown = true;
  board.service = "rockusb";
  check(!evaluateDriverAction(board, DriverAction::restoreOriginal,
                              {.originalBackupAvailable = true})
             .allowed,
        "an external driver change must not be overwritten by restoration");
  board.originalBound = true;
  board.infName = "oem42.inf";
  check(evaluateDriverAction(board, DriverAction::restoreOriginal,
                             {.originalBackupAvailable = true})
                .reason == DriverBlockReason::alreadyOriginal,
        "already restored requires verification of the original package");
  board.infName.clear();
  board.infName.clear();
  check(!evaluateDriverAction(board, DriverAction::installWinUsb, {}).allowed,
        "do not replace a driver whose package cannot be identified");
  check(!evaluateDriverAction(board, DriverAction::installWinUsb,
                              {.libraryAvailable = false})
             .allowed,
        "missing bundled installer must prevent installation");
  check(matchesUsbHardwareId("USB\\VID_2207&PID_350B&MI_00",
                             "USB\\VID_2207&PID_350B&REV_0100&MI_00"),
        "composite revision hardware ID must match its interface");
  check(matchesUsbHardwareId("USB\\VID_2207&PID_350B",
                             "usb\\vid_2207&pid_350b&rev_0100"),
        "single interface revision ID must match without case sensitivity");
  check(!matchesUsbHardwareId("USB\\VID_2207&PID_350B&MI_00",
                              "USB\\VID_2207&PID_350B&REV_0100&MI_01"),
        "never match a different interface");
  check(!matchesUsbHardwareId("USB\\VID_2207&PID_350B",
                              "USB\\VID_2207&PID_350B&REV_0100&MI_00"),
        "a composite parent must not match an interface");
  check(!matchesUsbHardwareId("USB\\VID_2207&PID_350B",
                              "USB\\VID_2207&PID_350B&REV_ZZZZ"),
        "malformed revision must be rejected");
  return failures == 0 ? 0 : 1;
}
