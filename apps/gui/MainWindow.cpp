// SPDX-License-Identifier: MIT

#include "MainWindow.hpp"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QDateTime>
#include <QFrame>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QStatusBar>
#include <QStringList>
#include <QStyle>
#include <QTableWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>
#include <QtConcurrent/QtConcurrentRun>

#include <cstdint>
#include <exception>
#include <string>
#include <string_view>

namespace {

enum Column
{
    StatusColumn,
    ModeColumn,
    DeviceIdColumn,
    BusColumn,
    PortColumn,
    UsbVersionColumn,
    ColumnCount
};

constexpr int DeviceKeyRole = Qt::UserRole + 1;

QString modeText(zzrk::transport::DeviceMode mode)
{
    using zzrk::transport::DeviceMode;

    switch (mode) {
    case DeviceMode::Adb:
        return QStringLiteral("ADB");
    case DeviceMode::Maskrom:
        return QStringLiteral("Maskrom");
    case DeviceMode::Loader:
        return QStringLiteral("Loader");
    case DeviceMode::Unknown:
        return QStringLiteral("未知");
    }

    const std::string_view name = zzrk::transport::toString(mode);
    return QString::fromLatin1(name.data(), static_cast<qsizetype>(name.size()));
}

QString deviceIdText(const zzrk::transport::UsbDeviceInfo &device)
{
    return QStringLiteral("%1:%2")
        .arg(device.vendorId, 4, 16, QLatin1Char('0'))
        .arg(device.productId, 4, 16, QLatin1Char('0'))
        .toUpper();
}

QString usbVersionText(std::uint16_t bcdUsb)
{
    return QStringLiteral("%1.%2%3")
        .arg((bcdUsb >> 8) & 0xff)
        .arg((bcdUsb >> 4) & 0x0f)
        .arg(bcdUsb & 0x0f);
}

QString portPathText(const zzrk::transport::UsbDeviceInfo &device)
{
    if (!device.portPath.empty()) {
        return QString::fromStdString(device.portPath);
    }

    QStringList ports;
    ports.reserve(static_cast<qsizetype>(device.portNumbers.size()));
    for (const std::uint8_t port : device.portNumbers) {
        ports.append(QString::number(port));
    }
    return ports.isEmpty() ? QStringLiteral("-") : ports.join(QLatin1Char('.'));
}

QString deviceKey(const zzrk::transport::UsbDeviceInfo &device)
{
    return QStringLiteral("%1|%2|%3|%4")
        .arg(device.busNumber)
        .arg(portPathText(device))
        .arg(device.vendorId)
        .arg(device.productId);
}

QTableWidgetItem *makeItem(const QString &text)
{
    auto *item = new QTableWidgetItem(text);
    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    return item;
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("ZzRkDevTool"));
    resize(920, 560);
    setMinimumSize(760, 440);

    auto *centralWidget = new QWidget(this);
    auto *mainLayout = new QVBoxLayout(centralWidget);
    mainLayout->setContentsMargins(14, 12, 14, 10);
    mainLayout->setSpacing(10);

    auto *toolRow = new QHBoxLayout;
    toolRow->setSpacing(8);

    m_connectionIcon = new QLabel(centralWidget);
    m_connectionIcon->setObjectName(QStringLiteral("connectionStatusIcon"));
    m_connectionIcon->setFixedSize(18, 18);
    m_connectionIcon->setAlignment(Qt::AlignCenter);
    m_connectionIcon->setAccessibleName(QStringLiteral("连接状态"));

    m_connectionText = new QLabel(QStringLiteral("正在检测设备..."), centralWidget);
    m_connectionText->setObjectName(QStringLiteral("connectionStatusText"));
    QFont connectionFont = m_connectionText->font();
    connectionFont.setBold(true);
    m_connectionText->setFont(connectionFont);

    m_autoRefreshCheckBox = new QCheckBox(QStringLiteral("每 2 秒自动刷新"), centralWidget);
    m_autoRefreshCheckBox->setObjectName(QStringLiteral("autoRefreshCheckBox"));
    m_autoRefreshCheckBox->setChecked(true);

    m_refreshButton = new QToolButton(centralWidget);
    m_refreshButton->setObjectName(QStringLiteral("refreshButton"));
    m_refreshButton->setIcon(style()->standardIcon(QStyle::SP_BrowserReload));
    m_refreshButton->setToolTip(QStringLiteral("立即刷新设备列表"));
    m_refreshButton->setAccessibleName(QStringLiteral("刷新设备列表"));
    m_refreshButton->setFixedSize(30, 30);

    toolRow->addWidget(m_connectionIcon);
    toolRow->addWidget(m_connectionText);
    toolRow->addStretch();
    toolRow->addWidget(m_autoRefreshCheckBox);
    toolRow->addWidget(m_refreshButton);
    mainLayout->addLayout(toolRow);

    auto *separator = new QFrame(centralWidget);
    separator->setFrameShape(QFrame::HLine);
    separator->setFrameShadow(QFrame::Sunken);
    mainLayout->addWidget(separator);

