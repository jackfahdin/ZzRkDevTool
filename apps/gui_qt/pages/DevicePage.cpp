// SPDX-License-Identifier: MIT

#include "DevicePage.hpp"
#include "GuiTranslations.hpp"
#include "WorkspaceUi.hpp"

#include <QAbstractItemView>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>

namespace zzrk::gui {
namespace {
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
  ui::configurePage(this, layout);
  auto *listCard = ui::card(this);
  auto *listLayout = new QVBoxLayout(listCard);
  listLayout->setContentsMargins(ui::CardPadding, ui::CardPadding,
                                 ui::CardPadding, ui::CardPadding);
  listLayout->addWidget(ui::sectionTitle(guiText("已连接设备"), listCard));
  m_table = new QTableWidget(0, ColumnCount, this);
  m_table->setObjectName(QStringLiteral("deviceTable"));
  bindGuiTranslation(m_table, [this] {
    const QSignalBlocker blocker(m_table);
    const bool sorting = m_table->isSortingEnabled();
    m_table->setSortingEnabled(false);
    // 排序后按设备键定位文案，不重建行，保留当前选择和排序。
    for (int row = 0; row < m_table->rowCount(); ++row) {
      auto *status = m_table->item(row, StatusColumn);
      if (!status)
        continue;
      const auto found = std::find_if(m_rows.begin(), m_rows.end(),
                                      [status](const DeviceRow &device) {
                                        return device.key == status->toolTip();
                                      });
      if (found != m_rows.end()) {
        status->setText(found->status.translated());
        m_table->item(row, ModeColumn)->setText(found->mode.translated());
      }
    }
    m_table->setSortingEnabled(sorting);
    m_table->setHorizontalHeaderLabels(
        {guiText("状态"), guiText("设备模式"), QStringLiteral("VID:PID"),
         guiText("总线"), guiText("端口路径"), guiText("USB 版本")});
  });
  ui::configureTable(m_table);
  m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
  m_table->setSelectionMode(QAbstractItemView::SingleSelection);
  m_table->setShowGrid(false);
  m_table->setSortingEnabled(true);
  m_table->verticalHeader()->setVisible(false);
  auto *header = m_table->horizontalHeader();
  for (int column = 0; column < ColumnCount; ++column) {
    header->setSectionResizeMode(column, column == PortColumn
                                             ? QHeaderView::Stretch
                                             : QHeaderView::ResizeToContents);
  }
  listLayout->addWidget(m_table, 1);
  layout->addWidget(listCard, 1);

  auto *group = ui::card(this);
  auto *details = new QGridLayout(group);
  details->setContentsMargins(ui::CardPadding, ui::CardPadding, ui::CardPadding,
                              ui::CardPadding);
  details->setHorizontalSpacing(16);
  details->setVerticalSpacing(12);
  details->setColumnStretch(1, 1);
  details->setColumnStretch(3, 1);
  m_inspectButton = ui::button(guiText("读取详情"), ui::Icon::Info, group);
  m_inspectButton->setObjectName(QStringLiteral("inspectDeviceButton"));
  m_status = guiLabel(guiText("请选择 Loader 设备"), group);
  m_status->setObjectName(QStringLiteral("deviceDetailStatus"));
  m_status->setWordWrap(true);
  m_status->setTextFormat(Qt::PlainText);
  auto *actions = new QHBoxLayout;
  actions->addWidget(ui::sectionTitle(guiText("所选设备详情"), group));
  actions->addStretch();
  actions->addWidget(m_inspectButton);
  details->addLayout(actions, 0, 0, 1, 4);
  details->addWidget(m_status, 1, 0, 1, 4);
  const auto addDetail = [group, details](const GuiText &name, QLabel *&value,
                                          int row, int column,
                                          const QString &objectName) {
    details->addWidget(ui::secondaryText(name, group), row, column);
    value = new QLabel(QStringLiteral("-"), group);
    value->setObjectName(objectName);
    value->setTextInteractionFlags(Qt::TextSelectableByMouse);
    value->setWordWrap(true);
    value->setTextFormat(Qt::PlainText);
    details->addWidget(value, row, column + 1);
  };
  addDetail(guiText("芯片信息"), m_chipInfo, 2, 0,
            QStringLiteral("chipInfoValue"));
  addDetail(guiText("eMMC 容量"), m_capacity, 2, 2,
            QStringLiteral("capacityValue"));

