// SPDX-License-Identifier: MIT

#include "ImageDownloadPage.hpp"
#include "GuiTranslations.hpp"
#include "WorkspaceUi.hpp"
#include <ZzFluentUI/ZzInfoBar.h>

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStackedLayout>
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
  ui::configurePage(this, pageLayout);

  // 配置区独立为卡片；修改字段排列只需调整下面的网格坐标。
  auto *sourceCard = ui::card(this);
  auto *sourceCardLayout = new QVBoxLayout(sourceCard);
  sourceCardLayout->setContentsMargins(ui::CardPadding, ui::CardPadding,
                                       ui::CardPadding, ui::CardPadding);
  sourceCardLayout->setSpacing(12);
  sourceCardLayout->addWidget(
      ui::sectionTitle(guiText("设备与镜像来源"), sourceCard));

  auto *sourceLayout = new QGridLayout;
  sourceLayout->setHorizontalSpacing(8);
  sourceLayout->setVerticalSpacing(8);
  sourceLayout->setColumnStretch(1, 1);
  sourceLayout->setColumnStretch(4, 1);

  m_deviceCombo = new QComboBox(this);
  m_deviceCombo->setObjectName(QStringLiteral("downloadDeviceCombo"));
  m_deviceCombo->setMinimumContentsLength(18);
  sourceLayout->addWidget(guiLabel(guiText("目标设备"), this), 0, 0);
  sourceLayout->addWidget(m_deviceCombo, 0, 1, 1, 2);

  m_layoutCombo = new QComboBox(this);
  m_layoutCombo->setObjectName(QStringLiteral("downloadLayoutCombo"));
  m_layoutCombo->addItem(guiText("设备 GPT"));
  m_layoutCombo->addItem(QStringLiteral("parameter.txt"));
  bindGuiTranslation(m_layoutCombo, [this] {
    const QSignalBlocker blocker(m_layoutCombo);
    m_layoutCombo->setItemText(0, guiText("设备 GPT"));
  });
  sourceLayout->addWidget(guiLabel(guiText("分区布局"), this), 0, 3);
  sourceLayout->addWidget(m_layoutCombo, 0, 4);

  m_readGptButton = new QToolButton(this);
  m_readGptButton->setObjectName(QStringLiteral("readDownloadGptButton"));
  ui::configureToolButton(m_readGptButton, ui::Icon::Refresh);
  setGuiText(m_readGptButton, "toolTip", guiText("读取设备 GPT"));
  setGuiText(m_readGptButton, "accessibleName", guiText("读取设备 GPT"));
  sourceLayout->addWidget(m_readGptButton, 0, 5);

  m_layoutPathEdit = new QLineEdit(this);
  m_layoutPathEdit->setObjectName(QStringLiteral("downloadParameterPath"));
  m_layoutPathEdit->setReadOnly(true);
  setGuiText(m_layoutPathEdit, "placeholderText",
             guiText("设备 GPT 将在读取后显示，或选择 parameter.txt"));
  m_chooseParameterButton = new QToolButton(this);
  m_chooseParameterButton->setObjectName(
      QStringLiteral("chooseDownloadParameterButton"));
  ui::configureToolButton(m_chooseParameterButton, ui::Icon::Folder);
  setGuiText(m_chooseParameterButton, "toolTip", guiText("选择 parameter.txt"));
  setGuiText(m_chooseParameterButton, "accessibleName",
             guiText("选择 parameter.txt"));
  sourceLayout->addWidget(guiLabel(guiText("布局文件"), this), 1, 0);
  sourceLayout->addWidget(m_layoutPathEdit, 1, 1, 1, 4);
  sourceLayout->addWidget(m_chooseParameterButton, 1, 5);

  m_directoryEdit = new QLineEdit(this);
  m_directoryEdit->setObjectName(QStringLiteral("downloadImageDirectory"));
  m_directoryEdit->setReadOnly(true);
  setGuiText(m_directoryEdit, "placeholderText",
             guiText("选择镜像目录以自动匹配分区"));
  m_chooseDirectoryButton = new QToolButton(this);
  m_chooseDirectoryButton->setObjectName(
      QStringLiteral("chooseDownloadDirectoryButton"));
  ui::configureToolButton(m_chooseDirectoryButton, ui::Icon::Folder);
  setGuiText(m_chooseDirectoryButton, "toolTip", guiText("选择镜像目录"));
  setGuiText(m_chooseDirectoryButton, "accessibleName",
             guiText("选择镜像目录"));
  sourceLayout->addWidget(guiLabel(guiText("镜像目录"), this), 2, 0);
  sourceLayout->addWidget(m_directoryEdit, 2, 1, 1, 4);
  sourceLayout->addWidget(m_chooseDirectoryButton, 2, 5);
  for (auto *control : {static_cast<QWidget *>(m_deviceCombo),
                        static_cast<QWidget *>(m_layoutCombo),
                        static_cast<QWidget *>(m_layoutPathEdit),
                        static_cast<QWidget *>(m_directoryEdit)})
    control->setMinimumHeight(ui::ControlHeight);
  sourceCardLayout->addLayout(sourceLayout);
  pageLayout->addWidget(sourceCard);

  auto *planCard = ui::card(this);
  auto *planLayout = new QVBoxLayout(planCard);
  planLayout->setContentsMargins(ui::CardPadding, 12, ui::CardPadding, 12);
  planLayout->setSpacing(10);
  auto *planHeader = new QHBoxLayout;
  planHeader->addWidget(ui::sectionTitle(guiText("烧录清单"), planCard));
  planHeader->addStretch();
  m_chooseImageButton = ui::button(guiText("选择镜像"), ui::Icon::Folder, this);
  m_chooseImageButton->setObjectName(
      QStringLiteral("chooseDownloadImageButton"));
  m_clearImageButton = ui::button(guiText("清除"), ui::Icon::Clear, this);
  m_clearImageButton->setObjectName(QStringLiteral("clearDownloadImageButton"));
  planHeader->addWidget(m_chooseImageButton);
  planHeader->addWidget(m_clearImageButton);
  planLayout->addLayout(planHeader);

  m_table = new QTableWidget(0, ColumnCount, this);
  m_table->setObjectName(QStringLiteral("downloadImageTable"));
  bindGuiTranslation(m_table, [this] {
    const QSignalBlocker blocker(m_table);
    for (std::size_t index = 0; index < m_planRows.size(); ++index) {
      const auto &row = m_planRows[index];
      for (const auto &[column, text] :
           {std::pair{LbaColumn, row.lba}, std::pair{FormatColumn, row.format},
            std::pair{StatusColumn, row.status}})
        if (auto *item = m_table->item(static_cast<int>(index), column))
          item->setText(text.translated());
    }
    m_table->setHorizontalHeaderLabels(
        {guiText("选择"), guiText("目标"), guiText("起始 LBA"), guiText("容量"),
         guiText("镜像文件"), guiText("格式"), guiText("展开大小"),
         guiText("校验"), guiText("状态")});
  });
  ui::configureTable(m_table);
  m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
  m_table->setSelectionMode(QAbstractItemView::SingleSelection);
  m_table->setShowGrid(false);
  m_table->verticalHeader()->setVisible(false);
  auto *header = m_table->horizontalHeader();
  for (int column = 0; column < ColumnCount; ++column) {
    header->setSectionResizeMode(column, column == FileColumn
                                             ? QHeaderView::Stretch
                                             : QHeaderView::ResizeToContents);
  }
  // 空态和表格共用同一块空间，空态不会覆盖或拦截已加载的数据行。
  auto *planBody = new QWidget(planCard);
  // 长错误需要额外任务空间时，清单可缩小并滚动，不让空态的最小尺寸撑出页面。
  planBody->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Ignored);
  m_planStack = new QStackedLayout(planBody);
  m_planStack->setSizeConstraint(QLayout::SetNoConstraint);
  m_planStack->setContentsMargins(0, 0, 0, 0);
  m_planStack->addWidget(m_table);
  auto *empty = new QWidget(planBody);
  auto *emptyLayout = new QVBoxLayout(empty);
  emptyLayout->addStretch();
  auto *emptyTitle = ui::sectionTitle(guiText("准备你的烧录清单"), empty);
  emptyTitle->setAlignment(Qt::AlignCenter);
  auto *emptyHint = ui::secondaryText(
      guiText("读取设备 GPT 或选择 parameter.txt，然后选择镜像目录。"), empty);
  emptyHint->setAlignment(Qt::AlignCenter);
  emptyLayout->addWidget(emptyTitle);
  emptyLayout->addWidget(emptyHint);
  emptyLayout->addStretch();
  m_planStack->addWidget(empty);
  m_planStack->setCurrentIndex(1);
  planLayout->addWidget(planBody, 1);
  pageLayout->addWidget(planCard, 1);

  // 任务区固定在清单下方，长清单滚动时仍能看到进度与停止入口。
  auto *taskCard = ui::card(this);
  auto *taskLayout = new QVBoxLayout(taskCard);
  taskLayout->setContentsMargins(ui::CardPadding, 12, ui::CardPadding, 12);
  taskLayout->setSpacing(6);
  // 保护错误通知、双进度和按钮的最小空间，剩余高度由清单承担。
  taskLayout->setSizeConstraint(QLayout::SetMinimumSize);
  m_status = guiLabel(guiText("请选择目标设备和分区布局"), taskCard);
  m_status->setTextFormat(Qt::PlainText);
  m_status->setObjectName(QStringLiteral("imageDownloadStatus"));
  m_status->setWordWrap(true);
  taskLayout->addWidget(m_status);
  m_errorNotice = ui::addErrorNotice(taskLayout);
  m_errorNotice->setObjectName(QStringLiteral("imageDownloadErrorNotice"));

  auto *actions = new QHBoxLayout;
  m_rebootCheckBox = guiCheckBox(guiText("完成后重启"), this);
  m_rebootCheckBox->setObjectName(QStringLiteral("downloadRebootCheckBox"));
  m_rebootCheckBox->setChecked(true);
  m_startButton = ui::button(guiText("开始烧录"), ui::Icon::Flash, this, true);
  m_startButton->setObjectName(QStringLiteral("startImageDownloadButton"));
  m_cancelButton = ui::button(guiText("取消"), ui::Icon::Cancel, this);
  m_cancelButton->setObjectName(QStringLiteral("cancelImageDownloadButton"));
  actions->addWidget(m_rebootCheckBox);
  actions->addStretch();
  actions->addWidget(m_startButton);
  actions->addWidget(m_cancelButton);

  m_overallProgress = new QProgressBar(this);
  m_overallProgress->setObjectName(
      QStringLiteral("overallImageDownloadProgress"));
  m_overallProgress->setRange(0, 100);
  setGuiText(m_overallProgress, "format", guiText("完整计划 %p%"));
  m_operationProgress = new QProgressBar(this);
  m_operationProgress->setObjectName(
      QStringLiteral("operationImageDownloadProgress"));
  m_operationProgress->setRange(0, 100);
  setGuiText(m_operationProgress, "format", guiText("当前分区 %p%"));
  ui::addProgressRow(taskLayout, guiText("完整计划"), m_overallProgress, true);
  ui::addProgressRow(taskLayout, guiText("当前分区"), m_operationProgress);
  taskLayout->addSpacing(6);
  taskLayout->addLayout(actions);
  pageLayout->addWidget(taskCard);

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
          &ImageDownloadPage::handlePlanItemChanged);
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

