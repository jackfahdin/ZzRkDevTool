// SPDX-License-Identifier: MIT

#include "MainWindow.hpp"

#include <zzrk/qt/FirmwareUpgradeReportWriter.hpp>

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QGroupBox>
#include <QGridLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>
#include <QMetaObject>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QStringList>
#include <QStyle>
#include <QTableWidget>
#include <QTabWidget>
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
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <sstream>
#include <stdexcept>
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
constexpr int ReportIdRole = Qt::UserRole + 4;
constexpr int ManageableRole = Qt::UserRole + 5;
constexpr int FirstLbaRole = Qt::UserRole + 6;
constexpr int LastLbaRole = Qt::UserRole + 7;
constexpr int DownloadTargetRole = Qt::UserRole + 8;
constexpr int DownloadKindRole = Qt::UserRole + 9;
constexpr int DownloadCapacityRole = Qt::UserRole + 10;

enum DownloadColumn
{
    DownloadSelectedColumn,
    DownloadTargetColumn,
    DownloadLbaColumn,
    DownloadCapacityColumn,
    DownloadFileColumn,
    DownloadFormatColumn,
    DownloadExpandedSizeColumn,
    DownloadVerifyColumn,
    DownloadStatusColumn,
    DownloadColumnCount
};

enum PartitionColumn
{
    PartitionNameColumn,
    PartitionFirstLbaColumn,
    PartitionLastLbaColumn,
    PartitionSectorCountColumn,
    PartitionSizeColumn,
    PartitionAttributesColumn,
    PartitionColumnCount
};

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

