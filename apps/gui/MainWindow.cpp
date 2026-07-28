// SPDX-License-Identifier: MIT

#include "MainWindow.hpp"

#include <zzrk/qt/FirmwareMd5Verifier.hpp>

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QFileDialog>
#include <QFrame>
#include <QGridLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMetaObject>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QStatusBar>
#include <QStringList>
#include <QStyle>
#include <QTableWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

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
constexpr int PortPathRole = Qt::UserRole + 2;
constexpr int FlashableRole = Qt::UserRole + 3;

bool isSupportedFlashDevice(const zzrk::transport::UsbDeviceInfo &device)
{
    if (device.vendorId != 0x2207U || device.productId != 0x350bU) {
        return false;
    }
    if (device.mode == zzrk::transport::DeviceMode::Loader) {
        return device.bcdUsb == 0x0201U && device.bcdDevice == 0x0223U;
    }
    return device.mode == zzrk::transport::DeviceMode::Maskrom
        && device.bcdUsb == 0x0200U && device.bcdDevice == 0x0100U;
}

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

QString workflowErrorText(const zzrk::workflow::FirmwareUpgradeError &error)
{
    using zzrk::workflow::FirmwareUpgradeErrorCode;
    QString reason;
    switch (error.code) {
    case FirmwareUpgradeErrorCode::cancelled:
        reason = QStringLiteral("操作已取消");
        break;
    case FirmwareUpgradeErrorCode::imageInvalid:
        reason = QStringLiteral("固件格式无效");
        break;
    case FirmwareUpgradeErrorCode::digestFailed:
        reason = QStringLiteral("固件 MD5 校验失败");
        break;
    case FirmwareUpgradeErrorCode::planInvalid:
        reason = QStringLiteral("无法生成完整升级计划");
        break;
    case FirmwareUpgradeErrorCode::scanFailed:
    case FirmwareUpgradeErrorCode::deviceNotFound:
    case FirmwareUpgradeErrorCode::deviceAmbiguous:
    case FirmwareUpgradeErrorCode::deviceUnsupported:
        reason = QStringLiteral("目标 USB 设备不可用");
        break;
    case FirmwareUpgradeErrorCode::openFailed:
        reason = QStringLiteral("无法打开 RockUSB 接口");
        break;
    case FirmwareUpgradeErrorCode::loaderResetFailed:
    case FirmwareUpgradeErrorCode::reenumerationTimedOut:
    case FirmwareUpgradeErrorCode::loaderDownloadFailed:
    case FirmwareUpgradeErrorCode::loaderNotReady:
        reason = QStringLiteral("RAM Loader 准备失败");
        break;
    case FirmwareUpgradeErrorCode::wrongChip:
        reason = QStringLiteral("目标芯片不是 RK3588");
        break;
    case FirmwareUpgradeErrorCode::storageCheckFailed:
    case FirmwareUpgradeErrorCode::storageNotEmmc:
    case FirmwareUpgradeErrorCode::deviceTooSmall:
        reason = QStringLiteral("eMMC 检查失败");
        break;
    case FirmwareUpgradeErrorCode::flashFailed:
        reason = QStringLiteral("固件写入或回读校验失败");
        break;
    case FirmwareUpgradeErrorCode::resetFailed:
        reason = QStringLiteral("固件已写入，但设备复位失败");
        break;
    case FirmwareUpgradeErrorCode::closeFailed:
        reason = QStringLiteral("无法释放 RockUSB 接口");
        break;
    }
    if (!error.target.empty()) {
        reason += QStringLiteral("（%1）").arg(QString::fromStdString(error.target));
    }
    return reason;
}

zzrk::image::FirmwareVerifyMode verifyModeFromIndex(const int index)
{
    using zzrk::image::FirmwareVerifyMode;
    switch (index) {
    case 1:
        return FirmwareVerifyMode::all;
    case 2:
        return FirmwareVerifyMode::none;
    default:
        return FirmwareVerifyMode::critical;
    }
}

