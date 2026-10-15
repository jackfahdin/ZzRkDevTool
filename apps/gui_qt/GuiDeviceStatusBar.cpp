// SPDX-License-Identifier: MIT
#include "GuiDeviceStatusBar.hpp"
#include "GuiTranslations.hpp"
#include "WorkspaceUi.hpp"
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QToolButton>
#include <algorithm>

namespace zzrk::gui {
namespace {
// 保留完整文本供读屏和悬浮提示使用，只在绘制时省略，避免长消息撑宽窗口。
class StatusLabel final : public QLabel {
public:
  explicit StatusLabel(QWidget *parent) : QLabel(parent) {
    setTextFormat(Qt::PlainText);
    setMinimumWidth(0);
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
  }

protected:
  void paintEvent(QPaintEvent *) override {
    QPainter painter(this);
    painter.setPen(palette().color(foregroundRole()));
    painter.drawText(contentsRect(), Qt::AlignLeft | Qt::AlignVCenter,
                     fontMetrics().elidedText(text(), Qt::ElideRight,
                                              contentsRect().width()));
  }
};
QString modeText(transport::DeviceMode mode) {
  return mode == transport::DeviceMode::Unknown
             ? guiText("未知")
             : QString::fromLatin1(transport::toString(mode));
}
QString metadata(const transport::UsbDeviceInfo &device) {
  return QStringLiteral("%1 · %2\nVID:PID %3:%4 · %5 %6\nUSB %7 · bcdDevice %8")
      .arg(QString::fromStdString(device.portPath), modeText(device.mode))
      .arg(device.vendorId, 4, 16, QLatin1Char('0'))
      .arg(device.productId, 4, 16, QLatin1Char('0'))
      .arg(guiText("总线"))
      .arg(device.busNumber)
      .arg(QStringLiteral("%1.%2%3")
               .arg((device.bcdUsb >> 8) & 0xff)
               .arg((device.bcdUsb >> 4) & 0xf)
               .arg(device.bcdUsb & 0xf))
      .arg(device.bcdDevice, 4, 16, QLatin1Char('0'));
}
} // namespace

GuiDeviceStatusBar::GuiDeviceStatusBar(QStatusBar *host) : QWidget(host) {
  setObjectName(QStringLiteral("globalDeviceStatusBar"));
  auto *layout = new QHBoxLayout(this);
  layout->setContentsMargins(2, 2, 2, 2);
  layout->setSpacing(8);
  auto *label = guiLabel(guiText("设备"), this);
  m_devices = new QComboBox(this);
  m_devices->setObjectName(QStringLiteral("statusDeviceCombo"));
  m_devices->setMinimumContentsLength(10);
  m_devices->setSizeAdjustPolicy(
      QComboBox::AdjustToMinimumContentsLengthWithIcon);
  setGuiText(m_devices, "accessibleName", guiText("目标设备"));
  label->setBuddy(m_devices);
  layout->addWidget(label);
  layout->addWidget(m_devices);
  m_state = new StatusLabel(this);
  m_state->setObjectName(QStringLiteral("connectionStatusText"));
  m_state->setProperty("hotplugAvailable", true);
  layout->addWidget(m_state, 2);
  auto *message = new StatusLabel(this);
  message->setObjectName(QStringLiteral("statusOperationText"));
  setGuiText(message, "accessibleName", guiText("操作状态"));
  layout->addWidget(message, 3);
  m_refresh = new QToolButton(this);
  m_refresh->setObjectName(QStringLiteral("refreshButton"));
  ui::configureToolButton(m_refresh, ui::Icon::Refresh);
  setGuiText(m_refresh, "toolTip", guiText("立即刷新设备列表"));
  setGuiText(m_refresh, "accessibleName", guiText("刷新设备列表"));
  layout->addWidget(m_refresh);
  auto *time = guiLabel(guiText("尚未扫描"), this);
  time->setObjectName(QStringLiteral("lastScanText"));
  layout->addWidget(time);
  connect(m_devices, &QComboBox::currentIndexChanged, this,
          [this] { emit portSelected(m_devices->currentData().toString()); });
  connect(m_refresh, &QToolButton::clicked, this,
          &GuiDeviceStatusBar::refreshRequested);
  // 永久区域不会被 QStatusBar::showMessage 隐藏；现有消息绑定和翻译继续沿用。
  connect(host, &QStatusBar::messageChanged, message,
          [message](const QString &text) {
            message->setText(text);
            message->setToolTip(text);
          });
  message->setText(host->currentMessage());
  host->addPermanentWidget(this, 1);
  bindGuiTranslation(this, [this] { refreshText(); });
}

void GuiDeviceStatusBar::setDevices(const transport::ScanResult &result,
                                    const QString &selected,
                                    const QString &disconnected) {
  m_result = result;
  std::ranges::sort(m_result.devices, {}, &transport::UsbDeviceInfo::portPath);
  m_selected = selected;
  m_disconnected = disconnected;
  refreshText();
}
void GuiDeviceStatusBar::refreshText() {
  const QSignalBlocker blocker(m_devices);
  m_devices->clear();
  const transport::UsbDeviceInfo *selected = nullptr;
  for (const auto &device : m_result.devices) {
    const auto port = QString::fromStdString(device.portPath);
    m_devices->addItem(
        QStringLiteral("%1 (%2)").arg(port, modeText(device.mode)), port);
    m_devices->setItemData(m_devices->count() - 1, metadata(device),
                           Qt::ToolTipRole);
    if (port == m_selected)
      selected = &device;
  }
  m_devices->setCurrentIndex(m_devices->findData(m_selected));
  m_devices->setPlaceholderText(guiText("请选择设备"));
  QString state;
  if (selected) {
    state = guiText("已连接") + QStringLiteral(" · USB %1.%2%3")
                                    .arg((selected->bcdUsb >> 8) & 0xff)
                                    .arg((selected->bcdUsb >> 4) & 0xf)
                                    .arg(selected->bcdUsb & 0xf);
    m_devices->setToolTip(metadata(*selected));
  } else {
    state = !m_disconnected.isEmpty()
                ? guiText("设备 %1 已断开，请重新选择").arg(m_disconnected)
            : m_result.devices.empty() ? guiText("未检测到 Rockchip 设备")
                                       : guiText("已连接 %1 台设备，请选择目标")
                                             .arg(m_result.devices.size());
    m_devices->setToolTip(state);
  }
  if (m_result.error)
    state = m_result.devices.empty() ? guiText("设备扫描失败")
                                     : guiText("扫描结果不完整");
  if (!m_monitoringError.isEmpty())
    state += QStringLiteral(" · ") + guiText("热插拔不可用，请手动刷新");
  m_state->setText(state);
  m_state->setToolTip(
      state +
      (m_result.error ? QStringLiteral("\n") +
                            QString::fromStdString(m_result.error->message)
                      : QString{}) +
      (m_monitoringError.isEmpty() ? QString{}
                                   : QStringLiteral("\n") + m_monitoringError));
  m_devices->setEnabled(!m_busy && !m_result.devices.empty());
}
void GuiDeviceStatusBar::setBusy(bool busy) {
  m_busy = busy;
  m_devices->setEnabled(!busy && !m_result.devices.empty());
  m_refresh->setEnabled(!busy);
}
void GuiDeviceStatusBar::setMonitoringError(QString error) {
  m_monitoringError = std::move(error);
  m_state->setProperty("hotplugAvailable", m_monitoringError.isEmpty());
  refreshText();
}
} // namespace zzrk::gui
