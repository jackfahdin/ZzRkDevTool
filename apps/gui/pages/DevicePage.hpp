// SPDX-License-Identifier: MIT

#pragma once

#include <zzrk/transport/UsbDeviceScanner.hpp>

#include <QWidget>

class QLabel;
class QPushButton;
class QTableWidget;

namespace zzrk::gui {

class DevicePage final : public QWidget {
  Q_OBJECT

public:
  enum Column {
    StatusColumn,
    ModeColumn,
    DeviceIdColumn,
    BusColumn,
    PortColumn,
    UsbVersionColumn,
    ColumnCount
  };
  static constexpr int DeviceKeyRole = Qt::UserRole + 1;
  static constexpr int PortPathRole = Qt::UserRole + 2;
  static constexpr int FlashableRole = Qt::UserRole + 3;
  static constexpr int ManageableRole = Qt::UserRole + 5;

  [[nodiscard]] static DevicePage *buildUi(QWidget *parent);
  [[nodiscard]] QTableWidget *table() const noexcept;
  [[nodiscard]] QString selectedPortPath() const;
  [[nodiscard]] bool selectedDeviceFlashable() const;
  [[nodiscard]] bool selectedDeviceManageable() const;
  void selectPort(const QString &port);
  void setDevices(const zzrk::transport::ScanResult &result);
  void setInspection(const QString &status, const QString &chipInfo = {},
                     const QString &capability = {},
                     const QString &flashId = {}, const QString &capacity = {},
                     const QString &flashInfo = {},
                     const QString &usbInterface = {});
  void setBusy(bool busy);

signals:
  void selectionChanged();
  void inspectRequested();

private:
  explicit DevicePage(QWidget *parent);

  QTableWidget *m_table = nullptr;
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
