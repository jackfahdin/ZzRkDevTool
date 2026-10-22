// SPDX-License-Identifier: MIT
#pragma once
#include <string>
namespace zzrk::driver {
struct DriverDevice {
  std::string instanceId, hardwareId, service, infName;
  std::string description, provider, version, location, portPath,
      originalInfName;
  bool backupAvailable{};
  bool bindingKnown{};
  bool originalBound{};
};
enum class DriverAction { installWinUsb, restoreOriginal };
enum class DriverBlockReason {
  none,
  unsupportedDevice,
  busy,
  libraryMissing,
  alreadyWinUsb,
  backupMissing,
  originalPackageUnknown,
  alreadyOriginal,
  bindingUnknown
};
struct DriverContext {
  bool busy{};
  bool libraryAvailable{true};
  bool originalBackupAvailable{};
};
struct DriverDecision {
  bool allowed{};
  bool backupRequired{};
  DriverBlockReason reason{DriverBlockReason::unsupportedDevice};
};
bool isManagedDevice(const DriverDevice &device);
// 系统硬件 ID 可能在 MI 前插入 REV；只接受精确的 VID/PID/MI 对应关系。
bool matchesUsbHardwareId(const std::string &canonical,
                          const std::string &actual);
DriverDecision evaluateDriverAction(const DriverDevice &device,
                                    DriverAction action, DriverContext context);
} // namespace zzrk::driver