  // 原始协议字段保留在可展开区域，日常使用先展示芯片与容量。
  auto *advancedButton = new QToolButton(group);
  setGuiText(advancedButton, "text", guiText("原始设备信息"));
  advancedButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  advancedButton->setArrowType(Qt::RightArrow);
  advancedButton->setCheckable(true);
  advancedButton->setAutoRaise(true);
  advancedButton->setObjectName(QStringLiteral("deviceAdvancedDetailsButton"));
  details->addWidget(advancedButton, 3, 0, 1, 4);
  auto *advanced = new QWidget(group);
  auto *advancedLayout = new QGridLayout(advanced);
  advancedLayout->setContentsMargins(0, 0, 0, 0);
  advancedLayout->setColumnStretch(1, 1);
  advancedLayout->setColumnStretch(3, 1);
  const auto addRaw = [advanced, advancedLayout](
                          const GuiText &name, QLabel *&value, int row,
                          int column, const QString &objectName) {
    advancedLayout->addWidget(ui::secondaryText(name, advanced), row, column);
    value = new QLabel(QStringLiteral("-"), advanced);
    value->setObjectName(objectName);
    value->setTextFormat(Qt::PlainText);
    value->setTextInteractionFlags(Qt::TextSelectableByMouse);
    value->setWordWrap(true);
    advancedLayout->addWidget(value, row, column + 1);
  };
  addRaw(QStringLiteral("Capability"), m_capability, 0, 0,
         QStringLiteral("capabilityValue"));
  addRaw(QStringLiteral("FlashID"), m_flashId, 0, 2,
         QStringLiteral("flashIdValue"));
  addRaw(QStringLiteral("FlashInfo"), m_flashInfo, 1, 0,
         QStringLiteral("flashInfoValue"));
  addRaw(guiText("USB 接口"), m_usbInterface, 1, 2,
         QStringLiteral("usbInterfaceValue"));
  details->addWidget(advanced, 4, 0, 1, 4);
  advanced->hide();
  connect(advancedButton, &QToolButton::toggled, advanced,
          [advanced, advancedButton](bool expanded) {
            advanced->setVisible(expanded);
            advancedButton->setArrowType(expanded ? Qt::DownArrow
                                                  : Qt::RightArrow);
          });
  layout->addWidget(group);
  connect(m_table, &QTableWidget::itemSelectionChanged, this,
          &DevicePage::selectionChanged);
  connect(m_inspectButton, &QPushButton::clicked, this,
          &DevicePage::inspectRequested);
}

QString DevicePage::selectedPortPath() const {
  const int row = m_table->currentRow();
  const auto *value = row < 0 ? nullptr : m_table->item(row, StatusColumn);
  return value == nullptr ? QString{} : value->data(Qt::UserRole).toString();
}
void DevicePage::selectPort(const QString &port) {
  for (int row = 0; row < m_table->rowCount(); ++row) {
    const auto *value = m_table->item(row, StatusColumn);
    if (value != nullptr && value->data(Qt::UserRole).toString() == port) {
      m_table->selectRow(row);
      return;
    }
  }
}
void DevicePage::setDevices(std::vector<DeviceRow> devices) {
  const QSignalBlocker blocker{m_table};
  m_rows = devices;
  const QString selected = selectedPortPath();
  m_table->setSortingEnabled(false);
  m_table->clearContents();
  m_table->setRowCount(static_cast<int>(devices.size()));
  int row = 0;
  for (const auto &device : devices) {
    auto *status = item(device.status.translated());
    status->setData(Qt::UserRole, device.portPath);
    status->setToolTip(device.key);
    m_table->setItem(row, StatusColumn, status);
    m_table->setItem(row, ModeColumn, item(device.mode.translated()));
    m_table->setItem(row, DeviceIdColumn, item(device.deviceId));
    m_table->setItem(row, BusColumn, item(device.bus));
    m_table->setItem(row, PortColumn, item(device.portPath));
    m_table->setItem(row, UsbVersionColumn, item(device.usbVersion));
    ++row;
  }
  m_table->setSortingEnabled(true);
  selectPort(selected);
  if (m_table->currentRow() < 0 && m_table->rowCount() > 0)
    m_table->selectRow(0);
}
void DevicePage::setInspection(const GuiText &status, const GuiText &chipInfo,
                               const GuiText &capability,
                               const GuiText &flashId, const GuiText &capacity,
                               const GuiText &flashInfo,
                               const GuiText &usbInterface) {
  setGuiText(m_status, "text", status);
  setGuiText(m_chipInfo, "text", chipInfo.isEmpty() ? GuiText("-") : chipInfo);
  setGuiText(m_capability, "text",
             capability.isEmpty() ? GuiText("-") : capability);
  setGuiText(m_flashId, "text", flashId.isEmpty() ? GuiText("-") : flashId);
  setGuiText(m_capacity, "text", capacity.isEmpty() ? GuiText("-") : capacity);
  setGuiText(m_flashInfo, "text",
             flashInfo.isEmpty() ? GuiText("-") : flashInfo);
  setGuiText(m_usbInterface, "text",
             usbInterface.isEmpty() ? GuiText("-") : usbInterface);
}
void DevicePage::setBusy(const bool busy, const bool canInspect) {
  m_table->setEnabled(!busy);
  m_inspectButton->setEnabled(!busy && canInspect);
}

} // namespace zzrk::gui
