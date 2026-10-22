// SPDX-License-Identifier: MIT
#pragma once
#include <filesystem>
#include <string>
#include <vector>
// Win32 SDK 头文件存在顺序依赖，不能按字母序排列。
// clang-format off
#include <windows.h>
#include <setupapi.h>
// clang-format on
#include <zzrk/driver/DriverPolicy.hpp>

namespace zzrk::driver {
std::wstring wide(const std::string &value);
std::string utf8(const std::wstring &value);
std::string jsonString(const std::string &value);
std::vector<DriverDevice> devices();
void requireExclusiveDevice(const DriverDevice &selected,
                            const std::string &targetHardware);
struct BindingStatus {
  bool rebootRequired{}, ready{};
};
BindingStatus bindingStatus(const DriverDevice &device);
bool originalPackageBound(const DriverDevice &device);
void useProtectedResourceOwner();
std::filesystem::path applicationDirectory();
std::filesystem::path driverDirectory(const DriverDevice &device);
bool trustedTree(const std::filesystem::path &path);
void createProtectedDirectory(const std::filesystem::path &path);
void protectExportedPackage(const std::filesystem::path &directory);
bool hasOriginalBackup(const DriverDevice &device);
std::filesystem::path originalInf(const DriverDevice &device);
bool runSystemTool(const std::wstring &file, const std::wstring &arguments);
void restoreSelectedDriver(const DriverDevice &device,
                           const std::filesystem::path &inf, bool &reboot);
void writeRecord(const std::filesystem::path &file, const wchar_t *key,
                 const std::wstring &value);
std::wstring readRecord(const std::filesystem::path &file, const wchar_t *key);
void failWindows(const char *operation);
} // namespace zzrk::driver
