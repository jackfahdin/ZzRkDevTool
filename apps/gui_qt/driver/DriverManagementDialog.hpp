// SPDX-License-Identifier: MIT
#pragma once

#include <QByteArray>
#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QString>
#include <vector>
#include <zzrk/driver/DriverPolicy.hpp>

class QLabel;
class QPushButton;
class QProcess;
class QProgressBar;
class QTimer;
class QWidget;
namespace ZzFluentUI {
class ZzContentDialog;
}

namespace zzrk::gui {
struct DriverDeviceDetails {
  zzrk::driver::DriverDevice device;
  QString portPath, backupPath, lastMessage;
};
struct DriverInventory {
  bool valid{}, libraryAvailable{};
  std::vector<DriverDeviceDetails> devices;
};
DriverInventory parseDriverInventory(const QByteArray &json);
const DriverDeviceDetails *
selectedDriverDevice(const DriverInventory &inventory,
                     const QString &selectedPort);
zzrk::driver::DriverDecision
driverActionDecision(const DriverInventory &inventory,
                     const QString &selectedPort,
                     zzrk::driver::DriverAction action, bool busy);

// 内容弹窗由框架提供；本对象负责查询、策略与辅助进程的生命周期。
class DriverManagementDialog final : public QObject {
  Q_OBJECT
public:
  explicit DriverManagementDialog(QString selectedPort, bool workspaceBusy,
                                  QWidget *parent = nullptr);
  ~DriverManagementDialog() override;
  int exec();
  void applyInventory(const QByteArray &json);
  [[nodiscard]] QWidget *widget() const;
  [[nodiscard]] bool operationRunning() const noexcept;

protected:
  bool eventFilter(QObject *watched, QEvent *event) override;

private:
  void refresh();
  void query(bool probe, const QString &instanceId = {});
  void updateControls();
  void startAction(zzrk::driver::DriverAction action);
  void beginOperation(bool installing, const QString &instanceId);
  void finishOperation(unsigned long code);
  QPointer<ZzFluentUI::ZzContentDialog> m_dialog;
  QPointer<QWidget> m_owner;
  QLabel *m_details{}, *m_status{}, *m_binding{};
  QLabel *m_elapsed{};
  QProgressBar *m_progress{};
  QTimer *m_activityTimer{};
  QElapsedTimer m_activityClock;
  QPushButton *m_install{}, *m_restore{}, *m_refresh{};
  QProcess *m_query{};
  QString m_selectedPort, m_operationInstance;
  DriverInventory m_inventory;
  bool m_workspaceBusy{}, m_running{}, m_probeAfterRefresh{}, m_installing{};
  void *m_processHandle{};
};
} // namespace zzrk::gui