std::filesystem::path filesystemPath(const QString &path)
{
#ifdef _WIN32
    return std::filesystem::path{path.toStdWString()};
#else
    return std::filesystem::path{path.toUtf8().constData()};
#endif
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("ZzRkDevTool"));
    resize(980, 720);
    setMinimumSize(800, 620);

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

    auto *upgradeSeparator = new QFrame(centralWidget);
    upgradeSeparator->setFrameShape(QFrame::HLine);
    upgradeSeparator->setFrameShadow(QFrame::Sunken);
    mainLayout->addWidget(upgradeSeparator);

    auto *upgradeLayout = new QGridLayout;
    upgradeLayout->setHorizontalSpacing(8);
    upgradeLayout->setVerticalSpacing(8);
    upgradeLayout->setColumnStretch(1, 1);

    auto *imageLabel = new QLabel(QStringLiteral("固件"), centralWidget);
    m_imagePathEdit = new QLineEdit(centralWidget);
    m_imagePathEdit->setObjectName(QStringLiteral("firmwareImagePath"));
    m_imagePathEdit->setReadOnly(true);
    m_imagePathEdit->setPlaceholderText(QStringLiteral("选择 update.img"));
    m_chooseImageButton = new QToolButton(centralWidget);
    m_chooseImageButton->setObjectName(QStringLiteral("chooseFirmwareButton"));
    m_chooseImageButton->setIcon(style()->standardIcon(QStyle::SP_DirOpenIcon));
    m_chooseImageButton->setToolTip(QStringLiteral("选择固件镜像"));
    m_chooseImageButton->setAccessibleName(QStringLiteral("选择固件镜像"));
    m_chooseImageButton->setFixedSize(30, 30);
    upgradeLayout->addWidget(imageLabel, 0, 0);
    upgradeLayout->addWidget(m_imagePathEdit, 0, 1);
    upgradeLayout->addWidget(m_chooseImageButton, 0, 2);

    auto *planLabel = new QLabel(QStringLiteral("升级计划"), centralWidget);
    planLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_planPreview = new QPlainTextEdit(centralWidget);
    m_planPreview->setObjectName(QStringLiteral("firmwarePlanPreview"));
    m_planPreview->setReadOnly(true);
    m_planPreview->setMaximumBlockCount(64);
    m_planPreview->setFixedHeight(88);
    upgradeLayout->addWidget(planLabel, 1, 0);
    upgradeLayout->addWidget(m_planPreview, 1, 1, 1, 2);

    auto *optionLabel = new QLabel(QStringLiteral("校验"), centralWidget);
    m_verifyModeCombo = new QComboBox(centralWidget);
    m_verifyModeCombo->setObjectName(QStringLiteral("verifyModeCombo"));
    m_verifyModeCombo->addItem(QStringLiteral("关键分区"));
    m_verifyModeCombo->addItem(QStringLiteral("全部分区"));
    m_verifyModeCombo->addItem(QStringLiteral("不回读"));
    m_rebootCheckBox = new QCheckBox(QStringLiteral("完成后重启"), centralWidget);
    m_rebootCheckBox->setObjectName(QStringLiteral("rebootAfterFlashCheckBox"));
    m_rebootCheckBox->setChecked(true);

    auto *optionRow = new QHBoxLayout;
    optionRow->setSpacing(10);
    optionRow->addWidget(m_verifyModeCombo);
    optionRow->addWidget(m_rebootCheckBox);
    optionRow->addStretch();
    m_startUpgradeButton = new QPushButton(QStringLiteral("开始升级"), centralWidget);
    m_startUpgradeButton->setObjectName(QStringLiteral("startUpgradeButton"));
    m_startUpgradeButton->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
    m_cancelUpgradeButton = new QPushButton(QStringLiteral("取消"), centralWidget);
    m_cancelUpgradeButton->setObjectName(QStringLiteral("cancelUpgradeButton"));
    m_cancelUpgradeButton->setIcon(style()->standardIcon(QStyle::SP_DialogCancelButton));
    m_cancelUpgradeButton->setEnabled(false);
    optionRow->addWidget(m_startUpgradeButton);
    optionRow->addWidget(m_cancelUpgradeButton);
    upgradeLayout->addWidget(optionLabel, 2, 0);
    upgradeLayout->addLayout(optionRow, 2, 1, 1, 2);

    m_upgradeProgress = new QProgressBar(centralWidget);
    m_upgradeProgress->setObjectName(QStringLiteral("upgradeProgress"));
    m_upgradeProgress->setRange(0, 100);
    m_upgradeProgress->setValue(0);
    m_upgradeProgress->setTextVisible(true);
    m_upgradeStatus = new QLabel(QStringLiteral("未选择固件"), centralWidget);
    m_upgradeStatus->setObjectName(QStringLiteral("upgradeStatus"));
    m_upgradeStatus->setWordWrap(true);
    upgradeLayout->addWidget(m_upgradeProgress, 3, 0, 1, 3);
    upgradeLayout->addWidget(m_upgradeStatus, 4, 0, 1, 3);
    mainLayout->addLayout(upgradeLayout);

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
    connect(m_deviceTable, &QTableWidget::itemSelectionChanged,
            this, &MainWindow::updateUpgradeControls);
    connect(m_chooseImageButton, &QToolButton::clicked,
            this, &MainWindow::chooseFirmwareImage);
    connect(m_startUpgradeButton, &QPushButton::clicked,
            this, &MainWindow::startUpgrade);
    connect(m_cancelUpgradeButton, &QPushButton::clicked,
            this, &MainWindow::cancelUpgrade);
    connect(&m_prepareWatcher,
            &QFutureWatcher<zzrk::workflow::PreparedFirmwareUpgradeResult>::finished,
            this, &MainWindow::handlePrepareFinished);
    connect(&m_upgradeWatcher,
            &QFutureWatcher<zzrk::workflow::FirmwareUpgradeResult>::finished,
            this, &MainWindow::handleUpgradeFinished);

    updateUpgradeControls();

    QTimer::singleShot(0, this, &MainWindow::refreshDevices);
}

