// SPDX-License-Identifier: MIT

#include "FullUpgradePage.hpp"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QGridLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QStyle>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>

namespace zzrk::gui {
namespace {
constexpr int ReportIdRole = Qt::UserRole + 1;

QTableWidgetItem *item(const QString &text) {
  auto *result = new QTableWidgetItem(text);
  result->setFlags(result->flags() & ~Qt::ItemIsEditable);
  return result;
}
} // namespace

FullUpgradePage *FullUpgradePage::buildUi(QWidget *parent) {
  return new FullUpgradePage(parent);
}

FullUpgradePage::FullUpgradePage(QWidget *parent) : QWidget(parent) {
  setObjectName(QStringLiteral("fullUpgradePage"));
  auto *pageLayout = new QVBoxLayout(this);
  pageLayout->setContentsMargins(10, 10, 10, 10);
  auto *layout = new QGridLayout;
  layout->setHorizontalSpacing(8);
  layout->setVerticalSpacing(8);
  layout->setColumnStretch(1, 1);

  m_imagePath = new QLineEdit(this);
  m_imagePath->setObjectName(QStringLiteral("firmwareImagePath"));
  m_imagePath->setReadOnly(true);
  m_imagePath->setPlaceholderText(QStringLiteral("选择 update.img"));
  m_chooseImageButton = new QToolButton(this);
  m_chooseImageButton->setObjectName(QStringLiteral("chooseFirmwareButton"));
  m_chooseImageButton->setIcon(style()->standardIcon(QStyle::SP_DirOpenIcon));
  m_chooseImageButton->setToolTip(QStringLiteral("选择固件镜像"));
  m_chooseImageButton->setAccessibleName(QStringLiteral("选择固件镜像"));
  m_chooseImageButton->setFixedSize(30, 30);
  layout->addWidget(new QLabel(QStringLiteral("固件"), this), 0, 0);
  layout->addWidget(m_imagePath, 0, 1);
  layout->addWidget(m_chooseImageButton, 0, 2);

  auto *planLabel = new QLabel(QStringLiteral("升级计划"), this);
  planLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
  m_planPreview = new QPlainTextEdit(this);
  m_planPreview->setObjectName(QStringLiteral("firmwarePlanPreview"));
  m_planPreview->setReadOnly(true);
  m_planPreview->setMaximumBlockCount(64);
  m_planPreview->setFixedHeight(88);
  layout->addWidget(planLabel, 1, 0);
  layout->addWidget(m_planPreview, 1, 1, 1, 2);

  m_verifyModeCombo = new QComboBox(this);
  m_verifyModeCombo->setObjectName(QStringLiteral("verifyModeCombo"));
  m_verifyModeCombo->addItems({QStringLiteral("关键分区"),
                               QStringLiteral("全部分区"),
                               QStringLiteral("不回读")});
  m_rebootCheckBox = new QCheckBox(QStringLiteral("完成后重启"), this);
  m_rebootCheckBox->setObjectName(QStringLiteral("rebootAfterFlashCheckBox"));
  m_rebootCheckBox->setChecked(true);
  m_startButton = new QPushButton(QStringLiteral("开始升级"), this);
  m_startButton->setObjectName(QStringLiteral("startUpgradeButton"));
  m_startButton->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
  m_cancelButton = new QPushButton(QStringLiteral("取消"), this);
  m_cancelButton->setObjectName(QStringLiteral("cancelUpgradeButton"));
  m_cancelButton->setIcon(style()->standardIcon(QStyle::SP_DialogCancelButton));
  auto *options = new QHBoxLayout;
  options->addWidget(m_verifyModeCombo);
  options->addWidget(m_rebootCheckBox);
  options->addStretch();
  options->addWidget(m_startButton);
  options->addWidget(m_cancelButton);
  layout->addWidget(new QLabel(QStringLiteral("校验"), this), 2, 0);
  layout->addLayout(options, 2, 1, 1, 2);

  m_overallProgress = new QProgressBar(this);
  m_overallProgress->setObjectName(QStringLiteral("overallUpgradeProgress"));
  m_overallProgress->setRange(0, 100);
  m_overallProgress->setFormat(QStringLiteral("总体 %p%"));
  m_operationProgress = new QProgressBar(this);
  m_operationProgress->setObjectName(QStringLiteral("operationUpgradeProgress"));
  m_operationProgress->setRange(0, 100);
  m_operationProgress->setFormat(QStringLiteral("当前 %p%"));
  m_status = new QLabel(QStringLiteral("未选择固件"), this);
  m_status->setObjectName(QStringLiteral("upgradeStatus"));
  m_status->setWordWrap(true);
  layout->addWidget(new QLabel(QStringLiteral("总体"), this), 3, 0);
  layout->addWidget(m_overallProgress, 3, 1, 1, 2);
  layout->addWidget(new QLabel(QStringLiteral("当前"), this), 4, 0);
  layout->addWidget(m_operationProgress, 4, 1, 1, 2);
  layout->addWidget(m_status, 5, 0, 1, 3);
  pageLayout->addLayout(layout);

  auto *historyHeader = new QHBoxLayout;
  historyHeader->addWidget(new QLabel(QStringLiteral("任务历史（本次会话）"), this));
  historyHeader->addStretch();
  m_exportReportButton = new QToolButton(this);
  m_exportReportButton->setObjectName(QStringLiteral("exportReportButton"));
  m_exportReportButton->setIcon(style()->standardIcon(QStyle::SP_DialogSaveButton));
  m_exportReportButton->setToolTip(QStringLiteral("导出所选任务报告"));
  m_exportReportButton->setAccessibleName(QStringLiteral("导出任务报告"));
  m_exportReportButton->setFixedSize(30, 30);
  historyHeader->addWidget(m_exportReportButton);
  pageLayout->addLayout(historyHeader);

  m_historyTable = new QTableWidget(0, 4, this);
  m_historyTable->setObjectName(QStringLiteral("upgradeHistoryTable"));
  m_historyTable->setHorizontalHeaderLabels(
      {QStringLiteral("开始时间"), QStringLiteral("端口"),
       QStringLiteral("固件"), QStringLiteral("结果")});
  m_historyTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_historyTable->setSelectionBehavior(QAbstractItemView::SelectRows);
  m_historyTable->setSelectionMode(QAbstractItemView::SingleSelection);
  m_historyTable->setShowGrid(false);
  m_historyTable->setAlternatingRowColors(true);
  m_historyTable->verticalHeader()->setVisible(false);
  m_historyTable->verticalHeader()->setDefaultSectionSize(28);
  m_historyTable->setFixedHeight(126);
  auto *header = m_historyTable->horizontalHeader();
  header->setSectionResizeMode(0, QHeaderView::ResizeToContents);
  header->setSectionResizeMode(1, QHeaderView::ResizeToContents);
  header->setSectionResizeMode(2, QHeaderView::Stretch);
  header->setSectionResizeMode(3, QHeaderView::ResizeToContents);
  pageLayout->addWidget(m_historyTable);
  pageLayout->addStretch();

  connect(m_chooseImageButton, &QToolButton::clicked, this,
          &FullUpgradePage::chooseFirmwareRequested);
  connect(m_startButton, &QPushButton::clicked, this,
          &FullUpgradePage::startRequested);
  connect(m_cancelButton, &QPushButton::clicked, this,
          &FullUpgradePage::cancelRequested);
  connect(m_exportReportButton, &QToolButton::clicked, this,
          &FullUpgradePage::exportReportRequested);
  connect(m_historyTable, &QTableWidget::itemSelectionChanged, this,
          &FullUpgradePage::historySelectionChanged);
}

zzrk::image::FirmwareVerifyMode FullUpgradePage::verifyMode() const {
  using zzrk::image::FirmwareVerifyMode;
  if (m_verifyModeCombo->currentIndex() == 1)
    return FirmwareVerifyMode::all;
  if (m_verifyModeCombo->currentIndex() == 2)
    return FirmwareVerifyMode::none;
  return FirmwareVerifyMode::critical;
}

bool FullUpgradePage::rebootAfterFlash() const { return m_rebootCheckBox->isChecked(); }

std::optional<std::uint64_t> FullUpgradePage::selectedReportId() const {
  const int row = m_historyTable->currentRow();
  const auto *result = row < 0 ? nullptr : m_historyTable->item(row, 3);
  bool valid = false;
  const auto id = result == nullptr ? 0U : result->data(ReportIdRole).toULongLong(&valid);
  return valid ? std::optional<std::uint64_t>{id} : std::nullopt;
}

QString FullUpgradePage::selectedReportImageName() const {
  const int row = m_historyTable->currentRow();
  const auto *image = row < 0 ? nullptr : m_historyTable->item(row, 2);
  return image == nullptr ? QString{} : image->text();
}

void FullUpgradePage::setFirmwarePath(const QString &path) { m_imagePath->setText(path); }
void FullUpgradePage::setPlanPreview(const QString &text) { m_planPreview->setPlainText(text); }
void FullUpgradePage::setOverallProgress(int percent, bool indeterminate) {
  m_overallProgress->setRange(0, indeterminate ? 0 : 100);
  if (!indeterminate) m_overallProgress->setValue(percent);
}
void FullUpgradePage::setOperationProgress(int percent, bool indeterminate) {
  m_operationProgress->setRange(0, indeterminate ? 0 : 100);
  if (!indeterminate) m_operationProgress->setValue(percent);
}
void FullUpgradePage::setStatus(const QString &text) { m_status->setText(text); }

void FullUpgradePage::setHistoryEntry(int row, const HistoryEntry &entry) {
  if (row < 0 || row >= m_historyTable->rowCount()) return;
  m_historyTable->setItem(row, 0, item(entry.startedAt));
  m_historyTable->setItem(row, 1, item(entry.portPath));
  m_historyTable->setItem(row, 2, item(entry.imageName));
  auto *result = item(entry.result);
  result->setToolTip(entry.detail);
  if (entry.reportId.has_value())
    result->setData(ReportIdRole, QVariant::fromValue<qulonglong>(*entry.reportId));
  m_historyTable->setItem(row, 3, result);
}
void FullUpgradePage::updateHistoryResult(
    int row, const QString &result, const QString &detail,
    std::optional<std::uint64_t> reportId) {
  if (row < 0 || row >= m_historyTable->rowCount()) return;
  auto *resultItem = m_historyTable->item(row, 3);
  if (resultItem == nullptr) {
    resultItem = item({});
    m_historyTable->setItem(row, 3, resultItem);
  }
  resultItem->setText(result);
  resultItem->setToolTip(detail);
  if (reportId.has_value())
    resultItem->setData(ReportIdRole,
                        QVariant::fromValue<qulonglong>(*reportId));
}
void FullUpgradePage::prependHistoryEntry(const HistoryEntry &entry) {
  m_historyTable->insertRow(0);
  setHistoryEntry(0, entry);
}
std::optional<std::uint64_t> FullUpgradePage::removeLastHistoryEntry() {
  if (m_historyTable->rowCount() == 0) return std::nullopt;
  const int row = m_historyTable->rowCount() - 1;
  const auto *resultItem = m_historyTable->item(row, 3);
  bool valid = false;
  const auto reportId = resultItem == nullptr
                            ? 0U
                            : resultItem->data(ReportIdRole).toULongLong(&valid);
  m_historyTable->removeRow(row);
  return valid ? std::optional<std::uint64_t>{reportId} : std::nullopt;
}
int FullUpgradePage::historyCount() const { return m_historyTable->rowCount(); }
void FullUpgradePage::selectHistoryRow(int row) { m_historyTable->selectRow(row); }

void FullUpgradePage::setBusy(bool busy, bool canStart, bool canCancel,
                              bool canExportReport) {
  m_chooseImageButton->setEnabled(!busy);
  m_startButton->setEnabled(!busy && canStart);
  m_cancelButton->setEnabled(canCancel);
  m_verifyModeCombo->setEnabled(!busy);
  m_rebootCheckBox->setEnabled(!busy);
  m_exportReportButton->setEnabled(!busy && canExportReport);
}

} // namespace zzrk::gui
