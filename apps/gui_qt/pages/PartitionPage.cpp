// SPDX-License-Identifier: MIT

#include "PartitionPage.hpp"
#include "WorkspaceUi.hpp"
#include <ZzFluentUI/ZzInfoBar.h>

#include <QAbstractItemView>
#include <QCheckBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QProgressBar>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>

namespace zzrk::gui {
namespace {
enum Column { Name, FirstLba, LastLba, SectorCount, Size, Attributes, Count };
constexpr int PartitionIndexRole = Qt::UserRole + 1;
QTableWidgetItem *item(const QString &text) {
  auto *result = new QTableWidgetItem(text);
  result->setFlags(result->flags() & ~Qt::ItemIsEditable);
  return result;
}
QString sizeText(std::uint64_t bytes) {
  return QLocale().formattedDataSize(static_cast<qint64>(bytes), 2,
                                     QLocale::DataSizeTraditionalFormat);
}
} // namespace

PartitionPage *PartitionPage::buildUi(QWidget *parent) {
  return new PartitionPage(parent);
}

PartitionPage::PartitionPage(QWidget *parent) : QWidget(parent) {
  setObjectName(QStringLiteral("partitionPage"));
  auto *layout = new QVBoxLayout(this);
  ui::configurePage(this, layout);
  auto *tableCard = ui::card(this);
  auto *tableLayout = new QVBoxLayout(tableCard);
  tableLayout->setContentsMargins(ui::CardPadding, ui::CardPadding,
                                  ui::CardPadding, ui::CardPadding);
  auto *toolbar = new QHBoxLayout;
  toolbar->addWidget(ui::sectionTitle(QStringLiteral("设备分区表"), tableCard));
  toolbar->addStretch();
  m_readButton = new QToolButton(this);
  m_readButton->setObjectName(QStringLiteral("readPartitionsButton"));
  ui::configureToolButton(m_readButton, ui::Icon::Refresh);
  m_readButton->setToolTip(QStringLiteral("重新读取并校验 GPT"));
  m_readButton->setAccessibleName(QStringLiteral("读取 GPT 分区表"));
  toolbar->addWidget(m_readButton);
  tableLayout->addLayout(toolbar);
  tableLayout->addWidget(ui::secondaryText(
      QStringLiteral("目标设备：在“设备”页选中的 Loader 设备"), tableCard));

  m_table = new QTableWidget(0, Count, this);
  m_table->setObjectName(QStringLiteral("partitionTable"));
  m_table->setHorizontalHeaderLabels(
      {QStringLiteral("分区名"), QStringLiteral("起始 LBA"),
       QStringLiteral("结束 LBA"), QStringLiteral("扇区数"),
       QStringLiteral("容量"), QStringLiteral("属性")});
  ui::configureTable(m_table);
  m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
  m_table->setSelectionMode(QAbstractItemView::SingleSelection);
  m_table->setShowGrid(false);
  m_table->setSortingEnabled(true);
  m_table->verticalHeader()->setVisible(false);
  m_table->horizontalHeader()->setSectionResizeMode(Name, QHeaderView::Stretch);
  for (int column = FirstLba; column < Count; ++column)
    m_table->horizontalHeader()->setSectionResizeMode(
        column, QHeaderView::ResizeToContents);
  tableLayout->addWidget(m_table, 1);
  layout->addWidget(tableCard, 1);

  auto *taskCard = ui::card(this);
  auto *taskLayout = new QVBoxLayout(taskCard);
  taskLayout->setContentsMargins(ui::CardPadding, 12, ui::CardPadding, 12);
  taskLayout->setSpacing(8);
  taskLayout->setSizeConstraint(QLayout::SetMinimumSize);

  auto *actions = new QHBoxLayout;
  m_verifyWriteCheckBox =
      new QCheckBox(QStringLiteral("写入后逐块回读校验"), this);
  m_verifyWriteCheckBox->setObjectName(
      QStringLiteral("verifyPartitionWriteCheckBox"));
  m_verifyWriteCheckBox->setChecked(true);
  m_backupButton = ui::button(QStringLiteral("备份分区"), ui::Icon::Save, this);
  m_backupButton->setObjectName(QStringLiteral("backupPartitionButton"));
  m_writeButton =
      ui::button(QStringLiteral("写入镜像"), ui::Icon::Flash, this, true);
  m_writeButton->setObjectName(QStringLiteral("writePartitionButton"));
  // 擦除独立放置，不与强调色的写入操作紧挨，原有确认流程仍由控制器负责。
  m_eraseButton = ui::button(QStringLiteral("擦除分区"), ui::Icon::Clear, this);
  m_eraseButton->setObjectName(QStringLiteral("erasePartitionButton"));
  m_eraseButton->setToolTip(QStringLiteral("危险操作：擦除所选分区的数据"));
  m_cancelButton = ui::button(QStringLiteral("取消"), ui::Icon::Cancel, this);
  m_cancelButton->setObjectName(QStringLiteral("cancelPartitionButton"));
  actions->addWidget(m_eraseButton);
  actions->addStretch();
  actions->addWidget(m_backupButton);
  actions->addWidget(m_writeButton);
  actions->addWidget(m_cancelButton);

  m_progress = new QProgressBar(this);
  m_progress->setObjectName(QStringLiteral("partitionProgress"));
  m_progress->setRange(0, 100);
  m_progress->setFormat(QStringLiteral("当前操作 %p%"));
  m_status = new QLabel(QStringLiteral("尚未读取分区表"), this);
  m_status->setObjectName(QStringLiteral("partitionStatus"));
  m_status->setWordWrap(true);
  m_status->setTextFormat(Qt::PlainText);
  taskLayout->addWidget(m_status);
  m_errorNotice = ui::addErrorNotice(taskLayout);
  m_errorNotice->setObjectName(QStringLiteral("partitionErrorNotice"));
  ui::addProgressRow(taskLayout, QStringLiteral("当前操作"), m_progress);
  taskLayout->addWidget(m_verifyWriteCheckBox);
  taskLayout->addLayout(actions);
  layout->addWidget(taskCard);

  connect(m_readButton, &QToolButton::clicked, this,
          &PartitionPage::readRequested);
  connect(m_table, &QTableWidget::itemSelectionChanged, this,
          &PartitionPage::selectionChanged);
  connect(m_backupButton, &QPushButton::clicked, this,
          &PartitionPage::backupRequested);
  connect(m_writeButton, &QPushButton::clicked, this,
          &PartitionPage::writeRequested);
  connect(m_eraseButton, &QPushButton::clicked, this,
          &PartitionPage::eraseRequested);
  connect(m_cancelButton, &QPushButton::clicked, this,
          &PartitionPage::cancelRequested);
}

std::optional<PartitionPage::PartitionRow>
PartitionPage::selectedPartition() const {
  const int row = m_table->currentRow();
  const auto *name = row < 0 ? nullptr : m_table->item(row, Name);
  bool valid = false;
  const auto index =
      name == nullptr ? 0U : name->data(PartitionIndexRole).toULongLong(&valid);
  return valid && index < m_partitions.size()
             ? std::optional<PartitionRow>{m_partitions[index]}
             : std::nullopt;
}
bool PartitionPage::verifyWrite() const {
  return m_verifyWriteCheckBox->isChecked();
}
void PartitionPage::setPartitions(std::vector<PartitionRow> partitions) {
  const QSignalBlocker blocker{m_table};
  m_partitions = std::move(partitions);
  m_table->setSortingEnabled(false);
  m_table->clearContents();
  m_table->setRowCount(static_cast<int>(m_partitions.size()));
  for (std::size_t index = 0; index < m_partitions.size(); ++index) {
    const auto &partition = m_partitions[index];
    const auto row = static_cast<int>(index);
    const auto sectors = partition.lastLba - partition.firstLba + 1U;
    auto *name = item(partition.name);
    name->setData(PartitionIndexRole, QVariant::fromValue<qulonglong>(index));
    m_table->setItem(row, Name, name);
    const auto setNumber = [&](int column, std::uint64_t value) {
      auto *number = item({});
      number->setData(Qt::DisplayRole, QVariant::fromValue<qulonglong>(value));
      number->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
      m_table->setItem(row, column, number);
    };
    setNumber(FirstLba, partition.firstLba);
    setNumber(LastLba, partition.lastLba);
    setNumber(SectorCount, sectors);
    m_table->setItem(row, Size, item(sizeText(sectors * 512U)));
    m_table->setItem(
        row, Attributes,
        item(QStringLiteral("0x%1").arg(partition.attributes, 0, 16)));
  }
  m_table->setSortingEnabled(true);
}
void PartitionPage::clearPartitions() {
  m_partitions.clear();
  m_table->setRowCount(0);
}
void PartitionPage::setProgress(int percent, bool indeterminate) {
  m_progress->setRange(0, indeterminate ? 0 : 100);
  if (!indeterminate)
    m_progress->setValue(percent);
  ui::refreshProgressText(m_progress);
}
void PartitionPage::setStatus(const QString &text) {
  m_status->setText(text);
  m_status->show();
  m_errorNotice->setOpen(false);
}
void PartitionPage::setError(const QString &text) {
  m_status->setText(text);
  m_status->hide();
  m_errorNotice->setMessage(text);
  m_errorNotice->setOpen(true);
  m_errorNotice->setMinimumHeight(m_errorNotice->heightForWidth(m_errorNotice->width()));
}
void PartitionPage::setBusy(bool busy, bool canRead, bool canOperate,
                            bool canCancel) {
  m_readButton->setEnabled(!busy && canRead);
  m_table->setEnabled(!busy);
  m_verifyWriteCheckBox->setEnabled(!busy);
  m_backupButton->setEnabled(!busy && canOperate);
  m_writeButton->setEnabled(!busy && canOperate);
  m_eraseButton->setEnabled(!busy && canOperate);
  m_cancelButton->setEnabled(canCancel);
}

} // namespace zzrk::gui