MainWindow::~MainWindow()
{
    m_shuttingDown = true;
    if (m_upgradeCancellation) {
        m_upgradeCancellation->store(true);
    }
    m_autoRefreshTimer->stop();
    disconnect(&m_scanWatcher, nullptr, this, nullptr);
    disconnect(&m_prepareWatcher, nullptr, this, nullptr);
    disconnect(&m_upgradeWatcher, nullptr, this, nullptr);
    m_scanWatcher.waitForFinished();
    m_prepareWatcher.waitForFinished();
    m_upgradeWatcher.waitForFinished();
}

void MainWindow::refreshDevices()
{
    if (m_shuttingDown || m_scanWatcher.isRunning()
        || m_upgradeWatcher.isRunning()) {
        return;
    }

    m_autoRefreshTimer->stop();
    m_refreshButton->setEnabled(false);
    statusBar()->showMessage(QStringLiteral("正在扫描 USB 设备..."));
    updateUpgradeControls();

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

    if (m_autoRefreshCheckBox->isChecked()
        && !m_upgradeWatcher.isRunning()) {
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
        statusItem->setData(PortPathRole, portPathText(device));
        statusItem->setData(FlashableRole, isSupportedFlashDevice(device));
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
    updateUpgradeControls();
}

void MainWindow::setAutoRefreshEnabled(bool enabled)
{
    m_autoRefreshTimer->stop();
    if (enabled && m_hasCompletedScan && !m_scanWatcher.isRunning()
        && !m_upgradeWatcher.isRunning()) {
        refreshDevices();
    }
}

QString MainWindow::selectedPortPath() const
{
    const int row = m_deviceTable->currentRow();
    if (row < 0) {
        return {};
    }
    const QTableWidgetItem *item = m_deviceTable->item(row, StatusColumn);
    return item == nullptr ? QString{} : item->data(PortPathRole).toString();
}

void MainWindow::updateUpgradeControls()
{
    const bool preparing = m_prepareWatcher.isRunning();
    const bool upgrading = m_upgradeWatcher.isRunning();
    const bool scanning = m_scanWatcher.isRunning();
    const bool hasPlan = m_preparedUpgrade.has_value();
    const int selectedRow = m_deviceTable->currentRow();
    const QTableWidgetItem *selectedItem =
        selectedRow < 0 ? nullptr
                        : m_deviceTable->item(selectedRow, StatusColumn);
    const bool hasDevice = selectedItem != nullptr
        && selectedItem->data(FlashableRole).toBool()
        && !selectedPortPath().isEmpty();

    m_chooseImageButton->setEnabled(!preparing && !upgrading);
    m_startUpgradeButton->setEnabled(hasPlan && hasDevice && !preparing
                                     && !upgrading && !scanning);
    m_cancelUpgradeButton->setEnabled(upgrading);
    m_verifyModeCombo->setEnabled(!upgrading);
    m_rebootCheckBox->setEnabled(!upgrading);
    m_deviceTable->setEnabled(!upgrading);
    m_autoRefreshCheckBox->setEnabled(!upgrading);
}

void MainWindow::chooseFirmwareImage()
{
    if (m_prepareWatcher.isRunning() || m_upgradeWatcher.isRunning()) {
        return;
    }
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("选择 Rockchip 固件"), {},
        QStringLiteral("Rockchip 固件 (*.img);;所有文件 (*)"));
    if (path.isEmpty()) {
        return;
    }

    m_preparedUpgrade.reset();
    m_imagePathEdit->setText(path);
    m_planPreview->clear();
    m_upgradeStatus->setText(QStringLiteral("正在解析固件并校验 MD5..."));
    m_upgradeProgress->setRange(0, 0);
    updateUpgradeControls();

    const std::filesystem::path imagePath = filesystemPath(path);
    auto future = QtConcurrent::run([imagePath] {
        auto backend = zzrk::workflow::createLibusbFirmwareUpgradeBackend();
        zzrk::qt::FirmwareMd5Verifier verifier;
        zzrk::workflow::FirmwareUpgradeWorkflow workflow{*backend, verifier};
        return workflow.prepare(imagePath);
    });
    m_prepareWatcher.setFuture(future);
    updateUpgradeControls();
}

