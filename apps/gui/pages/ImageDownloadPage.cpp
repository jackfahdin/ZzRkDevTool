// SPDX-License-Identifier: MIT

#include "ImageDownloadPage.hpp"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QStyle>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>

namespace zzrk::gui {

ImageDownloadPage *ImageDownloadPage::buildUi(QWidget *parent) {
  return new ImageDownloadPage(parent);
}

ImageDownloadPage::ImageDownloadPage(QWidget *parent) : QWidget(parent) {
  setObjectName(QStringLiteral("downloadImagePage"));
  auto *pageLayout = new QVBoxLayout(this);
  pageLayout->setContentsMargins(10, 10, 10, 10);

  auto *sourceLayout = new QGridLayout;
  sourceLayout->setHorizontalSpacing(8);
  sourceLayout->setVerticalSpacing(8);
  sourceLayout->setColumnStretch(1, 1);
  sourceLayout->setColumnStretch(4, 1);

  m_deviceCombo = new QComboBox(this);
  m_deviceCombo->setObjectName(QStringLiteral("downloadDeviceCombo"));
  m_deviceCombo->setMinimumContentsLength(18);
  sourceLayout->addWidget(new QLabel(QStringLiteral("目标设备"), this), 0, 0);
  sourceLayout->addWidget(m_deviceCombo, 0, 1, 1, 2);

  m_layoutCombo = new QComboBox(this);
  m_layoutCombo->setObjectName(QStringLiteral("downloadLayoutCombo"));
  m_layoutCombo->addItem(QStringLiteral("设备 GPT"));
  m_layoutCombo->addItem(QStringLiteral("parameter.txt"));
  sourceLayout->addWidget(new QLabel(QStringLiteral("分区布局"), this), 0, 3);
  sourceLayout->addWidget(m_layoutCombo, 0, 4);

  m_readGptButton = new QToolButton(this);
  m_readGptButton->setObjectName(QStringLiteral("readDownloadGptButton"));
  m_readGptButton->setIcon(style()->standardIcon(QStyle::SP_BrowserReload));
  m_readGptButton->setToolTip(QStringLiteral("读取设备 GPT"));
  m_readGptButton->setAccessibleName(QStringLiteral("读取设备 GPT"));
  m_readGptButton->setFixedSize(30, 30);
  sourceLayout->addWidget(m_readGptButton, 0, 5);

  m_layoutPathEdit = new QLineEdit(this);
  m_layoutPathEdit->setObjectName(QStringLiteral("downloadParameterPath"));
  m_layoutPathEdit->setReadOnly(true);
  m_layoutPathEdit->setPlaceholderText(
      QStringLiteral("设备 GPT 将在读取后显示，或选择 parameter.txt"));
  m_chooseParameterButton = new QToolButton(this);
  m_chooseParameterButton->setObjectName(
      QStringLiteral("chooseDownloadParameterButton"));
  m_chooseParameterButton->setIcon(
      style()->standardIcon(QStyle::SP_DirOpenIcon));
  m_chooseParameterButton->setToolTip(QStringLiteral("选择 parameter.txt"));
  m_chooseParameterButton->setAccessibleName(
      QStringLiteral("选择 parameter.txt"));
  m_chooseParameterButton->setFixedSize(30, 30);
  sourceLayout->addWidget(new QLabel(QStringLiteral("布局文件"), this), 1, 0);
  sourceLayout->addWidget(m_layoutPathEdit, 1, 1, 1, 4);
  sourceLayout->addWidget(m_chooseParameterButton, 1, 5);

  m_directoryEdit = new QLineEdit(this);
  m_directoryEdit->setObjectName(QStringLiteral("downloadImageDirectory"));
  m_directoryEdit->setReadOnly(true);
  m_directoryEdit->setPlaceholderText(
      QStringLiteral("选择镜像目录以自动匹配分区"));
  m_chooseDirectoryButton = new QToolButton(this);
  m_chooseDirectoryButton->setObjectName(
      QStringLiteral("chooseDownloadDirectoryButton"));
  m_chooseDirectoryButton->setIcon(
      style()->standardIcon(QStyle::SP_DirOpenIcon));
  m_chooseDirectoryButton->setToolTip(QStringLiteral("选择镜像目录"));
  m_chooseDirectoryButton->setAccessibleName(QStringLiteral("选择镜像目录"));
  m_chooseDirectoryButton->setFixedSize(30, 30);
  sourceLayout->addWidget(new QLabel(QStringLiteral("镜像目录"), this), 2, 0);
  sourceLayout->addWidget(m_directoryEdit, 2, 1, 1, 4);
  sourceLayout->addWidget(m_chooseDirectoryButton, 2, 5);
  pageLayout->addLayout(sourceLayout);

  m_table = new QTableWidget(0, ColumnCount, this);
  m_table->setObjectName(QStringLiteral("downloadImageTable"));
  m_table->setHorizontalHeaderLabels(
      {QStringLiteral("选择"), QStringLiteral("目标"),
       QStringLiteral("起始 LBA"), QStringLiteral("容量"),
       QStringLiteral("镜像文件"), QStringLiteral("格式"),
       QStringLiteral("展开大小"), QStringLiteral("校验"),
       QStringLiteral("状态")});
  m_table->setAlternatingRowColors(true);
  m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
  m_table->setSelectionMode(QAbstractItemView::SingleSelection);
  m_table->setShowGrid(false);
  m_table->verticalHeader()->setVisible(false);
  m_table->verticalHeader()->setDefaultSectionSize(30);
  auto *header = m_table->horizontalHeader();
  for (int column = 0; column < ColumnCount; ++column) {
    header->setSectionResizeMode(column, column == FileColumn
                                             ? QHeaderView::Stretch
                                             : QHeaderView::ResizeToContents);
  }
  pageLayout->addWidget(m_table, 1);

  auto *actions = new QHBoxLayout;
  m_chooseImageButton = new QPushButton(QStringLiteral("选择镜像"), this);
  m_chooseImageButton->setObjectName(
      QStringLiteral("chooseDownloadImageButton"));
  m_chooseImageButton->setIcon(style()->standardIcon(QStyle::SP_DirOpenIcon));
  m_clearImageButton = new QPushButton(QStringLiteral("清除"), this);
  m_clearImageButton->setObjectName(QStringLiteral("clearDownloadImageButton"));
  m_clearImageButton->setIcon(
      style()->standardIcon(QStyle::SP_DialogResetButton));
  m_rebootCheckBox = new QCheckBox(QStringLiteral("完成后重启"), this);
  m_rebootCheckBox->setObjectName(QStringLiteral("downloadRebootCheckBox"));
  m_rebootCheckBox->setChecked(true);
  m_startButton = new QPushButton(QStringLiteral("开始下载"), this);
  m_startButton->setObjectName(QStringLiteral("startImageDownloadButton"));
  m_startButton->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
  m_cancelButton = new QPushButton(QStringLiteral("取消"), this);
  m_cancelButton->setObjectName(QStringLiteral("cancelImageDownloadButton"));
  m_cancelButton->setIcon(style()->standardIcon(QStyle::SP_DialogCancelButton));
  actions->addWidget(m_chooseImageButton);
  actions->addWidget(m_clearImageButton);
  actions->addWidget(m_rebootCheckBox);
  actions->addStretch();
  actions->addWidget(m_startButton);
  actions->addWidget(m_cancelButton);
  pageLayout->addLayout(actions);

  m_overallProgress = new QProgressBar(this);
  m_overallProgress->setObjectName(
      QStringLiteral("overallImageDownloadProgress"));
  m_overallProgress->setRange(0, 100);
  m_overallProgress->setFormat(QStringLiteral("完整计划 %p%"));
  m_operationProgress = new QProgressBar(this);
  m_operationProgress->setObjectName(
      QStringLiteral("operationImageDownloadProgress"));
  m_operationProgress->setRange(0, 100);
  m_operationProgress->setFormat(QStringLiteral("当前分区 %p%"));
  m_status = new QLabel(QStringLiteral("请选择目标设备和分区布局"), this);
  m_status->setObjectName(QStringLiteral("imageDownloadStatus"));
  m_status->setWordWrap(true);
  pageLayout->addWidget(m_overallProgress);
  pageLayout->addWidget(m_operationProgress);
  pageLayout->addWidget(m_status);

  connect(m_deviceCombo, &QComboBox::currentIndexChanged, this,
          &ImageDownloadPage::deviceChanged);
  connect(m_layoutCombo, &QComboBox::currentIndexChanged, this,
          &ImageDownloadPage::layoutChanged);
  connect(m_chooseParameterButton, &QToolButton::clicked, this,
          &ImageDownloadPage::chooseParameterRequested);
  connect(m_readGptButton, &QToolButton::clicked, this,
          &ImageDownloadPage::readDeviceGptRequested);
  connect(m_chooseDirectoryButton, &QToolButton::clicked, this,
          &ImageDownloadPage::chooseDirectoryRequested);
  connect(m_chooseImageButton, &QPushButton::clicked, this,
          &ImageDownloadPage::chooseImageRequested);
  connect(m_clearImageButton, &QPushButton::clicked, this,
          &ImageDownloadPage::clearImageRequested);
  connect(m_table, &QTableWidget::itemSelectionChanged, this,
          &ImageDownloadPage::planChanged);
  connect(m_table, &QTableWidget::itemChanged, this,
          &ImageDownloadPage::planChanged);
  connect(m_startButton, &QPushButton::clicked, this,
          &ImageDownloadPage::startRequested);
  connect(m_cancelButton, &QPushButton::clicked, this,
          &ImageDownloadPage::cancelRequested);
}

QComboBox *ImageDownloadPage::deviceCombo() const noexcept {
  return m_deviceCombo;
}
QComboBox *ImageDownloadPage::layoutCombo() const noexcept {
  return m_layoutCombo;
}
QLineEdit *ImageDownloadPage::layoutPathEdit() const noexcept {
  return m_layoutPathEdit;
}
QLineEdit *ImageDownloadPage::directoryEdit() const noexcept {
  return m_directoryEdit;
}
QTableWidget *ImageDownloadPage::table() const noexcept { return m_table; }
QCheckBox *ImageDownloadPage::rebootCheckBox() const noexcept {
  return m_rebootCheckBox;
}
QString ImageDownloadPage::selectedPortPath() const {
  return m_deviceCombo->currentData().toString();
}
bool ImageDownloadPage::selectedDeviceManageable() const {
  return m_deviceCombo->currentData(Qt::UserRole + 1).toBool();
}
int ImageDownloadPage::selectedDeviceMode() const {
  return m_deviceCombo->currentData(Qt::UserRole + 2).toInt();
}

void ImageDownloadPage::setPlan(const int rowCount) {
  m_table->clearContents();
  m_table->setRowCount(rowCount);
}

void ImageDownloadPage::setProgress(const int overall, const int operation,
                                    const bool overallIndeterminate,
                                    const bool operationIndeterminate) {
  m_overallProgress->setRange(0, overallIndeterminate ? 0 : 100);
  m_operationProgress->setRange(0, operationIndeterminate ? 0 : 100);
  if (!overallIndeterminate)
    m_overallProgress->setValue(overall);
  if (!operationIndeterminate)
    m_operationProgress->setValue(operation);
}

void ImageDownloadPage::setStatus(const QString &text) {
  m_status->setText(text);
}

void ImageDownloadPage::setBusy(const bool busy, const bool canStart,
                                const bool canCancel) {
  const bool parameterLayout = m_layoutCombo->currentIndex() == 1;
  m_deviceCombo->setEnabled(!busy);
  m_layoutCombo->setEnabled(!busy);
  m_chooseParameterButton->setEnabled(!busy && parameterLayout);
  m_readGptButton->setEnabled(!busy && !parameterLayout &&
                              selectedDeviceManageable());
  m_chooseDirectoryButton->setEnabled(!busy);
  m_table->setEnabled(!busy);
  const auto *target = m_table->currentRow() < 0
                           ? nullptr
                           : m_table->item(m_table->currentRow(), TargetColumn);
  const bool specialGpt =
      target != nullptr && target->data(KindRole).toInt() == 1;
  m_chooseImageButton->setEnabled(!busy && target != nullptr && !specialGpt);
  m_clearImageButton->setEnabled(!busy && target != nullptr && !specialGpt);
  m_rebootCheckBox->setEnabled(!busy);
  m_startButton->setEnabled(!busy && canStart);
  m_cancelButton->setEnabled(canCancel);
}

} // namespace zzrk::gui
