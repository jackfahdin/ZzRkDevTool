// SPDX-License-Identifier: MIT

#include "DevicePage.hpp"
#include "GuiTranslations.hpp"
#include "WorkspaceUi.hpp"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>

namespace zzrk::gui {
DevicePage *DevicePage::buildUi(QWidget *parent) {
  return new DevicePage(parent);
}

DevicePage::DevicePage(QWidget *parent) : QWidget(parent) {
  setObjectName(QStringLiteral("devicePage"));
  auto *layout = new QVBoxLayout(this);
  ui::configurePage(this, layout);
  // 目标设备统一在底部状态栏选择，本页只展示所选设备的详情。
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
  layout->addStretch();
  connect(m_inspectButton, &QPushButton::clicked, this,
          &DevicePage::inspectRequested);
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
  m_inspectButton->setEnabled(!busy && canInspect);
}

} // namespace zzrk::gui