    m_deviceTable = new QTableWidget(0, ColumnCount, centralWidget);
    m_deviceTable->setObjectName(QStringLiteral("deviceTable"));
    m_deviceTable->setHorizontalHeaderLabels({
        QStringLiteral("状态"),
        QStringLiteral("设备模式"),
        QStringLiteral("VID:PID"),
        QStringLiteral("总线"),
        QStringLiteral("端口路径"),
        QStringLiteral("USB 版本"),
    });
    m_deviceTable->setAlternatingRowColors(true);
    m_deviceTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_deviceTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_deviceTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_deviceTable->setShowGrid(false);
    m_deviceTable->setSortingEnabled(true);
    m_deviceTable->verticalHeader()->setVisible(false);
    m_deviceTable->verticalHeader()->setDefaultSectionSize(32);

    QHeaderView *header = m_deviceTable->horizontalHeader();
    header->setHighlightSections(false);
    header->setSectionsClickable(true);
    header->setStretchLastSection(false);
    header->setSectionResizeMode(StatusColumn, QHeaderView::Fixed);
    header->setSectionResizeMode(ModeColumn, QHeaderView::Fixed);
    header->setSectionResizeMode(DeviceIdColumn, QHeaderView::Fixed);
    header->setSectionResizeMode(BusColumn, QHeaderView::Fixed);
    header->setSectionResizeMode(PortColumn, QHeaderView::Stretch);
    header->setSectionResizeMode(UsbVersionColumn, QHeaderView::Fixed);
    m_deviceTable->setColumnWidth(StatusColumn, 92);
    m_deviceTable->setColumnWidth(ModeColumn, 108);
    m_deviceTable->setColumnWidth(DeviceIdColumn, 124);
    m_deviceTable->setColumnWidth(BusColumn, 72);
    m_deviceTable->setColumnWidth(UsbVersionColumn, 96);
    header->setSortIndicator(BusColumn, Qt::AscendingOrder);
    mainLayout->addWidget(m_deviceTable, 1);

    auto *readOnlyRow = new QHBoxLayout;
    readOnlyRow->setSpacing(6);
    auto *readOnlyIcon = new QLabel(centralWidget);
    readOnlyIcon->setPixmap(style()->standardIcon(QStyle::SP_MessageBoxInformation).pixmap(16, 16));
    readOnlyIcon->setFixedSize(18, 18);
    auto *readOnlyText = new QLabel(
        QStringLiteral("只读模式：仅枚举 USB 设备，不会打开设备或执行写入操作。"),
        centralWidget);
    readOnlyText->setWordWrap(true);
    readOnlyRow->addWidget(readOnlyIcon);
    readOnlyRow->addWidget(readOnlyText, 1);
    mainLayout->addLayout(readOnlyRow);

    setCentralWidget(centralWidget);

    m_lastScanText = new QLabel(QStringLiteral("尚未扫描"), this);
    m_lastScanText->setObjectName(QStringLiteral("lastScanText"));
    statusBar()->addPermanentWidget(m_lastScanText);
    statusBar()->showMessage(QStringLiteral("准备就绪"));

    m_autoRefreshTimer = new QTimer(this);
    m_autoRefreshTimer->setInterval(2000);
    m_autoRefreshTimer->setSingleShot(true);
    m_autoRefreshTimer->setTimerType(Qt::CoarseTimer);

    connect(m_refreshButton, &QToolButton::clicked, this, &MainWindow::refreshDevices);
    connect(m_autoRefreshCheckBox,
            &QCheckBox::toggled,
            this,
            &MainWindow::setAutoRefreshEnabled);
    connect(m_autoRefreshTimer, &QTimer::timeout, this, &MainWindow::refreshDevices);
    connect(&m_scanWatcher,
            &QFutureWatcher<zzrk::transport::ScanResult>::finished,
            this,
            &MainWindow::handleScanFinished);

    QTimer::singleShot(0, this, &MainWindow::refreshDevices);
}

MainWindow::~MainWindow()
{
    m_shuttingDown = true;
    m_autoRefreshTimer->stop();
    disconnect(&m_scanWatcher, nullptr, this, nullptr);
    m_scanWatcher.waitForFinished();
}

void MainWindow::refreshDevices()
{
    if (m_shuttingDown || m_scanWatcher.isRunning()) {
        return;
    }

    m_autoRefreshTimer->stop();
    m_refreshButton->setEnabled(false);
    statusBar()->showMessage(QStringLiteral("正在扫描 USB 设备..."));

    auto future = QtConcurrent::run([scanner = &m_scanner] {
        try {
            return scanner->scanRockchipDevices();
        } catch (const std::exception &error) {
            zzrk::transport::ScanResult result;
            result.error = zzrk::transport::ScanError{-1, error.what()};
            return result;
        } catch (...) {
            zzrk::transport::ScanResult result;
            result.error = zzrk::transport::ScanError{-1, "Unknown USB scan failure"};
            return result;
        }
    });
    m_scanWatcher.setFuture(future);
}

