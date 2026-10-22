// SPDX-License-Identifier: MIT
#include <algorithm>
#include <cctype>
#include <zzrk/driver/DriverPolicy.hpp>
namespace zzrk::driver {
namespace {
std::string upper(std::string text) {
  std::ranges::transform(text, text.begin(), [](unsigned char c) {
    return static_cast<char>(std::toupper(c));
  });
  return text;
}
bool isOemInf(const std::string &name) {
  const auto value = upper(name);
  return value.starts_with("OEM") && value.ends_with(".INF") &&
         value.size() > 7 &&
         std::all_of(value.begin() + 3, value.end() - 4,
                     [](unsigned char c) { return std::isdigit(c) != 0; });
}
} // namespace
bool matchesUsbHardwareId(const std::string &canonical,
                          const std::string &actual) {
  auto value = upper(actual);
  const auto revision = value.find("&REV_");
  if (revision != std::string::npos) {
    const auto start = revision + 5;
    if (start + 4 > value.size() ||
        !std::all_of(value.begin() + static_cast<std::ptrdiff_t>(start),
                     value.begin() + static_cast<std::ptrdiff_t>(start + 4),
                     [](unsigned char c) { return std::isxdigit(c) != 0; }))
      return false;
    value.erase(revision, 9);
  }
  return upper(canonical) == value;
}
bool isManagedDevice(const DriverDevice &device) {
  // 白名单只覆盖项目已验证的 RK3588；不能让驱动管理变成任意 USB 换驱动工具。
  const auto hardware = upper(device.hardwareId);
  constexpr auto base = "USB\\VID_2207&PID_350B";
  if (hardware != base) {
    const std::string prefix = std::string(base) + "&MI_";
    if (!hardware.starts_with(prefix) || hardware.size() != prefix.size() + 2 ||
        !std::isxdigit(
            static_cast<unsigned char>(hardware[hardware.size() - 1])) ||
        !std::isxdigit(
            static_cast<unsigned char>(hardware[hardware.size() - 2])))
      return false;
  }
  const auto instance = upper(device.instanceId);
  if (!instance.starts_with(hardware + "\\") ||
      instance.size() <= hardware.size() + 1 || instance.size() > 200)
    return false;
  // 禁止命令行引号、控制字符及额外路径组件；实例 ID 不是文件路径。
  const auto suffix = instance.substr(hardware.size() + 1);
  if (suffix.find_first_of("\\/\"\r\n\t") != std::string::npos)
    return false;
  const auto service = upper(device.service);
  return service != "USBCCGP" && service != "USBHUB" && service != "USBHUB3";
}
DriverDecision evaluateDriverAction(const DriverDevice &device,
                                    DriverAction action,
                                    DriverContext context) {
  const auto deny = [](DriverBlockReason reason) {
    return DriverDecision{false, false, reason};
  };
  if (!isManagedDevice(device))
    return deny(DriverBlockReason::unsupportedDevice);
  if (context.busy)
    return deny(DriverBlockReason::busy);
  if (!device.bindingKnown)
    return deny(DriverBlockReason::bindingUnknown);
  const bool winusb = upper(device.service) == "WINUSB";
  if (action == DriverAction::restoreOriginal) {
    if (!context.originalBackupAvailable)
      return deny(DriverBlockReason::backupMissing);
    if (device.originalBound)
      return deny(DriverBlockReason::alreadyOriginal);
    if (!winusb && (!device.service.empty() || !device.infName.empty()))
      return deny(DriverBlockReason::originalPackageUnknown);
    return {true, false, DriverBlockReason::none};
  }
  if (!context.libraryAvailable)
    return deny(DriverBlockReason::libraryMissing);
  if (winusb)
    return deny(DriverBlockReason::alreadyWinUsb);
  const bool backupRequired =
      !device.service.empty() || !device.infName.empty();
  if (backupRequired && !isOemInf(device.infName))
    return deny(DriverBlockReason::originalPackageUnknown);
  return {true, backupRequired, DriverBlockReason::none};
}
} // namespace zzrk::driver
