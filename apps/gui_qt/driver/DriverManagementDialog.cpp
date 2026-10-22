// SPDX-License-Identifier: MIT
#include "DriverManagementDialog.hpp"
#include "GuiDialogs.hpp"
#include "GuiTranslations.hpp"
#include "WorkspaceUi.hpp"
#include <QCloseEvent>
#include <QCoreApplication>
#include <QDir>
#include <QEvent>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QProcess>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <ZzFluentUI/ZzContentDialog.h>
#if defined(Q_OS_WIN) && defined(ZZRK_ENABLE_WINDOWS_DRIVER_MANAGEMENT)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
// clang-format off
#include <windows.h>
#include <shellapi.h>
// clang-format on
#endif

namespace zzrk::gui {
namespace {
constexpr qsizetype outputLimit = 1024 * 1024;
QString helperPath() {
  return QDir(QCoreApplication::applicationDirPath())
      .filePath(QStringLiteral("zzrk-driver-helper.exe"));
}
GuiText reasonText(zzrk::driver::DriverBlockReason reason) {
  using zzrk::driver::DriverBlockReason;
  switch (reason) {
  case DriverBlockReason::none:
    return {};
  case DriverBlockReason::busy:
    return guiText("设备操作正在进行，请等待完成。");
  case DriverBlockReason::libraryMissing:
    return guiText("驱动安装库不可用，请检查安装包。");
  case DriverBlockReason::alreadyWinUsb:
    return guiText("当前已使用 WinUSB。");
  case DriverBlockReason::backupMissing:
    return guiText("没有可用的原驱动恢复记录。");
  case DriverBlockReason::originalPackageUnknown:
    return guiText("无法确认原驱动包，禁止替换。");
  case DriverBlockReason::alreadyOriginal:
    return guiText("已核实当前绑定为原驱动，无需恢复。");
  case DriverBlockReason::unsupportedDevice:
    return guiText("此设备不在驱动管理支持范围内。");
  case DriverBlockReason::bindingUnknown:
    return guiText("无法确认当前驱动绑定，禁止变更。");
  }
  return {};
}
} // namespace

DriverInventory parseDriverInventory(const QByteArray &json) {
  if (json.size() > outputLimit)
    return {};
  const auto document = QJsonDocument::fromJson(json);
  if (!document.isObject())
    return {};
  const auto root = document.object();
  if (!root.value("libraryAvailable").isBool() ||
      !root.value("devices").isArray())
    return {};
  DriverInventory result;
  result.libraryAvailable = root.value("libraryAvailable").toBool();
  for (const auto &value : root.value("devices").toArray()) {
    if (!value.isObject())
      return {};
    const auto item = value.toObject();
    for (const auto *key : {"instanceId", "hardwareId", "service", "infName",
                            "description", "provider", "version", "location",
                            "portPath", "backupPath", "lastMessage"}) {
      const auto field = item.value(QLatin1String(key));
      if (!field.isString() || field.toString().contains(QChar(0)) ||
          field.toString().size() > 8192)
        return {};
    }
    if (!item.value("backupAvailable").isBool())
      return {};
    DriverDeviceDetails details;
    auto &device = details.device;
    const auto string = [&item](const char *key) {
      return item.value(QLatin1String(key)).toString().toStdString();
    };
    device.instanceId = string("instanceId");
    device.hardwareId = string("hardwareId");
    device.service = string("service");
    device.infName = string("infName");
    device.description = string("description");
    device.provider = string("provider");
    device.version = string("version");
    device.location = string("location");
    device.backupAvailable = item.value("backupAvailable").toBool();
    // 老版本或不完整的查询不能把默认值误认为已核实的绑定。
    device.bindingKnown = item.value("bindingKnown").toBool(false);
    device.originalBound = item.value("originalBound").toBool(false);
    device.originalInfName =
        item.value("originalInfName").toString().toStdString();
    device.portPath = string("portPath");
    details.portPath = item.value("portPath").toString();
    details.backupPath = item.value("backupPath").toString();
    details.lastMessage = item.value("lastMessage").toString();
    result.devices.push_back(std::move(details));
  }
  result.valid = true;
  return result;
}
const DriverDeviceDetails *
selectedDriverDevice(const DriverInventory &inventory,
                     const QString &selectedPort) {
  if (!inventory.valid || selectedPort.isEmpty())
    return nullptr;
  const DriverDeviceDetails *selected = nullptr;
  for (const auto &device : inventory.devices)
    if (device.portPath == selectedPort) {
      if (selected)
        return nullptr;
      selected = &device;
    }
  if (!selected || selected->device.instanceId.empty())
    return nullptr;
  for (const auto &device : inventory.devices)
    if (&device != selected &&
        QString::fromStdString(device.device.instanceId)
                .compare(QString::fromStdString(selected->device.instanceId),
                         Qt::CaseInsensitive) == 0)
      return nullptr;
  return selected;
}
zzrk::driver::DriverDecision
driverActionDecision(const DriverInventory &inventory,
                     const QString &selectedPort,
                     zzrk::driver::DriverAction action, bool busy) {
  const auto *selected = selectedDriverDevice(inventory, selectedPort);
  if (!selected)
    return {};
  return zzrk::driver::evaluateDriverAction(
      selected->device, action,
      {busy, inventory.libraryAvailable, selected->device.backupAvailable});
}

DriverManagementDialog::DriverManagementDialog(QString selectedPort,
                                               bool workspaceBusy,
                                               QWidget *parent)
    : QObject(nullptr), m_dialog(new ZzFluentUI::ZzContentDialog(parent)),
      m_owner(parent ? parent->window() : nullptr),
      m_selectedPort(std::move(selectedPort)), m_workspaceBusy(workspaceBusy) {
  m_dialog->setObjectName(QStringLiteral("driverManagementDialog"));
  setGuiText(m_dialog, "title", guiText("驱动管理"));
  setGuiText(m_dialog, "closeButtonText", guiText("关闭"));
  m_dialog->setDefaultButton(ZzFluentUI::ZzContentDialogButton::Close);
  auto *body = new QWidget(m_dialog);
  auto *layout = new QVBoxLayout(body);
  m_details = ui::secondaryText({}, body);
  m_details->setObjectName(QStringLiteral("driverDetails"));
  m_details->setWordWrap(true);
  m_details->setTextFormat(Qt::PlainText);
  m_details->setTextInteractionFlags(Qt::TextSelectableByMouse);
  layout->addWidget(m_details);
  m_status = ui::secondaryText({}, body);
  m_status->setObjectName(QStringLiteral("driverStatus"));
  m_status->setWordWrap(true);
  m_status->setTextFormat(Qt::PlainText);
  layout->addWidget(m_status);
  auto *verification = ui::secondaryText(
      guiText("恢复原驱动后，请使用原厂工具验证连接。"), body);
  verification->setWordWrap(true);
  layout->addWidget(verification);
  auto *buttons = new QHBoxLayout;
  m_refresh = ui::button(guiText("刷新"), ui::Icon::Refresh, body);
  m_install = ui::button(guiText("安装 WinUSB"), ui::Icon::Device, body, true);
  m_restore = ui::button(guiText("恢复原驱动"), ui::Icon::Back, body);
  m_install->setObjectName(QStringLiteral("installWinUsbButton"));
  m_restore->setObjectName(QStringLiteral("restoreDriverButton"));
  buttons->addWidget(m_refresh);
  buttons->addStretch();
  buttons->addWidget(m_install);
  buttons->addWidget(m_restore);
  layout->addLayout(buttons);
  m_dialog->setContentWidget(body);
  m_dialog->setMinimumWidth(620);
  m_dialog->installEventFilter(this);
  QCoreApplication::instance()->installEventFilter(this);
  if (m_owner)
    m_owner->installEventFilter(this);
  connect(m_refresh, &QPushButton::clicked, this,
          &DriverManagementDialog::refresh);
  connect(m_install, &QPushButton::clicked, this,
          [this] { startAction(zzrk::driver::DriverAction::installWinUsb); });
  connect(m_restore, &QPushButton::clicked, this,
          [this] { startAction(zzrk::driver::DriverAction::restoreOriginal); });
  updateControls();
}
DriverManagementDialog::~DriverManagementDialog() {
  if (m_query) {
    // 仅收敛普通权限的 list/probe；安装进程由独立句柄管理，绝不终止。
    disconnect(m_query, nullptr, this, nullptr);
    if (m_query->state() != QProcess::NotRunning) {
      m_query->kill();
      (void)m_query->waitForFinished(1000);
    }
    delete m_query;
    m_query = nullptr;
  }
#if defined(Q_OS_WIN) && defined(ZZRK_ENABLE_WINDOWS_DRIVER_MANAGEMENT)
  if (m_processHandle)
    CloseHandle(static_cast<HANDLE>(m_processHandle));
#endif
  delete m_dialog.data();
}
QWidget *DriverManagementDialog::widget() const { return m_dialog; }
bool DriverManagementDialog::operationRunning() const noexcept {
  return m_running;
}
int DriverManagementDialog::exec() {
  QTimer::singleShot(0, this, &DriverManagementDialog::refresh);
  return m_dialog->exec();
}
bool DriverManagementDialog::eventFilter(QObject *watched, QEvent *event) {
  if (m_running && watched == QCoreApplication::instance() &&
      event->type() == QEvent::Quit)
    return true;
  if (m_running && (watched == m_dialog || watched == m_owner)) {
    if (event->type() == QEvent::Close) {
      static_cast<QCloseEvent *>(event)->ignore();
      return true;
    }
    if (event->type() == QEvent::KeyPress &&
        static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape)
      return true;
  }
  return QObject::eventFilter(watched, event);
}
void DriverManagementDialog::applyInventory(const QByteArray &json) {
  m_inventory = parseDriverInventory(json);
  if (!m_dialog)
    return;
  const auto *selected = selectedDriverDevice(m_inventory, m_selectedPort);
  if (!selected) {
    setGuiText(m_details, "text",
               guiText("无法唯一匹配当前端口的设备，请关闭后重新选择设备。"));
  } else {
    const auto &d = selected->device;
    setGuiText(
        m_details, "text",
        guiText(
            "端口：%1\n设备：%2\n服务：%3\n硬件 ID：%4\n实例 "
            "ID：%5\n提供商：%6\n版本：%7\n"
            "当前包：%8\n位置：%9\n恢复记录：%10\n最近结果：%11\n原驱动包：%12")
            .arg(m_selectedPort)
            .arg(QString::fromStdString(d.description))
            .arg(QString::fromStdString(d.service))
            .arg(QString::fromStdString(d.hardwareId))
            .arg(QString::fromStdString(d.instanceId))
            .arg(QString::fromStdString(d.provider))
            .arg(QString::fromStdString(d.version))
            .arg(QString::fromStdString(d.infName))
            .arg(QString::fromStdString(d.location))
            .arg(d.backupAvailable ? GuiText(selected->backupPath)
                                   : guiText("无"))
            .arg(selected->lastMessage)
            .arg(QString::fromStdString(d.originalInfName)));
  }
  updateControls();
}
void DriverManagementDialog::updateControls() {
  if (!m_dialog)
    return;
  const bool busy = m_workspaceBusy || m_running || m_query;
  const auto install =
      driverActionDecision(m_inventory, m_selectedPort,
                           zzrk::driver::DriverAction::installWinUsb, busy);
  const auto restore =
      driverActionDecision(m_inventory, m_selectedPort,
                           zzrk::driver::DriverAction::restoreOriginal, busy);
  m_install->setEnabled(install.allowed);
  m_restore->setEnabled(restore.allowed);
  setGuiText(m_install, "toolTip", reasonText(install.reason));
  setGuiText(m_restore, "toolTip", reasonText(restore.reason));
  m_refresh->setEnabled(!busy);
  m_dialog->setCloseButtonEnabled(!m_running);
}
void DriverManagementDialog::refresh() {
  if (!m_running && !m_query)
    query(false);
}
void DriverManagementDialog::query(bool probe, const QString &instanceId) {
  auto *process = new QProcess(this);
  m_query = process;
  auto *timeout = new QTimer(process);
  timeout->setSingleShot(true);
  connect(timeout, &QTimer::timeout, process, [process] {
    process->setProperty("queryFailed", true);
    process->kill(); // 查询可终止；驱动变更从不通过 QProcess 执行。
  });
  connect(process, &QProcess::readyReadStandardOutput, process, [process] {
    auto output = process->property("output").toByteArray();
    while (process->bytesAvailable() > 0) {
      const auto chunk = process->read(64 * 1024);
      if (chunk.isEmpty())
        break;
      if (process->property("queryFailed").toBool())
        continue;
      if (chunk.size() > outputLimit - output.size()) {
        process->setProperty("queryFailed", true);
        process->kill();
        output.clear();
        break;
      }
      output += chunk;
    }
    process->setProperty("output", output);
  });
  connect(process, &QProcess::readyReadStandardError, process,
          [process] { (void)process->readAllStandardError(); });
  const auto finished = [this, process, probe](int code) {
    if (m_query != process)
      return;
    const bool ok = code == 0 && !process->property("queryFailed").toBool() &&
                    process->exitStatus() == QProcess::NormalExit;
    m_query = nullptr;
    if (!m_dialog) {
      process->deleteLater();
      return;
    }
    if (!ok) {
      m_probeAfterRefresh = false;
      if (!probe)
        applyInventory({});
      setGuiText(
          m_status, "text",
          probe ? guiText(
                      "接口探测失败，请检查驱动或重新插拔设备；不会自动重装。")
                : guiText("驱动查询失败或超时，请检查辅助程序。"));
    } else if (probe) {
      setGuiText(m_status, "text",
                 guiText("接口探测成功：已打开、声明并释放接口。"));
    } else {
      applyInventory(process->property("output").toByteArray());
      if (m_probeAfterRefresh) {
        m_probeAfterRefresh = false;
        const auto *device = selectedDriverDevice(m_inventory, m_selectedPort);
        // 端口可在拔插后被另一块板复用，不能拿另一实例的 probe
        // 冒充本次安装结果。
        if (device &&
            QString::fromStdString(device->device.instanceId)
                    .compare(m_operationInstance, Qt::CaseInsensitive) == 0)
          query(true, QString::fromStdString(device->device.instanceId));
        else
          setGuiText(m_status, "text",
                     guiText("设备身份待确认，请重新插拔并刷新。"));
      }
    }
    process->deleteLater();
    updateControls();
  };
  connect(process, &QProcess::finished, this,
          [finished](int code, QProcess::ExitStatus) { finished(code); });
  connect(process, &QProcess::errorOccurred, this,
          [finished](QProcess::ProcessError error) {
            if (error == QProcess::FailedToStart)
              finished(-1);
          });
  updateControls();
  timeout->start(10000);
  process->start(helperPath(),
                 probe ? QStringList{QStringLiteral("probe"), instanceId}
                       : QStringList{QStringLiteral("list")});
}
void DriverManagementDialog::startAction(zzrk::driver::DriverAction action) {
  const auto decision =
      driverActionDecision(m_inventory, m_selectedPort, action,
                           m_workspaceBusy || m_running || m_query);
  if (!decision.allowed)
    return;
  const auto *selected = selectedDriverDevice(m_inventory, m_selectedPort);
  if (!selected)
    return;
  const auto instanceId = QString::fromStdString(selected->device.instanceId);
  const bool install = action == zzrk::driver::DriverAction::installWinUsb;
  if (!dialogs::confirm(m_dialog, guiText("确认驱动变更"),
                        guiText("将修改实例 %1 的驱动。同硬件 ID "
                                "的其他设备也可能受到影响。操作前请仅连接目标板"
                                "卡，并关闭原厂工具。安装前会备份原驱动"
                                "包；恢复仅使用已记录的备份。是否继续？")
                            .arg(instanceId),
                        install ? guiText("安装 WinUSB")
                                : guiText("恢复原驱动")))
    return;
  if (!driverActionDecision(m_inventory, m_selectedPort, action,
                            m_workspaceBusy || m_running || m_query)
           .allowed)
    return;
#if defined(Q_OS_WIN) && defined(ZZRK_ENABLE_WINDOWS_DRIVER_MANAGEMENT)
  const auto executable = QDir::toNativeSeparators(helperPath()).toStdWString();
  const auto parameters =
      ((install ? QStringLiteral("install \"") : QStringLiteral("restore \"")) +
       instanceId + QStringLiteral("\""))
          .toStdWString();
  SHELLEXECUTEINFOW info{};
  info.cbSize = sizeof(info);
  info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
  info.hwnd = reinterpret_cast<HWND>(m_dialog->winId());
  info.lpVerb = L"runas";
  info.lpFile = executable.c_str();
  info.lpParameters = parameters.c_str();
  info.nShow = SW_HIDE;
  m_installing = install;
  m_operationInstance = instanceId;
  m_running = true; // UAC 确认期间也禁止退出和重复启动。
  updateControls();
  if (!ShellExecuteExW(&info) || !info.hProcess) {
    finishOperation(GetLastError() == ERROR_CANCELLED ? 20UL : 22UL);
    return;
  }
  m_processHandle = info.hProcess;
  setGuiText(m_status, "text",
             guiText("驱动变更正在进行，请勿拔出设备或退出应用。"));
  updateControls();
  auto *poll = new QTimer(this);
  connect(poll, &QTimer::timeout, this, [this, poll] {
    if (WaitForSingleObject(static_cast<HANDLE>(m_processHandle), 0) !=
        WAIT_OBJECT_0)
      return;
    DWORD code = 22;
    GetExitCodeProcess(static_cast<HANDLE>(m_processHandle), &code);
    CloseHandle(static_cast<HANDLE>(m_processHandle));
    m_processHandle = nullptr;
    poll->stop();
    poll->deleteLater();
    finishOperation(code);
  });
  poll->start(200); // 无强制超时，不杀死正在写入驱动的辅助进程。
#else
  setGuiText(m_status, "text", guiText("此平台未启用 Windows 驱动管理。"));
#endif
}
void DriverManagementDialog::finishOperation(unsigned long code) {
  m_running = false;
  GuiText message;
  switch (code) {
  case 0:
    message = m_installing
                  ? guiText("驱动绑定已验证，正在重新枚举和探测接口。")
                  : guiText("原驱动绑定已恢复，请使用原厂工具验证连接。");
    break;
  case 10:
    message = guiText("驱动变更完成，需要重启 Windows。");
    break;
  case 20:
    message = guiText("驱动变更已取消。");
    break;
  case 23:
    message = guiText("绑定待确认，请重新插拔设备后刷新。");
    break;
  case 24:
    message = guiText("另一驱动操作正在进行，请等待完成。");
    break;
  default:
    message = guiText("驱动变更失败，请查看最近结果；不会自动重装。");
    break;
  }
  setGuiText(m_status, "text", message);
  // 原厂驱动可能本来就不支持 libusb，恢复成功后不能再用 WinUSB probe 判为失败。
  m_probeAfterRefresh = code == 0 && m_installing;
  refresh();
}
} // namespace zzrk::gui