bool isSupportedManagementDevice(const zzrk::transport::UsbDeviceInfo &device)
{
    return device.vendorId == 0x2207U && device.productId == 0x350bU
        && device.bcdUsb == 0x0201U && device.bcdDevice == 0x0223U
        && device.mode == zzrk::transport::DeviceMode::Loader;
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

QString dataSizeText(const std::uint64_t bytes)
{
    return QLocale().formattedDataSize(static_cast<qint64>(bytes), 2,
                                       QLocale::DataSizeTraditionalFormat);
}

template <std::size_t Size>
QString hexBytes(const std::array<std::byte, Size> &bytes)
{
    QStringList values;
    values.reserve(static_cast<qsizetype>(Size));
    for (const auto value : bytes) {
        values.append(QStringLiteral("%1")
                          .arg(std::to_integer<unsigned int>(value), 2, 16,
                               QLatin1Char('0')));
    }
    return values.join(QLatin1Char(' ')).toUpper();
}

template <std::size_t Size>
QString asciiBytes(const std::array<std::byte, Size> &bytes)
{
    QByteArray text;
    text.reserve(static_cast<qsizetype>(Size));
    for (const auto value : bytes) {
        const char character = static_cast<char>(
            std::to_integer<unsigned int>(value));
        text.append(character >= 0x20 && character <= 0x7e ? character : '.');
    }
    return QString::fromLatin1(text);
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

QString imageDownloadErrorText(
    const zzrk::workflow::ImageDownloadError &error)
{
    using zzrk::workflow::ImageDownloadErrorCode;
    QString reason;
    switch (error.code) {
    case ImageDownloadErrorCode::cancelled:
        reason = QStringLiteral("操作已取消");
        break;
    case ImageDownloadErrorCode::invalidRequest:
        reason = QStringLiteral("下载计划不完整");
        break;
    case ImageDownloadErrorCode::imageInvalid:
        reason = QStringLiteral("镜像格式无效或与 RK3588 不匹配");
        break;
    case ImageDownloadErrorCode::digestFailed:
        reason = QStringLiteral("无法计算镜像 MD5");
        break;
    case ImageDownloadErrorCode::digestChanged:
        reason = QStringLiteral("镜像在执行前发生变化");
        break;
    case ImageDownloadErrorCode::layoutInvalid:
        reason = QStringLiteral("分区布局无效");
        break;
    case ImageDownloadErrorCode::scanFailed:
    case ImageDownloadErrorCode::deviceNotFound:
    case ImageDownloadErrorCode::deviceAmbiguous:
    case ImageDownloadErrorCode::deviceUnsupported:
        reason = QStringLiteral("目标 USB 设备不可用");
        break;
    case ImageDownloadErrorCode::loaderRequired:
        reason = QStringLiteral("Maskrom 模式必须选择 MiniLoaderAll.bin");
        break;
    case ImageDownloadErrorCode::openFailed:
        reason = QStringLiteral("无法打开 RockUSB 接口");
        break;
    case ImageDownloadErrorCode::loaderResetFailed:
    case ImageDownloadErrorCode::reenumerationTimedOut:
    case ImageDownloadErrorCode::loaderDownloadFailed:
    case ImageDownloadErrorCode::loaderNotReady:
        reason = QStringLiteral("RAM Loader 准备失败");
        break;
    case ImageDownloadErrorCode::wrongChip:
        reason = QStringLiteral("目标芯片不是 RK3588");
        break;
    case ImageDownloadErrorCode::storageCheckFailed:
    case ImageDownloadErrorCode::storageNotEmmc:
    case ImageDownloadErrorCode::deviceTooSmall:
        reason = QStringLiteral("eMMC 检查失败或空间不足");
        break;
    case ImageDownloadErrorCode::deviceLayoutChanged:
        reason = QStringLiteral("设备 GPT 在准备后发生变化");
        break;
    case ImageDownloadErrorCode::flashFailed:
        reason = QStringLiteral("镜像写入或回读校验失败");
        break;
    case ImageDownloadErrorCode::resetFailed:
        reason = QStringLiteral("镜像已写入，但设备复位失败");
        break;
    case ImageDownloadErrorCode::closeFailed:
        reason = QStringLiteral("无法释放 RockUSB 接口");
        break;
    }
    if (!error.target.empty()) {
        reason += QStringLiteral("（%1）")
                      .arg(QString::fromStdString(error.target));
    }
    return reason;
}

QString imageDownloadStageText(const zzrk::workflow::ImageDownloadStage stage)
{
    using zzrk::workflow::ImageDownloadStage;
    switch (stage) {
    case ImageDownloadStage::inspectFiles:
        return QStringLiteral("检查镜像");
    case ImageDownloadStage::verifyFiles:
        return QStringLiteral("校验 MD5");
    case ImageDownloadStage::selectDevice:
        return QStringLiteral("选择设备");
    case ImageDownloadStage::enterMaskrom:
        return QStringLiteral("进入 Maskrom");
    case ImageDownloadStage::waitForMaskrom:
        return QStringLiteral("等待 Maskrom");
    case ImageDownloadStage::downloadLoader:
        return QStringLiteral("下载 RAM Loader");
    case ImageDownloadStage::waitForRamLoader:
        return QStringLiteral("等待 RAM Loader");
    case ImageDownloadStage::checkRamLoader:
        return QStringLiteral("检查 RAM Loader");
    case ImageDownloadStage::checkStorage:
        return QStringLiteral("检查 eMMC");
    case ImageDownloadStage::validateLayout:
        return QStringLiteral("核对 GPT");
    case ImageDownloadStage::erase:
        return QStringLiteral("擦除");
    case ImageDownloadStage::write:
        return QStringLiteral("写入");
    case ImageDownloadStage::verify:
        return QStringLiteral("回读校验");
    case ImageDownloadStage::reset:
        return QStringLiteral("复位设备");
    }
    return QStringLiteral("处理");
}

QString payloadFormatText(const zzrk::image::PayloadFormat format)
{
    switch (format) {
    case zzrk::image::PayloadFormat::androidSparse:
        return QStringLiteral("Android Sparse");
    case zzrk::image::PayloadFormat::rockchipParameter:
        return QStringLiteral("parameter.txt");
    case zzrk::image::PayloadFormat::raw:
        return QStringLiteral("Raw");
    case zzrk::image::PayloadFormat::unknown:
        break;
    }
    return QStringLiteral("-");
}

std::string slotBaseTarget(const std::string &target)
{
    if (target.size() > 2U
        && (target.ends_with("_a") || target.ends_with("_b"))) {
        return target.substr(0U, target.size() - 2U);
    }
    return target;
}

zzrk::workflow::ImageDownloadError unexpectedImageDownloadError(
    const std::string &target)
{
    return zzrk::workflow::ImageDownloadError{
        .code = zzrk::workflow::ImageDownloadErrorCode::invalidRequest,
        .stage = zzrk::workflow::ImageDownloadStage::inspectFiles,
        .target = target,
        .completedPlanBytes = 0U,
        .itemIndex = {},
        .imageError = {},
        .digestError = {},
        .scanError = {},
        .openError = {},
        .loaderError = {},
        .sessionError = {},
        .gptError = {},
        .flashError = {},
    };
}

QString deviceManagementErrorText(
    const zzrk::workflow::DeviceManagementError &error)
{
    using zzrk::workflow::DeviceManagementErrorCode;
    QString reason;
    switch (error.code) {
    case DeviceManagementErrorCode::cancelled:
        reason = QStringLiteral("操作已取消");
        break;
    case DeviceManagementErrorCode::scanFailed:
    case DeviceManagementErrorCode::deviceNotFound:
    case DeviceManagementErrorCode::deviceAmbiguous:
        reason = QStringLiteral("目标 USB 设备不可用");
        break;
    case DeviceManagementErrorCode::deviceUnsupported:
        reason = QStringLiteral("分区管理只支持已验证的 RK3588 Loader 模式");
        break;
    case DeviceManagementErrorCode::openFailed:
        reason = QStringLiteral("无法打开 RockUSB 接口");
        break;
    case DeviceManagementErrorCode::chipInfoFailed:
        reason = QStringLiteral("读取 ChipInfo 失败");
        break;
    case DeviceManagementErrorCode::capabilityFailed:
        reason = QStringLiteral("读取 Capability 失败");
        break;
    case DeviceManagementErrorCode::flashInfoFailed:
        reason = QStringLiteral("读取 FlashInfo 失败");
        break;
    case DeviceManagementErrorCode::flashIdFailed:
        reason = QStringLiteral("读取 FlashID 失败");
        break;
    case DeviceManagementErrorCode::storageNotEmmc:
        reason = QStringLiteral("目标存储不是 eMMC");
        break;
    case DeviceManagementErrorCode::gptFailed:
        reason = QStringLiteral("读取或校验 GPT 失败");
        break;
    case DeviceManagementErrorCode::partitionNotFound:
    case DeviceManagementErrorCode::partitionAmbiguous:
        reason = QStringLiteral("目标分区不存在或名称不唯一");
        break;
    case DeviceManagementErrorCode::inputInvalid:
        reason = QStringLiteral("输入镜像无效");
        break;
    case DeviceManagementErrorCode::inputTooLarge:
        reason = QStringLiteral("输入镜像大于目标分区");
        break;
    case DeviceManagementErrorCode::inputOpenFailed:
    case DeviceManagementErrorCode::inputReadFailed:
        reason = QStringLiteral("读取输入镜像失败");
        break;
    case DeviceManagementErrorCode::outputExists:
        reason = QStringLiteral("输出文件已存在，未覆盖");
        break;
    case DeviceManagementErrorCode::outputOpenFailed:
    case DeviceManagementErrorCode::outputWriteFailed:
    case DeviceManagementErrorCode::outputFinalizeFailed:
        reason = QStringLiteral("保存分区备份失败");
        break;
    case DeviceManagementErrorCode::readFailed:
        reason = QStringLiteral("读取 eMMC 分区失败");
        break;
    case DeviceManagementErrorCode::writeFailed:
        reason = QStringLiteral("写入 eMMC 分区失败");
        break;
    case DeviceManagementErrorCode::verifyFailed:
        reason = QStringLiteral("分区回读校验失败");
        break;
    case DeviceManagementErrorCode::eraseFailed:
        reason = QStringLiteral("擦除 eMMC 分区失败");
        break;
    case DeviceManagementErrorCode::closeFailed:
        reason = QStringLiteral("无法释放 RockUSB 接口");
        break;
    }
    if (!error.target.empty()) {
        reason += QStringLiteral("（%1）").arg(QString::fromStdString(error.target));
    }
    return reason;
}

zzrk::workflow::DeviceManagementError unexpectedDeviceManagementError(
    const std::string &target)
{
    return zzrk::workflow::DeviceManagementError{
        .code = zzrk::workflow::DeviceManagementErrorCode::openFailed,
        .target = target,
        .completedBytes = 0U,
        .totalBytes = 0U,
        .scanError = {},
        .openError = {},
        .sessionError = {},
        .gptError = {},
    };
}

QString partitionPhaseText(
    const zzrk::workflow::PartitionOperationPhase phase)
{
    using zzrk::workflow::PartitionOperationPhase;
    switch (phase) {
    case PartitionOperationPhase::read:
        return QStringLiteral("备份");
    case PartitionOperationPhase::write:
        return QStringLiteral("写入");
    case PartitionOperationPhase::verify:
        return QStringLiteral("校验");
    case PartitionOperationPhase::erase:
        return QStringLiteral("擦除");
    }
    return QStringLiteral("操作");
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

QString filesystemPathText(const std::filesystem::path &path)
{
#ifdef _WIN32
    return QString::fromStdWString(path.wstring());
#else
    const std::string text = path.string();
    return QString::fromUtf8(text.data(),
                             static_cast<qsizetype>(text.size()));
#endif
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : MainWindow(zzrk::gui::createGuiOperationBackend(), true, parent)
{
}

MainWindow::MainWindow(
    std::shared_ptr<zzrk::gui::IGuiOperationBackend> backend,
    const bool showModalDialogs,
    QWidget *parent)
    : QMainWindow(parent)
    , m_backend(std::move(backend))
    , m_workspaceController(
          std::make_unique<zzrk::gui::MainWorkspaceController>(
              m_backend, showModalDialogs))
    , m_showModalDialogs(showModalDialogs)
{
    if (!m_backend) {
        throw std::invalid_argument("MainWindow requires a GUI backend");
    }
    setWindowTitle(QStringLiteral("ZzRkDevTool"));
    resize(980, 820);
    setMinimumSize(800, 700);

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
    m_connectionText->setProperty("hotplugAvailable", true);
    QFont connectionFont = m_connectionText->font();
    connectionFont.setBold(true);
    m_connectionText->setFont(connectionFont);

    m_refreshButton = new QToolButton(centralWidget);
    m_refreshButton->setObjectName(QStringLiteral("refreshButton"));
    m_refreshButton->setIcon(style()->standardIcon(QStyle::SP_BrowserReload));
    m_refreshButton->setToolTip(QStringLiteral("立即刷新设备列表"));
    m_refreshButton->setAccessibleName(QStringLiteral("刷新设备列表"));
    m_refreshButton->setFixedSize(30, 30);

    toolRow->addWidget(m_connectionIcon);
    toolRow->addWidget(m_connectionText);
    toolRow->addStretch();
    toolRow->addWidget(m_refreshButton);
    mainLayout->addLayout(toolRow);

    auto *separator = new QFrame(centralWidget);
    separator->setFrameShape(QFrame::HLine);
    separator->setFrameShadow(QFrame::Sunken);
    mainLayout->addWidget(separator);

    m_tabs = new QTabWidget(centralWidget);
    m_tabs->setObjectName(QStringLiteral("mainTabs"));
    auto *downloadTab = new QWidget(m_tabs);
    auto *deviceTab = new QWidget(m_tabs);
    auto *upgradeTab = new QWidget(m_tabs);
    auto *partitionTab = new QWidget(m_tabs);
    auto *downloadLayout = new QVBoxLayout(downloadTab);
    auto *deviceLayout = new QVBoxLayout(deviceTab);
    auto *upgradePageLayout = new QVBoxLayout(upgradeTab);
    auto *partitionLayout = new QVBoxLayout(partitionTab);
    downloadLayout->setContentsMargins(10, 10, 10, 10);
    deviceLayout->setContentsMargins(10, 10, 10, 10);
    upgradePageLayout->setContentsMargins(10, 10, 10, 10);
    partitionLayout->setContentsMargins(10, 10, 10, 10);
    m_tabs->addTab(downloadTab, QStringLiteral("下载镜像"));
    m_tabs->addTab(deviceTab, QStringLiteral("设备"));
    m_tabs->addTab(upgradeTab, QStringLiteral("完整升级"));
    m_tabs->addTab(partitionTab, QStringLiteral("分区"));

    auto *downloadSourceLayout = new QGridLayout;
    downloadSourceLayout->setHorizontalSpacing(8);
    downloadSourceLayout->setVerticalSpacing(8);
    downloadSourceLayout->setColumnStretch(1, 1);
    downloadSourceLayout->setColumnStretch(4, 1);

    m_downloadDeviceCombo = new QComboBox(downloadTab);
    m_downloadDeviceCombo->setObjectName(
        QStringLiteral("downloadDeviceCombo"));
    m_downloadDeviceCombo->setMinimumContentsLength(18);
    downloadSourceLayout->addWidget(
        new QLabel(QStringLiteral("目标设备"), downloadTab), 0, 0);
    downloadSourceLayout->addWidget(m_downloadDeviceCombo, 0, 1, 1, 2);

    m_downloadLayoutCombo = new QComboBox(downloadTab);
    m_downloadLayoutCombo->setObjectName(
        QStringLiteral("downloadLayoutCombo"));
    m_downloadLayoutCombo->addItem(QStringLiteral("设备 GPT"));
    m_downloadLayoutCombo->addItem(QStringLiteral("parameter.txt"));
    downloadSourceLayout->addWidget(
        new QLabel(QStringLiteral("分区布局"), downloadTab), 0, 3);
    downloadSourceLayout->addWidget(m_downloadLayoutCombo, 0, 4);

    m_readDownloadGptButton = new QToolButton(downloadTab);
    m_readDownloadGptButton->setObjectName(
        QStringLiteral("readDownloadGptButton"));
    m_readDownloadGptButton->setIcon(
        style()->standardIcon(QStyle::SP_BrowserReload));
    m_readDownloadGptButton->setToolTip(QStringLiteral("读取设备 GPT"));
    m_readDownloadGptButton->setAccessibleName(QStringLiteral("读取设备 GPT"));
    m_readDownloadGptButton->setFixedSize(30, 30);
    downloadSourceLayout->addWidget(m_readDownloadGptButton, 0, 5);

    m_downloadLayoutPathEdit = new QLineEdit(downloadTab);
    m_downloadLayoutPathEdit->setObjectName(
        QStringLiteral("downloadParameterPath"));
    m_downloadLayoutPathEdit->setReadOnly(true);
    m_downloadLayoutPathEdit->setPlaceholderText(
        QStringLiteral("设备 GPT 将在读取后显示，或选择 parameter.txt"));
    m_chooseDownloadParameterButton = new QToolButton(downloadTab);
    m_chooseDownloadParameterButton->setObjectName(
        QStringLiteral("chooseDownloadParameterButton"));
    m_chooseDownloadParameterButton->setIcon(
        style()->standardIcon(QStyle::SP_DirOpenIcon));
    m_chooseDownloadParameterButton->setToolTip(
        QStringLiteral("选择 parameter.txt"));
    m_chooseDownloadParameterButton->setAccessibleName(
        QStringLiteral("选择 parameter.txt"));
    m_chooseDownloadParameterButton->setFixedSize(30, 30);
    downloadSourceLayout->addWidget(
        new QLabel(QStringLiteral("布局文件"), downloadTab), 1, 0);
    downloadSourceLayout->addWidget(m_downloadLayoutPathEdit, 1, 1, 1, 4);
    downloadSourceLayout->addWidget(m_chooseDownloadParameterButton, 1, 5);

    m_downloadDirectoryEdit = new QLineEdit(downloadTab);
    m_downloadDirectoryEdit->setObjectName(
        QStringLiteral("downloadImageDirectory"));
    m_downloadDirectoryEdit->setReadOnly(true);
    m_downloadDirectoryEdit->setPlaceholderText(
        QStringLiteral("选择镜像目录以自动匹配分区"));
    m_chooseDownloadDirectoryButton = new QToolButton(downloadTab);
    m_chooseDownloadDirectoryButton->setObjectName(
        QStringLiteral("chooseDownloadDirectoryButton"));
    m_chooseDownloadDirectoryButton->setIcon(
        style()->standardIcon(QStyle::SP_DirOpenIcon));
    m_chooseDownloadDirectoryButton->setToolTip(QStringLiteral("选择镜像目录"));
    m_chooseDownloadDirectoryButton->setAccessibleName(
        QStringLiteral("选择镜像目录"));
    m_chooseDownloadDirectoryButton->setFixedSize(30, 30);
    downloadSourceLayout->addWidget(
        new QLabel(QStringLiteral("镜像目录"), downloadTab), 2, 0);
    downloadSourceLayout->addWidget(m_downloadDirectoryEdit, 2, 1, 1, 4);
    downloadSourceLayout->addWidget(m_chooseDownloadDirectoryButton, 2, 5);
    downloadLayout->addLayout(downloadSourceLayout);

    m_downloadTable =
        new QTableWidget(0, DownloadColumnCount, downloadTab);
    m_downloadTable->setObjectName(QStringLiteral("downloadImageTable"));
    m_downloadTable->setHorizontalHeaderLabels({
        QStringLiteral("选择"), QStringLiteral("目标"),
        QStringLiteral("起始 LBA"), QStringLiteral("容量"),
        QStringLiteral("镜像文件"), QStringLiteral("格式"),
        QStringLiteral("展开大小"), QStringLiteral("校验"),
        QStringLiteral("状态"),
    });
    m_downloadTable->setAlternatingRowColors(true);
    m_downloadTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_downloadTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_downloadTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_downloadTable->setShowGrid(false);
    m_downloadTable->verticalHeader()->setVisible(false);
    m_downloadTable->verticalHeader()->setDefaultSectionSize(30);
    auto *downloadHeader = m_downloadTable->horizontalHeader();
    downloadHeader->setSectionResizeMode(DownloadSelectedColumn,
                                          QHeaderView::ResizeToContents);
    downloadHeader->setSectionResizeMode(DownloadTargetColumn,
                                          QHeaderView::ResizeToContents);
    downloadHeader->setSectionResizeMode(DownloadLbaColumn,
                                          QHeaderView::ResizeToContents);
    downloadHeader->setSectionResizeMode(DownloadCapacityColumn,
                                          QHeaderView::ResizeToContents);
    downloadHeader->setSectionResizeMode(DownloadFileColumn,
                                          QHeaderView::Stretch);
    downloadHeader->setSectionResizeMode(DownloadFormatColumn,
                                          QHeaderView::ResizeToContents);
    downloadHeader->setSectionResizeMode(DownloadExpandedSizeColumn,
                                          QHeaderView::ResizeToContents);
    downloadHeader->setSectionResizeMode(DownloadVerifyColumn,
                                          QHeaderView::ResizeToContents);
    downloadHeader->setSectionResizeMode(DownloadStatusColumn,
                                          QHeaderView::ResizeToContents);
    downloadLayout->addWidget(m_downloadTable, 1);

    auto *downloadActions = new QHBoxLayout;
    m_chooseDownloadImageButton =
        new QPushButton(QStringLiteral("选择镜像"), downloadTab);
    m_chooseDownloadImageButton->setObjectName(
        QStringLiteral("chooseDownloadImageButton"));
    m_chooseDownloadImageButton->setIcon(
        style()->standardIcon(QStyle::SP_DirOpenIcon));
    m_clearDownloadImageButton =
        new QPushButton(QStringLiteral("清除"), downloadTab);
    m_clearDownloadImageButton->setObjectName(
        QStringLiteral("clearDownloadImageButton"));
    m_clearDownloadImageButton->setIcon(
        style()->standardIcon(QStyle::SP_DialogResetButton));
    m_downloadRebootCheckBox =
        new QCheckBox(QStringLiteral("完成后重启"), downloadTab);
    m_downloadRebootCheckBox->setObjectName(
        QStringLiteral("downloadRebootCheckBox"));
    m_downloadRebootCheckBox->setChecked(true);
    m_startDownloadButton =
        new QPushButton(QStringLiteral("开始下载"), downloadTab);
    m_startDownloadButton->setObjectName(
        QStringLiteral("startImageDownloadButton"));
    m_startDownloadButton->setIcon(
        style()->standardIcon(QStyle::SP_MediaPlay));
    m_cancelDownloadButton = new QPushButton(QStringLiteral("取消"), downloadTab);
    m_cancelDownloadButton->setObjectName(
        QStringLiteral("cancelImageDownloadButton"));
    m_cancelDownloadButton->setIcon(
        style()->standardIcon(QStyle::SP_DialogCancelButton));
    downloadActions->addWidget(m_chooseDownloadImageButton);
    downloadActions->addWidget(m_clearDownloadImageButton);
    downloadActions->addWidget(m_downloadRebootCheckBox);
    downloadActions->addStretch();
    downloadActions->addWidget(m_startDownloadButton);
    downloadActions->addWidget(m_cancelDownloadButton);
    downloadLayout->addLayout(downloadActions);

    m_downloadOverallProgress = new QProgressBar(downloadTab);
    m_downloadOverallProgress->setObjectName(
        QStringLiteral("overallImageDownloadProgress"));
    m_downloadOverallProgress->setRange(0, 100);
    m_downloadOverallProgress->setValue(0);
    m_downloadOverallProgress->setFormat(QStringLiteral("完整计划 %p%"));
    m_downloadOperationProgress = new QProgressBar(downloadTab);
    m_downloadOperationProgress->setObjectName(
        QStringLiteral("operationImageDownloadProgress"));
    m_downloadOperationProgress->setRange(0, 100);
    m_downloadOperationProgress->setValue(0);
    m_downloadOperationProgress->setFormat(QStringLiteral("当前分区 %p%"));
    m_downloadStatus = new QLabel(QStringLiteral("请选择目标设备和分区布局"),
                                  downloadTab);
    m_downloadStatus->setObjectName(QStringLiteral("imageDownloadStatus"));
    m_downloadStatus->setWordWrap(true);
    downloadLayout->addWidget(m_downloadOverallProgress);
    downloadLayout->addWidget(m_downloadOperationProgress);
    downloadLayout->addWidget(m_downloadStatus);

    m_deviceTable = new QTableWidget(0, ColumnCount, deviceTab);
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
    deviceLayout->addWidget(m_deviceTable, 1);

    auto *detailGroup = new QGroupBox(QStringLiteral("所选设备详情"), deviceTab);
    auto *detailLayout = new QGridLayout(detailGroup);
    detailLayout->setColumnStretch(1, 1);
    m_inspectDeviceButton = new QPushButton(QStringLiteral("读取详情"), detailGroup);
    m_inspectDeviceButton->setObjectName(QStringLiteral("inspectDeviceButton"));
    m_inspectDeviceButton->setIcon(
        style()->standardIcon(QStyle::SP_FileDialogInfoView));
    m_deviceDetailStatus = new QLabel(QStringLiteral("请选择 Loader 设备"), detailGroup);
    m_deviceDetailStatus->setObjectName(QStringLiteral("deviceDetailStatus"));
    m_deviceDetailStatus->setWordWrap(true);
    auto *detailActionRow = new QHBoxLayout;
    detailActionRow->addWidget(m_inspectDeviceButton);
    detailActionRow->addWidget(m_deviceDetailStatus, 1);
    detailLayout->addLayout(detailActionRow, 0, 0, 1, 4);

    const auto addDetail = [detailGroup, detailLayout](
                               const QString &name, QLabel *&value,
                               const int row, const int column) {
        auto *label = new QLabel(name, detailGroup);
        value = new QLabel(QStringLiteral("-"), detailGroup);
        value->setTextInteractionFlags(Qt::TextSelectableByMouse);
        value->setWordWrap(true);
        detailLayout->addWidget(label, row, column);
        detailLayout->addWidget(value, row, column + 1);
    };
    addDetail(QStringLiteral("ChipInfo"), m_chipInfoValue, 1, 0);
    addDetail(QStringLiteral("Capability"), m_capabilityValue, 1, 2);
    addDetail(QStringLiteral("FlashID"), m_flashIdValue, 2, 0);
    addDetail(QStringLiteral("eMMC 容量"), m_capacityValue, 2, 2);
    addDetail(QStringLiteral("FlashInfo"), m_flashInfoValue, 3, 0);
    addDetail(QStringLiteral("USB 接口"), m_usbInterfaceValue, 3, 2);
    deviceLayout->addWidget(detailGroup);

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

    auto *overallLabel = new QLabel(QStringLiteral("总体"), centralWidget);
    m_overallProgress = new QProgressBar(centralWidget);
    m_overallProgress->setObjectName(QStringLiteral("overallUpgradeProgress"));
    m_overallProgress->setRange(0, 100);
    m_overallProgress->setValue(0);
    m_overallProgress->setFormat(QStringLiteral("总体 %p%"));
    m_overallProgress->setTextVisible(true);
    auto *operationLabel = new QLabel(QStringLiteral("当前"), centralWidget);
    m_operationProgress = new QProgressBar(centralWidget);
    m_operationProgress->setObjectName(QStringLiteral("operationUpgradeProgress"));
    m_operationProgress->setRange(0, 100);
    m_operationProgress->setValue(0);
    m_operationProgress->setFormat(QStringLiteral("当前 %p%"));
    m_operationProgress->setTextVisible(true);
    m_upgradeStatus = new QLabel(QStringLiteral("未选择固件"), centralWidget);
    m_upgradeStatus->setObjectName(QStringLiteral("upgradeStatus"));
    m_upgradeStatus->setWordWrap(true);
    upgradeLayout->addWidget(overallLabel, 3, 0);
    upgradeLayout->addWidget(m_overallProgress, 3, 1, 1, 2);
    upgradeLayout->addWidget(operationLabel, 4, 0);
    upgradeLayout->addWidget(m_operationProgress, 4, 1, 1, 2);
    upgradeLayout->addWidget(m_upgradeStatus, 5, 0, 1, 3);
    upgradePageLayout->addLayout(upgradeLayout);

    auto *historyRow = new QHBoxLayout;
    auto *historyLabel = new QLabel(QStringLiteral("任务历史（本次会话）"),
                                    centralWidget);
    m_exportReportButton = new QToolButton(centralWidget);
    m_exportReportButton->setObjectName(QStringLiteral("exportReportButton"));
    m_exportReportButton->setIcon(
        style()->standardIcon(QStyle::SP_DialogSaveButton));
    m_exportReportButton->setToolTip(QStringLiteral("导出所选任务报告"));
    m_exportReportButton->setAccessibleName(QStringLiteral("导出任务报告"));
    m_exportReportButton->setFixedSize(30, 30);
    m_exportReportButton->setEnabled(false);
    historyRow->addWidget(historyLabel);
    historyRow->addStretch();
    historyRow->addWidget(m_exportReportButton);
    m_historyTable = new QTableWidget(0, 4, centralWidget);
    m_historyTable->setObjectName(QStringLiteral("upgradeHistoryTable"));
    m_historyTable->setHorizontalHeaderLabels({
        QStringLiteral("开始时间"),
        QStringLiteral("端口"),
        QStringLiteral("固件"),
        QStringLiteral("结果"),
    });
    m_historyTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_historyTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_historyTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_historyTable->setShowGrid(false);
    m_historyTable->setAlternatingRowColors(true);
    m_historyTable->verticalHeader()->setVisible(false);
    m_historyTable->verticalHeader()->setDefaultSectionSize(28);
    m_historyTable->setFixedHeight(126);
    auto *historyHeader = m_historyTable->horizontalHeader();
    historyHeader->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    historyHeader->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    historyHeader->setSectionResizeMode(2, QHeaderView::Stretch);
    historyHeader->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    upgradePageLayout->addLayout(historyRow);
    upgradePageLayout->addWidget(m_historyTable);
    upgradePageLayout->addStretch();

    auto *partitionToolbar = new QHBoxLayout;
    m_readPartitionsButton = new QToolButton(partitionTab);
    m_readPartitionsButton->setObjectName(QStringLiteral("readPartitionsButton"));
    m_readPartitionsButton->setIcon(
        style()->standardIcon(QStyle::SP_BrowserReload));
    m_readPartitionsButton->setToolTip(QStringLiteral("重新读取并校验 GPT"));
    m_readPartitionsButton->setAccessibleName(QStringLiteral("读取 GPT 分区表"));
    m_readPartitionsButton->setFixedSize(30, 30);
    auto *partitionHint = new QLabel(
        QStringLiteral("目标设备使用“设备”页当前选中的 Loader"), partitionTab);
    partitionToolbar->addWidget(m_readPartitionsButton);
    partitionToolbar->addWidget(partitionHint);
    partitionToolbar->addStretch();
    partitionLayout->addLayout(partitionToolbar);

    m_partitionTable =
        new QTableWidget(0, PartitionColumnCount, partitionTab);
    m_partitionTable->setObjectName(QStringLiteral("partitionTable"));
    m_partitionTable->setHorizontalHeaderLabels({
        QStringLiteral("分区名"), QStringLiteral("起始 LBA"),
        QStringLiteral("结束 LBA"), QStringLiteral("扇区数"),
        QStringLiteral("容量"), QStringLiteral("属性"),
    });
    m_partitionTable->setAlternatingRowColors(true);
    m_partitionTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_partitionTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_partitionTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_partitionTable->setShowGrid(false);
    m_partitionTable->setSortingEnabled(true);
    m_partitionTable->verticalHeader()->setVisible(false);
    m_partitionTable->verticalHeader()->setDefaultSectionSize(30);
    auto *partitionHeader = m_partitionTable->horizontalHeader();
    partitionHeader->setSectionResizeMode(PartitionNameColumn,
                                           QHeaderView::Stretch);
    for (int column = PartitionFirstLbaColumn;
         column < PartitionColumnCount; ++column) {
        partitionHeader->setSectionResizeMode(column,
                                               QHeaderView::ResizeToContents);
    }
    partitionLayout->addWidget(m_partitionTable, 1);

    auto *partitionActions = new QHBoxLayout;
    m_verifyPartitionWriteCheckBox =
        new QCheckBox(QStringLiteral("写入后逐块回读校验"), partitionTab);
    m_verifyPartitionWriteCheckBox->setObjectName(
        QStringLiteral("verifyPartitionWriteCheckBox"));
    m_verifyPartitionWriteCheckBox->setChecked(true);
    m_backupPartitionButton =
        new QPushButton(QStringLiteral("备份分区"), partitionTab);
    m_backupPartitionButton->setObjectName(
        QStringLiteral("backupPartitionButton"));
    m_backupPartitionButton->setIcon(
        style()->standardIcon(QStyle::SP_DialogSaveButton));
    m_writePartitionButton =
        new QPushButton(QStringLiteral("写入镜像"), partitionTab);
    m_writePartitionButton->setObjectName(
        QStringLiteral("writePartitionButton"));
    m_writePartitionButton->setIcon(
        style()->standardIcon(QStyle::SP_ArrowDown));
    m_erasePartitionButton =
        new QPushButton(QStringLiteral("擦除分区"), partitionTab);
    m_erasePartitionButton->setObjectName(
        QStringLiteral("erasePartitionButton"));
    m_erasePartitionButton->setIcon(
        style()->standardIcon(QStyle::SP_TrashIcon));
    m_cancelPartitionButton =
        new QPushButton(QStringLiteral("取消"), partitionTab);
    m_cancelPartitionButton->setObjectName(
        QStringLiteral("cancelPartitionButton"));
    m_cancelPartitionButton->setIcon(
        style()->standardIcon(QStyle::SP_DialogCancelButton));
    partitionActions->addWidget(m_verifyPartitionWriteCheckBox);
    partitionActions->addStretch();
    partitionActions->addWidget(m_backupPartitionButton);
    partitionActions->addWidget(m_writePartitionButton);
    partitionActions->addWidget(m_erasePartitionButton);
    partitionActions->addWidget(m_cancelPartitionButton);
    partitionLayout->addLayout(partitionActions);

    m_partitionProgress = new QProgressBar(partitionTab);
    m_partitionProgress->setObjectName(QStringLiteral("partitionProgress"));
    m_partitionProgress->setRange(0, 100);
    m_partitionProgress->setValue(0);
    m_partitionProgress->setFormat(QStringLiteral("当前操作 %p%"));
    m_partitionStatus = new QLabel(QStringLiteral("尚未读取分区表"), partitionTab);
    m_partitionStatus->setObjectName(QStringLiteral("partitionStatus"));
    m_partitionStatus->setWordWrap(true);
    partitionLayout->addWidget(m_partitionProgress);
    partitionLayout->addWidget(m_partitionStatus);

    mainLayout->addWidget(m_tabs, 1);

    setCentralWidget(centralWidget);

    m_lastScanText = new QLabel(QStringLiteral("尚未扫描"), this);
    m_lastScanText->setObjectName(QStringLiteral("lastScanText"));
    statusBar()->addPermanentWidget(m_lastScanText);
    statusBar()->showMessage(QStringLiteral("准备就绪"));

    m_hotplugDebounceTimer = new QTimer(this);
    m_hotplugDebounceTimer->setInterval(150);
    m_hotplugDebounceTimer->setSingleShot(true);

    connect(m_refreshButton, &QToolButton::clicked, this, &MainWindow::refreshDevices);
    connect(m_hotplugDebounceTimer, &QTimer::timeout,
            this, &MainWindow::processPendingDeviceRefresh);
    connect(&m_scanWatcher,
            &QFutureWatcher<zzrk::transport::ScanResult>::finished,
            this,
            &MainWindow::handleScanFinished);
    connect(m_deviceTable, &QTableWidget::itemSelectionChanged,
            this, &MainWindow::handleDeviceSelectionChanged);
    connect(m_downloadDeviceCombo, &QComboBox::currentIndexChanged,
            this, &MainWindow::handleDownloadDeviceChanged);
    connect(m_downloadLayoutCombo, &QComboBox::currentIndexChanged,
            this, &MainWindow::handleDownloadLayoutChanged);
    connect(m_chooseDownloadParameterButton, &QToolButton::clicked,
            this, &MainWindow::chooseDownloadParameter);
    connect(m_readDownloadGptButton, &QToolButton::clicked,
            this, [this] {
                m_downloadStatus->setText(
                    QStringLiteral("正在读取并校验设备 GPT..."));
                readSelectedDevicePartitions();
            });
    connect(m_chooseDownloadDirectoryButton, &QToolButton::clicked,
            this, &MainWindow::chooseDownloadDirectory);
    connect(m_chooseDownloadImageButton, &QPushButton::clicked,
            this, &MainWindow::chooseSelectedDownloadImage);
    connect(m_clearDownloadImageButton, &QPushButton::clicked,
            this, &MainWindow::clearSelectedDownloadImage);
    connect(m_downloadTable, &QTableWidget::itemSelectionChanged,
            this, &MainWindow::updateDownloadControls);
    connect(m_downloadTable, &QTableWidget::itemChanged,
            this, [this](QTableWidgetItem *item) {
                if (item != nullptr
                    && (item->column() == DownloadSelectedColumn
                        || item->column() == DownloadVerifyColumn)) {
                    const auto *target = m_downloadTable->item(
                        item->row(), DownloadTargetColumn);
                    if (target != nullptr) {
                        auto &states = item->column() == DownloadSelectedColumn
                            ? m_downloadSelections
                            : m_downloadVerifications;
                        states.insert_or_assign(
                            target->data(DownloadTargetRole)
                                .toString()
                                .toStdString(),
                            item->checkState() == Qt::Checked);
                    }
                }
                updateDownloadControls();
            });
    connect(m_startDownloadButton, &QPushButton::clicked,
            this, &MainWindow::startImageDownload);
    connect(m_cancelDownloadButton, &QPushButton::clicked,
            this, &MainWindow::cancelImageDownload);
    connect(&m_downloadPrepareWatcher,
            &QFutureWatcher<zzrk::workflow::PreparedImageDownloadResult>::finished,
            this, &MainWindow::handleDownloadPrepareFinished);
    connect(&m_imageDownloadWatcher,
            &QFutureWatcher<zzrk::workflow::ImageDownloadResult>::finished,
            this, &MainWindow::handleImageDownloadFinished);
    connect(m_inspectDeviceButton, &QPushButton::clicked,
            this, &MainWindow::inspectSelectedDevice);
    connect(&m_inspectionWatcher,
            &QFutureWatcher<zzrk::workflow::DeviceInspectionResult>::finished,
            this, &MainWindow::handleInspectionFinished);
    connect(m_readPartitionsButton, &QToolButton::clicked,
            this, &MainWindow::readSelectedDevicePartitions);
    connect(&m_partitionTableWatcher,
            &QFutureWatcher<zzrk::workflow::DevicePartitionTableResult>::finished,
            this, &MainWindow::handlePartitionTableFinished);
    connect(m_partitionTable, &QTableWidget::itemSelectionChanged,
            this, &MainWindow::updateUpgradeControls);
    connect(m_backupPartitionButton, &QPushButton::clicked,
            this, &MainWindow::backupSelectedPartition);
    connect(m_writePartitionButton, &QPushButton::clicked,
            this, &MainWindow::writeSelectedPartition);
    connect(m_erasePartitionButton, &QPushButton::clicked,
            this, &MainWindow::eraseSelectedPartition);
    connect(m_cancelPartitionButton, &QPushButton::clicked,
            this, &MainWindow::cancelPartitionOperation);
    connect(&m_partitionOperationWatcher,
            &QFutureWatcher<zzrk::workflow::PartitionOperationResult>::finished,
            this, &MainWindow::handlePartitionOperationFinished);
    connect(m_chooseImageButton, &QToolButton::clicked,
            this, &MainWindow::chooseFirmwareImage);
    connect(m_startUpgradeButton, &QPushButton::clicked,
            this, &MainWindow::startUpgrade);
    connect(m_cancelUpgradeButton, &QPushButton::clicked,
            this, &MainWindow::cancelUpgrade);
    connect(m_exportReportButton, &QToolButton::clicked,
            this, &MainWindow::exportSelectedReport);
    connect(m_historyTable, &QTableWidget::itemSelectionChanged,
            this, &MainWindow::updateReportExportControls);
    connect(&m_prepareWatcher,
            &QFutureWatcher<zzrk::workflow::PreparedFirmwareUpgradeResult>::finished,
            this, &MainWindow::handlePrepareFinished);
    connect(&m_upgradeWatcher,
            &QFutureWatcher<zzrk::workflow::FirmwareUpgradeResult>::finished,
            this, &MainWindow::handleUpgradeFinished);

    const auto monitoringError = m_backend->startDeviceMonitoring(
        [this](zzrk::transport::UsbHotplugEvent) {
            QMetaObject::invokeMethod(
                this, [this] { handleDeviceChange(); }, Qt::QueuedConnection);
        },
        [this](const zzrk::transport::UsbHotplugError& error) {
            QMetaObject::invokeMethod(
                this,
                [this, error] { handleDeviceMonitoringError(error); },
                Qt::QueuedConnection);
        });
    if (monitoringError.has_value()) {
        handleDeviceMonitoringError(*monitoringError);
    }

    rebuildDownloadTable();
    updateUpgradeControls();

    QTimer::singleShot(0, this, &MainWindow::refreshDevices);
}

MainWindow::~MainWindow()
{
    if (m_workspaceController) {
        m_workspaceController->shutdown();
    }
    m_shuttingDown = true;
    m_hotplugDebounceTimer->stop();
    m_backend->stopDeviceMonitoring();
    if (m_upgradeCancellation) {
        m_upgradeCancellation->store(true);
    }
    if (m_downloadCancellation) {
        m_downloadCancellation->store(true);
    }
    if (m_partitionCancellation) {
        m_partitionCancellation->store(true);
    }
    disconnect(&m_scanWatcher, nullptr, this, nullptr);
    disconnect(&m_prepareWatcher, nullptr, this, nullptr);
    disconnect(&m_upgradeWatcher, nullptr, this, nullptr);
    disconnect(&m_downloadPrepareWatcher, nullptr, this, nullptr);
    disconnect(&m_imageDownloadWatcher, nullptr, this, nullptr);
    disconnect(&m_inspectionWatcher, nullptr, this, nullptr);
    disconnect(&m_partitionTableWatcher, nullptr, this, nullptr);
    disconnect(&m_partitionOperationWatcher, nullptr, this, nullptr);
    m_scanWatcher.waitForFinished();
    m_prepareWatcher.waitForFinished();
    m_upgradeWatcher.waitForFinished();
    m_downloadPrepareWatcher.waitForFinished();
    m_imageDownloadWatcher.waitForFinished();
    m_inspectionWatcher.waitForFinished();
    m_partitionTableWatcher.waitForFinished();
    m_partitionOperationWatcher.waitForFinished();
}

void MainWindow::refreshDevices()
{
    if (m_shuttingDown || usbOperationRunning()) {
        return;
    }

    m_hotplugRefreshPending = false;
    m_hotplugDebounceTimer->stop();
    m_refreshButton->setEnabled(false);
    statusBar()->showMessage(QStringLiteral("正在扫描 USB 设备..."));

    const auto backend = m_backend;
    auto future = QtConcurrent::run([backend] {
        try {
            return backend->scanDevices();
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
    updateUpgradeControls();
}

void MainWindow::handleDeviceChange()
{
    if (m_shuttingDown || !m_deviceMonitoringError.isEmpty()) {
        return;
    }

    m_hotplugRefreshPending = true;
    if (!usbOperationRunning()) {
        m_hotplugDebounceTimer->start();
    }
}

void MainWindow::handleDeviceMonitoringError(
    const zzrk::transport::UsbHotplugError& error)
{
    if (m_shuttingDown) {
        return;
    }

    m_deviceMonitoringError = QString::fromStdString(error.message);
    m_hotplugRefreshPending = false;
    m_hotplugDebounceTimer->stop();
    m_connectionText->setProperty("hotplugAvailable", false);
    m_connectionText->setText(QStringLiteral("设备热插拔检测不可用"));
    m_connectionText->setToolTip(m_deviceMonitoringError);
    statusBar()->showMessage(
        QStringLiteral("设备热插拔检测不可用：%1；请使用手动刷新")
            .arg(m_deviceMonitoringError));
}

void MainWindow::processPendingDeviceRefresh()
{
    if (m_shuttingDown || !m_hotplugRefreshPending
        || usbOperationRunning()) {
        return;
    }
    refreshDevices();
}

void MainWindow::handleScanFinished()
{
    if (m_shuttingDown) {
        return;
    }

    applyScanResult(m_scanWatcher.result());
    m_refreshButton->setEnabled(true);

    emit scanCompleted();
}

void MainWindow::applyScanResult(const zzrk::transport::ScanResult &result)
{
    const QSignalBlocker selectionBlocker{m_deviceTable};
    QSignalBlocker downloadDeviceBlocker{m_downloadDeviceCombo};
    const QString selectedDownloadPort =
        m_downloadDeviceCombo->currentData().toString();
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
    m_downloadDeviceCombo->clear();

    const QIcon connectedIcon = style()->standardIcon(QStyle::SP_DialogApplyButton);
    int row = 0;
    for (const zzrk::transport::UsbDeviceInfo &device : result.devices) {
        auto *statusItem = makeItem(QStringLiteral("已连接"));
        statusItem->setIcon(connectedIcon);
        statusItem->setData(DeviceKeyRole, deviceKey(device));
        statusItem->setData(PortPathRole, portPathText(device));
        statusItem->setData(FlashableRole, isSupportedFlashDevice(device));
        statusItem->setData(ManageableRole,
                            isSupportedManagementDevice(device));
        m_deviceTable->setItem(row, StatusColumn, statusItem);
        m_deviceTable->setItem(row, ModeColumn, makeItem(modeText(device.mode)));
        m_deviceTable->setItem(row, DeviceIdColumn, makeItem(deviceIdText(device)));

        auto *busItem = makeItem(QString::number(device.busNumber));
        busItem->setData(Qt::DisplayRole, device.busNumber);
        busItem->setTextAlignment(Qt::AlignCenter);
        m_deviceTable->setItem(row, BusColumn, busItem);
        m_deviceTable->setItem(row, PortColumn, makeItem(portPathText(device)));
        m_deviceTable->setItem(row, UsbVersionColumn, makeItem(usbVersionText(device.bcdUsb)));
        if (isSupportedFlashDevice(device)) {
            const QString port = portPathText(device);
            m_downloadDeviceCombo->addItem(
                QStringLiteral("%1 (%2)").arg(port, modeText(device.mode)),
                port);
            const int comboIndex = m_downloadDeviceCombo->count() - 1;
            m_downloadDeviceCombo->setItemData(
                comboIndex, isSupportedManagementDevice(device),
                Qt::UserRole + 1);
            m_downloadDeviceCombo->setItemData(
                comboIndex, static_cast<int>(device.mode), Qt::UserRole + 2);
        }
        ++row;
    }

    const int restoredDownloadIndex =
        m_downloadDeviceCombo->findData(selectedDownloadPort);
    if (restoredDownloadIndex >= 0) {
        m_downloadDeviceCombo->setCurrentIndex(restoredDownloadIndex);
    }
    downloadDeviceBlocker.unblock();

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
    if (!m_deviceMonitoringError.isEmpty()) {
        m_connectionText->setText(
            m_connectionText->text() + QStringLiteral("；热插拔检测不可用"));
        m_connectionText->setToolTip(m_deviceMonitoringError);
        statusBar()->showMessage(
            QStringLiteral("设备热插拔检测不可用：%1；请使用手动刷新")
                .arg(m_deviceMonitoringError));
    }
    handleDownloadDeviceChanged();
    updateUpgradeControls();
    handleDeviceSelectionChanged();
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

QString MainWindow::selectedPartitionName() const
{
    const int row = m_partitionTable->currentRow();
    const auto *item = row < 0
        ? nullptr
        : m_partitionTable->item(row, PartitionNameColumn);
    return item == nullptr ? QString{} : item->text();
}

bool MainWindow::usbOperationRunning() const
{
    return m_scanWatcher.isRunning() || m_upgradeWatcher.isRunning()
        || m_imageDownloadWatcher.isRunning()
        || m_inspectionWatcher.isRunning()
        || m_partitionTableWatcher.isRunning()
        || m_partitionOperationWatcher.isRunning();
}

void MainWindow::handleDeviceSelectionChanged()
{
    const QString portPath = selectedPortPath();
    if (portPath != m_selectedPortCache) {
        m_selectedPortCache = portPath;
        clearDeviceInspection();
        clearPartitionTable();
    }
    if (!portPath.isEmpty()) {
        syncDownloadDeviceFromTable();
    }
    if (portPath.isEmpty()) {
        m_deviceDetailStatus->setText(QStringLiteral("请选择 Loader 设备"));
    } else {
        const int row = m_deviceTable->currentRow();
        const auto *item = row < 0
            ? nullptr
            : m_deviceTable->item(row, StatusColumn);
        if (item == nullptr || !item->data(ManageableRole).toBool()) {
            m_deviceDetailStatus->setText(
                QStringLiteral("当前模式不支持详情与分区操作"));
        }
    }
    updateUpgradeControls();
}

void MainWindow::clearDeviceInspection()
{
    m_inspectedPort.clear();
    m_deviceDetailStatus->setText(QStringLiteral("尚未读取详情"));
    m_chipInfoValue->setText(QStringLiteral("-"));
    m_capabilityValue->setText(QStringLiteral("-"));
    m_flashIdValue->setText(QStringLiteral("-"));
    m_capacityValue->setText(QStringLiteral("-"));
    m_flashInfoValue->setText(QStringLiteral("-"));
    m_usbInterfaceValue->setText(QStringLiteral("-"));
}

void MainWindow::clearPartitionTable()
{
    m_partitions.reset();
    m_partitionPort.clear();
    m_partitionTable->setRowCount(0);
    m_partitionProgress->setRange(0, 100);
    m_partitionProgress->setValue(0);
    m_partitionStatus->setText(QStringLiteral("尚未读取分区表"));
    if (m_downloadLayoutCombo->currentIndex() == 0) {
        m_downloadLayout.reset();
        m_downloadLayoutPort.clear();
        m_downloadLayoutPathEdit->clear();
        rebuildDownloadTable();
    }
}

void MainWindow::inspectSelectedDevice()
{
    if (usbOperationRunning()) {
        return;
    }
    const QString portPath = selectedPortPath();
    if (portPath.isEmpty()) {
        return;
    }
    m_deviceDetailStatus->setText(QStringLiteral("正在读取设备详情..."));
    statusBar()->showMessage(QStringLiteral("正在读取设备详情"));
    const auto backend = m_backend;
    auto future = QtConcurrent::run([backend, portPath] {
        try {
            return backend->inspectDevice(portPath.toStdString());
        } catch (...) {
            return zzrk::workflow::DeviceInspectionResult{
                unexpectedDeviceManagementError(portPath.toStdString())};
        }
    });
    m_inspectionWatcher.setFuture(future);
    updateUpgradeControls();
}

void MainWindow::handleInspectionFinished()
{
    if (m_shuttingDown) {
        return;
    }
    const auto result = m_inspectionWatcher.result();
    if (const auto *error =
            std::get_if<zzrk::workflow::DeviceManagementError>(&result)) {
        clearDeviceInspection();
        const QString message = deviceManagementErrorText(*error);
        m_deviceDetailStatus->setText(message);
        statusBar()->showMessage(QStringLiteral("设备详情读取失败"));
        if (m_showModalDialogs) {
            QMessageBox::critical(this, QStringLiteral("读取失败"), message);
        }
        updateUpgradeControls();
        emit deviceInspectionCompleted(false);
        return;
    }

    const auto &inspection =
        std::get<zzrk::workflow::DeviceInspection>(result);
    m_inspectedPort = QString::fromStdString(inspection.device.portPath);
    m_chipInfoValue->setText(hexBytes(inspection.chipInfo.bytes));
    m_capabilityValue->setText(hexBytes(inspection.capability.rawBytes));
    m_flashIdValue->setText(
        QStringLiteral("%1 [%2]")
            .arg(asciiBytes(inspection.flashId.rawBytes),
                 hexBytes(inspection.flashId.rawBytes)));
    const auto sectors = inspection.flashInfo.legacy.sectorCount;
    m_capacityValue->setText(
        QStringLiteral("%1（%2 扇区）")
            .arg(dataSizeText(static_cast<std::uint64_t>(sectors) * 512U))
            .arg(sectors));
    m_flashInfoValue->setText(
        QStringLiteral("原始 %1；块 %2，页 %3，ECC %4")
            .arg(hexBytes(inspection.flashInfo.rawBytes))
            .arg(inspection.flashInfo.legacy.blockSizeCode)
            .arg(inspection.flashInfo.legacy.pageSizeCode)
            .arg(inspection.flashInfo.legacy.eccBits));
    const auto &usb = inspection.interfaceInfo;
    m_usbInterfaceValue->setText(
        QStringLiteral("接口 %1/%2；IN 0x%3 (%4 B)，OUT 0x%5 (%6 B)")
            .arg(usb.interfaceNumber)
            .arg(usb.alternateSetting)
            .arg(usb.bulkInEndpoint, 2, 16, QLatin1Char('0'))
            .arg(usb.bulkInMaxPacketSize)
            .arg(usb.bulkOutEndpoint, 2, 16, QLatin1Char('0'))
            .arg(usb.bulkOutMaxPacketSize)
            .toUpper());
    m_deviceDetailStatus->setText(
        QStringLiteral("端口 %1，详情读取完成").arg(m_inspectedPort));
    statusBar()->showMessage(QStringLiteral("设备详情已更新"));
    updateUpgradeControls();
    emit deviceInspectionCompleted(true);
}

void MainWindow::readSelectedDevicePartitions()
{
    if (usbOperationRunning()) {
        return;
    }
    const QString portPath = selectedPortPath();
    if (portPath.isEmpty()) {
        return;
    }
    m_partitionStatus->setText(QStringLiteral("正在读取并校验 GPT..."));
    m_partitionProgress->setRange(0, 0);
    statusBar()->showMessage(QStringLiteral("正在读取 GPT 分区表"));
    const auto backend = m_backend;
    auto future = QtConcurrent::run([backend, portPath] {
        try {
            return backend->readPartitions(portPath.toStdString());
        } catch (...) {
            return zzrk::workflow::DevicePartitionTableResult{
                unexpectedDeviceManagementError(portPath.toStdString())};
        }
    });
    m_partitionTableWatcher.setFuture(future);
    updateUpgradeControls();
}

void MainWindow::handlePartitionTableFinished()
{
    if (m_shuttingDown) {
        return;
    }
    auto result = m_partitionTableWatcher.result();
    m_partitionProgress->setRange(0, 100);
    m_partitionProgress->setValue(0);
    if (const auto *error =
            std::get_if<zzrk::workflow::DeviceManagementError>(&result)) {
        clearPartitionTable();
        const QString message = deviceManagementErrorText(*error);
        m_partitionStatus->setText(message);
        if (m_downloadLayoutCombo->currentIndex() == 0) {
            m_downloadStatus->setText(message);
        }
        statusBar()->showMessage(QStringLiteral("GPT 分区表读取失败"));
        if (m_showModalDialogs) {
            QMessageBox::critical(this, QStringLiteral("读取失败"), message);
        }
        updateUpgradeControls();
        emit partitionTableCompleted(false);
        return;
    }

    m_partitions =
        std::get<zzrk::workflow::DevicePartitionTable>(std::move(result));
    m_partitionPort = QString::fromStdString(m_partitions->device.portPath);
    if (m_downloadLayoutCombo->currentIndex() == 0
        && m_partitionPort == downloadPortPath()) {
        zzrk::image::RockchipParameterLayout layout{};
        layout.model = "device-gpt";
        layout.partitions.reserve(m_partitions->table.partitions.size());
        for (const auto &partition : m_partitions->table.partitions) {
            layout.partitions.push_back(zzrk::image::FirmwarePartition{
                .name = partition.name,
                .firstLba = partition.firstLba,
                .sectorCount = partition.sectorCount(),
                .grow = false,
            });
        }
        m_downloadLayout = std::move(layout);
        m_downloadLayoutPort = m_partitionPort;
        m_downloadLayoutPathEdit->setText(
            QStringLiteral("%1 / 设备 GPT").arg(m_partitionPort));
        rebuildDownloadTable();
        matchDownloadDirectory();
        m_downloadStatus->setText(
            QStringLiteral("已读取设备 GPT，共 %1 个分区")
                .arg(static_cast<qulonglong>(
                    m_downloadLayout->partitions.size())));
    }
    m_partitionTable->setSortingEnabled(false);
    m_partitionTable->setRowCount(
        static_cast<int>(m_partitions->table.partitions.size()));
    int row = 0;
    for (const auto &partition : m_partitions->table.partitions) {
        auto *name = makeItem(QString::fromStdString(partition.name));
        name->setData(FirstLbaRole,
                      QVariant::fromValue<qulonglong>(partition.firstLba));
        name->setData(LastLbaRole,
                      QVariant::fromValue<qulonglong>(partition.lastLba));
        m_partitionTable->setItem(row, PartitionNameColumn, name);
        const auto setNumber = [this, row](const int column,
                                           const std::uint64_t value) {
            auto *item = makeItem({});
            item->setData(Qt::DisplayRole,
                          QVariant::fromValue<qulonglong>(value));
            m_partitionTable->setItem(row, column, item);
        };
        setNumber(PartitionFirstLbaColumn, partition.firstLba);
        setNumber(PartitionLastLbaColumn, partition.lastLba);
        setNumber(PartitionSectorCountColumn, partition.sectorCount());
        m_partitionTable->setItem(
            row, PartitionSizeColumn,
            makeItem(dataSizeText(partition.sectorCount() * 512U)));
        m_partitionTable->setItem(
            row, PartitionAttributesColumn,
            makeItem(QStringLiteral("0x%1").arg(
                static_cast<qulonglong>(partition.attributes), 0, 16)));
        ++row;
    }
    m_partitionTable->setSortingEnabled(true);
    const auto backup = zzrk::storage::toString(
        m_partitions->table.backupStatus);
    m_partitionStatus->setText(
        QStringLiteral("端口 %1：%2 个分区，eMMC %3，备份 GPT %4")
            .arg(m_partitionPort)
            .arg(m_partitionTable->rowCount())
            .arg(dataSizeText(m_partitions->deviceSectors * 512U))
            .arg(QString::fromLatin1(backup.data(),
                                     static_cast<qsizetype>(backup.size()))));
    statusBar()->showMessage(QStringLiteral("GPT 分区表已更新"));
    updateUpgradeControls();
    emit partitionTableCompleted(true);
}

void MainWindow::backupSelectedPartition()
{
    if (usbOperationRunning()) {
        return;
    }
    const QString partition = selectedPartitionName();
    if (partition.isEmpty()) {
        return;
    }
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("备份分区"), partition + QStringLiteral(".img"),
        QStringLiteral("分区镜像 (*.img);;所有文件 (*)"));
    if (path.isEmpty()) {
        return;
    }
    if (QFileInfo::exists(path)) {
        const QString message = QStringLiteral("输出文件已存在，备份不会覆盖该文件。");
        statusBar()->showMessage(message);
        if (m_showModalDialogs) {
            QMessageBox::warning(this, QStringLiteral("无法备份"), message);
        }
        return;
    }
    startPartitionOperation(
        zzrk::workflow::PartitionOperationPhase::read,
        filesystemPath(path));
}

void MainWindow::writeSelectedPartition()
{
    if (usbOperationRunning()) {
        return;
    }
    const int row = m_partitionTable->currentRow();
    const QString partition = selectedPartitionName();
    const auto *item = row < 0
        ? nullptr
        : m_partitionTable->item(row, PartitionNameColumn);
    if (partition.isEmpty() || item == nullptr) {
        return;
    }
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("选择分区镜像"), {},
        QStringLiteral("分区镜像 (*.img *.bin);;所有文件 (*)"));
    if (path.isEmpty()) {
        return;
    }
    const auto firstLba = item->data(FirstLbaRole).toULongLong();
    const auto lastLba = item->data(LastLbaRole).toULongLong();
    const auto partitionBytes = (lastLba - firstLba + 1U) * 512U;
    const QFileInfo inputInfo{path};
    if (inputInfo.size() <= 0
        || static_cast<qulonglong>(inputInfo.size()) > partitionBytes) {
        const QString message = QStringLiteral("镜像大小无效或超过目标分区容量。");
        statusBar()->showMessage(message);
        if (m_showModalDialogs) {
            QMessageBox::critical(this, QStringLiteral("无法写入"), message);
        }
        return;
    }
    if (m_showModalDialogs) {
        const auto confirmation = QMessageBox::warning(
            this, QStringLiteral("确认写入分区"),
            QStringLiteral(
                "端口：%1\n分区：%2\nLBA：%3 - %4\n分区容量：%5\n镜像：%6\n回读校验：%7\n\n该操作会覆盖目标分区现有数据。")
                .arg(selectedPortPath(), partition)
                .arg(firstLba)
                .arg(lastLba)
                .arg(dataSizeText(partitionBytes), inputInfo.fileName(),
                     m_verifyPartitionWriteCheckBox->isChecked()
                         ? QStringLiteral("启用")
                         : QStringLiteral("关闭")),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
        if (confirmation != QMessageBox::Yes) {
            return;
        }
    }
    startPartitionOperation(
        zzrk::workflow::PartitionOperationPhase::write,
        filesystemPath(path));
}

void MainWindow::eraseSelectedPartition()
{
    if (usbOperationRunning()) {
        return;
    }
    const int row = m_partitionTable->currentRow();
    const QString partition = selectedPartitionName();
    const auto *item = row < 0
        ? nullptr
        : m_partitionTable->item(row, PartitionNameColumn);
    if (partition.isEmpty() || item == nullptr) {
        return;
    }
    const auto firstLba = item->data(FirstLbaRole).toULongLong();
    const auto lastLba = item->data(LastLbaRole).toULongLong();
    const auto partitionBytes = (lastLba - firstLba + 1U) * 512U;
    if (m_showModalDialogs) {
        const auto confirmation = QMessageBox::critical(
            this, QStringLiteral("确认擦除分区"),
            QStringLiteral(
                "端口：%1\n分区：%2\nLBA：%3 - %4\n容量：%5\n\n该分区的全部数据将被擦除，且无法恢复。")
                .arg(selectedPortPath(), partition)
                .arg(firstLba)
                .arg(lastLba)
                .arg(dataSizeText(partitionBytes)),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
        if (confirmation != QMessageBox::Yes) {
            return;
        }
    }
    startPartitionOperation(zzrk::workflow::PartitionOperationPhase::erase);
}

void MainWindow::startPartitionOperation(
    const zzrk::workflow::PartitionOperationPhase phase,
    const std::filesystem::path &path)
{
    if (usbOperationRunning()) {
        return;
    }
    const QString portPath = selectedPortPath();
    const QString partition = selectedPartitionName();
    if (portPath.isEmpty() || partition.isEmpty()
        || m_partitionPort != portPath) {
        return;
    }

    m_partitionOperationPhase = phase;
    m_partitionCancellation = std::make_shared<std::atomic_bool>(false);
    m_partitionProgress->setRange(0, 100);
    m_partitionProgress->setValue(0);
    m_partitionStatus->setText(
        QStringLiteral("正在%1分区 %2...")
            .arg(partitionPhaseText(phase), partition));
    statusBar()->showMessage(QStringLiteral("分区操作进行中"));

    const auto backend = m_backend;
    const auto cancellation = m_partitionCancellation;
    const bool verify = m_verifyPartitionWriteCheckBox->isChecked();
    auto future = QtConcurrent::run(
        [this, backend, cancellation, phase, portPath, partition, path,
         verify] {
            const auto progress =
                [this](const zzrk::workflow::PartitionOperationProgress
                           &event) {
                    QMetaObject::invokeMethod(
                        this,
                        [this, event] {
                            handlePartitionOperationProgress(event);
                        },
                        Qt::QueuedConnection);
                };
            const auto cancelled = [cancellation] {
                return cancellation->load();
            };
            try {
                switch (phase) {
                case zzrk::workflow::PartitionOperationPhase::read:
                    return backend->readPartition(
                        portPath.toStdString(), partition.toStdString(), path,
                        progress, cancelled);
                case zzrk::workflow::PartitionOperationPhase::write:
                    return backend->writePartition(
                        portPath.toStdString(), partition.toStdString(), path,
                        verify, progress, cancelled);
                case zzrk::workflow::PartitionOperationPhase::erase:
                    return backend->erasePartition(
                        portPath.toStdString(), partition.toStdString(),
                        progress, cancelled);
                case zzrk::workflow::PartitionOperationPhase::verify:
                    break;
                }
            } catch (...) {
            }
            return zzrk::workflow::PartitionOperationResult{
                unexpectedDeviceManagementError(partition.toStdString())};
        });
    m_partitionOperationWatcher.setFuture(future);
    updateUpgradeControls();
}

void MainWindow::handlePartitionOperationProgress(
    const zzrk::workflow::PartitionOperationProgress &progress)
{
    if (m_shuttingDown || !m_partitionOperationWatcher.isRunning()) {
        return;
    }
    if (progress.totalBytes == 0U) {
        m_partitionProgress->setRange(0, 0);
    } else {
        const auto percent = static_cast<int>(std::min<std::uint64_t>(
            100U, progress.completedBytes * 100U / progress.totalBytes));
        m_partitionProgress->setRange(0, 100);
        m_partitionProgress->setValue(percent);
    }
    m_partitionStatus->setText(
        QStringLiteral("正在%1分区 %2：%3 / %4")
            .arg(partitionPhaseText(progress.phase),
                 QString::fromStdString(progress.partition),
                 dataSizeText(progress.completedBytes),
                 dataSizeText(progress.totalBytes)));
}

void MainWindow::cancelPartitionOperation()
{
    if (!m_partitionCancellation
        || !m_partitionOperationWatcher.isRunning()) {
        return;
    }
    m_partitionCancellation->store(true);
    m_cancelPartitionButton->setEnabled(false);
    m_partitionStatus->setText(
        QStringLiteral("正在等待当前 USB 分块完成..."));
}

void MainWindow::handlePartitionOperationFinished()
{
    if (m_shuttingDown) {
        return;
    }
    const auto result = m_partitionOperationWatcher.result();
    m_partitionCancellation.reset();
    m_partitionProgress->setRange(0, 100);
    if (const auto *error =
            std::get_if<zzrk::workflow::DeviceManagementError>(&result)) {
        const QString message = deviceManagementErrorText(*error);
        m_partitionStatus->setText(message);
        statusBar()->showMessage(
            error->code == zzrk::workflow::DeviceManagementErrorCode::cancelled
                ? QStringLiteral("分区操作已取消")
                : QStringLiteral("分区操作失败"));
        if (error->code !=
                zzrk::workflow::DeviceManagementErrorCode::cancelled
            && m_showModalDialogs) {
            QMessageBox::critical(this, QStringLiteral("操作失败"), message);
        }
        updateUpgradeControls();
        emit partitionOperationCompleted(false);
        return;
    }

    const auto &success =
        std::get<zzrk::workflow::PartitionOperationSuccess>(result);
    m_partitionProgress->setValue(100);
    const QString message =
        QStringLiteral("%1完成：分区 %2，处理 %3%4")
            .arg(partitionPhaseText(success.phase),
                 QString::fromStdString(success.partition),
                 dataSizeText(success.bytesProcessed),
                 success.verified ? QStringLiteral("，回读校验通过")
                                  : QString{});
    m_partitionStatus->setText(message);
    statusBar()->showMessage(QStringLiteral("分区操作完成"));
    if (m_showModalDialogs) {
        QMessageBox::information(this, QStringLiteral("操作完成"), message);
    }
    updateUpgradeControls();
    emit partitionOperationCompleted(true);
}

QString MainWindow::downloadPortPath() const
{
    return m_downloadDeviceCombo->currentData().toString();
}

void MainWindow::syncDownloadDeviceFromTable()
{
    const QString port = selectedPortPath();
    const int index = m_downloadDeviceCombo->findData(port);
    if (index < 0 || index == m_downloadDeviceCombo->currentIndex()) {
        return;
    }
    const QSignalBlocker blocker{m_downloadDeviceCombo};
    m_downloadDeviceCombo->setCurrentIndex(index);
    handleDownloadDeviceChanged();
}

void MainWindow::syncDeviceTableFromDownload()
{
    const QString port = downloadPortPath();
    if (port.isEmpty() || selectedPortPath() == port) {
        return;
    }
    for (int row = 0; row < m_deviceTable->rowCount(); ++row) {
        const auto *item = m_deviceTable->item(row, StatusColumn);
        if (item != nullptr && item->data(PortPathRole).toString() == port) {
            m_deviceTable->selectRow(row);
            return;
        }
    }
}

void MainWindow::handleDownloadDeviceChanged()
{
    syncDeviceTableFromDownload();
    if (m_downloadLayoutCombo->currentIndex() == 0
        && m_downloadLayoutPort != downloadPortPath()) {
        m_downloadLayout.reset();
        m_downloadLayoutPort.clear();
        m_downloadLayoutPathEdit->clear();
        rebuildDownloadTable();
    }
    updateDownloadControls();
}

void MainWindow::handleDownloadLayoutChanged()
{
    if (m_downloadLayoutCombo->currentIndex() == 1) {
        if (!m_downloadParameterPath.empty()) {
            loadDownloadParameter(
                filesystemPathText(m_downloadParameterPath));
            return;
        }
        m_downloadLayout.reset();
        m_downloadLayoutPort.clear();
        m_downloadLayoutPathEdit->clear();
    } else if (m_partitions.has_value()
               && m_partitionPort == downloadPortPath()) {
        zzrk::image::RockchipParameterLayout layout{};
        layout.model = "device-gpt";
        layout.partitions.reserve(m_partitions->table.partitions.size());
        for (const auto &partition : m_partitions->table.partitions) {
            layout.partitions.push_back(zzrk::image::FirmwarePartition{
                .name = partition.name,
                .firstLba = partition.firstLba,
                .sectorCount = partition.sectorCount(),
                .grow = false,
            });
        }
        m_downloadLayout = std::move(layout);
        m_downloadLayoutPort = m_partitionPort;
        m_downloadLayoutPathEdit->setText(
            QStringLiteral("%1 / 设备 GPT").arg(m_partitionPort));
    } else {
        m_downloadLayout.reset();
        m_downloadLayoutPort.clear();
        m_downloadLayoutPathEdit->clear();
    }
    rebuildDownloadTable();
    matchDownloadDirectory();
}

void MainWindow::chooseDownloadParameter()
{
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("选择 parameter.txt"), {},
        QStringLiteral("Rockchip parameter (parameter.txt *.txt);;所有文件 (*)"));
    if (!path.isEmpty()) {
        loadDownloadParameter(path);
    }
}

void MainWindow::loadDownloadParameter(const QString &path)
{
    if (path.isEmpty() || m_downloadPrepareWatcher.isRunning()
        || m_imageDownloadWatcher.isRunning()) {
        return;
    }
    const auto parameterPath = filesystemPath(path);
    auto parsed = zzrk::image::parseRockchipParameterFile(parameterPath);
    if (const auto *error =
            std::get_if<zzrk::image::FirmwareImageError>(&parsed)) {
        m_downloadStatus->setText(
            QStringLiteral("parameter.txt 解析失败（错误 %1）")
                .arg(static_cast<int>(error->code)));
        return;
    }
    {
        const QSignalBlocker blocker{m_downloadLayoutCombo};
        m_downloadLayoutCombo->setCurrentIndex(1);
    }
    m_downloadParameterPath = parameterPath;
    m_downloadLayout = std::get<zzrk::image::RockchipParameterLayout>(
        std::move(parsed));
    m_downloadLayoutPort.clear();
    m_downloadLayoutPathEdit->setText(path);
    if (m_downloadDirectoryPath.empty()) {
        m_downloadDirectoryPath = parameterPath.parent_path();
        m_downloadDirectoryEdit->setText(
            filesystemPathText(m_downloadDirectoryPath));
    }
    rebuildDownloadTable();
    matchDownloadDirectory();
    m_downloadStatus->setText(
        QStringLiteral("已加载 parameter.txt，共 %1 个分区")
            .arg(static_cast<qulonglong>(m_downloadLayout->partitions.size())));
}

void MainWindow::chooseDownloadDirectory()
{
    const QString path = QFileDialog::getExistingDirectory(
        this, QStringLiteral("选择镜像目录"), m_downloadDirectoryEdit->text());
    if (!path.isEmpty()) {
        loadDownloadDirectory(path);
    }
}

void MainWindow::loadDownloadDirectory(const QString &path)
{
    if (path.isEmpty() || m_downloadPrepareWatcher.isRunning()
        || m_imageDownloadWatcher.isRunning()) {
        return;
    }
    const QDir directory{path};
    if (!directory.exists()) {
        m_downloadStatus->setText(QStringLiteral("镜像目录不存在"));
        return;
    }
    m_downloadDirectoryPath = filesystemPath(directory.absolutePath());
    m_downloadDirectoryEdit->setText(directory.absolutePath());
    matchDownloadDirectory();
}

void MainWindow::matchDownloadDirectory()
{
    if (m_downloadDirectoryPath.empty()) {
        updateDownloadControls();
        return;
    }
    const QDir directory{
        filesystemPathText(m_downloadDirectoryPath)};
    if (!directory.exists()) {
        return;
    }

    std::unordered_map<std::string, std::filesystem::path> files;
    const auto entries = directory.entryInfoList(QDir::Files | QDir::Readable,
                                                  QDir::Name);
    for (const QFileInfo &entry : entries) {
        files.insert_or_assign(entry.fileName().toLower().toStdString(),
                               filesystemPath(entry.absoluteFilePath()));
    }
    m_downloadImages.clear();
    m_downloadSelections.clear();
    m_validDownloadImages.clear();

    const auto findCandidate = [&](const std::string &stem) {
        for (const std::string &name : {stem + ".img", stem + ".bin", stem}) {
            if (const auto found = files.find(QString::fromStdString(name)
                                                  .toLower()
                                                  .toStdString());
                found != files.end()) {
                return found->second;
            }
        }
        return std::filesystem::path{};
    };
    if (const auto loader = files.find("miniloaderall.bin");
        loader != files.end()) {
        assignDownloadImage("Loader", loader->second, false, false);
    }
    if (m_downloadLayout.has_value()) {
        for (const auto &partition : m_downloadLayout->partitions) {
            auto path = findCandidate(partition.name);
            if (path.empty()) {
                path = findCandidate(slotBaseTarget(partition.name));
            }
            if (!path.empty()) {
                assignDownloadImage(partition.name, path, false, false);
            }
        }
    }
    rebuildDownloadTable();
    const auto matched = static_cast<qulonglong>(m_validDownloadImages.size());
    m_downloadStatus->setText(
        QStringLiteral("已匹配 %1 个可下载目标").arg(matched));
}

void MainWindow::assignDownloadImage(const std::string &target,
                                     const std::filesystem::path &path,
                                     const bool applySlotPair,
                                     const bool rebuild)
{
    std::vector<std::string> targets{target};
    if (applySlotPair && target != "Loader"
        && slotBaseTarget(target) != target
        && QFileInfo{filesystemPathText(path)}.completeBaseName()
                   .compare(QString::fromStdString(slotBaseTarget(target)),
                            Qt::CaseInsensitive)
               == 0
        && m_downloadLayout.has_value()) {
        targets.clear();
        for (const auto &partition : m_downloadLayout->partitions) {
            if (slotBaseTarget(partition.name) == slotBaseTarget(target)) {
                targets.push_back(partition.name);
            }
        }
    }

    std::optional<zzrk::image::FirmwarePayloadSource> inspectedPayload;
    bool loaderValid = false;
    if (target == "Loader") {
        auto parsed = zzrk::image::parseRkLoaderImage(path);
        if (const auto *loader = std::get_if<zzrk::image::RkLoaderImage>(
                &parsed)) {
            loaderValid = loader->info.chipType == 0x33353838U;
        }
    } else {
        auto inspected = zzrk::image::inspectFirmwarePayload(path);
        if (const auto *payload =
                std::get_if<zzrk::image::FirmwarePayloadSource>(&inspected)) {
            if (payload->format == zzrk::image::PayloadFormat::raw
                || payload->format
                    == zzrk::image::PayloadFormat::androidSparse) {
                inspectedPayload = *payload;
            }
        }
    }
    for (const auto &matchedTarget : targets) {
        bool valid = loaderValid;
        if (matchedTarget != "Loader" && inspectedPayload.has_value()
            && m_downloadLayout.has_value()) {
            const auto partition = std::ranges::find(
                m_downloadLayout->partitions, matchedTarget,
                &zzrk::image::FirmwarePartition::name);
            valid = partition != m_downloadLayout->partitions.end();
            if (valid && partition->sectorCount.has_value()) {
                valid = *partition->sectorCount
                        <= std::numeric_limits<std::uint64_t>::max() / 512U
                    && inspectedPayload->logicalSize
                        <= *partition->sectorCount * 512U;
            }
        }
        m_downloadImages.insert_or_assign(matchedTarget, path);
        m_downloadSelections.insert_or_assign(matchedTarget, true);
        m_downloadVerifications.insert_or_assign(matchedTarget, true);
        if (valid) {
            m_validDownloadImages.insert(matchedTarget);
        } else {
            m_validDownloadImages.erase(matchedTarget);
        }
    }
    if (rebuild) {
        rebuildDownloadTable();
    }
}

void MainWindow::rebuildDownloadTable()
{
    if (m_downloadTable == nullptr) {
        return;
    }
    const QSignalBlocker blocker{m_downloadTable};
    const int partitionRows = m_downloadLayout.has_value()
        ? static_cast<int>(m_downloadLayout->partitions.size())
        : 0;
    const bool parameterLayout = m_downloadLayoutCombo->currentIndex() == 1
        && m_downloadLayout.has_value();
    m_downloadTable->clearContents();
    m_downloadTable->setRowCount(1 + (parameterLayout ? 1 : 0)
                                 + partitionRows);

    const auto addRow = [&](const int row,
                            const zzrk::workflow::ImageDownloadItemKind kind,
                            const std::string &target,
                            const QString &lba,
                            const std::uint64_t capacity,
                            const bool forced) {
        auto *selected = makeItem({});
        selected->setFlags(selected->flags() | Qt::ItemIsUserCheckable);
        selected->setCheckState(forced
                                    || (m_downloadSelections.contains(target)
                                        && m_downloadSelections.at(target))
                                    ? Qt::Checked
                                    : Qt::Unchecked);
        if (forced) {
            selected->setFlags(selected->flags() & ~Qt::ItemIsEnabled);
        }
        m_downloadTable->setItem(row, DownloadSelectedColumn, selected);

        auto *targetItem = makeItem(QString::fromStdString(target));
        targetItem->setData(DownloadTargetRole,
                            QString::fromStdString(target));
        targetItem->setData(DownloadKindRole, static_cast<int>(kind));
        targetItem->setData(DownloadCapacityRole,
                            QVariant::fromValue<qulonglong>(capacity));
        m_downloadTable->setItem(row, DownloadTargetColumn, targetItem);
        m_downloadTable->setItem(row, DownloadLbaColumn, makeItem(lba));
        m_downloadTable->setItem(
            row, DownloadCapacityColumn,
            makeItem(capacity == 0U ? QStringLiteral("-")
                                    : dataSizeText(capacity)));

        auto *verify = makeItem({});
        verify->setFlags(verify->flags() | Qt::ItemIsUserCheckable);
        verify->setCheckState(
            m_downloadVerifications.contains(target)
                    && !m_downloadVerifications.at(target)
                ? Qt::Unchecked
                : Qt::Checked);
        m_downloadTable->setItem(row, DownloadVerifyColumn, verify);

        QString fileText = QStringLiteral("-");
        QString formatText = QStringLiteral("-");
        QString expandedText = QStringLiteral("-");
        QString statusText = QStringLiteral("未选择");
        if (kind == zzrk::workflow::ImageDownloadItemKind::partitionTable) {
            fileText = filesystemPathText(m_downloadParameterPath);
            formatText = payloadFormatText(
                zzrk::image::PayloadFormat::rockchipParameter);
            expandedText = dataSizeText(67U * 512U);
            statusText = QStringLiteral("就绪");
        } else if (const auto found = m_downloadImages.find(target);
                   found != m_downloadImages.end()) {
            fileText = filesystemPathText(found->second);
            if (kind == zzrk::workflow::ImageDownloadItemKind::loader) {
                auto parsed = zzrk::image::parseRkLoaderImage(found->second);
                if (const auto *loader =
                        std::get_if<zzrk::image::RkLoaderImage>(&parsed)) {
                    formatText = QStringLiteral("Loader");
                    expandedText = dataSizeText(loader->info.idBlockSize);
                }
            } else {
                auto inspected =
                    zzrk::image::inspectFirmwarePayload(found->second);
                if (const auto *payload =
                        std::get_if<zzrk::image::FirmwarePayloadSource>(
                            &inspected)) {
                    formatText = payloadFormatText(payload->format);
                    expandedText = dataSizeText(payload->logicalSize);
                }
            }
            statusText = m_validDownloadImages.contains(target)
                ? QStringLiteral("就绪")
                : QStringLiteral("镜像无效或超过分区容量");
        }
        auto *fileItem = makeItem(fileText);
        fileItem->setToolTip(fileText);
        m_downloadTable->setItem(row, DownloadFileColumn, fileItem);
        m_downloadTable->setItem(row, DownloadFormatColumn,
                                 makeItem(formatText));
        m_downloadTable->setItem(row, DownloadExpandedSizeColumn,
                                 makeItem(expandedText));
        m_downloadTable->setItem(row, DownloadStatusColumn,
                                 makeItem(statusText));
    };

    addRow(0, zzrk::workflow::ImageDownloadItemKind::loader, "Loader",
           QStringLiteral("64"), 0U, false);
    int row = 1;
    if (parameterLayout) {
        addRow(row++,
               zzrk::workflow::ImageDownloadItemKind::partitionTable,
               "GPT", QStringLiteral("主 / 备"), 67U * 512U, true);
    }
    if (m_downloadLayout.has_value()) {
        for (const auto &partition : m_downloadLayout->partitions) {
            const std::uint64_t capacity =
                partition.sectorCount.has_value()
                    && *partition.sectorCount
                        <= std::numeric_limits<std::uint64_t>::max() / 512U
                ? *partition.sectorCount * 512U
                : 0U;
            addRow(row++, zzrk::workflow::ImageDownloadItemKind::partition,
                   partition.name,
                   QString::number(
                       static_cast<qulonglong>(partition.firstLba)),
                   capacity, false);
        }
    }
    if (m_downloadTable->rowCount() > 0) {
        m_downloadTable->selectRow(0);
    }
    updateDownloadControls();
}

void MainWindow::chooseSelectedDownloadImage()
{
    const int row = m_downloadTable->currentRow();
    const auto *targetItem = row < 0
        ? nullptr
        : m_downloadTable->item(row, DownloadTargetColumn);
    if (targetItem == nullptr
        || targetItem->data(DownloadKindRole).toInt()
            == static_cast<int>(
                zzrk::workflow::ImageDownloadItemKind::partitionTable)) {
        return;
    }
    const std::string target =
        targetItem->data(DownloadTargetRole).toString().toStdString();
    const QString title = target == "Loader"
        ? QStringLiteral("选择 MiniLoaderAll.bin")
        : QStringLiteral("选择分区镜像");
    const QString filter = target == "Loader"
        ? QStringLiteral("Rockchip Loader (MiniLoaderAll.bin *.bin);;所有文件 (*)")
        : QStringLiteral("分区镜像 (*.img *.bin);;所有文件 (*)");
    const QString path = QFileDialog::getOpenFileName(
        this, title, m_downloadDirectoryEdit->text(), filter);
    if (!path.isEmpty()) {
        assignDownloadImage(target, filesystemPath(path));
    }
}

void MainWindow::clearSelectedDownloadImage()
{
    const int row = m_downloadTable->currentRow();
    const auto *targetItem = row < 0
        ? nullptr
        : m_downloadTable->item(row, DownloadTargetColumn);
    if (targetItem == nullptr
        || targetItem->data(DownloadKindRole).toInt()
            == static_cast<int>(
                zzrk::workflow::ImageDownloadItemKind::partitionTable)) {
        return;
    }
    const std::string target =
        targetItem->data(DownloadTargetRole).toString().toStdString();
    m_downloadImages.erase(target);
    m_downloadSelections.erase(target);
    m_downloadVerifications.erase(target);
    m_validDownloadImages.erase(target);
    rebuildDownloadTable();
}

std::optional<zzrk::workflow::ImageDownloadPrepareRequest>
MainWindow::imageDownloadPrepareRequest() const
{
    zzrk::workflow::ImageDownloadPrepareRequest request{};
    request.layoutSource = m_downloadLayoutCombo->currentIndex() == 1
        ? zzrk::workflow::ImageDownloadLayoutSource::parameterFile
        : zzrk::workflow::ImageDownloadLayoutSource::deviceGpt;
    if (request.layoutSource
        == zzrk::workflow::ImageDownloadLayoutSource::parameterFile) {
        if (!m_downloadLayout.has_value() || m_downloadParameterPath.empty()) {
            return std::nullopt;
        }
        request.parameterPath = m_downloadParameterPath;
    } else if (m_downloadLayout.has_value()) {
        request.devicePartitions = m_downloadLayout->partitions;
        if (m_partitions.has_value()
            && m_downloadLayoutPort == downloadPortPath()) {
            request.deviceSectors = m_partitions->deviceSectors;
        }
    }

    for (int row = 0; row < m_downloadTable->rowCount(); ++row) {
        const auto *selected =
            m_downloadTable->item(row, DownloadSelectedColumn);
        const auto *targetItem =
            m_downloadTable->item(row, DownloadTargetColumn);
        const auto *verify = m_downloadTable->item(row, DownloadVerifyColumn);
        if (selected == nullptr || targetItem == nullptr
            || selected->checkState() != Qt::Checked) {
            continue;
        }
        const std::string target =
            targetItem->data(DownloadTargetRole).toString().toStdString();
        const auto kind = static_cast<zzrk::workflow::ImageDownloadItemKind>(
            targetItem->data(DownloadKindRole).toInt());
        const bool shouldVerify =
            verify != nullptr && verify->checkState() == Qt::Checked;
        if (kind == zzrk::workflow::ImageDownloadItemKind::partitionTable) {
            request.verifyPartitionTable = shouldVerify;
            continue;
        }
        const auto path = m_downloadImages.find(target);
        if (path == m_downloadImages.end()
            || !m_validDownloadImages.contains(target)) {
            return std::nullopt;
        }
        if (kind == zzrk::workflow::ImageDownloadItemKind::loader) {
            request.loaderPath = path->second;
            request.verifyLoader = shouldVerify;
        } else {
            request.partitions.push_back(
                zzrk::workflow::ImageDownloadPartitionSelection{
                    .target = target,
                    .imagePath = path->second,
                    .verify = shouldVerify,
                });
        }
    }
    if (request.layoutSource
            == zzrk::workflow::ImageDownloadLayoutSource::deviceGpt
        && request.loaderPath.empty() && request.partitions.empty()) {
        return std::nullopt;
    }
    return request;
}

void MainWindow::startImageDownload()
{
    if (m_downloadPrepareWatcher.isRunning()
        || m_imageDownloadWatcher.isRunning() || usbOperationRunning()) {
        return;
    }
    const QString port = downloadPortPath();
    const auto request = imageDownloadPrepareRequest();
    if (port.isEmpty() || !request.has_value()) {
        return;
    }
    if (m_showModalDialogs) {
        QStringList targets;
        if (!request->loaderPath.empty()) {
            targets << QStringLiteral("Loader");
        }
        if (request->layoutSource
            == zzrk::workflow::ImageDownloadLayoutSource::parameterFile) {
            targets << QStringLiteral("主/备 GPT");
        }
        for (const auto &partition : request->partitions) {
            targets << QString::fromStdString(partition.target);
        }
        const QString layout = request->layoutSource
                == zzrk::workflow::ImageDownloadLayoutSource::parameterFile
            ? filesystemPathText(request->parameterPath)
            : QStringLiteral("设备当前 GPT");
        const auto confirmation = QMessageBox::warning(
            this, QStringLiteral("确认下载镜像"),
            QStringLiteral("端口：%1\n布局：%2\n目标：%3\n完成后重启：%4\n\n所选目标的现有数据会被覆盖。")
                .arg(port, layout, targets.join(QStringLiteral("、")),
                     m_downloadRebootCheckBox->isChecked()
                         ? QStringLiteral("是")
                         : QStringLiteral("否")),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
        if (confirmation != QMessageBox::Yes) {
            return;
        }
    }

    m_pendingDownloadRequest = zzrk::workflow::ImageDownloadRequest{
        .portPath = port.toStdString(),
        .reboot = m_downloadRebootCheckBox->isChecked(),
    };
    m_preparedDownload.reset();
    m_downloadOverallProgress->setRange(0, 0);
    m_downloadOperationProgress->setRange(0, 0);
    m_downloadStatus->setText(QStringLiteral("正在检查镜像并计算 MD5..."));
    const auto backend = m_backend;
    auto future = QtConcurrent::run([backend, request = *request] {
        try {
            return backend->prepareImageDownload(request);
        } catch (...) {
            return zzrk::workflow::PreparedImageDownloadResult{
                unexpectedImageDownloadError("images")};
        }
    });
    m_downloadPrepareWatcher.setFuture(future);
    updateUpgradeControls();
}

void MainWindow::handleDownloadPrepareFinished()
{
    if (m_shuttingDown) {
        return;
    }
    auto result = m_downloadPrepareWatcher.result();
    if (const auto *error =
            std::get_if<zzrk::workflow::ImageDownloadError>(&result)) {
        m_downloadOverallProgress->setRange(0, 100);
        m_downloadOverallProgress->setValue(0);
        m_downloadOperationProgress->setRange(0, 100);
        m_downloadOperationProgress->setValue(0);
        m_downloadStatus->setText(imageDownloadErrorText(*error));
        updateUpgradeControls();
        emit imageDownloadCompleted(false);
        return;
    }
    if (usbOperationRunning()) {
        m_downloadStatus->setText(QStringLiteral("另一个 USB 操作正在运行"));
        updateUpgradeControls();
        emit imageDownloadCompleted(false);
        return;
    }

    m_preparedDownload =
        std::get<zzrk::workflow::PreparedImageDownloadPlan>(
            std::move(result));
    const auto prepared = *m_preparedDownload;
    m_downloadCancellation = std::make_shared<std::atomic_bool>(false);
    m_downloadOverallProgress->setRange(0, 100);
    m_downloadOverallProgress->setValue(0);
    m_downloadOperationProgress->setRange(0, 0);
    m_downloadStatus->setText(QStringLiteral("正在启动镜像下载..."));
    const auto backend = m_backend;
    const auto request = m_pendingDownloadRequest;
    const auto cancellation = m_downloadCancellation;
    auto future = QtConcurrent::run(
        [this, backend, request, prepared, cancellation]() mutable {
            const auto progress =
                [this](const zzrk::workflow::ImageDownloadEvent &event) {
                    QMetaObject::invokeMethod(
                        this,
                        [this, event] { handleImageDownloadProgress(event); },
                        Qt::QueuedConnection);
                };
            const auto cancelled = [cancellation] {
                return cancellation->load();
            };
            try {
                return backend->executeImageDownload(
                    request, std::move(prepared), progress, cancelled);
            } catch (...) {
                return zzrk::workflow::ImageDownloadResult{
                    unexpectedImageDownloadError(request.portPath)};
            }
        });
    m_imageDownloadWatcher.setFuture(future);
    updateUpgradeControls();
}

void MainWindow::cancelImageDownload()
{
    if (!m_downloadCancellation || !m_imageDownloadWatcher.isRunning()) {
        return;
    }
    m_downloadCancellation->store(true);
    m_cancelDownloadButton->setEnabled(false);
    m_downloadStatus->setText(
        QStringLiteral("正在等待当前 USB 分块完成..."));
}

void MainWindow::handleImageDownloadProgress(
    const zzrk::workflow::ImageDownloadEvent &event)
{
    if (m_shuttingDown || !m_imageDownloadWatcher.isRunning()) {
        return;
    }
    const auto percent = [](const std::uint64_t completed,
                            const std::uint64_t total) {
        return total == 0U
            ? 0
            : static_cast<int>(std::min<std::uint64_t>(
                  100U, completed * 100U / total));
    };
    const int operationPercent = percent(event.completedBytes,
                                         event.totalBytes);
    const int overallPercent = percent(event.completedPlanBytes,
                                       event.totalPlanBytes);
    if (event.totalBytes == 0U) {
        m_downloadOperationProgress->setRange(0, 0);
    } else {
        m_downloadOperationProgress->setRange(0, 100);
        m_downloadOperationProgress->setValue(operationPercent);
    }
    if (event.totalPlanBytes != 0U) {
        m_downloadOverallProgress->setRange(0, 100);
        m_downloadOverallProgress->setValue(overallPercent);
    }

    const QString stage = imageDownloadStageText(event.stage);
    m_downloadStatus->setText(
        event.target.empty()
            ? stage
            : QStringLiteral("%1：%2")
                  .arg(stage, QString::fromStdString(event.target)));
    if (event.itemIndex.has_value() && m_preparedDownload.has_value()
        && *event.itemIndex < m_preparedDownload->items.size()) {
        const QString target = QString::fromStdString(
            m_preparedDownload->items[*event.itemIndex].target);
        for (int row = 0; row < m_downloadTable->rowCount(); ++row) {
            const auto *item =
                m_downloadTable->item(row, DownloadTargetColumn);
            if (item != nullptr
                && item->data(DownloadTargetRole).toString() == target) {
                m_downloadTable->item(row, DownloadStatusColumn)
                    ->setText(event.totalBytes == 0U
                                  ? stage
                                  : QStringLiteral("%1 %2%")
                                        .arg(stage)
                                        .arg(operationPercent));
                break;
            }
        }
    }
    emit imageDownloadProgressChanged(overallPercent, operationPercent);
}

void MainWindow::handleImageDownloadFinished()
{
    if (m_shuttingDown) {
        return;
    }
    const auto result = m_imageDownloadWatcher.result();
    m_downloadCancellation.reset();
    m_downloadOverallProgress->setRange(0, 100);
    m_downloadOperationProgress->setRange(0, 100);
    bool succeeded = false;
    if (const auto *error =
            std::get_if<zzrk::workflow::ImageDownloadError>(&result)) {
        const QString message = imageDownloadErrorText(*error);
        m_downloadStatus->setText(message);
        if (error->itemIndex.has_value() && m_preparedDownload.has_value()
            && *error->itemIndex < m_preparedDownload->items.size()) {
            const QString target = QString::fromStdString(
                m_preparedDownload->items[*error->itemIndex].target);
            for (int row = 0; row < m_downloadTable->rowCount(); ++row) {
                const auto *item =
                    m_downloadTable->item(row, DownloadTargetColumn);
                if (item != nullptr
                    && item->data(DownloadTargetRole).toString() == target) {
                    m_downloadTable->item(row, DownloadStatusColumn)
                        ->setText(error->code
                                         == zzrk::workflow::
                                             ImageDownloadErrorCode::cancelled
                                     ? QStringLiteral("已取消")
                                     : QStringLiteral("失败"));
                    break;
                }
            }
        }
        if (error->code
                != zzrk::workflow::ImageDownloadErrorCode::cancelled
            && m_showModalDialogs) {
            QMessageBox::critical(this, QStringLiteral("下载失败"), message);
        }
    } else {
        const auto &success =
            std::get<zzrk::workflow::ImageDownloadSuccess>(result);
        m_downloadOverallProgress->setValue(100);
        m_downloadOperationProgress->setValue(100);
        for (int row = 0; row < m_downloadTable->rowCount(); ++row) {
            const auto *selected =
                m_downloadTable->item(row, DownloadSelectedColumn);
            if (selected != nullptr && selected->checkState() == Qt::Checked) {
                m_downloadTable->item(row, DownloadStatusColumn)
                    ->setText(QStringLiteral("完成"));
            }
        }
        const QString message =
            QStringLiteral("下载完成：写入 %1 项，校验 %2 项，共 %3")
                .arg(success.itemsWritten)
                .arg(success.itemsVerified)
                .arg(dataSizeText(success.partitionBytesWritten));
        m_downloadStatus->setText(message);
        succeeded = true;
        if (m_showModalDialogs) {
            QMessageBox::information(this, QStringLiteral("下载完成"),
                                     message);
        }
    }
    m_preparedDownload.reset();
    updateUpgradeControls();
    emit imageDownloadCompleted(succeeded);
}

void MainWindow::updateDownloadControls()
{
    if (m_downloadTable == nullptr) {
        return;
    }
    const bool preparing = m_downloadPrepareWatcher.isRunning();
    const bool running = m_imageDownloadWatcher.isRunning();
    const bool busy = usbOperationRunning() || preparing;
    const int row = m_downloadTable->currentRow();
    const auto *target = row < 0
        ? nullptr
        : m_downloadTable->item(row, DownloadTargetColumn);
    const bool specialGpt = target != nullptr
        && target->data(DownloadKindRole).toInt()
            == static_cast<int>(
                zzrk::workflow::ImageDownloadItemKind::partitionTable);
    const bool manageable =
        m_downloadDeviceCombo->currentData(Qt::UserRole + 1).toBool();
    const auto request = imageDownloadPrepareRequest();
    bool maskromHasLoader = true;
    if (m_downloadDeviceCombo->currentData(Qt::UserRole + 2).toInt()
        == static_cast<int>(zzrk::transport::DeviceMode::Maskrom)) {
        maskromHasLoader = request.has_value()
            && !request->loaderPath.empty();
    }

    m_downloadDeviceCombo->setEnabled(!busy);
    m_downloadLayoutCombo->setEnabled(!busy);
    m_chooseDownloadParameterButton->setEnabled(
        !busy && m_downloadLayoutCombo->currentIndex() == 1);
    m_readDownloadGptButton->setEnabled(
        !busy && m_downloadLayoutCombo->currentIndex() == 0 && manageable);
    m_chooseDownloadDirectoryButton->setEnabled(!busy);
    m_downloadTable->setEnabled(!busy);
    m_chooseDownloadImageButton->setEnabled(
        !busy && target != nullptr && !specialGpt);
    m_clearDownloadImageButton->setEnabled(
        !busy && target != nullptr && !specialGpt);
    m_downloadRebootCheckBox->setEnabled(!busy);
    m_startDownloadButton->setEnabled(
        !busy && !downloadPortPath().isEmpty() && request.has_value()
        && maskromHasLoader);
    m_cancelDownloadButton->setEnabled(running);
}

void MainWindow::updateUpgradeControls()
{
    const bool preparing = m_prepareWatcher.isRunning();
    const bool upgrading = m_upgradeWatcher.isRunning();
    const bool downloadPreparing = m_downloadPrepareWatcher.isRunning();
    const bool usbBusy = usbOperationRunning();
    const bool uiBusy = usbBusy || downloadPreparing;
    const bool hasPlan = m_preparedUpgrade.has_value();
    const int selectedRow = m_deviceTable->currentRow();
    const QTableWidgetItem *selectedItem =
        selectedRow < 0 ? nullptr
                        : m_deviceTable->item(selectedRow, StatusColumn);
    const bool hasDevice = selectedItem != nullptr
        && selectedItem->data(FlashableRole).toBool()
        && !selectedPortPath().isEmpty();
    const bool manageable = selectedItem != nullptr
        && selectedItem->data(ManageableRole).toBool()
        && !selectedPortPath().isEmpty();
    const bool hasPartition = !selectedPartitionName().isEmpty()
        && m_partitions.has_value()
        && m_partitionPort == selectedPortPath();

    m_chooseImageButton->setEnabled(!preparing && !upgrading
                                    && !downloadPreparing);
    m_startUpgradeButton->setEnabled(hasPlan && hasDevice && !preparing
                                     && !uiBusy);
    m_cancelUpgradeButton->setEnabled(upgrading);
    m_verifyModeCombo->setEnabled(!upgrading);
    m_rebootCheckBox->setEnabled(!upgrading);
    m_deviceTable->setEnabled(!uiBusy);
    m_refreshButton->setEnabled(!uiBusy);
    m_inspectDeviceButton->setEnabled(manageable && !uiBusy);
    m_readPartitionsButton->setEnabled(manageable && !uiBusy);
    m_partitionTable->setEnabled(!uiBusy);
    m_verifyPartitionWriteCheckBox->setEnabled(!uiBusy);
    m_backupPartitionButton->setEnabled(hasPartition && !uiBusy);
    m_writePartitionButton->setEnabled(hasPartition && !uiBusy);
    m_erasePartitionButton->setEnabled(hasPartition && !uiBusy);
    m_cancelPartitionButton->setEnabled(
        m_partitionOperationWatcher.isRunning());
    updateDownloadControls();
    updateReportExportControls();
    if (m_hotplugRefreshPending && !usbBusy
        && !m_hotplugDebounceTimer->isActive()) {
        m_hotplugDebounceTimer->start();
    }
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

    loadFirmwareImage(path);
}

void MainWindow::loadFirmwareImage(const QString &path)
{
    if (path.isEmpty() || m_prepareWatcher.isRunning()
        || m_upgradeWatcher.isRunning()) {
        return;
    }

    m_preparedUpgrade.reset();
    m_progressTracker.reset();
    m_imagePathEdit->setText(path);
    m_planPreview->clear();
    m_upgradeStatus->setText(QStringLiteral("正在解析固件并校验 MD5..."));
    m_overallProgress->setRange(0, 0);
    m_operationProgress->setRange(0, 0);
    updateUpgradeControls();

    const std::filesystem::path imagePath = filesystemPath(path);
    const auto backend = m_backend;
    auto future = QtConcurrent::run([backend, imagePath] {
        return backend->prepareFirmware(imagePath);
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
        m_overallProgress->setRange(0, 100);
        m_overallProgress->setValue(0);
        m_operationProgress->setRange(0, 100);
        m_operationProgress->setValue(0);
        m_upgradeStatus->setText(workflowErrorText(*error));
        statusBar()->showMessage(QStringLiteral("固件预检失败"));
        updateUpgradeControls();
        emit firmwarePreparationCompleted(false);
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
    m_overallProgress->setRange(0, 100);
    m_overallProgress->setValue(0);
    m_operationProgress->setRange(0, 100);
    m_operationProgress->setValue(0);
    m_upgradeStatus->setText(QStringLiteral("固件预检通过"));
    statusBar()->showMessage(QStringLiteral("升级计划已生成"));
    updateUpgradeControls();
    emit firmwarePreparationCompleted(true);
}

void MainWindow::startUpgrade()
{
    if (!m_preparedUpgrade.has_value() || m_upgradeWatcher.isRunning()
        || m_prepareWatcher.isRunning() || usbOperationRunning()) {
        return;
    }
    const QString portPath = selectedPortPath();
    if (portPath.isEmpty()) {
        return;
    }
    if (m_showModalDialogs) {
        const auto confirmation = QMessageBox::warning(
            this, QStringLiteral("确认完整升级"),
            QStringLiteral("将向端口 %1 的 eMMC 写入 Loader、GPT 和固件中的全部分区。")
                .arg(portPath),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
        if (confirmation != QMessageBox::Yes) {
            return;
        }
    }

    m_refreshButton->setEnabled(false);
    m_upgradeCancellation = std::make_shared<std::atomic_bool>(false);
    m_overallProgress->setRange(0, 100);
    m_overallProgress->setValue(0);
    m_operationProgress->setRange(0, 0);
    m_upgradeStatus->setText(QStringLiteral("正在启动完整升级..."));
    statusBar()->showMessage(QStringLiteral("固件升级进行中"));

    const auto prepared = *m_preparedUpgrade;
    const auto cancellation = m_upgradeCancellation;
    const auto verifyMode = verifyModeFromIndex(m_verifyModeCombo->currentIndex());
    m_progressTracker.emplace(prepared.plan, verifyMode);
    beginHistoryEntry(portPath);
    const zzrk::workflow::FirmwareUpgradeRequest request{
        .portPath = portPath.toStdString(),
        .imagePath = prepared.image.sourcePath,
        .verifyMode = verifyMode,
        .reboot = m_rebootCheckBox->isChecked(),
        .prepareOnly = false,
    };
    m_activeReportBuilder =
        std::make_shared<zzrk::workflow::FirmwareUpgradeReportBuilder>(request);
    m_activeReportBuilder->setPrepared(prepared);
    const auto reportBuilder = m_activeReportBuilder;
    const auto backend = m_backend;
    auto future = QtConcurrent::run(
        [this, backend, request, prepared, cancellation,
         reportBuilder]() mutable {
            const auto progress =
                [this, reportBuilder](
                    const zzrk::workflow::FirmwareUpgradeEvent &event) {
                    reportBuilder->observe(event);
                    QMetaObject::invokeMethod(
                        this,
                        [this, event] { handleUpgradeProgress(event); },
                        Qt::QueuedConnection);
                };
            const auto cancelled = [cancellation] {
                return cancellation->load();
            };
            return backend->executeUpgrade(request, std::move(prepared),
                                           progress, cancelled);
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
    if (m_activeHistoryRow >= 0) {
        m_historyTable->item(m_activeHistoryRow, 3)
            ->setText(QStringLiteral("正在取消"));
    }
}

void MainWindow::handleUpgradeProgress(
    const zzrk::workflow::FirmwareUpgradeEvent &event)
{
    if (m_shuttingDown || !m_upgradeWatcher.isRunning()) {
        return;
    }
    if (!m_progressTracker.has_value()) {
        return;
    }
    const auto snapshot = m_progressTracker->update(event);
    m_overallProgress->setRange(0, 100);
    m_overallProgress->setValue(snapshot.overallPercent);
    if (!snapshot.operationDeterminate) {
        m_operationProgress->setRange(0, 0);
    } else {
        m_operationProgress->setRange(0, 100);
        m_operationProgress->setValue(snapshot.operationPercent);
    }
    const QString stage = QString::fromLatin1(
        zzrk::workflow::toString(event.stage).data(),
        static_cast<qsizetype>(zzrk::workflow::toString(event.stage).size()));
    m_upgradeStatus->setText(
        event.target.empty()
            ? stage
            : QStringLiteral("%1：%2")
                  .arg(stage, QString::fromStdString(event.target)));
    emit upgradeProgressChanged(snapshot.overallPercent,
                                snapshot.operationPercent);
}

void MainWindow::handleUpgradeFinished()
{
    if (m_shuttingDown) {
        return;
    }
    const auto result = m_upgradeWatcher.result();
    m_activeReportBuilder->finish(result);
    auto completedReport =
        std::move(*m_activeReportBuilder).takeReport();
    m_activeReportBuilder.reset();
    m_upgradeCancellation.reset();
    m_overallProgress->setRange(0, 100);
    m_operationProgress->setRange(0, 100);
    m_refreshButton->setEnabled(true);

    if (const auto *error =
            std::get_if<zzrk::workflow::FirmwareUpgradeError>(&result)) {
        const QString message = workflowErrorText(*error);
        m_upgradeStatus->setText(message);
        statusBar()->showMessage(QStringLiteral("固件升级未完成"));
        finishHistoryEntry(
            error->code == zzrk::workflow::FirmwareUpgradeErrorCode::cancelled
                ? QStringLiteral("已取消")
                : QStringLiteral("失败"),
            message, std::move(completedReport));
        if (error->code !=
                zzrk::workflow::FirmwareUpgradeErrorCode::cancelled
            && m_showModalDialogs) {
            QMessageBox::critical(this, QStringLiteral("升级失败"), message);
        }
    } else {
        const auto &success =
            std::get<zzrk::workflow::FirmwareUpgradeSuccess>(result);
        const auto snapshot = m_progressTracker.has_value()
            ? m_progressTracker->complete()
            : zzrk::gui::UpgradeProgressSnapshot{100, 100, true};
        m_overallProgress->setValue(snapshot.overallPercent);
        m_operationProgress->setValue(snapshot.operationPercent);
        const QString message =
            QStringLiteral("升级完成：%1 字节，已校验 %2 个分区")
                .arg(static_cast<qulonglong>(success.partitionBytesWritten))
                .arg(success.partitionsVerified);
        m_upgradeStatus->setText(message);
        statusBar()->showMessage(QStringLiteral("固件升级完成"));
        finishHistoryEntry(QStringLiteral("已完成"), message,
                           std::move(completedReport));
        if (m_showModalDialogs) {
            QMessageBox::information(this, QStringLiteral("升级完成"), message);
        }
    }

    m_progressTracker.reset();
    updateUpgradeControls();
    emit upgradeCompleted();
}

void MainWindow::beginHistoryEntry(const QString &portPath)
{
    m_historyTable->insertRow(0);
    m_activeHistoryRow = 0;
    m_historyTable->setItem(
        0, 0, makeItem(QDateTime::currentDateTime().toString(
                  QStringLiteral("yyyy-MM-dd HH:mm:ss"))));
    m_historyTable->setItem(0, 1, makeItem(portPath));
    const QString imageName = QFileInfo{m_imagePathEdit->text()}.fileName();
    m_historyTable->setItem(0, 2, makeItem(imageName));
    m_historyTable->setItem(0, 3, makeItem(QStringLiteral("进行中")));
    constexpr int maximumHistoryRows = 50;
    while (m_historyTable->rowCount() > maximumHistoryRows) {
        const int lastRow = m_historyTable->rowCount() - 1;
        if (const auto *item = m_historyTable->item(lastRow, 3)) {
            bool valid = false;
            const auto reportId =
                item->data(ReportIdRole).toULongLong(&valid);
            if (valid) {
                m_historyReports.erase(reportId);
            }
        }
        m_historyTable->removeRow(lastRow);
    }
    updateReportExportControls();
}

void MainWindow::finishHistoryEntry(const QString &result,
                                    const QString &detail,
                                    zzrk::workflow::FirmwareUpgradeReport report)
{
    if (m_activeHistoryRow < 0) {
        return;
    }
    auto *resultItem = m_historyTable->item(m_activeHistoryRow, 3);
    resultItem->setText(result);
    resultItem->setToolTip(detail);
    const auto reportId = m_nextReportId++;
    m_historyReports.emplace(reportId, std::move(report));
    resultItem->setData(ReportIdRole,
                        QVariant::fromValue<qulonglong>(reportId));
    m_historyTable->selectRow(m_activeHistoryRow);
    m_activeHistoryRow = -1;
    updateReportExportControls();
}

void MainWindow::updateReportExportControls()
{
    const int row = m_historyTable->currentRow();
    const auto *item = row < 0 ? nullptr : m_historyTable->item(row, 3);
    bool valid = false;
    const auto reportId =
        item == nullptr ? 0U : item->data(ReportIdRole).toULongLong(&valid);
    m_exportReportButton->setEnabled(
        valid && m_historyReports.contains(reportId) &&
        !usbOperationRunning());
}

void MainWindow::exportSelectedReport()
{
    const int row = m_historyTable->currentRow();
    const auto *item = row < 0 ? nullptr : m_historyTable->item(row, 3);
    bool valid = false;
    const auto reportId =
        item == nullptr ? 0U : item->data(ReportIdRole).toULongLong(&valid);
    const auto found = valid ? m_historyReports.find(reportId)
                             : m_historyReports.end();
    if (found == m_historyReports.end()) {
        return;
    }

    const QString imageName = m_historyTable->item(row, 2)->text();
    const QString suggested =
        QStringLiteral("%1-%2.json")
            .arg(imageName,
                 QDateTime::currentDateTime().toString(
                     QStringLiteral("yyyyMMdd-HHmmss")));
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("导出升级报告"), suggested,
        QStringLiteral("JSON 报告 (*.json)"));
    if (path.isEmpty()) {
        return;
    }

    const auto error = zzrk::qt::writeFirmwareUpgradeReport(
        found->second, filesystemPath(path),
        zzrk::qt::FirmwareUpgradeReportWriteOptions{
            .includeFirmwarePath = false,
            .applicationVersion = ZZRK_VERSION,
        });
    if (error.has_value()) {
        const QString message =
            QStringLiteral("无法导出报告：%1")
                .arg(QString::fromStdString(error->message));
        statusBar()->showMessage(message);
        if (m_showModalDialogs) {
            QMessageBox::critical(this, QStringLiteral("导出失败"), message);
        }
        return;
    }
    statusBar()->showMessage(QStringLiteral("升级报告已导出"));
}
