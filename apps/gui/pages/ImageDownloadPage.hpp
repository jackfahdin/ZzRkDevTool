// SPDX-License-Identifier: MIT

#pragma once

#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QTableWidget;
class QToolButton;

namespace zzrk::gui {

class ImageDownloadPage final : public QWidget {
  Q_OBJECT

public:
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

  static constexpr int TargetRole = Qt::UserRole + 8;
  static constexpr int KindRole = Qt::UserRole + 9;
  static constexpr int CapacityRole = Qt::UserRole + 10;

  [[nodiscard]] static ImageDownloadPage *buildUi(QWidget *parent);

  [[nodiscard]] QComboBox *deviceCombo() const noexcept;
  [[nodiscard]] QComboBox *layoutCombo() const noexcept;
  [[nodiscard]] QLineEdit *layoutPathEdit() const noexcept;
  [[nodiscard]] QLineEdit *directoryEdit() const noexcept;
  [[nodiscard]] QTableWidget *table() const noexcept;
  [[nodiscard]] QCheckBox *rebootCheckBox() const noexcept;
  [[nodiscard]] QString selectedPortPath() const;
  [[nodiscard]] bool selectedDeviceManageable() const;
  [[nodiscard]] int selectedDeviceMode() const;

  void setPlan(int rowCount);
  void setProgress(int overallPercent, int operationPercent,
                   bool overallIndeterminate = false,
                   bool operationIndeterminate = false);
  void setStatus(const QString &text);
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

  QComboBox *m_deviceCombo = nullptr;
  QComboBox *m_layoutCombo = nullptr;
  QLineEdit *m_layoutPathEdit = nullptr;
  QToolButton *m_chooseParameterButton = nullptr;
  QToolButton *m_readGptButton = nullptr;
  QLineEdit *m_directoryEdit = nullptr;
  QToolButton *m_chooseDirectoryButton = nullptr;
  QTableWidget *m_table = nullptr;
  QPushButton *m_chooseImageButton = nullptr;
  QPushButton *m_clearImageButton = nullptr;
  QCheckBox *m_rebootCheckBox = nullptr;
  QPushButton *m_startButton = nullptr;
  QPushButton *m_cancelButton = nullptr;
  QProgressBar *m_overallProgress = nullptr;
  QProgressBar *m_operationProgress = nullptr;
  QLabel *m_status = nullptr;
};

} // namespace zzrk::gui