void MainWindow::handleScanFinished()
{
    if (m_shuttingDown) {
        return;
    }

    applyScanResult(m_scanWatcher.result());
    m_refreshButton->setEnabled(true);
    m_hasCompletedScan = true;

    if (m_autoRefreshCheckBox->isChecked()) {
        m_autoRefreshTimer->start();
    }

    emit scanCompleted();
}

void MainWindow::applyScanResult(const zzrk::transport::ScanResult &result)
{
    QString selectedDeviceKey;
    if (const int selectedRow = m_deviceTable->currentRow(); selectedRow >= 0) {
        if (const QTableWidgetItem *item = m_deviceTable->item(selectedRow, StatusColumn)) {
            selectedDeviceKey = item->data(DeviceKeyRole).toString();
        }
    }

    QHeaderView *header = m_deviceTable->horizontalHeader();
    const int sortColumn = header->sortIndicatorSection();
    const Qt::SortOrder sortOrder = header->sortIndicatorOrder();

    m_deviceTable->setUpdatesEnabled(false);
    m_deviceTable->setSortingEnabled(false);
    m_deviceTable->clearContents();
    m_deviceTable->setRowCount(static_cast<int>(result.devices.size()));

    const QIcon connectedIcon = style()->standardIcon(QStyle::SP_DialogApplyButton);
    int row = 0;
    for (const zzrk::transport::UsbDeviceInfo &device : result.devices) {
        auto *statusItem = makeItem(QStringLiteral("已连接"));
        statusItem->setIcon(connectedIcon);
        statusItem->setData(DeviceKeyRole, deviceKey(device));
        m_deviceTable->setItem(row, StatusColumn, statusItem);
        m_deviceTable->setItem(row, ModeColumn, makeItem(modeText(device.mode)));
        m_deviceTable->setItem(row, DeviceIdColumn, makeItem(deviceIdText(device)));

        auto *busItem = makeItem(QString::number(device.busNumber));
        busItem->setData(Qt::DisplayRole, device.busNumber);
        busItem->setTextAlignment(Qt::AlignCenter);
        m_deviceTable->setItem(row, BusColumn, busItem);
        m_deviceTable->setItem(row, PortColumn, makeItem(portPathText(device)));
        m_deviceTable->setItem(row, UsbVersionColumn, makeItem(usbVersionText(device.bcdUsb)));
        ++row;
    }

    m_deviceTable->setSortingEnabled(true);
    if (sortColumn >= 0) {
        m_deviceTable->sortItems(sortColumn, sortOrder);
    }

    if (!selectedDeviceKey.isEmpty()) {
        for (int currentRow = 0; currentRow < m_deviceTable->rowCount(); ++currentRow) {
            QTableWidgetItem *item = m_deviceTable->item(currentRow, StatusColumn);
            if (item != nullptr && item->data(DeviceKeyRole).toString() == selectedDeviceKey) {
                m_deviceTable->selectRow(currentRow);
                break;
            }
        }
    }
    m_deviceTable->setUpdatesEnabled(true);

    const int deviceCount = static_cast<int>(result.devices.size());
    const QDateTime now = QDateTime::currentDateTime();
    m_lastScanText->setText(QStringLiteral("更新于 %1").arg(now.toString(QStringLiteral("HH:mm:ss"))));

    if (result.error.has_value()) {
        m_connectionIcon->setPixmap(
            style()->standardIcon(QStyle::SP_MessageBoxCritical).pixmap(16, 16));
        m_connectionText->setText(deviceCount == 0
                                      ? QStringLiteral("设备扫描失败")
                                      : QStringLiteral("已检测到 %1 台设备（结果可能不完整）")
                                            .arg(deviceCount));
        statusBar()->showMessage(
            QStringLiteral("USB 扫描错误（%1）：%2")
                .arg(result.error->code)
                .arg(QString::fromStdString(result.error->message)));
    } else if (deviceCount == 0) {
        m_connectionIcon->setPixmap(
            style()->standardIcon(QStyle::SP_MessageBoxInformation).pixmap(16, 16));
        m_connectionText->setText(QStringLiteral("未检测到 Rockchip 设备"));
        statusBar()->showMessage(QStringLiteral("扫描完成，未发现设备"));
    } else {
        m_connectionIcon->setPixmap(
            style()->standardIcon(QStyle::SP_DialogApplyButton).pixmap(16, 16));
        m_connectionText->setText(QStringLiteral("已连接 %1 台 Rockchip 设备").arg(deviceCount));
        statusBar()->showMessage(QStringLiteral("设备列表已更新"));
    }
}

void MainWindow::setAutoRefreshEnabled(bool enabled)
{
    m_autoRefreshTimer->stop();
    if (enabled && m_hasCompletedScan && !m_scanWatcher.isRunning()) {
        refreshDevices();
    }
}