void MainWindow::handlePrepareFinished()
{
    if (m_shuttingDown) {
        return;
    }
    auto result = m_prepareWatcher.result();
    if (const auto *error =
            std::get_if<zzrk::workflow::FirmwareUpgradeError>(&result)) {
        m_preparedUpgrade.reset();
        m_planPreview->clear();
        m_upgradeProgress->setRange(0, 100);
        m_upgradeProgress->setValue(0);
        m_upgradeStatus->setText(workflowErrorText(*error));
        statusBar()->showMessage(QStringLiteral("固件预检失败"));
        updateUpgradeControls();
        return;
    }

    m_preparedUpgrade =
        std::get<zzrk::workflow::PreparedFirmwareUpgrade>(std::move(result));
    const auto &prepared = *m_preparedUpgrade;
    QStringList lines;
    lines << QStringLiteral("目标：%1 / %2")
                 .arg(QString::fromStdString(prepared.plan.targetChip),
                      QString::fromStdString(prepared.plan.targetModel));
    lines << QStringLiteral("最低 eMMC 扇区：%1；分区数据：%2 字节")
                 .arg(static_cast<qulonglong>(prepared.plan.minimumDeviceSectors))
                 .arg(static_cast<qulonglong>(prepared.plan.totalPartitionBytes));
    lines << QStringLiteral("步骤：%1；MD5：%2")
                 .arg(static_cast<qulonglong>(prepared.plan.steps.size()))
                 .arg(QString::fromLatin1(prepared.md5.data()));
    for (const auto &step : prepared.plan.steps) {
        if (!step.target.empty()) {
            lines << QStringLiteral("- %1（%2 字节）")
                         .arg(QString::fromStdString(step.target))
                         .arg(static_cast<qulonglong>(step.outputSize));
        }
    }
    m_planPreview->setPlainText(lines.join(QLatin1Char('\n')));
    m_upgradeProgress->setRange(0, 100);
    m_upgradeProgress->setValue(0);
    m_upgradeStatus->setText(QStringLiteral("固件预检通过"));
    statusBar()->showMessage(QStringLiteral("升级计划已生成"));
    updateUpgradeControls();
}

