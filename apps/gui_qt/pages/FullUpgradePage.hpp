// SPDX-License-Identifier: MIT

#pragma once

#include <zzrk/image/RkFirmwareFlasher.hpp>

#include <QString>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QToolButton;
namespace ZzFluentUI {
class ZzInfoBar;
}

namespace zzrk::gui {

class FullUpgradePage final : public QWidget {
  Q_OBJECT

public:
  [[nodiscard]] static FullUpgradePage *buildUi(QWidget *parent);

  [[nodiscard]] zzrk::image::FirmwareVerifyMode verifyMode() const;
  [[nodiscard]] bool rebootAfterFlash() const;

  void setFirmwarePath(const QString &path);
  void setPlanPreview(const QString &text);
  void setOverallProgress(int percent, bool indeterminate = false);
  void setOperationProgress(int percent, bool indeterminate = false);
  void setStatus(const QString &text);
  void setError(const QString &text);
  void setBusy(bool busy, bool canStart, bool canCancel);

signals:
  void chooseFirmwareRequested();
  void startRequested();
  void cancelRequested();

private:
  explicit FullUpgradePage(QWidget *parent);

  QLineEdit *m_imagePath = nullptr;
  QToolButton *m_chooseImageButton = nullptr;
  QPlainTextEdit *m_planPreview = nullptr;
  QComboBox *m_verifyModeCombo = nullptr;
  QCheckBox *m_rebootCheckBox = nullptr;
  QPushButton *m_startButton = nullptr;
  QPushButton *m_cancelButton = nullptr;
  QProgressBar *m_overallProgress = nullptr;
  QProgressBar *m_operationProgress = nullptr;
  QLabel *m_status = nullptr;
  ZzFluentUI::ZzInfoBar *m_errorNotice = nullptr;
};

} // namespace zzrk::gui
