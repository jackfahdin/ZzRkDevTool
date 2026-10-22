// SPDX-License-Identifier: MIT

#pragma once

#include "GuiTranslations.hpp"
#include <QString>
#include <QWidget>

class QLabel;
class QPushButton;

namespace zzrk::gui {

class DevicePage final : public QWidget {
  Q_OBJECT

public:
  [[nodiscard]] static DevicePage *buildUi(QWidget *parent);
  void setInspection(const GuiText &status, const GuiText &chipInfo = {},
                     const GuiText &capability = {},
                     const GuiText &flashId = {}, const GuiText &capacity = {},
                     const GuiText &flashInfo = {},
                     const GuiText &usbInterface = {});
  void setBusy(bool busy, bool canInspect);

signals:
  void inspectRequested();
  void driverManagementRequested();

private:
  explicit DevicePage(QWidget *parent);

  QPushButton *m_inspectButton = nullptr;
  QPushButton *m_driverManagementButton = nullptr;
  QLabel *m_status = nullptr;
  QLabel *m_chipInfo = nullptr;
  QLabel *m_capability = nullptr;
  QLabel *m_flashId = nullptr;
  QLabel *m_capacity = nullptr;
  QLabel *m_flashInfo = nullptr;
  QLabel *m_usbInterface = nullptr;
};

} // namespace zzrk::gui