void MainWindow::startUpgrade()
{
    if (!m_preparedUpgrade.has_value() || m_upgradeWatcher.isRunning()
        || m_prepareWatcher.isRunning() || m_scanWatcher.isRunning()) {
        return;
    }
    const QString portPath = selectedPortPath();
    if (portPath.isEmpty()) {
        return;
    }
    const auto confirmation = QMessageBox::warning(
        this, QStringLiteral("确认完整升级"),
        QStringLiteral("将向端口 %1 的 eMMC 写入 Loader、GPT 和固件中的全部分区。")
            .arg(portPath),
        QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
    if (confirmation != QMessageBox::Yes) {
        return;
    }

    m_autoRefreshTimer->stop();
    m_refreshButton->setEnabled(false);
    m_upgradeCancellation = std::make_shared<std::atomic_bool>(false);
    m_upgradeProgress->setRange(0, 0);
    m_upgradeStatus->setText(QStringLiteral("正在启动完整升级..."));
    statusBar()->showMessage(QStringLiteral("固件升级进行中"));

    const auto prepared = *m_preparedUpgrade;
    const auto cancellation = m_upgradeCancellation;
    const zzrk::workflow::FirmwareUpgradeRequest request{
        .portPath = portPath.toStdString(),
        .imagePath = prepared.image.sourcePath,
        .verifyMode = verifyModeFromIndex(m_verifyModeCombo->currentIndex()),
        .reboot = m_rebootCheckBox->isChecked(),
        .prepareOnly = false,
    };
    auto future = QtConcurrent::run(
        [this, request, prepared, cancellation]() mutable {
            auto backend =
                zzrk::workflow::createLibusbFirmwareUpgradeBackend();
            zzrk::qt::FirmwareMd5Verifier verifier;
            zzrk::workflow::FirmwareUpgradeWorkflow workflow{*backend,
                                                              verifier};
            const auto progress =
                [this](const zzrk::workflow::FirmwareUpgradeEvent &event) {
                    QMetaObject::invokeMethod(
                        this,
                        [this, event] { handleUpgradeProgress(event); },
                        Qt::QueuedConnection);
                };
            const auto cancelled = [cancellation] {
                return cancellation->load();
            };
            return workflow.execute(request, std::move(prepared), progress,
                                    cancelled);
        });
    m_upgradeWatcher.setFuture(future);
    updateUpgradeControls();
}

void MainWindow::cancelUpgrade()
{
    if (!m_upgradeCancellation || !m_upgradeWatcher.isRunning()) {
        return;
    }
    m_upgradeCancellation->store(true);
    m_cancelUpgradeButton->setEnabled(false);
    m_upgradeStatus->setText(QStringLiteral("正在等待当前 USB 分块完成..."));
}

void MainWindow::handleUpgradeProgress(
    const zzrk::workflow::FirmwareUpgradeEvent &event)
{
    if (m_shuttingDown || !m_upgradeWatcher.isRunning()) {
        return;
    }
    if (event.totalBytes == 0U) {
        m_upgradeProgress->setRange(0, 0);
    } else {
        const auto percent = std::min<std::uint64_t>(
            100U, event.completedBytes * 100U / event.totalBytes);
        m_upgradeProgress->setRange(0, 100);
        m_upgradeProgress->setValue(static_cast<int>(percent));
    }
    const QString stage = QString::fromLatin1(
        zzrk::workflow::toString(event.stage).data(),
        static_cast<qsizetype>(zzrk::workflow::toString(event.stage).size()));
    m_upgradeStatus->setText(
        event.target.empty()
            ? stage
            : QStringLiteral("%1：%2")
                  .arg(stage, QString::fromStdString(event.target)));
}

void MainWindow::handleUpgradeFinished()
{
    if (m_shuttingDown) {
        return;
    }
    const auto result = m_upgradeWatcher.result();
    m_upgradeCancellation.reset();
    m_upgradeProgress->setRange(0, 100);
    m_refreshButton->setEnabled(true);

    if (const auto *error =
            std::get_if<zzrk::workflow::FirmwareUpgradeError>(&result)) {
        const QString message = workflowErrorText(*error);
        m_upgradeStatus->setText(message);
        statusBar()->showMessage(QStringLiteral("固件升级未完成"));
        if (error->code !=
            zzrk::workflow::FirmwareUpgradeErrorCode::cancelled) {
            QMessageBox::critical(this, QStringLiteral("升级失败"), message);
        }
    } else {
        const auto &success =
            std::get<zzrk::workflow::FirmwareUpgradeSuccess>(result);
        m_upgradeProgress->setValue(100);
        const QString message =
            QStringLiteral("升级完成：%1 字节，已校验 %2 个分区")
                .arg(static_cast<qulonglong>(success.partitionBytesWritten))
                .arg(success.partitionsVerified);
        m_upgradeStatus->setText(message);
        statusBar()->showMessage(QStringLiteral("固件升级完成"));
        QMessageBox::information(this, QStringLiteral("升级完成"), message);
    }

    updateUpgradeControls();
    if (m_autoRefreshCheckBox->isChecked()) {
        m_autoRefreshTimer->start();
    }
}