std::vector<ImageDownloadPage::PlanChoice>
ImageDownloadPage::planChoices() const {
  std::vector<PlanChoice> choices;
  choices.reserve(m_planRows.size());
  for (const auto &row : m_planRows) {
    choices.push_back({.kind = row.kind,
                       .target = row.target,
                       .selected = row.selected,
                       .verify = row.verify});
  }
  return choices;
}

std::optional<ImageDownloadPage::PlanTarget>
ImageDownloadPage::currentTarget() const {
  const int row = m_table->currentRow();
  if (row < 0 || static_cast<std::size_t>(row) >= m_planRows.size())
    return std::nullopt;
  const auto &target = m_planRows[static_cast<std::size_t>(row)];
  return PlanTarget{.kind = target.kind, .target = target.target};
}

void ImageDownloadPage::setPlan(std::vector<PlanRow> rows) {
  const QSignalBlocker blocker{m_table};
  m_planRows = std::move(rows);
  m_table->clearContents();
  m_table->setRowCount(static_cast<int>(m_planRows.size()));
  m_planStack->setCurrentIndex(m_planRows.empty() ? 1 : 0);
  const auto makeItem = [](const QString &text) {
    auto *item = new QTableWidgetItem(text);
    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    return item;
  };
  int index = 0;
  for (const auto &row : m_planRows) {
    auto *selected = makeItem({});
    selected->setFlags(selected->flags() | Qt::ItemIsUserCheckable);
    selected->setCheckState(row.selected ? Qt::Checked : Qt::Unchecked);
    if (row.selectionForced)
      selected->setFlags(selected->flags() & ~Qt::ItemIsEnabled);
    m_table->setItem(index, SelectedColumn, selected);
    m_table->setItem(index, TargetColumn, makeItem(row.target));
    m_table->setItem(index, LbaColumn, makeItem(row.lba.translated()));
    m_table->setItem(index, CapacityColumn, makeItem(row.capacity));
    // 只改变显示文字，业务规划仍保存完整路径；鼠标悬停可查看原始路径。
    auto *file = makeItem(QFileInfo(row.file).fileName());
    file->setToolTip(row.file);
    m_table->setItem(index, FileColumn, file);
    m_table->setItem(index, FormatColumn, makeItem(row.format.translated()));
    m_table->setItem(index, ExpandedSizeColumn, makeItem(row.expandedSize));
    auto *verify = makeItem({});
    verify->setFlags(verify->flags() | Qt::ItemIsUserCheckable);
    verify->setCheckState(row.verify ? Qt::Checked : Qt::Unchecked);
    m_table->setItem(index, VerifyColumn, verify);
    m_table->setItem(index, StatusColumn, makeItem(row.status.translated()));
    for (const int column : {LbaColumn, CapacityColumn, ExpandedSizeColumn})
      m_table->item(index, column)
          ->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    ++index;
  }
  if (!m_planRows.empty())
    m_table->selectRow(0);
}

