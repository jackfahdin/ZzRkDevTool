// SPDX-License-Identifier: MIT

#pragma once

#include "GuiTranslations.hpp"
#include <QString>
#include <QWidget>

#include <vector>

class QLabel;
class QPushButton;
class QTableWidget;

namespace zzrk::gui {

class DevicePage final : public QWidget {
  Q_OBJECT

public:
  struct DeviceRow {
    QString key;
    GuiText status;
    GuiText mode;
    QString deviceId;
    QString bus;
    QString portPath;
    QString usbVersion;
  };

  enum Column {
    StatusColumn,
    ModeColumn,
    DeviceIdColumn,
    BusColumn,
    PortColumn,
    UsbVersionColumn,
    ColumnCount
  };
  [[nodiscard]] static DevicePage *buildUi(QWidget *parent);
  [[nodiscard]] QString selectedPortPath() const;
  void selectPort(const QString &port);
  void setDevices(std::vector<DeviceRow> devices);
  void setInspection(const GuiText &status, const GuiText &chipInfo = {},
                     const GuiText &capability = {},
                     const GuiText &flashId = {}, const GuiText &capacity = {},
                     const GuiText &flashInfo = {},
                     const GuiText &usbInterface = {});
  void setBusy(bool busy, bool canInspect);

signals:
  void selectionChanged();
  void inspectRequested();

private:
  explicit DevicePage(QWidget *parent);

  QTableWidget *m_table = nullptr;
  std::vector<DeviceRow> m_rows;
  QPushButton *m_inspectButton = nullptr;
  QLabel *m_status = nullptr;
  QLabel *m_chipInfo = nullptr;
  QLabel *m_capability = nullptr;
  QLabel *m_flashId = nullptr;
  QLabel *m_capacity = nullptr;
  QLabel *m_flashInfo = nullptr;
  QLabel *m_usbInterface = nullptr;
};

} // namespace zzrk::gui
