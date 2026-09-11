// SPDX-License-Identifier: MIT

#include "PartitionPage.hpp"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QProgressBar>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStyle>
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

PartitionPage *PartitionPage::buildUi(QWidget *parent) { return new PartitionPage(parent); }

PartitionPage::PartitionPage(QWidget *parent) : QWidget(parent) {
  setObjectName(QStringLiteral("partitionPage"));
  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(10, 10, 10, 10);
  auto *toolbar = new QHBoxLayout;
  m_readButton = new QToolButton(this);
  m_readButton->setObjectName(QStringLiteral("readPartitionsButton"));
  m_readButton->setIcon(style()->standardIcon(QStyle::SP_BrowserReload));
  m_readButton->setToolTip(QStringLiteral("重新读取并校验 GPT"));
  m_readButton->setAccessibleName(QStringLiteral("读取 GPT 分区表"));
  m_readButton->setFixedSize(30, 30);
  toolbar->addWidget(m_readButton);
  toolbar->addWidget(new QLabel(QStringLiteral("目标设备使用“设备”页当前选中的 Loader"), this));
  toolbar->addStretch();
  layout->addLayout(toolbar);

  m_table = new QTableWidget(0, Count, this);
  m_table->setObjectName(QStringLiteral("partitionTable"));
  m_table->setHorizontalHeaderLabels({QStringLiteral("分区名"), QStringLiteral("起始 LBA"),
      QStringLiteral("结束 LBA"), QStringLiteral("扇区数"), QStringLiteral("容量"),
      QStringLiteral("属性")});
  m_table->setAlternatingRowColors(true);
  m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
  m_table->setSelectionMode(QAbstractItemView::SingleSelection);
  m_table->setShowGrid(false);
  m_table->setSortingEnabled(true);
  m_table->verticalHeader()->setVisible(false);
  m_table->verticalHeader()->setDefaultSectionSize(30);
  m_table->horizontalHeader()->setSectionResizeMode(Name, QHeaderView::Stretch);
  for (int column = FirstLba; column < Count; ++column)
    m_table->horizontalHeader()->setSectionResizeMode(column, QHeaderView::ResizeToContents);
  layout->addWidget(m_table, 1);

  auto *actions = new QHBoxLayout;
  m_verifyWriteCheckBox = new QCheckBox(QStringLiteral("写入后逐块回读校验"), this);
  m_verifyWriteCheckBox->setObjectName(QStringLiteral("verifyPartitionWriteCheckBox"));
  m_verifyWriteCheckBox->setChecked(true);
  m_backupButton = new QPushButton(QStringLiteral("备份分区"), this);
  m_backupButton->setObjectName(QStringLiteral("backupPartitionButton"));
  m_backupButton->setIcon(style()->standardIcon(QStyle::SP_DialogSaveButton));
  m_writeButton = new QPushButton(QStringLiteral("写入镜像"), this);
  m_writeButton->setObjectName(QStringLiteral("writePartitionButton"));
  m_writeButton->setIcon(style()->standardIcon(QStyle::SP_ArrowDown));
  m_eraseButton = new QPushButton(QStringLiteral("擦除分区"), this);
  m_eraseButton->setObjectName(QStringLiteral("erasePartitionButton"));
  m_eraseButton->setIcon(style()->standardIcon(QStyle::SP_TrashIcon));
  m_cancelButton = new QPushButton(QStringLiteral("取消"), this);
  m_cancelButton->setObjectName(QStringLiteral("cancelPartitionButton"));
  m_cancelButton->setIcon(style()->standardIcon(QStyle::SP_DialogCancelButton));
  actions->addWidget(m_verifyWriteCheckBox);
  actions->addStretch();
  actions->addWidget(m_backupButton);
  actions->addWidget(m_writeButton);
  actions->addWidget(m_eraseButton);
  actions->addWidget(m_cancelButton);
  layout->addLayout(actions);

  m_progress = new QProgressBar(this);
  m_progress->setObjectName(QStringLiteral("partitionProgress"));
  m_progress->setRange(0, 100);
  m_progress->setFormat(QStringLiteral("当前操作 %p%"));
  m_status = new QLabel(QStringLiteral("尚未读取分区表"), this);
  m_status->setObjectName(QStringLiteral("partitionStatus"));
  m_status->setWordWrap(true);
  layout->addWidget(m_progress);
  layout->addWidget(m_status);

  connect(m_readButton, &QToolButton::clicked, this, &PartitionPage::readRequested);
  connect(m_table, &QTableWidget::itemSelectionChanged, this, &PartitionPage::selectionChanged);
  connect(m_backupButton, &QPushButton::clicked, this, &PartitionPage::backupRequested);
  connect(m_writeButton, &QPushButton::clicked, this, &PartitionPage::writeRequested);
  connect(m_eraseButton, &QPushButton::clicked, this, &PartitionPage::eraseRequested);
  connect(m_cancelButton, &QPushButton::clicked, this, &PartitionPage::cancelRequested);
}

std::optional<PartitionPage::PartitionRow> PartitionPage::selectedPartition() const {
  const int row = m_table->currentRow();
  const auto *name = row < 0 ? nullptr : m_table->item(row, Name);
  bool valid = false;
  const auto index = name == nullptr
                         ? 0U
                         : name->data(PartitionIndexRole).toULongLong(&valid);
  return valid && index < m_partitions.size()
             ? std::optional<PartitionRow>{m_partitions[index]}
             : std::nullopt;
}
bool PartitionPage::verifyWrite() const { return m_verifyWriteCheckBox->isChecked(); }
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
    name->setData(PartitionIndexRole,
                  QVariant::fromValue<qulonglong>(index));
    m_table->setItem(row, Name, name);
    const auto setNumber = [&](int column, std::uint64_t value) {
      auto *number = item({});
      number->setData(Qt::DisplayRole, QVariant::fromValue<qulonglong>(value));
      m_table->setItem(row, column, number);
    };
    setNumber(FirstLba, partition.firstLba);
    setNumber(LastLba, partition.lastLba);
    setNumber(SectorCount, sectors);
    m_table->setItem(row, Size, item(sizeText(sectors * 512U)));
    m_table->setItem(row, Attributes,
                     item(QStringLiteral("0x%1").arg(partition.attributes, 0, 16)));
  }
  m_table->setSortingEnabled(true);
}
void PartitionPage::clearPartitions() { m_partitions.clear(); m_table->setRowCount(0); }
void PartitionPage::setProgress(int percent, bool indeterminate) {
  m_progress->setRange(0, indeterminate ? 0 : 100);
  if (!indeterminate) m_progress->setValue(percent);
}
void PartitionPage::setStatus(const QString &text) { m_status->setText(text); }
void PartitionPage::setBusy(bool busy, bool canRead, bool canOperate, bool canCancel) {
  m_readButton->setEnabled(!busy && canRead);
  m_table->setEnabled(!busy);
  m_verifyWriteCheckBox->setEnabled(!busy);
  m_backupButton->setEnabled(!busy && canOperate);
  m_writeButton->setEnabled(!busy && canOperate);
  m_eraseButton->setEnabled(!busy && canOperate);
  m_cancelButton->setEnabled(canCancel);
}

} // namespace zzrk::gui