void ImageDownloadPage::setTargetStatus(const QString &target,
                                        const GuiText &status) {
  for (std::size_t index = 0; index < m_planRows.size(); ++index) {
    auto &row = m_planRows[index];
    if (row.target != target)
      continue;
    row.status = status;
    if (auto *item = m_table->item(static_cast<int>(index), StatusColumn))
      item->setText(status.translated());
    return;
  }
}

void ImageDownloadPage::setSelectedTargetsStatus(const GuiText &status) {
  for (std::size_t index = 0; index < m_planRows.size(); ++index) {
    auto &row = m_planRows[index];
    if (!row.selected)
      continue;
    row.status = status;
    if (auto *item = m_table->item(static_cast<int>(index), StatusColumn))
      item->setText(status.translated());
  }
}

void ImageDownloadPage::handlePlanItemChanged(QTableWidgetItem *item) {
  if (item == nullptr || item->row() < 0 ||
      static_cast<std::size_t>(item->row()) >= m_planRows.size())
    return;
  auto &row = m_planRows[static_cast<std::size_t>(item->row())];
  if (item->column() == SelectedColumn)
    row.selected = item->checkState() == Qt::Checked;
  else if (item->column() == VerifyColumn)
    row.verify = item->checkState() == Qt::Checked;
  else
    return;
  emit planChanged();
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
  // 修改范围不一定触发 valueChanged，未知总量时也必须更新显示文字。
  ui::refreshProgressText(m_overallProgress);
  ui::refreshProgressText(m_operationProgress);
}

void ImageDownloadPage::setStatus(const GuiText &text) {
  setGuiText(m_status, "text", text);
  m_status->show();
  m_errorNotice->setOpen(false);
}

void ImageDownloadPage::setError(const GuiText &text) {
  // 控制器传入明确的失败结果，禁止根据中文消息内容猜测错误级别。
  setGuiText(m_status, "text", text);
  m_status->hide();
  setGuiText(m_errorNotice, "message", text);
  m_errorNotice->setOpen(true);
  m_errorNotice->setMinimumHeight(
      m_errorNotice->heightForWidth(m_errorNotice->width()));
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
  const auto target = currentTarget();
  const bool specialGpt =
      target.has_value() && target->kind == RowKind::partitionTable;
  m_chooseImageButton->setEnabled(!busy && target.has_value() && !specialGpt);
  m_clearImageButton->setEnabled(!busy && target.has_value() && !specialGpt);
  m_rebootCheckBox->setEnabled(!busy);
  m_startButton->setEnabled(!busy && canStart);
  m_cancelButton->setEnabled(canCancel);
}

} // namespace zzrk::gui
