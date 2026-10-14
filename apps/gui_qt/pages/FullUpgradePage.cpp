// SPDX-License-Identifier: MIT

#include "FullUpgradePage.hpp"
#include "WorkspaceUi.hpp"
#include <ZzFluentUI/ZzInfoBar.h>

#include <QCheckBox>
#include <QComboBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>

namespace zzrk::gui {

FullUpgradePage *FullUpgradePage::buildUi(QWidget *parent) {
  return new FullUpgradePage(parent);
}

FullUpgradePage::FullUpgradePage(QWidget *parent) : QWidget(parent) {
  setObjectName(QStringLiteral("fullUpgradePage"));
  auto *pageLayout = new QVBoxLayout(this);
  ui::configurePage(this, pageLayout);
  auto *firmwareCard = ui::card(this);
  auto *firmwareLayout = new QVBoxLayout(firmwareCard);
  firmwareLayout->setContentsMargins(ui::CardPadding, ui::CardPadding,
                                     ui::CardPadding, ui::CardPadding);
  firmwareLayout->setSpacing(10);
  firmwareLayout->addWidget(
      ui::sectionTitle(QStringLiteral("固件与升级计划"), firmwareCard));
  auto *layout = new QGridLayout;
  layout->setHorizontalSpacing(8);
  layout->setVerticalSpacing(8);
  layout->setColumnStretch(1, 1);

  m_imagePath = new QLineEdit(this);
  m_imagePath->setObjectName(QStringLiteral("firmwareImagePath"));
  m_imagePath->setReadOnly(true);
  m_imagePath->setPlaceholderText(QStringLiteral("选择 update.img"));
  m_imagePath->setMinimumHeight(ui::ControlHeight);
  m_chooseImageButton = new QToolButton(this);
  m_chooseImageButton->setObjectName(QStringLiteral("chooseFirmwareButton"));
  ui::configureToolButton(m_chooseImageButton, ui::Icon::Folder);
  m_chooseImageButton->setToolTip(QStringLiteral("选择固件镜像"));
  m_chooseImageButton->setAccessibleName(QStringLiteral("选择固件镜像"));
  layout->addWidget(new QLabel(QStringLiteral("固件"), this), 0, 0);
  layout->addWidget(m_imagePath, 0, 1);
  layout->addWidget(m_chooseImageButton, 0, 2);

  auto *planLabel = ui::secondaryText(QStringLiteral("升级计划"), this);
  planLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
  m_planPreview = new QPlainTextEdit(this);
  m_planPreview->setObjectName(QStringLiteral("firmwarePlanPreview"));
  m_planPreview->setReadOnly(true);
  m_planPreview->setMaximumBlockCount(64);
  // 计划区随窗口伸展，底部任务区保留操作所需高度。
  m_planPreview->setMinimumHeight(88);
  m_planPreview->setFrameShape(QFrame::NoFrame);
  m_planPreview->setPlaceholderText(
      QStringLiteral("选择固件后显示型号、校验结果和升级计划。"));
  // 完整计划仍使用纯文本并可复制，避免重新解析固件或遗漏技术信息。
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
  m_startButton =
      ui::button(QStringLiteral("开始升级"), ui::Icon::Flash, this, true);
  m_startButton->setObjectName(QStringLiteral("startUpgradeButton"));
  m_cancelButton = ui::button(QStringLiteral("取消"), ui::Icon::Cancel, this);
  m_cancelButton->setObjectName(QStringLiteral("cancelUpgradeButton"));
  auto *options = new QHBoxLayout;
  options->addWidget(m_verifyModeCombo);
  options->addWidget(m_rebootCheckBox);
  options->addStretch();
  layout->addWidget(new QLabel(QStringLiteral("校验"), this), 2, 0);
  layout->addLayout(options, 2, 1, 1, 2);
  firmwareLayout->addLayout(layout);
  pageLayout->addWidget(firmwareCard, 1);
  firmwareLayout->setStretch(1, 1);
  layout->setRowStretch(1, 1);

  auto *taskCard = ui::card(this);
  auto *taskLayout = new QVBoxLayout(taskCard);
  taskLayout->setContentsMargins(ui::CardPadding, 12, ui::CardPadding, 12);
  taskLayout->setSpacing(6);
  taskLayout->setSizeConstraint(QLayout::SetMinimumSize);

  m_overallProgress = new QProgressBar(this);
  m_overallProgress->setObjectName(QStringLiteral("overallUpgradeProgress"));
  m_overallProgress->setRange(0, 100);
  m_overallProgress->setFormat(QStringLiteral("总体 %p%"));
  m_operationProgress = new QProgressBar(this);
  m_operationProgress->setObjectName(
      QStringLiteral("operationUpgradeProgress"));
  m_operationProgress->setRange(0, 100);
  m_operationProgress->setFormat(QStringLiteral("当前 %p%"));
  m_status = new QLabel(QStringLiteral("未选择固件"), this);
  m_status->setObjectName(QStringLiteral("upgradeStatus"));
  m_status->setWordWrap(true);
  m_status->setTextFormat(Qt::PlainText);
  taskLayout->addWidget(m_status);
  m_errorNotice = ui::addErrorNotice(taskLayout);
  m_errorNotice->setObjectName(QStringLiteral("upgradeErrorNotice"));
  ui::addProgressRow(taskLayout, QStringLiteral("完整计划"), m_overallProgress,
                     true);
  ui::addProgressRow(taskLayout, QStringLiteral("当前分区"),
                     m_operationProgress);
  auto *taskActions = new QHBoxLayout;
  taskActions->addStretch();
  taskActions->addWidget(m_startButton);
  taskActions->addWidget(m_cancelButton);
  taskLayout->addSpacing(6);
  taskLayout->addLayout(taskActions);

  pageLayout->addWidget(taskCard);

  connect(m_chooseImageButton, &QToolButton::clicked, this,
          &FullUpgradePage::chooseFirmwareRequested);
  connect(m_startButton, &QPushButton::clicked, this,
          &FullUpgradePage::startRequested);
  connect(m_cancelButton, &QPushButton::clicked, this,
          &FullUpgradePage::cancelRequested);
}

zzrk::image::FirmwareVerifyMode FullUpgradePage::verifyMode() const {
  using zzrk::image::FirmwareVerifyMode;
  if (m_verifyModeCombo->currentIndex() == 1)
    return FirmwareVerifyMode::all;
  if (m_verifyModeCombo->currentIndex() == 2)
    return FirmwareVerifyMode::none;
  return FirmwareVerifyMode::critical;
}

bool FullUpgradePage::rebootAfterFlash() const {
  return m_rebootCheckBox->isChecked();
}

void FullUpgradePage::setFirmwarePath(const QString &path) {
  m_imagePath->setText(path);
}
void FullUpgradePage::setPlanPreview(const QString &text) {
  m_planPreview->setPlainText(text);
}
void FullUpgradePage::setOverallProgress(int percent, bool indeterminate) {
  m_overallProgress->setRange(0, indeterminate ? 0 : 100);
  if (!indeterminate)
    m_overallProgress->setValue(percent);
  ui::refreshProgressText(m_overallProgress);
}
void FullUpgradePage::setOperationProgress(int percent, bool indeterminate) {
  m_operationProgress->setRange(0, indeterminate ? 0 : 100);
  if (!indeterminate)
    m_operationProgress->setValue(percent);
  ui::refreshProgressText(m_operationProgress);
}
void FullUpgradePage::setStatus(const QString &text) {
  m_status->setText(text);
  m_status->show();
  m_errorNotice->setOpen(false);
}
void FullUpgradePage::setError(const QString &text) {
  m_status->setText(text);
  m_status->hide();
  m_errorNotice->setMessage(text);
  m_errorNotice->setOpen(true);
  m_errorNotice->setMinimumHeight(m_errorNotice->heightForWidth(m_errorNotice->width()));
}

void FullUpgradePage::setBusy(bool busy, bool canStart, bool canCancel) {
  m_chooseImageButton->setEnabled(!busy);
  m_startButton->setEnabled(!busy && canStart);
  m_cancelButton->setEnabled(canCancel);
  m_verifyModeCombo->setEnabled(!busy);
  m_rebootCheckBox->setEnabled(!busy);
}

} // namespace zzrk::gui
