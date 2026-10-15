// SPDX-License-Identifier: MIT
#pragma once
#include <QWidget>
#include <zzrk/transport/UsbDeviceScanner.hpp>
class QComboBox;
class QLabel;
class QToolButton;
class QStatusBar;
namespace zzrk::gui {
// 仅展示扫描快照与当前选择；任务、选择策略和缓存由工作区控制器管理。
class GuiDeviceStatusBar final : public QWidget {
  Q_OBJECT
public:
  explicit GuiDeviceStatusBar(QStatusBar *host);
  void setDevices(const transport::ScanResult &result, const QString &selected,
                  const QString &disconnected = {});
  void setBusy(bool busy);
  void setMonitoringError(QString error);
signals:
  void portSelected(const QString &port);
  void refreshRequested();

private:
  void refreshText();
  QComboBox *m_devices;
  QLabel *m_state;
  QToolButton *m_refresh;
  transport::ScanResult m_result;
  QString m_selected, m_disconnected, m_monitoringError;
  bool m_busy = false;
};
} // namespace zzrk::gui
