// SPDX-License-Identifier: MIT

#pragma once

#include <zzrk/image/RkFirmwareFlasher.hpp>

#include <QString>
#include <QWidget>

#include <cstdint>
#include <optional>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QTableWidget;
class QToolButton;
namespace ZzFluentUI {
class ZzInfoBar;
}

namespace zzrk::gui {

class FullUpgradePage final : public QWidget {
  Q_OBJECT

public:
  struct HistoryEntry {
    QString startedAt;
    QString portPath;
    QString imageName;
    QString result;
    QString detail;
    std::optional<std::uint64_t> reportId;
  };

  [[nodiscard]] static FullUpgradePage *buildUi(QWidget *parent);

  [[nodiscard]] zzrk::image::FirmwareVerifyMode verifyMode() const;
  [[nodiscard]] bool rebootAfterFlash() const;
  [[nodiscard]] std::optional<std::uint64_t> selectedReportId() const;
  [[nodiscard]] QString selectedReportImageName() const;

  void setFirmwarePath(const QString &path);
  void setPlanPreview(const QString &text);
  void setOverallProgress(int percent, bool indeterminate = false);
  void setOperationProgress(int percent, bool indeterminate = false);
  void setStatus(const QString &text);
  void setError(const QString &text);
  void setHistoryEntry(int row, const HistoryEntry &entry);
  void updateHistoryResult(int row, const QString &result,
                           const QString &detail = {},
                           std::optional<std::uint64_t> reportId = {});
  void prependHistoryEntry(const HistoryEntry &entry);
  [[nodiscard]] std::optional<std::uint64_t> removeLastHistoryEntry();
  [[nodiscard]] int historyCount() const;
  void selectHistoryRow(int row);
  void setBusy(bool busy, bool canStart, bool canCancel, bool canExportReport);

signals:
  void chooseFirmwareRequested();
  void startRequested();
  void cancelRequested();
  void exportReportRequested();
  void historySelectionChanged();

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
  QTableWidget *m_historyTable = nullptr;
  QToolButton *m_exportReportButton = nullptr;
};

} // namespace zzrk::gui
