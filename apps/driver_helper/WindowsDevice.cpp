// SPDX-License-Identifier: MIT
#define WIN32_LEAN_AND_MEAN
#include "WindowsDevice.hpp"
#include <aclapi.h>
#include <algorithm>
#include <array>
#include <bcrypt.h>
#include <cfgmgr32.h>
#include <cstring>
#include <fstream>
// initguid 必须位于定义设备属性 GUID 的头文件之前。
// clang-format off
#include <initguid.h>
#include <devpkey.h>
// clang-format on
#include <iomanip>
#include <knownfolders.h>
#include <libusb.h>
#include <map>
#include <memory>
#include <newdev.h>
#include <sddl.h>
#include <shlobj.h>
#include <sstream>
#include <stdexcept>

// COM 的 interface 宏会污染 libusb_config_descriptor::interface 成员名。
#ifdef interface
#undef interface
#endif

namespace fs = std::filesystem;
namespace zzrk::driver {
namespace {
struct DeviceSet {
  HDEVINFO handle{INVALID_HANDLE_VALUE};
  ~DeviceSet() {
    if (handle != INVALID_HANDLE_VALUE)
      SetupDiDestroyDeviceInfoList(handle);
  }
};
std::vector<std::wstring> properties(HDEVINFO set, SP_DEVINFO_DATA &data,
                                     const DEVPROPKEY &key,
                                     bool *known = nullptr) {
  DEVPROPTYPE type{};
  DWORD size{};
  SetupDiGetDevicePropertyW(set, &data, &key, &type, nullptr, 0, &size, 0);
  const DWORD queryError = GetLastError();
  if (known)
    *known = queryError == ERROR_NOT_FOUND ||
             queryError == ERROR_INSUFFICIENT_BUFFER;
  if (queryError == ERROR_NOT_FOUND)
    return {};
  if (size == 0 || size > 65536 || size % sizeof(wchar_t) != 0) {
    if (known)
      *known = false;
    return {};
  }
  std::vector<BYTE> bytes(size + 2 * sizeof(wchar_t), 0);
  if (!SetupDiGetDevicePropertyW(set, &data, &key, &type, bytes.data(), size,
                                 nullptr, 0)) {
    if (known)
      *known = false;
    return {};
  }
  if (type != DEVPROP_TYPE_STRING && type != DEVPROP_TYPE_STRING_LIST) {
    if (known)
      *known = false;
    return {};
  }
  if (known)
    *known = true;
  std::vector<std::wstring> values;
  const auto *text = reinterpret_cast<const wchar_t *>(bytes.data());
  // 按 MULTI_SZ 读取全部硬件 ID；REV 不是总在字符串尾部。
  for (size_t offset = 0; offset < size / sizeof(wchar_t) && text[offset];) {
    values.emplace_back(text + offset);
    offset += values.back().size() + 1;
    if (type == DEVPROP_TYPE_STRING)
      break;
  }
  return values;
}
std::wstring property(HDEVINFO set, SP_DEVINFO_DATA &data,
                      const DEVPROPKEY &key, bool *known = nullptr) {
  const auto values = properties(set, data, key, known);
  return values.empty() ? L"" : values.front();
}
std::wstring lower(std::wstring value) {
  std::transform(value.begin(), value.end(), value.begin(), towlower);
  return value;
}
std::wstring hardwareId(const std::wstring &instance) {
  // 实例 ID 的首段只用于关联，最终还必须与系统返回的硬件 ID 相互核实。
  const auto separator = instance.find(L'\\', 4);
  return separator == std::wstring::npos ? L"" : instance.substr(0, separator);
}
struct UsbIdentity {
  std::string portPath;
  std::vector<unsigned char> interfaces;
  bool composite{};
};
std::map<DEVINST, UsbIdentity> usbIdentities() {
  libusb_context *context{};
  const int initialized = libusb_init(&context);
  if (initialized < 0)
    throw std::runtime_error("libusb enumeration initialization failed: " +
                             std::to_string(initialized));
  std::unique_ptr<libusb_context, decltype(&libusb_exit)> contextGuard(
      context, libusb_exit);
  libusb_device **list{};
  const auto count = libusb_get_device_list(context, &list);
  if (count < 0)
    throw std::runtime_error("libusb device enumeration failed: " +
                             std::to_string(count));
  struct ListGuard {
    libusb_device **p;
    ~ListGuard() { libusb_free_device_list(p, 1); }
  } guard{list};
  std::map<DEVINST, UsbIdentity> result;
  for (ssize_t index = 0; index < count; ++index) {
    auto *device = list[index];
    libusb_device_descriptor descriptor{};
    if (libusb_get_device_descriptor(device, &descriptor) < 0 ||
        descriptor.idVendor != 0x2207 || descriptor.idProduct != 0x350b)
      continue;
    libusb_config_descriptor *config{};
    if (libusb_get_active_config_descriptor(device, &config) < 0)
      continue;
    std::unique_ptr<libusb_config_descriptor,
                    decltype(&libusb_free_config_descriptor)>
        configGuard(config, libusb_free_config_descriptor);
    UsbIdentity identity;
    identity.composite =
        config->bNumInterfaces > 1 ||
        (descriptor.bDeviceClass == 0xef && descriptor.bDeviceSubClass == 2 &&
         descriptor.bDeviceProtocol == 1);
    for (int interfaceIndex = 0; interfaceIndex < config->bNumInterfaces; ++interfaceIndex) {
      const auto &item = config->interface[interfaceIndex];
      for (int alt = 0; alt < item.num_altsetting; ++alt) {
        const auto &setting = item.altsetting[alt];
        if (setting.bInterfaceClass == 0xff &&
            setting.bInterfaceSubClass == 0x06 &&
            setting.bInterfaceProtocol == 0x05 &&
            setting.bAlternateSetting == 0)
          identity.interfaces.push_back(setting.bInterfaceNumber);
      }
    }
    if (identity.interfaces.size() != 1)
      continue;
    std::array<uint8_t, 8> ports{};
    const int portCount = libusb_get_port_numbers(
        device, ports.data(), static_cast<int>(ports.size()));
    if (portCount <= 0)
      continue;
    identity.portPath = std::to_string(libusb_get_bus_number(device)) + "-";
    for (int i = 0; i < portCount; ++i) {
      if (i)
        identity.portPath += '.';
      identity.portPath += std::to_string(ports[static_cast<size_t>(i)]);
    }
    // 本项目固定 libusb >=1.0.30，Windows WinUSB 后端的 session data
    // 为 DEVINST；据此精确关联状态栏端口与 PnP 实例，禁止按列表顺序猜测。
    result.emplace(static_cast<DEVINST>(libusb_get_session_data(device)),
                   std::move(identity));
  }
  return result;
}
bool trustedDirectory(const fs::path &path) {
  const DWORD attrs = GetFileAttributesW(path.c_str());
  if (attrs == INVALID_FILE_ATTRIBUTES ||
      (attrs & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
    return false;
  PSID owner{};
  PACL acl{};
  PSECURITY_DESCRIPTOR descriptor{};
  if (GetNamedSecurityInfoW(
          path.c_str(), SE_FILE_OBJECT,
          OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION, &owner,
          nullptr, &acl, nullptr, &descriptor) != ERROR_SUCCESS)
    return false;
  std::unique_ptr<void, decltype(&LocalFree)> guard(descriptor, LocalFree);
  std::array<BYTE, SECURITY_MAX_SID_SIZE> admins{}, system{};
  DWORD adminSize = static_cast<DWORD>(admins.size()),
        systemSize = static_cast<DWORD>(system.size());
  if (!CreateWellKnownSid(WinBuiltinAdministratorsSid, nullptr, admins.data(),
                          &adminSize) ||
      !CreateWellKnownSid(WinLocalSystemSid, nullptr, system.data(),
                          &systemSize) ||
      !owner || !acl ||
      (!EqualSid(owner, admins.data()) && !EqualSid(owner, system.data())))
    return false;
  constexpr DWORD writes = FILE_WRITE_DATA | FILE_APPEND_DATA | FILE_WRITE_EA |
                           FILE_WRITE_ATTRIBUTES | FILE_DELETE_CHILD | DELETE |
                           WRITE_DAC | WRITE_OWNER | GENERIC_WRITE |
                           GENERIC_ALL;
  for (DWORD i = 0; i < acl->AceCount; ++i) {
    void *entry{};
    if (!GetAce(acl, i, &entry))
      return false;
    auto *header = static_cast<ACE_HEADER *>(entry);
    if (header->AceType == ACCESS_DENIED_ACE_TYPE)
      continue;
    if (header->AceType != ACCESS_ALLOWED_ACE_TYPE)
      return false;
    auto *allowed = static_cast<ACCESS_ALLOWED_ACE *>(entry);
    auto *sid = &allowed->SidStart;
    if ((allowed->Mask & writes) && !EqualSid(sid, admins.data()) &&
        !EqualSid(sid, system.data()))
      return false;
  }
  return true;
}
fs::path driverRoot() {
  PWSTR programData{};
  if (FAILED(
          SHGetKnownFolderPath(FOLDERID_ProgramData, 0, nullptr, &programData)))
    throw std::runtime_error("Cannot locate ProgramData");
  fs::path root = fs::path(programData) / L"ZzRkDevTool-Drivers";
  CoTaskMemFree(programData);
  return root;
}
} // namespace
void failWindows(const char *operation) {
  throw std::runtime_error(std::string(operation) + " (Windows " +
                           std::to_string(GetLastError()) + ")");
}
std::wstring wide(const std::string &value) {
  if (value.empty())
    return {};
  const int size =
      MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                          static_cast<int>(value.size()), nullptr, 0);
  if (!size)
    failWindows("Invalid UTF-8");
  std::wstring result(static_cast<size_t>(size), L'\0');
  MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                      static_cast<int>(value.size()), result.data(), size);
  return result;
}
std::string utf8(const std::wstring &value) {
  if (value.empty())
    return {};
  const int size = WideCharToMultiByte(
      CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
      static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
  if (!size)
    failWindows("Invalid UTF-16");
  std::string result(static_cast<size_t>(size), '\0');
  WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                      static_cast<int>(value.size()), result.data(), size,
                      nullptr, nullptr);
  return result;
}
std::string jsonString(const std::string &value) {
  std::ostringstream out;
  out << '"';
  for (const char byte : value) {
    const auto c = static_cast<unsigned char>(byte);
    if (c == '"' || c == '\\')
      out << '\\' << static_cast<char>(c);
    else if (c < 32)
      out << "\\u" << std::hex << std::setw(4) << std::setfill('0')
          << unsigned(c);
    else
      out << static_cast<char>(c);
  }
  return out.str() + '"';
}
std::vector<DriverDevice> devices() {
  const auto identities = usbIdentities();
  DeviceSet set{SetupDiGetClassDevsW(nullptr, L"USB", nullptr,
                                     DIGCF_PRESENT | DIGCF_ALLCLASSES)};
  if (set.handle == INVALID_HANDLE_VALUE)
    failWindows("Enumerate USB devices");
  std::vector<DriverDevice> result;
  SP_DEVINFO_DATA data{};
  data.cbSize = sizeof(data);
  for (DWORD index = 0; SetupDiEnumDeviceInfo(set.handle, index, &data);
       ++index) {
    wchar_t id[MAX_DEVICE_ID_LEN]{};
    if (!SetupDiGetDeviceInstanceIdW(set.handle, &data, id, MAX_DEVICE_ID_LEN,
                                     nullptr))
      continue;
    auto canonical = hardwareId(id);
    const auto actual =
        properties(set.handle, data, DEVPKEY_Device_HardwareIds);
    if (std::none_of(actual.begin(), actual.end(), [&](const auto &value) {
          return matchesUsbHardwareId(utf8(canonical), utf8(value));
        }))
      continue;
    DriverDevice item;
    item.instanceId = utf8(id);
    item.hardwareId = utf8(canonical);
    bool serviceKnown{}, infKnown{};
    item.service =
        utf8(property(set.handle, data, DEVPKEY_Device_Service, &serviceKnown));
    if (!isManagedDevice(item))
      continue;
    auto identity = identities.find(data.DevInst);
    if (identity == identities.end() &&
        canonical.find(L"&MI_") != std::wstring::npos) {
      DEVINST parent{};
      if (CM_Get_Parent(&parent, data.DevInst, 0) == CR_SUCCESS)
        identity = identities.find(parent);
    }
    if (identity == identities.end())
      continue;
    const auto mi = item.hardwareId.find("&MI_");
    // 不依赖服务名识别复合父节点：无驱动的父节点同样不能整体替换。
    if (mi == std::string::npos && identity->second.composite)
      continue;
    if (mi != std::string::npos) {
      const auto number = static_cast<unsigned char>(
          std::stoul(item.hardwareId.substr(mi + 4), nullptr, 16));
      if (identity->second.interfaces.front() != number)
        continue;
    }
    item.portPath = identity->second.portPath;
    item.infName = utf8(
        property(set.handle, data, DEVPKEY_Device_DriverInfPath, &infKnown));
    item.bindingKnown = serviceKnown && infKnown;
    item.provider =
        utf8(property(set.handle, data, DEVPKEY_Device_DriverProvider));
    item.version =
        utf8(property(set.handle, data, DEVPKEY_Device_DriverVersion));
    item.description =
        utf8(property(set.handle, data, DEVPKEY_Device_DeviceDesc));
    item.location =
        utf8(property(set.handle, data, DEVPKEY_Device_LocationInfo));
    item.backupAvailable = hasOriginalBackup(item);
    if (item.backupAvailable) {
      item.originalInfName = utf8(originalInf(item).filename().wstring());
      item.originalBound = originalPackageBound(item);
    }
    result.push_back(std::move(item));
  }
  if (GetLastError() != ERROR_NO_MORE_ITEMS)
    failWindows("Read USB device list");
  return result;
}
void requireExclusiveDevice(const DriverDevice &selected,
                            const std::string &targetHardware) {
  if (!matchesUsbHardwareId(selected.hardwareId, targetHardware))
    throw std::runtime_error(
        "libwdi hardware identity disagrees with the selected interface");
  DeviceSet set{SetupDiGetClassDevsW(nullptr, L"USB", nullptr,
                                     DIGCF_PRESENT | DIGCF_ALLCLASSES)};
  if (set.handle == INVALID_HANDLE_VALUE)
    failWindows("Verify affected USB devices");
  SP_DEVINFO_DATA data{};
  data.cbSize = sizeof(data);
  for (DWORD index = 0; SetupDiEnumDeviceInfo(set.handle, index, &data);
       ++index) {
    bool known{};
    const auto ids =
        properties(set.handle, data, DEVPKEY_Device_HardwareIds, &known);
    if (!known)
      throw std::runtime_error(
          "Cannot verify hardware IDs of affected USB devices");
    const bool matches =
        std::any_of(ids.begin(), ids.end(), [&](const auto &id) {
          // 保守覆盖所有 REV，防止上游 INF 的不带 REV 匹配影响另一块板。
          return matchesUsbHardwareId(selected.hardwareId, utf8(id));
        });
    if (!matches)
      continue;
    wchar_t id[MAX_DEVICE_ID_LEN]{};
    if (!SetupDiGetDeviceInstanceIdW(set.handle, &data, id, MAX_DEVICE_ID_LEN,
                                     nullptr))
      failWindows("Verify affected device identity");
    if (_wcsicmp(id, wide(selected.instanceId).c_str()) != 0)
      throw std::runtime_error("Disconnect other boards with the same hardware "
                               "ID before installing WinUSB");
  }
  if (GetLastError() != ERROR_NO_MORE_ITEMS)
    failWindows("Verify affected device list");
}
BindingStatus bindingStatus(const DriverDevice &device) {
  DeviceSet set{SetupDiCreateDeviceInfoList(nullptr, nullptr)};
  SP_DEVINFO_DATA data{};
  data.cbSize = sizeof(data);
  if (set.handle == INVALID_HANDLE_VALUE ||
      !SetupDiOpenDeviceInfoW(set.handle, wide(device.instanceId).c_str(),
                              nullptr, 0, &data))
    return {};
  ULONG status{}, problem{};
  if (CM_Get_DevNode_Status(&status, &problem, data.DevInst, 0) != CR_SUCCESS)
    return {};
  SP_DEVINSTALL_PARAMS_W params{};
  params.cbSize = sizeof(params);
  if (!SetupDiGetDeviceInstallParamsW(set.handle, &data, &params))
    return {};
  const bool reboot = problem == CM_PROB_NEED_RESTART ||
                      (params.Flags & (DI_NEEDREBOOT | DI_NEEDRESTART)) != 0;
  return {reboot, !reboot && (status & DN_STARTED) != 0 &&
                      (status & DN_HAS_PROBLEM) == 0};
}
fs::path applicationDirectory() {
  std::wstring buffer(32768, L'\0');
  const DWORD count = GetModuleFileNameW(nullptr, buffer.data(),
                                         static_cast<DWORD>(buffer.size()));
  if (count == 0 || count >= buffer.size())
    failWindows("Locate driver helper");
  buffer.resize(count);
  return fs::path(buffer).parent_path();
}
fs::path driverDirectory(const DriverDevice &device) {
  auto id = wide(device.hardwareId);
  std::transform(id.begin(), id.end(), id.begin(), towupper);
  // 硬件 ID 已经由白名单验证；转成十六进制避免路径分隔符和名称冲突。
  std::wostringstream key;
  for (wchar_t c : id)
    key << std::hex << std::setw(4) << std::setfill(L'0') << unsigned(c);
  return driverRoot() / key.str();
}
bool trustedTree(const fs::path &path) {
  const auto root = driverRoot();
  auto current = path;
  for (;;) {
    if (!trustedDirectory(current))
      return false;
    if (current == root)
      return true;
    if (current == current.root_path() || current.empty())
      return false;
    current = current.parent_path();
  }
}
void createProtectedDirectory(const fs::path &path) {
  const auto root = driverRoot();
  if (path != root && !fs::exists(path.parent_path()))
    createProtectedDirectory(path.parent_path());
  PSECURITY_DESCRIPTOR descriptor{};
  // 仅管理员和 SYSTEM 可写，普通用户只读；不接受预先放置的可写目录或 junction。
  if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
          L"O:BAG:BAD:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;GRGX;;;BU)",
          SDDL_REVISION_1, &descriptor, nullptr))
    failWindows("Build backup permissions");
  SECURITY_ATTRIBUTES security{sizeof(security), descriptor, FALSE};
  const BOOL created = CreateDirectoryW(path.c_str(), &security);
  const DWORD code = GetLastError();
  LocalFree(descriptor);
  if (!created && code != ERROR_ALREADY_EXISTS) {
    SetLastError(code);
    failWindows("Create driver backup directory");
  }
  if (!trustedTree(path))
    throw std::runtime_error(
        "Unsafe driver backup directory permissions or reparse point");
}
void protectExportedPackage(const fs::path &directory) {
  // pnputil 导出的文件可能归当前管理员个人所有，必须收紧 owner 和 DACL，
  // 防止该用户的普通权限进程事后篡改恢复用的 INF/CAT/SYS。
  PSECURITY_DESCRIPTOR descriptor{};
  if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
          L"O:BAG:BAD:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;GRGX;;;BU)",
          SDDL_REVISION_1, &descriptor, nullptr))
    failWindows("Build exported package permissions");
  std::unique_ptr<void, decltype(&LocalFree)> guard(descriptor, LocalFree);
  PSID owner{};
  BOOL defaulted{}, present{};
  PACL acl{};
  GetSecurityDescriptorOwner(descriptor, &owner, &defaulted);
  GetSecurityDescriptorDacl(descriptor, &present, &acl, &defaulted);
  const auto protect = [&](const fs::path &path) {
    const auto attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES ||
        (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
      throw std::runtime_error(
          "Exported package contains an unsafe reparse point");
    const DWORD code = SetNamedSecurityInfoW(
        const_cast<wchar_t *>(path.c_str()), SE_FILE_OBJECT,
        OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION |
            PROTECTED_DACL_SECURITY_INFORMATION,
        owner, nullptr, acl, nullptr);
    if (code != ERROR_SUCCESS) {
      SetLastError(code);
      failWindows("Protect original driver package");
    }
  };
  if (fs::is_regular_file(directory))
    protect(directory);
  else
    for (const auto &entry : fs::recursive_directory_iterator(directory))
      protect(entry.path());
}
void writeRecord(const fs::path &file, const wchar_t *key,
                 const std::wstring &value) {
  if (!trustedTree(file.parent_path()) ||
      (fs::exists(file) && !trustedTree(file)))
    throw std::runtime_error("Unsafe driver record path");
  PSECURITY_DESCRIPTOR descriptor{};
  if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
          L"O:BAG:BAD:P(A;;FA;;;SY)(A;;FA;;;BA)(A;;GRGX;;;BU)", SDDL_REVISION_1,
          &descriptor, nullptr))
    failWindows("Build driver record permissions");
  SECURITY_ATTRIBUTES security{sizeof(security), descriptor, FALSE};
  HANDLE handle =
      CreateFileW(file.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &security,
                  OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  const DWORD error = GetLastError();
  LocalFree(descriptor);
  if (handle == INVALID_HANDLE_VALUE) {
    SetLastError(error);
    failWindows("Create driver record");
  }
  CloseHandle(handle);
  if (!WritePrivateProfileStringW(L"driver", key, value.c_str(), file.c_str()))
    failWindows("Save driver record");
  protectExportedPackage(file);
}
std::wstring readRecord(const fs::path &file, const wchar_t *key) {
  wchar_t buffer[4096]{};
  GetPrivateProfileStringW(L"driver", key, L"", buffer, 4096, file.c_str());
  return buffer;
}
fs::path originalInf(const DriverDevice &device) {
  const auto directory = driverDirectory(device) / L"original";
  if (!trustedTree(directory))
    return {};
  fs::path found;
  for (const auto &entry : fs::recursive_directory_iterator(directory)) {
    if (!trustedTree(entry.path()))
      return {};
    if (entry.is_regular_file() &&
        lower(entry.path().extension().wstring()) == L".inf") {
      if (!found.empty())
        return {};
      found = entry.path();
    }
  }
  return found;
}
bool hasOriginalBackup(const DriverDevice &device) {
  try {
    const auto directory = driverDirectory(device);
    const auto record = directory / L"state.ini";
    return trustedTree(record) &&
           lower(readRecord(record, L"hardware")) ==
               lower(wide(device.hardwareId)) &&
           !readRecord(record, L"infName").empty() &&
           !originalInf(device).empty();
  } catch (...) {
    return false;
  }
}
bool originalPackageBound(const DriverDevice &device) {
  if (!device.bindingKnown || device.infName.empty() ||
      !hasOriginalBackup(device))
    return false;
  wchar_t windows[MAX_PATH]{};
  if (!GetWindowsDirectoryW(windows, MAX_PATH))
    return false;
  const auto installed = fs::path(windows) / L"INF" / wide(device.infName);
  const auto saved = originalInf(device);
  try {
    const auto size = fs::file_size(installed);
    if (size > 16 * 1024 * 1024 || size != fs::file_size(saved) ||
        lower(readRecord(driverDirectory(device) / L"state.ini", L"service")) !=
            lower(wide(device.service)))
      return false;
    std::ifstream current(installed, std::ios::binary),
        backup(saved, std::ios::binary);
    return current && backup &&
           std::equal(std::istreambuf_iterator<char>(current),
                      std::istreambuf_iterator<char>(),
                      std::istreambuf_iterator<char>(backup),
                      std::istreambuf_iterator<char>());
  } catch (...) {
    return false;
  }
}
void useProtectedResourceOwner() {
  // 系统导出工具创建的文件也应从创建时就归管理员组，不能先以个人 owner 创建再补
  // ACL。
  HANDLE token{};
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_DEFAULT | TOKEN_QUERY,
                        &token))
    failWindows("Open protected resource token");
  std::array<BYTE, SECURITY_MAX_SID_SIZE> owner{};
  DWORD size = static_cast<DWORD>(owner.size());
  TOKEN_OWNER tokenOwner{owner.data()};
  const BOOL ok =
      CreateWellKnownSid(WinBuiltinAdministratorsSid, nullptr, owner.data(),
                         &size) &&
      SetTokenInformation(token, TokenOwner, &tokenOwner, sizeof(tokenOwner));
  const DWORD error = GetLastError();
  CloseHandle(token);
  if (!ok) {
    SetLastError(error);
    failWindows("Set protected resource owner");
  }
}
bool runSystemTool(const std::wstring &file, const std::wstring &arguments) {
  wchar_t system[MAX_PATH]{};
  if (!GetSystemDirectoryW(system, MAX_PATH))
    failWindows("Locate Windows system directory");
  auto executable = fs::path(system) / file;
  auto command = L"\"" + executable.wstring() + L"\" " + arguments;
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process{};
  if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr,
                      FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup,
                      &process))
    failWindows("Start system driver tool");
  CloseHandle(process.hThread);
  // 导出驱动不修改设备；超时进程仍须结束，不能与后续安装并发执行。
  const DWORD wait = WaitForSingleObject(process.hProcess, 120000);
  DWORD code{};
  if (wait != WAIT_OBJECT_0) {
    TerminateProcess(process.hProcess, ERROR_TIMEOUT);
    WaitForSingleObject(process.hProcess, 5000);
    CloseHandle(process.hProcess);
    throw std::runtime_error("Windows driver tool timed out");
  }
  GetExitCodeProcess(process.hProcess, &code);
  CloseHandle(process.hProcess);
  return code == ERROR_SUCCESS;
}
void restoreSelectedDriver(const DriverDevice &device, const fs::path &inf,
                           bool &reboot) {
  DeviceSet set{SetupDiCreateDeviceInfoList(nullptr, nullptr)};
  SP_DEVINFO_DATA data{};
  data.cbSize = sizeof(data);
  if (set.handle == INVALID_HANDLE_VALUE ||
      !SetupDiOpenDeviceInfoW(set.handle, wide(device.instanceId).c_str(),
                              nullptr, 0, &data))
    failWindows("Open restore target");
  SP_DEVINSTALL_PARAMS_W options{};
  options.cbSize = sizeof(options);
  if (!SetupDiGetDeviceInstallParamsW(set.handle, &data, &options))
    failWindows("Read restore options");
  options.Flags |= DI_ENUMSINGLEINF;
  if (inf.wstring().size() >= MAX_PATH)
    throw std::runtime_error("Original INF path is too long");
  wcscpy_s(options.DriverPath, MAX_PATH, inf.c_str());
  if (!SetupDiSetDeviceInstallParamsW(set.handle, &data, &options) ||
      !SetupDiBuildDriverInfoList(set.handle, &data, SPDIT_COMPATDRIVER))
    failWindows("Read original INF");
  SP_DRVINFO_DATA_W selected{};
  selected.cbSize = sizeof(selected);
  if (!SetupDiEnumDriverInfoW(set.handle, &data, SPDIT_COMPATDRIVER, 0,
                              &selected) ||
      !SetupDiSetSelectedDriverW(set.handle, &data, &selected))
    failWindows("Select original driver");
  BOOL needsReboot{};
  // 显式安装选中的原始 INF，避免较新的 WinUSB 包因排序优先而继续占用设备。
  HMODULE newdev =
      LoadLibraryExW(L"newdev.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
  if (!newdev)
    failWindows("Load Windows driver installation API");
  auto symbol = GetProcAddress(newdev, "DiInstallDevice");
  decltype(&DiInstallDevice) installDevice{};
  static_assert(sizeof(installDevice) == sizeof(symbol));
  std::memcpy(&installDevice, &symbol, sizeof(symbol));
  if (!installDevice) {
    FreeLibrary(newdev);
    throw std::runtime_error("Windows DiInstallDevice is unavailable");
  }
  const BOOL installed =
      installDevice(nullptr, set.handle, &data, &selected, 0, &needsReboot);
  const DWORD error = GetLastError();
  FreeLibrary(newdev);
  if (!installed) {
    SetLastError(error);
    failWindows("Restore original driver binding");
  }
  reboot = needsReboot != FALSE;
  SetupDiDestroyDriverInfoList(set.handle, &data, SPDIT_COMPATDRIVER);
}
} // namespace zzrk::driver
