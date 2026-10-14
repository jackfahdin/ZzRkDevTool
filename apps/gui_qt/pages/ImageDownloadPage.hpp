// SPDX-License-Identifier: MIT

#pragma once

#include "GuiTranslations.hpp"
#include <QString>
#include <QWidget>

#include <optional>
#include <vector>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QTableWidget;
class QTableWidgetItem;
class QToolButton;
class QStackedLayout;
namespace ZzFluentUI {
class ZzInfoBar;
}

namespace zzrk::gui {

class ImageDownloadPage final : public QWidget {
  Q_OBJECT

public:
  enum class RowKind { loader, partitionTable, partition };

  struct PlanRow {
    RowKind kind = RowKind::partition;
    QString target;
    GuiText lba;
    QString capacity;
    QString file;
    GuiText format;
    QString expandedSize;
    GuiText status;
    bool selected = false;
    bool selectionForced = false;
    bool verify = true;
  };

  struct PlanChoice {
    RowKind kind = RowKind::partition;
    QString target;
    bool selected = false;
    bool verify = true;
  };

  struct PlanTarget {
    RowKind kind = RowKind::partition;
    QString target;
  };

  enum Column {
    SelectedColumn,
    TargetColumn,
    LbaColumn,
    CapacityColumn,
    FileColumn,
    FormatColumn,
    ExpandedSizeColumn,
    VerifyColumn,
    StatusColumn,
    ColumnCount
  };

  [[nodiscard]] static ImageDownloadPage *buildUi(QWidget *parent);

  [[nodiscard]] QComboBox *deviceCombo() const noexcept;
  [[nodiscard]] QComboBox *layoutCombo() const noexcept;
  [[nodiscard]] QLineEdit *layoutPathEdit() const noexcept;
  [[nodiscard]] QLineEdit *directoryEdit() const noexcept;
  [[nodiscard]] QCheckBox *rebootCheckBox() const noexcept;
  [[nodiscard]] QString selectedPortPath() const;
  [[nodiscard]] bool selectedDeviceManageable() const;
  [[nodiscard]] int selectedDeviceMode() const;

  [[nodiscard]] std::vector<PlanChoice> planChoices() const;
  [[nodiscard]] std::optional<PlanTarget> currentTarget() const;

  void setPlan(std::vector<PlanRow> rows);
  void setTargetStatus(const QString &target, const GuiText &status);
  void setSelectedTargetsStatus(const GuiText &status);
  void setProgress(int overallPercent, int operationPercent,
                   bool overallIndeterminate = false,
                   bool operationIndeterminate = false);
  void setStatus(const GuiText &text);
  void setError(const GuiText &text);
  void setBusy(bool busy, bool canStart, bool canCancel);

signals:
  void deviceChanged();
  void layoutChanged();
  void chooseParameterRequested();
  void readDeviceGptRequested();
  void chooseDirectoryRequested();
  void chooseImageRequested();
  void clearImageRequested();
  void planChanged();
  void startRequested();
  void cancelRequested();

private:
  explicit ImageDownloadPage(QWidget *parent);
  void handlePlanItemChanged(QTableWidgetItem *item);

  QComboBox *m_deviceCombo = nullptr;
  QComboBox *m_layoutCombo = nullptr;
  QLineEdit *m_layoutPathEdit = nullptr;
  QToolButton *m_chooseParameterButton = nullptr;
  QToolButton *m_readGptButton = nullptr;
  QLineEdit *m_directoryEdit = nullptr;
  QToolButton *m_chooseDirectoryButton = nullptr;
  QTableWidget *m_table = nullptr;
  // 0 为清单，1 为空态；仅控制展示，不参与镜像选择和规划。
  QStackedLayout *m_planStack = nullptr;
  QPushButton *m_chooseImageButton = nullptr;
  QPushButton *m_clearImageButton = nullptr;
  QCheckBox *m_rebootCheckBox = nullptr;
  QPushButton *m_startButton = nullptr;
  QPushButton *m_cancelButton = nullptr;
  QProgressBar *m_overallProgress = nullptr;
  QProgressBar *m_operationProgress = nullptr;
  QLabel *m_status = nullptr;
  ZzFluentUI::ZzInfoBar *m_errorNotice = nullptr;
  std::vector<PlanRow> m_planRows;
};

} // namespace zzrk::gui
