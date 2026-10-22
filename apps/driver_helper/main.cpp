// SPDX-License-Identifier: MIT
#define WIN32_LEAN_AND_MEAN
#include "WindowsDevice.hpp"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <libwdi.h>
#include <memory>
#include <optional>
#include <stdexcept>
#include <thread>
#include <zzrk/transport/LibusbBulkTransport.hpp>

namespace fs = std::filesystem;
using namespace zzrk::driver;
namespace {
struct Module {
  HMODULE handle{};
  ~Module() {
    if (handle)
      FreeLibrary(handle);
  }
  template <class T> T function(const char *name) const {
    const auto entry = GetProcAddress(handle, name);
    if (!entry)
      throw std::runtime_error(std::string("libwdi entry missing: ") + name);
    // Windows 保证 GetProcAddress 的函数指针可以转换为对应导出类型。
    T result{};
    static_assert(sizeof(result) == sizeof(entry));
    std::memcpy(&result, &entry, sizeof(result));
    return result;
  }
};
DriverDevice findDevice(const std::string &id) {
  auto current = devices();
  auto it = std::find_if(current.begin(), current.end(), [&](const auto &d) {
    return _stricmp(d.instanceId.c_str(), id.c_str()) == 0;
  });
  if (it == current.end())
    throw std::runtime_error("Selected supported device is no longer present");
  return *it;
}
bool administrator() {
  SID_IDENTIFIER_AUTHORITY nt = SECURITY_NT_AUTHORITY;
  PSID group{};
  BOOL member{};
  if (!AllocateAndInitializeSid(&nt, 2, SECURITY_BUILTIN_DOMAIN_RID,
                                DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0,
                                &group))
    return false;
  CheckTokenMembership(nullptr, group, &member);
  FreeSid(group);
  return member != FALSE;
}
void printList() {
  const bool library =
      fs::is_regular_file(applicationDirectory() / L"libwdi.dll");
  std::cout << "{\"libraryAvailable\":" << (library ? "true" : "false")
            << ",\"devices\":[";
  bool first = true;
  for (const auto &d : devices()) {
    if (!first)
      std::cout << ',';
    first = false;
    std::cout << '{';
    const std::pair<const char *, std::string> fields[]{
        {"instanceId", d.instanceId},
        {"hardwareId", d.hardwareId},
        {"service", d.service},
        {"infName", d.infName},
        {"description", d.description},
        {"provider", d.provider},
        {"version", d.version},
        {"location", d.location},
        {"portPath", d.portPath},
        {"originalInfName", d.originalInfName},
        {"backupPath", utf8(driverDirectory(d).wstring())},
        {"lastMessage",
         trustedTree(driverDirectory(d) / L"last.ini")
             ? utf8(readRecord(driverDirectory(d) / L"last.ini", L"message"))
             : ""}};
    bool firstField = true;
    for (const auto &[key, value] : fields) {
      if (!firstField)
        std::cout << ',';
      firstField = false;
      std::cout << jsonString(key) << ':' << jsonString(value);
    }
    std::cout << ",\"backupAvailable\":"
              << (d.backupAvailable ? "true" : "false")
              << ",\"bindingKnown\":" << (d.bindingKnown ? "true" : "false")
              << ",\"originalBound\":" << (d.originalBound ? "true" : "false")
              << '}';
  }
  std::cout << "]}\n";
}
int probe(const DriverDevice &target) {
  zzrk::transport::UsbDeviceScanner scanner;
  const auto scan = scanner.scanRockchipDevices();
  if (scan.error)
    throw std::runtime_error("USB probe enumeration failed: " +
                             scan.error->message);
  for (const auto &device : scan.devices) {
    if (device.portPath != target.portPath)
      continue;
    auto opened =
        device.mode == zzrk::transport::DeviceMode::Maskrom
            ? zzrk::transport::LibusbBulkTransport::openMaskrom(device)
            : zzrk::transport::LibusbBulkTransport::open(device);
    if (auto *error = std::get_if<zzrk::transport::LibusbOpenError>(&opened))
      throw std::runtime_error("USB interface probe failed: stage " +
                               std::to_string(static_cast<int>(error->code)) +
                               ", libusb " + std::to_string(error->nativeCode));
    auto &transport =
        std::get<std::unique_ptr<zzrk::transport::LibusbBulkTransport>>(opened);
    const auto released = transport->close();
    if (released < 0)
      throw std::runtime_error("USB interface release failed: " +
                               std::to_string(released));
    std::cout
        << "USB interface probe passed without sending RockUSB commands\n";
    return 0;
  }
  throw std::runtime_error("USB probe target is no longer present");
}
void backupOriginal(const DriverDevice &device) {
  const auto root = driverDirectory(device);
  if (hasOriginalBackup(device)) {
    // 同硬件 ID 共享恢复记录，但不能覆盖另一个原厂驱动版本的备份。
    // 恢复后 Windows 可能重新分配 oemNN.inf 名称，比较内容而不是旧发布编号。
    if (!originalPackageBound(device))
      throw std::runtime_error("A different original driver is already "
                               "recorded for this hardware ID");
    return;
  }
  const auto original = root / L"original";
  createProtectedDirectory(original);
  // 失败残留不能当作新备份继续叠加，也不能无确认删除用户已有数据。
  if (!fs::is_empty(original))
    throw std::runtime_error("Incomplete original driver backup exists; "
                             "inspect backup directory before retrying");
  const auto arguments = L"/export-driver " + wide(device.infName) + L" \"" +
                         original.wstring() + L"\"";
  if (!runSystemTool(L"pnputil.exe", arguments))
    throw std::runtime_error("Failed to export original driver; device binding "
                             "has not been changed");
  protectExportedPackage(original);
  const auto inf = originalInf(device);
  if (inf.empty())
    throw std::runtime_error("Exported original driver could not be verified; "
                             "device binding has not been changed");
  const auto record = root / L"state.ini";
  writeRecord(record, L"hardware", wide(device.hardwareId));
  writeRecord(record, L"service", wide(device.service));
  writeRecord(record, L"infName", wide(device.infName));
  if (!hasOriginalBackup(device))
    throw std::runtime_error("Original driver recovery record is incomplete");
}
int install(const DriverDevice &device) {
  auto dll = applicationDirectory() / L"libwdi.dll";
  Module module{LoadLibraryExW(dll.c_str(), nullptr,
                               LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR |
                                   LOAD_LIBRARY_SEARCH_SYSTEM32)};
  if (!module.handle)
    failWindows("Load bundled libwdi.dll");
  auto createList =
      module.function<decltype(&wdi_create_list)>("wdi_create_list");
  auto destroyList =
      module.function<decltype(&wdi_destroy_list)>("wdi_destroy_list");
  auto prepare =
      module.function<decltype(&wdi_prepare_driver)>("wdi_prepare_driver");
  auto installDriver =
      module.function<decltype(&wdi_install_driver)>("wdi_install_driver");
  auto errorText = module.function<decltype(&wdi_strerror)>("wdi_strerror");
  auto requireWdi = [&](int code) {
    if (code != WDI_SUCCESS)
      throw std::runtime_error(std::string("libwdi: ") + errorText(code));
  };
  wdi_device_info *list{};
  wdi_options_create_list options{};
  options.list_all = TRUE;
  options.trim_whitespaces = TRUE;
  requireWdi(createList(&list, &options));
  std::unique_ptr<wdi_device_info, decltype(destroyList)> listGuard(
      list, destroyList);
  wdi_device_info *selected{};
  for (auto *entry = list; entry; entry = entry->next) {
    if (entry->device_id &&
        _stricmp(entry->device_id, device.instanceId.c_str()) == 0) {
      if (selected)
        throw std::runtime_error("Ambiguous libwdi device identity");
      selected = entry;
    }
  }
  if (!selected || selected->vid != 0x2207 || selected->pid != 0x350b)
    throw std::runtime_error(
        "libwdi could not confirm selected RockUSB device");
  if (!selected->hardware_id)
    throw std::runtime_error("libwdi hardware ID is unavailable");
  requireExclusiveDevice(device, selected->hardware_id);
  const auto package = driverDirectory(device) / L"winusb";
  createProtectedDirectory(package);
  const auto verifyResources = [&] {
    if (!trustedTree(package))
      throw std::runtime_error("Unsafe WinUSB resource directory");
    for (const auto &entry : fs::recursive_directory_iterator(package))
      if (!trustedTree(entry.path()))
        throw std::runtime_error(
            "Unsafe generated WinUSB resource permissions");
  };
  // 旧尝试留下的子目录也必须先检查，不能等提权写入后才发现 junction 或个人
  // owner。
  verifyResources();
  auto directory = utf8(package.wstring());
  wdi_options_prepare_driver prepareOptions{};
  prepareOptions.driver_type = WDI_WINUSB;
  char vendor[] = "Rockchip (ZzRkDevTool WinUSB)";
  prepareOptions.vendor_name = vendor;
  // 保留上游的签名与证书提示，不使用 disable_signing 或静默证书选项。
  requireWdi(
      prepare(selected, directory.c_str(), "zzrk-winusb.inf", &prepareOptions));
  // 隔离构建的 libwdi 从创建时使用 Administrators owner；执行前再次逐项验证。
  verifyResources();
  const auto fresh = findDevice(device.instanceId);
  if (!fresh.bindingKnown ||
      _stricmp(fresh.service.c_str(), device.service.c_str()) != 0 ||
      _stricmp(fresh.infName.c_str(), device.infName.c_str()) != 0)
    throw std::runtime_error("Device driver changed while preparing the "
                             "package; retry after refreshing");
  requireExclusiveDevice(fresh, selected->hardware_id);
  wdi_options_install_driver installOptions{};
  installOptions.pending_install_timeout = 120000;
  const int result = installDriver(selected, directory.c_str(),
                                   "zzrk-winusb.inf", &installOptions);
  if (result == WDI_ERROR_USER_CANCEL)
    return 20;
  requireWdi(result);
  return 0;
}
bool waitForService(const std::string &id, const std::string &service,
                    bool restoring, bool &reboot) {
  for (int retry = 0; retry < 30; ++retry) {
    for (const auto &d : devices()) {
      if (_stricmp(d.instanceId.c_str(), id.c_str()) != 0)
        continue;
      const auto status = bindingStatus(d);
      reboot = reboot || status.rebootRequired;
      if (reboot)
        return false;
      if (d.bindingKnown && status.ready &&
          _stricmp(d.service.c_str(), service.c_str()) == 0 &&
          (!restoring || d.originalBound))
        return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
  }
  return false;
}
} // namespace
int wmain(int argc, wchar_t **argv) {
  std::optional<DriverDevice> selected;
  try {
    if (argc == 2 && std::wstring(argv[1]) == L"list") {
      printList();
      return 0;
    }
    if (argc == 3 && std::wstring(argv[1]) == L"probe")
      return probe(findDevice(utf8(argv[2])));
    if (argc != 3 || (std::wstring(argv[1]) != L"install" &&
                      std::wstring(argv[1]) != L"restore"))
      throw std::runtime_error(
          "Usage: zzrk-driver-helper list | probe <instance-id> | install "
          "<instance-id> | restore <instance-id>");
    if (!administrator())
      throw std::runtime_error("Driver changes require administrator approval");
    useProtectedResourceOwner();
    // 不接受外部目录、命令、INF 或 DLL
    // 参数；所有安装资源来自固定的应用和备份目录。
    selected = findDevice(utf8(argv[2]));
    const bool restoring = std::wstring(argv[1]) == L"restore";
    const auto decision = evaluateDriverAction(
        *selected,
        restoring ? DriverAction::restoreOriginal : DriverAction::installWinUsb,
        {.libraryAvailable =
             fs::is_regular_file(applicationDirectory() / L"libwdi.dll"),
         .originalBackupAvailable = selected->backupAvailable});
    if (!decision.allowed)
      throw std::runtime_error(
          "Driver operation refused by device or backup policy");
    createProtectedDirectory(driverDirectory(*selected));
    // 跨进程串行化安装，避免两次 UAC 操作同时替换同一个硬件 ID。
    HANDLE mutex =
        CreateMutexW(nullptr, FALSE, L"Global\\ZzRkDevTool.DriverInstall");
    if (!mutex)
      failWindows("Create driver operation lock");
    const DWORD lock = WaitForSingleObject(mutex, 0);
    if (lock != WAIT_OBJECT_0 && lock != WAIT_ABANDONED) {
      CloseHandle(mutex);
      return 24;
    }
    struct Unlock {
      HANDLE h;
      ~Unlock() {
        ReleaseMutex(h);
        CloseHandle(h);
      }
    } unlock{mutex};
    writeRecord(driverDirectory(*selected) / L"last.ini", L"message",
                L"Driver operation started");
    int code{};
    std::string expected = "WinUSB";
    bool reboot{};
    if (restoring) {
      expected = utf8(
          readRecord(driverDirectory(*selected) / L"state.ini", L"service"));
      restoreSelectedDriver(*selected, originalInf(*selected), reboot);
    } else {
      if (decision.backupRequired)
        backupOriginal(*selected);
      code = install(*selected);
    }
    std::string message;
    if (code == 20)
      message = "Driver installation cancelled";
    else {
      const bool confirmed =
          !reboot &&
          waitForService(selected->instanceId, expected, restoring, reboot);
      if (reboot) {
        code = 10;
        message = "Windows requires a restart to complete the driver change";
      } else if (!confirmed) {
        code = 23;
        message = "Driver operation submitted, but binding not confirmed; "
                  "reconnect device and refresh";
      } else
        message =
            restoring
                ? "Original driver binding restored"
                : "WinUSB binding confirmed; run USB probe before flashing";
    }
    writeRecord(driverDirectory(*selected) / L"last.ini", L"message",
                wide(message));
    std::cout << message << '\n';
    return code;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    if (selected) {
      try {
        if (trustedTree(driverDirectory(*selected)))
          writeRecord(driverDirectory(*selected) / L"last.ini", L"message",
                      wide(error.what()));
      } catch (...) {
      }
    }
    return 22;
  }
}
