// SPDX-License-Identifier: MIT

#pragma once

#include <QString>
#include <QWidget>

#include <cstdint>
#include <optional>
#include <vector>

class QCheckBox;
class QLabel;
class QProgressBar;
class QPushButton;
class QTableWidget;
class QToolButton;

namespace zzrk::gui {

class PartitionPage final : public QWidget {
  Q_OBJECT

public:
  struct PartitionRow {
    QString name;
    std::uint64_t firstLba = 0;
    std::uint64_t lastLba = 0;
    std::uint64_t attributes = 0;
  };

  [[nodiscard]] static PartitionPage *buildUi(QWidget *parent);
  [[nodiscard]] std::optional<PartitionRow> selectedPartition() const;
  [[nodiscard]] bool verifyWrite() const;
  void setPartitions(std::vector<PartitionRow> partitions);
  void clearPartitions();
  void setProgress(int percent, bool indeterminate = false);
  void setStatus(const QString &text);
  void setBusy(bool busy, bool canRead, bool canOperate, bool canCancel);

signals:
  void readRequested();
  void selectionChanged();
  void backupRequested();
  void writeRequested();
  void eraseRequested();
  void cancelRequested();

private:
  explicit PartitionPage(QWidget *parent);

  QToolButton *m_readButton = nullptr;
  QTableWidget *m_table = nullptr;
  QCheckBox *m_verifyWriteCheckBox = nullptr;
  QPushButton *m_backupButton = nullptr;
  QPushButton *m_writeButton = nullptr;
  QPushButton *m_eraseButton = nullptr;
  QPushButton *m_cancelButton = nullptr;
  QProgressBar *m_progress = nullptr;
  QLabel *m_status = nullptr;
  std::vector<PartitionRow> m_partitions;
};

} // namespace zzrk::gui
