// SPDX-License-Identifier: MIT

#include "DevicePage.hpp"

#include <QAbstractItemView>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStyle>
#include <QTableWidget>
#include <QVBoxLayout>

namespace zzrk::gui {
namespace {
QString portText(const zzrk::transport::UsbDeviceInfo &device) {
  return device.portPath.empty() ? QStringLiteral("-")
                                 : QString::fromStdString(device.portPath);
}
QString modeText(const zzrk::transport::DeviceMode mode) {
  switch (mode) {
  case zzrk::transport::DeviceMode::Adb:
    return QStringLiteral("ADB");
  case zzrk::transport::DeviceMode::Maskrom:
    return QStringLiteral("Maskrom");
  case zzrk::transport::DeviceMode::Loader:
    return QStringLiteral("Loader");
  case zzrk::transport::DeviceMode::Unknown:
    return QStringLiteral("未知");
  }
  return QStringLiteral("未知");
}
bool flashable(const zzrk::transport::UsbDeviceInfo &device) {
  if (device.vendorId != 0x2207U || device.productId != 0x350bU)
    return false;
  return (device.mode == zzrk::transport::DeviceMode::Loader &&
          device.bcdUsb == 0x0201U && device.bcdDevice == 0x0223U) ||
         (device.mode == zzrk::transport::DeviceMode::Maskrom &&
          device.bcdUsb == 0x0200U && device.bcdDevice == 0x0100U);
}
bool manageable(const zzrk::transport::UsbDeviceInfo &device) {
  return device.vendorId == 0x2207U && device.productId == 0x350bU &&
         device.bcdUsb == 0x0201U && device.bcdDevice == 0x0223U &&
         device.mode == zzrk::transport::DeviceMode::Loader;
}
QTableWidgetItem *item(const QString &text) {
  auto *value = new QTableWidgetItem(text);
  value->setFlags(value->flags() & ~Qt::ItemIsEditable);
  return value;
}
} // namespace

DevicePage *DevicePage::buildUi(QWidget *parent) {
  return new DevicePage(parent);
}

DevicePage::DevicePage(QWidget *parent) : QWidget(parent) {
  setObjectName(QStringLiteral("devicePage"));
  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(10, 10, 10, 10);
  m_table = new QTableWidget(0, ColumnCount, this);
  m_table->setObjectName(QStringLiteral("deviceTable"));
  m_table->setHorizontalHeaderLabels(
      {QStringLiteral("状态"), QStringLiteral("设备模式"),
       QStringLiteral("VID:PID"), QStringLiteral("总线"),
       QStringLiteral("端口路径"), QStringLiteral("USB 版本")});
  m_table->setAlternatingRowColors(true);
  m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
  m_table->setSelectionMode(QAbstractItemView::SingleSelection);
  m_table->setShowGrid(false);
  m_table->setSortingEnabled(true);
  m_table->verticalHeader()->setVisible(false);
  m_table->verticalHeader()->setDefaultSectionSize(32);
  auto *header = m_table->horizontalHeader();
  for (int column = 0; column < ColumnCount; ++column) {
    header->setSectionResizeMode(column, column == PortColumn
                                             ? QHeaderView::Stretch
                                             : QHeaderView::ResizeToContents);
  }
  layout->addWidget(m_table, 1);

  auto *group = new QGroupBox(QStringLiteral("所选设备详情"), this);
  auto *details = new QGridLayout(group);
  details->setColumnStretch(1, 1);
  m_inspectButton = new QPushButton(QStringLiteral("读取详情"), group);
  m_inspectButton->setObjectName(QStringLiteral("inspectDeviceButton"));
  m_inspectButton->setIcon(
      style()->standardIcon(QStyle::SP_FileDialogInfoView));
  m_status = new QLabel(QStringLiteral("请选择 Loader 设备"), group);
  m_status->setObjectName(QStringLiteral("deviceDetailStatus"));
  m_status->setWordWrap(true);
  auto *actions = new QHBoxLayout;
  actions->addWidget(m_inspectButton);
  actions->addWidget(m_status, 1);
  details->addLayout(actions, 0, 0, 1, 4);
  const auto addDetail = [group, details](const QString &name, QLabel *&value,
                                          int row, int column,
                                          const QString &objectName) {
    details->addWidget(new QLabel(name, group), row, column);
    value = new QLabel(QStringLiteral("-"), group);
    value->setObjectName(objectName);
    value->setTextInteractionFlags(Qt::TextSelectableByMouse);
    value->setWordWrap(true);
    details->addWidget(value, row, column + 1);
  };
  addDetail(QStringLiteral("ChipInfo"), m_chipInfo, 1, 0,
            QStringLiteral("chipInfoValue"));
  addDetail(QStringLiteral("Capability"), m_capability, 1, 2,
            QStringLiteral("capabilityValue"));
  addDetail(QStringLiteral("FlashID"), m_flashId, 2, 0,
            QStringLiteral("flashIdValue"));
  addDetail(QStringLiteral("eMMC 容量"), m_capacity, 2, 2,
            QStringLiteral("capacityValue"));
  addDetail(QStringLiteral("FlashInfo"), m_flashInfo, 3, 0,
            QStringLiteral("flashInfoValue"));
  addDetail(QStringLiteral("USB 接口"), m_usbInterface, 3, 2,
            QStringLiteral("usbInterfaceValue"));
  layout->addWidget(group);
  connect(m_table, &QTableWidget::itemSelectionChanged, this,
          &DevicePage::selectionChanged);
  connect(m_inspectButton, &QPushButton::clicked, this,
          &DevicePage::inspectRequested);
}

QTableWidget *DevicePage::table() const noexcept { return m_table; }
QString DevicePage::selectedPortPath() const {
  const int row = m_table->currentRow();
  const auto *value = row < 0 ? nullptr : m_table->item(row, StatusColumn);
  return value == nullptr ? QString{} : value->data(PortPathRole).toString();
}
bool DevicePage::selectedDeviceFlashable() const {
  const int row = m_table->currentRow();
  const auto *value = row < 0 ? nullptr : m_table->item(row, StatusColumn);
  return value != nullptr && value->data(FlashableRole).toBool();
}
bool DevicePage::selectedDeviceManageable() const {
  const int row = m_table->currentRow();
  const auto *value = row < 0 ? nullptr : m_table->item(row, StatusColumn);
  return value != nullptr && value->data(ManageableRole).toBool();
}
void DevicePage::selectPort(const QString &port) {
  for (int row = 0; row < m_table->rowCount(); ++row) {
    const auto *value = m_table->item(row, StatusColumn);
    if (value != nullptr && value->data(PortPathRole).toString() == port) {
      m_table->selectRow(row);
      return;
    }
  }
}
void DevicePage::setDevices(const zzrk::transport::ScanResult &result) {
  const QSignalBlocker blocker{m_table};
  const QString selected = selectedPortPath();
  m_table->setSortingEnabled(false);
  m_table->clearContents();
  m_table->setRowCount(static_cast<int>(result.devices.size()));
  int row = 0;
  for (const auto &device : result.devices) {
    auto *status = item(QStringLiteral("已连接"));
    status->setIcon(style()->standardIcon(QStyle::SP_DialogApplyButton));
    status->setData(
        DeviceKeyRole,
        QStringLiteral("%1|%2").arg(device.busNumber).arg(portText(device)));
    status->setData(PortPathRole, portText(device));
    status->setData(FlashableRole, flashable(device));
    status->setData(ManageableRole, manageable(device));
    m_table->setItem(row, StatusColumn, status);
    m_table->setItem(row, ModeColumn, item(modeText(device.mode)));
    m_table->setItem(row, DeviceIdColumn,
                     item(QStringLiteral("%1:%2")
                              .arg(device.vendorId, 4, 16, QLatin1Char('0'))
                              .arg(device.productId, 4, 16, QLatin1Char('0'))
                              .toUpper()));
    m_table->setItem(row, BusColumn, item(QString::number(device.busNumber)));
    m_table->setItem(row, PortColumn, item(portText(device)));
    m_table->setItem(row, UsbVersionColumn,
                     item(QStringLiteral("%1.%2%3")
                              .arg((device.bcdUsb >> 8) & 0xff)
                              .arg((device.bcdUsb >> 4) & 0x0f)
                              .arg(device.bcdUsb & 0x0f)));
    ++row;
  }
  m_table->setSortingEnabled(true);
  selectPort(selected);
  if (m_table->currentRow() < 0 && m_table->rowCount() > 0)
    m_table->selectRow(0);
}
void DevicePage::setInspection(const QString &status, const QString &chipInfo,
                               const QString &capability,
                               const QString &flashId, const QString &capacity,
                               const QString &flashInfo,
                               const QString &usbInterface) {
  m_status->setText(status);
  m_chipInfo->setText(chipInfo.isEmpty() ? QStringLiteral("-") : chipInfo);
  m_capability->setText(capability.isEmpty() ? QStringLiteral("-")
                                             : capability);
  m_flashId->setText(flashId.isEmpty() ? QStringLiteral("-") : flashId);
  m_capacity->setText(capacity.isEmpty() ? QStringLiteral("-") : capacity);
  m_flashInfo->setText(flashInfo.isEmpty() ? QStringLiteral("-") : flashInfo);
  m_usbInterface->setText(usbInterface.isEmpty() ? QStringLiteral("-")
                                                 : usbInterface);
}
void DevicePage::setBusy(const bool busy) {
  m_table->setEnabled(!busy);
  m_inspectButton->setEnabled(!busy && selectedDeviceManageable());
}

} // namespace zzrk::gui
