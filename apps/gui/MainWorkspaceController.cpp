// SPDX-License-Identifier: MIT

#include "MainWorkspaceController.hpp"
#include "pages/DevicePage.hpp"
#include "pages/FullUpgradePage.hpp"
#include "pages/ImageDownloadPage.hpp"
#include "pages/PartitionPage.hpp"

#include <ZzFluentUI/ZzTabWidget.h>
#include <zzrk/qt/FirmwareUpgradeReportWriter.hpp>

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>
#include <QMetaObject>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>
#include <QtConcurrent/QtConcurrentRun>

#include <ZzCore/ZzErrorCode.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <exception>
#include <limits>
#include <ranges>
#include <stdexcept>
#include <utility>

namespace zzrk::gui {

namespace {

[[nodiscard]] ZzCore::ZzResult<void> workspaceFailure(QString message) {
  return ZzCore::ZzResult<void>::failure(ZzCore::ZzError(
      ZzCore::ZzErrorCode::InvalidArgument, std::move(message)));
}

template <typename Page>
[[nodiscard]] ZzCore::ZzResult<std::unique_ptr<ZzPureTools::ZzPageInstance>>
createFixedPage(QWidget *pageParent) {
  auto *view = Page::buildUi(pageParent);
  return ZzPureTools::ZzPageInstance::create(
      pageParent, view, std::make_unique<QObject>(),
      std::make_unique<QObject>());
}

std::filesystem::path filesystemPath(const QString &path) {
#ifdef _WIN32
  return std::filesystem::path{path.toStdWString()};
#else
  return std::filesystem::path{path.toUtf8().constData()};
#endif
}

QString filesystemPathText(const std::filesystem::path &path) {
#ifdef _WIN32
  return QString::fromStdWString(path.wstring());
#else
  return QString::fromUtf8(path.string());
#endif
}

QString dataSizeText(const std::uint64_t bytes) {
  return QLocale().formattedDataSize(static_cast<qint64>(bytes), 2,
                                     QLocale::DataSizeTraditionalFormat);
}

QString devicePortText(const zzrk::transport::UsbDeviceInfo &device) {
  return device.portPath.empty() ? QStringLiteral("-")
                                 : QString::fromStdString(device.portPath);
}

QString deviceModeText(const zzrk::transport::DeviceMode mode) {
  switch (mode) {
  case zzrk::transport::DeviceMode::Adb:
    return QStringLiteral("ADB");
  case zzrk::transport::DeviceMode::Maskrom:
    return QStringLiteral("Maskrom");
  case zzrk::transport::DeviceMode::Loader:
    return QStringLiteral("Loader");
  case zzrk::transport::DeviceMode::Unknown:
    return QStringLiteral("未知");
  }
  return QStringLiteral("未知");
}

bool isSupportedFlashDevice(const zzrk::transport::UsbDeviceInfo &device) {
  if (device.vendorId != 0x2207U || device.productId != 0x350bU)
    return false;
  return (device.mode == zzrk::transport::DeviceMode::Loader &&
          device.bcdUsb == 0x0201U && device.bcdDevice == 0x0223U) ||
         (device.mode == zzrk::transport::DeviceMode::Maskrom &&
          device.bcdUsb == 0x0200U && device.bcdDevice == 0x0100U);
}

bool isSupportedManagementDevice(const zzrk::transport::UsbDeviceInfo &device) {
  return device.vendorId == 0x2207U && device.productId == 0x350bU &&
         device.bcdUsb == 0x0201U && device.bcdDevice == 0x0223U &&
         device.mode == zzrk::transport::DeviceMode::Loader;
}

std::vector<DevicePage::DeviceRow>
deviceRows(const zzrk::transport::ScanResult &result) {
  std::vector<DevicePage::DeviceRow> rows;
  rows.reserve(result.devices.size());
  for (const auto &device : result.devices) {
    const QString port = devicePortText(device);
    rows.push_back({
        .key = QStringLiteral("%1|%2").arg(device.busNumber).arg(port),
        .status = QStringLiteral("已连接"),
        .mode = deviceModeText(device.mode),
        .deviceId = QStringLiteral("%1:%2")
                        .arg(device.vendorId, 4, 16, QLatin1Char('0'))
                        .arg(device.productId, 4, 16, QLatin1Char('0'))
                        .toUpper(),
        .bus = QString::number(device.busNumber),
        .portPath = port,
        .usbVersion = QStringLiteral("%1.%2%3")
                          .arg((device.bcdUsb >> 8) & 0xff)
                          .arg((device.bcdUsb >> 4) & 0x0f)
                          .arg(device.bcdUsb & 0x0f),
    });
  }
  return rows;
}

const zzrk::transport::UsbDeviceInfo *
findDevice(const zzrk::transport::ScanResult &result, const QString &port) {
  const auto found =
      std::ranges::find(result.devices, port.toStdString(),
                        &zzrk::transport::UsbDeviceInfo::portPath);
  return found == result.devices.end() ? nullptr : &*found;
}

template <std::size_t Size>
QString hexBytes(const std::array<std::byte, Size> &bytes) {
  QStringList values;
  for (const auto value : bytes) {
    values.append(QStringLiteral("%1").arg(std::to_integer<unsigned int>(value),
                                           2, 16, QLatin1Char('0')));
  }
  return values.join(QLatin1Char(' ')).toUpper();
}

template <std::size_t Size>
QString asciiBytes(const std::array<std::byte, Size> &bytes) {
  QByteArray text;
  for (const auto value : bytes) {
    const char character =
        static_cast<char>(std::to_integer<unsigned int>(value));
    text.append(character >= 0x20 && character <= 0x7e ? character : '.');
  }
  return QString::fromLatin1(text);
}

QString payloadFormatText(const zzrk::image::PayloadFormat format) {
  switch (format) {
  case zzrk::image::PayloadFormat::androidSparse:
    return QStringLiteral("Android Sparse");
  case zzrk::image::PayloadFormat::rockchipParameter:
    return QStringLiteral("parameter.txt");
  case zzrk::image::PayloadFormat::raw:
    return QStringLiteral("Raw");
  case zzrk::image::PayloadFormat::unknown:
    return QStringLiteral("-");
  }
  return QStringLiteral("-");
}

std::string slotBaseTarget(const std::string &target) {
  return target.size() > 2U &&
                 (target.ends_with("_a") || target.ends_with("_b"))
             ? target.substr(0U, target.size() - 2U)
             : target;
}

ImageDownloadPage::RowKind
pageRowKind(const zzrk::workflow::ImageDownloadItemKind kind) {
  switch (kind) {
  case zzrk::workflow::ImageDownloadItemKind::loader:
    return ImageDownloadPage::RowKind::loader;
  case zzrk::workflow::ImageDownloadItemKind::partitionTable:
    return ImageDownloadPage::RowKind::partitionTable;
  case zzrk::workflow::ImageDownloadItemKind::partition:
    return ImageDownloadPage::RowKind::partition;
  }
  return ImageDownloadPage::RowKind::partition;
}

QString imageDownloadStageText(const zzrk::workflow::ImageDownloadStage stage) {
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

QString
imageDownloadErrorText(const zzrk::workflow::ImageDownloadError &error) {
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
    reason +=
        QStringLiteral("（%1）").arg(QString::fromStdString(error.target));
  }
  return reason;
}

QString
deviceManagementErrorText(const zzrk::workflow::DeviceManagementError &error) {
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
    reason +=
        QStringLiteral("（%1）").arg(QString::fromStdString(error.target));
  }
  return reason;
}

QString workflowErrorText(const zzrk::workflow::FirmwareUpgradeError &error) {
  using zzrk::workflow::FirmwareUpgradeErrorCode;
  QString reason;
  switch (error.code) {
  case FirmwareUpgradeErrorCode::cancelled:
    reason = QStringLiteral("操作已取消"); break;
  case FirmwareUpgradeErrorCode::imageInvalid:
    reason = QStringLiteral("固件格式无效"); break;
  case FirmwareUpgradeErrorCode::digestFailed:
    reason = QStringLiteral("固件 MD5 校验失败"); break;
  case FirmwareUpgradeErrorCode::planInvalid:
    reason = QStringLiteral("无法生成完整升级计划"); break;
  case FirmwareUpgradeErrorCode::scanFailed:
  case FirmwareUpgradeErrorCode::deviceNotFound:
  case FirmwareUpgradeErrorCode::deviceAmbiguous:
  case FirmwareUpgradeErrorCode::deviceUnsupported:
    reason = QStringLiteral("目标 USB 设备不可用"); break;
  case FirmwareUpgradeErrorCode::openFailed:
    reason = QStringLiteral("无法打开 RockUSB 接口"); break;
  case FirmwareUpgradeErrorCode::loaderResetFailed:
  case FirmwareUpgradeErrorCode::reenumerationTimedOut:
  case FirmwareUpgradeErrorCode::loaderDownloadFailed:
  case FirmwareUpgradeErrorCode::loaderNotReady:
    reason = QStringLiteral("RAM Loader 准备失败"); break;
  case FirmwareUpgradeErrorCode::wrongChip:
    reason = QStringLiteral("目标芯片不是 RK3588"); break;
  case FirmwareUpgradeErrorCode::storageCheckFailed:
  case FirmwareUpgradeErrorCode::storageNotEmmc:
  case FirmwareUpgradeErrorCode::deviceTooSmall:
    reason = QStringLiteral("eMMC 检查失败"); break;
  case FirmwareUpgradeErrorCode::flashFailed:
    reason = QStringLiteral("固件写入或回读校验失败"); break;
  case FirmwareUpgradeErrorCode::resetFailed:
    reason = QStringLiteral("固件已写入，但设备复位失败"); break;
  case FirmwareUpgradeErrorCode::closeFailed:
    reason = QStringLiteral("无法释放 RockUSB 接口"); break;
  }
  if (!error.target.empty())
    reason += QStringLiteral("（%1）").arg(QString::fromStdString(error.target));
  return reason;
}

QString partitionPhaseText(zzrk::workflow::PartitionOperationPhase phase) {
  using zzrk::workflow::PartitionOperationPhase;
  switch (phase) {
  case PartitionOperationPhase::read: return QStringLiteral("备份");
  case PartitionOperationPhase::write: return QStringLiteral("写入");
  case PartitionOperationPhase::verify: return QStringLiteral("校验");
  case PartitionOperationPhase::erase: return QStringLiteral("擦除");
  }
  return QStringLiteral("操作");
}

zzrk::workflow::ImageDownloadError
unexpectedImageDownloadError(const std::string &target) {
  zzrk::workflow::ImageDownloadError error{};
  error.code = zzrk::workflow::ImageDownloadErrorCode::invalidRequest;
  error.stage = zzrk::workflow::ImageDownloadStage::inspectFiles;
  error.target = target;
  return error;
}

zzrk::workflow::DeviceManagementError
unexpectedDeviceManagementError(const std::string &target) {
  zzrk::workflow::DeviceManagementError error{};
  error.code = zzrk::workflow::DeviceManagementErrorCode::openFailed;
  error.target = target;
  return error;
}

zzrk::workflow::FirmwareUpgradeError
unexpectedFirmwareUpgradeError(const std::string &target) {
  zzrk::workflow::FirmwareUpgradeError error{};
  error.code = zzrk::workflow::FirmwareUpgradeErrorCode::imageInvalid;
  error.target = target;
  return error;
}

} // namespace

MainWorkspaceController::MainWorkspaceController(
    std::shared_ptr<IGuiOperationBackend> backend,
    MainWorkspaceViewModel *viewModel, const bool showModalDialogs,
    QObject *parent)
    : QObject(parent), m_backend(std::move(backend)),
      m_lifecycle(std::make_shared<MainWorkspaceLifecycle>()),
      m_viewModel(viewModel),
      m_showModalDialogs(showModalDialogs) {
  if (!m_backend) {
    throw std::invalid_argument(
        "MainWorkspaceController requires a GUI backend");
  }
  if (m_viewModel == nullptr) {
    throw std::invalid_argument(
        "MainWorkspaceController requires a workspace view model");
  }
  m_viewModel->setUpgradeCancellation(
      std::make_shared<std::atomic_bool>(false));
  m_viewModel->setDownloadCancellation(
      std::make_shared<std::atomic_bool>(false));
  m_viewModel->setPartitionCancellation(
      std::make_shared<std::atomic_bool>(false));

  connect(&m_scanWatcher,
          &QFutureWatcher<zzrk::transport::ScanResult>::finished, this,
          &MainWorkspaceController::handleScanFinished);
  connect(
      &m_downloadPrepareWatcher,
      &QFutureWatcher<zzrk::workflow::PreparedImageDownloadResult>::finished,
      this, &MainWorkspaceController::handleDownloadPrepareFinished);
  connect(&m_imageDownloadWatcher,
          &QFutureWatcher<zzrk::workflow::ImageDownloadResult>::finished, this,
          &MainWorkspaceController::handleImageDownloadFinished);
  connect(&m_inspectionWatcher,
          &QFutureWatcher<zzrk::workflow::DeviceInspectionResult>::finished,
          this, &MainWorkspaceController::handleInspectionFinished);
  connect(&m_partitionTableWatcher,
          &QFutureWatcher<zzrk::workflow::DevicePartitionTableResult>::finished,
          this, &MainWorkspaceController::handlePartitionTableFinished);
  connect(&m_partitionOperationWatcher,
          &QFutureWatcher<zzrk::workflow::PartitionOperationResult>::finished,
          this, &MainWorkspaceController::handlePartitionOperationFinished);
  connect(&m_prepareWatcher,
          &QFutureWatcher<zzrk::workflow::PreparedFirmwareUpgradeResult>::finished,
          this, &MainWorkspaceController::handlePrepareFinished);
  connect(&m_upgradeWatcher,
          &QFutureWatcher<zzrk::workflow::FirmwareUpgradeResult>::finished,
          this, &MainWorkspaceController::handleUpgradeFinished);
}

MainWorkspaceController::~MainWorkspaceController() { shutdown(); }

IGuiOperationBackend *MainWorkspaceController::backend() const noexcept {
  return m_backend.get();
}

MainWorkspaceViewModel *MainWorkspaceController::viewModel() const noexcept {
  return m_viewModel.data();
}

bool MainWorkspaceController::showModalDialogs() const noexcept {
  return m_showModalDialogs;
}

void MainWorkspaceController::setShowModalDialogs(const bool show) {
  m_showModalDialogs = show;
}

std::shared_ptr<const MainWorkspaceLifecycle>
MainWorkspaceController::lifecycle() const noexcept {
  return m_lifecycle;
}

ZzCore::ZzResult<void> MainWorkspaceController::attachWorkspace(
    ZzFluentUI::ZzTabWidget *tabs, QStatusBar *statusBar,
    ImageDownloadPage *downloadPage) {
  if (m_shuttingDown) {
    return workspaceFailure(QStringLiteral("工作区控制器正在关闭"));
  }
  if (tabs == nullptr || statusBar == nullptr || downloadPage == nullptr) {
    return workspaceFailure(
        QStringLiteral("工作区标签、状态栏和下载页面不能为空"));
  }
  m_tabs = tabs;
  m_tabs->setObjectName(QStringLiteral("mainTabs"));
  m_statusBar = statusBar;

  auto deviceResult = createFixedPage<DevicePage>(tabs);
  if (!deviceResult) {
    return ZzCore::ZzResult<void>::failure(deviceResult.error());
  }
  auto deviceInstance = std::move(deviceResult).value();
  auto *devicePage = qobject_cast<DevicePage *>(deviceInstance->view());
  if (tabs->addTab(devicePage, QStringLiteral("设备")) < 0) {
    return workspaceFailure(QStringLiteral("无法加入设备页面标签"));
  }

  auto upgradeResult = createFixedPage<FullUpgradePage>(tabs);
  if (!upgradeResult) {
    return ZzCore::ZzResult<void>::failure(upgradeResult.error());
  }
  auto upgradeInstance = std::move(upgradeResult).value();
  auto *upgradePage =
      qobject_cast<FullUpgradePage *>(upgradeInstance->view());
  if (tabs->addTab(upgradePage, QStringLiteral("完整升级")) < 0) {
    return workspaceFailure(QStringLiteral("无法加入完整升级页面标签"));
  }

  auto partitionResult = createFixedPage<PartitionPage>(tabs);
  if (!partitionResult) {
    return ZzCore::ZzResult<void>::failure(partitionResult.error());
  }
  auto partitionInstance = std::move(partitionResult).value();
  auto *partitionPage =
      qobject_cast<PartitionPage *>(partitionInstance->view());
  if (tabs->addTab(partitionPage, QStringLiteral("分区")) < 0) {
    return workspaceFailure(QStringLiteral("无法加入分区页面标签"));
  }

  m_devicePageInstance = std::move(deviceInstance);
  m_upgradePageInstance = std::move(upgradeInstance);
  m_partitionPageInstance = std::move(partitionInstance);
  attachPages(downloadPage, devicePage, upgradePage, partitionPage);

  QWidget *const host = statusBar;
  m_connectionText =
      host->findChild<QLabel *>(QStringLiteral("connectionStatusText"));
  if (m_connectionText == nullptr) {
    m_connectionText = new QLabel(QStringLiteral("正在检测设备..."), host);
    m_connectionText->setObjectName(QStringLiteral("connectionStatusText"));
    m_connectionText->setProperty("hotplugAvailable", true);
    m_connectionText->setAccessibleName(QStringLiteral("连接状态"));
  }

  m_refreshButton =
      host->findChild<QToolButton *>(QStringLiteral("refreshButton"));
  if (m_refreshButton == nullptr) {
    m_refreshButton = new QToolButton(host);
    m_refreshButton->setObjectName(QStringLiteral("refreshButton"));
    m_refreshButton->setIcon(
        host->style()->standardIcon(QStyle::SP_BrowserReload));
    m_refreshButton->setToolTip(QStringLiteral("立即刷新设备列表"));
    m_refreshButton->setAccessibleName(QStringLiteral("刷新设备列表"));
    m_refreshButton->setFixedSize(30, 30);
  }

  m_lastScanText = host->findChild<QLabel *>(QStringLiteral("lastScanText"));
  if (m_lastScanText == nullptr) {
    m_lastScanText = new QLabel(QStringLiteral("尚未扫描"), host);
    m_lastScanText->setObjectName(QStringLiteral("lastScanText"));
  }

  m_hotplugDebounceTimer = new QTimer(this);
  m_hotplugDebounceTimer->setInterval(150);
  m_hotplugDebounceTimer->setSingleShot(true);
  connect(m_refreshButton, &QToolButton::clicked, this,
          &MainWorkspaceController::refreshDevices);
  connect(m_hotplugDebounceTimer, &QTimer::timeout, this,
          &MainWorkspaceController::processPendingDeviceRefresh);

  if (m_statusBar != nullptr) {
    m_statusBar->addWidget(m_connectionText, 1);
    m_statusBar->addPermanentWidget(m_refreshButton);
    m_statusBar->addPermanentWidget(m_lastScanText);
    m_statusBar->showMessage(QStringLiteral("准备就绪"));
  }
  startMonitoring();
  QTimer::singleShot(0, this, &MainWorkspaceController::refreshDevices);
  return ZzCore::ZzResult<void>::success();
}

void MainWorkspaceController::setExternalBusyCallback(
    std::function<bool()> callback) {
  m_externalBusyCallback = std::move(callback);
}

void MainWorkspaceController::attachPages(ImageDownloadPage *downloadPage,
                                          DevicePage *devicePage,
                                          FullUpgradePage *upgradePage,
                                          PartitionPage *partitionPage) {
  if (m_downloadPage != downloadPage) {
    if (m_downloadPage != nullptr)
      disconnect(m_downloadPage, nullptr, this, nullptr);
    m_downloadPage = downloadPage;
    if (m_downloadPage != nullptr) {
      connect(m_downloadPage, &ImageDownloadPage::deviceChanged, this,
              &MainWorkspaceController::handleDownloadDeviceChanged);
      connect(m_downloadPage, &ImageDownloadPage::layoutChanged, this,
              &MainWorkspaceController::handleDownloadLayoutChanged);
      connect(m_downloadPage, &ImageDownloadPage::chooseParameterRequested,
              this, &MainWorkspaceController::chooseDownloadParameter);
      connect(m_downloadPage, &ImageDownloadPage::readDeviceGptRequested, this,
              &MainWorkspaceController::readSelectedDevicePartitions);
      connect(m_downloadPage, &ImageDownloadPage::chooseDirectoryRequested,
              this, &MainWorkspaceController::chooseDownloadDirectory);
      connect(m_downloadPage, &ImageDownloadPage::chooseImageRequested, this,
              &MainWorkspaceController::chooseSelectedDownloadImage);
      connect(m_downloadPage, &ImageDownloadPage::clearImageRequested, this,
              &MainWorkspaceController::clearSelectedDownloadImage);
      connect(m_downloadPage, &ImageDownloadPage::planChanged, this,
              &MainWorkspaceController::handleDownloadPlanChanged);
      connect(m_downloadPage, &ImageDownloadPage::startRequested, this,
              &MainWorkspaceController::startImageDownload);
      connect(m_downloadPage, &ImageDownloadPage::cancelRequested, this,
              &MainWorkspaceController::cancelImageDownload);
      rebuildDownloadTable();
    }
  }
  if (m_devicePage != devicePage) {
    if (m_devicePage != nullptr)
      disconnect(m_devicePage, nullptr, this, nullptr);
    m_devicePage = devicePage;
    if (m_devicePage != nullptr) {
      connect(m_devicePage, &DevicePage::selectionChanged, this,
              &MainWorkspaceController::handleDeviceSelectionChanged);
      connect(m_devicePage, &DevicePage::inspectRequested, this,
              &MainWorkspaceController::inspectSelectedDevice);
      m_devicePage->setDevices(deviceRows(m_lastScanResult));
    }
  }
  if (m_upgradePage != upgradePage) {
    if (m_upgradePage != nullptr)
      disconnect(m_upgradePage, nullptr, this, nullptr);
    m_upgradePage = upgradePage;
    if (m_upgradePage != nullptr) {
      connect(m_upgradePage, &FullUpgradePage::chooseFirmwareRequested, this,
              &MainWorkspaceController::chooseFirmwareImage);
      connect(m_upgradePage, &FullUpgradePage::startRequested, this,
              &MainWorkspaceController::startUpgrade);
      connect(m_upgradePage, &FullUpgradePage::cancelRequested, this,
              &MainWorkspaceController::cancelUpgrade);
      connect(m_upgradePage, &FullUpgradePage::exportReportRequested, this,
              &MainWorkspaceController::exportSelectedReport);
      connect(m_upgradePage, &FullUpgradePage::historySelectionChanged, this,
              &MainWorkspaceController::updateReportExportControls);
    }
  }
  if (m_partitionPage != partitionPage) {
    if (m_partitionPage != nullptr)
      disconnect(m_partitionPage, nullptr, this, nullptr);
    m_partitionPage = partitionPage;
    if (m_partitionPage != nullptr) {
      connect(m_partitionPage, &PartitionPage::readRequested, this,
              &MainWorkspaceController::readSelectedDevicePartitions);
      connect(m_partitionPage, &PartitionPage::selectionChanged, this,
              &MainWorkspaceController::updatePartitionControls);
      connect(m_partitionPage, &PartitionPage::backupRequested, this,
              &MainWorkspaceController::backupSelectedPartition);
      connect(m_partitionPage, &PartitionPage::writeRequested, this,
              &MainWorkspaceController::writeSelectedPartition);
      connect(m_partitionPage, &PartitionPage::eraseRequested, this,
              &MainWorkspaceController::eraseSelectedPartition);
      connect(m_partitionPage, &PartitionPage::cancelRequested, this,
              &MainWorkspaceController::cancelPartitionOperation);
    }
  }
  updateUpgradeControls();
  updatePartitionControls();
}

void MainWorkspaceController::startMonitoring() {
  if (m_monitoringStarted || m_shuttingDown || !m_backend) {
    return;
  }
  const auto monitoringError = m_backend->startDeviceMonitoring(
      [this](zzrk::transport::UsbHotplugEvent) {
        QMetaObject::invokeMethod(
            this, [this] { handleDeviceChange(); }, Qt::QueuedConnection);
      },
      [this](const zzrk::transport::UsbHotplugError &error) {
        QMetaObject::invokeMethod(
            this, [this, error] { handleDeviceMonitoringError(error); },
            Qt::QueuedConnection);
      });
  if (monitoringError.has_value()) {
    handleDeviceMonitoringError(*monitoringError);
    return;
  }
  m_monitoringStarted = true;
}

void MainWorkspaceController::refreshDevices() {
  if (m_shuttingDown || usbOperationRunning() || m_scanWatcher.isRunning()) {
    return;
  }
  m_hotplugRefreshPending = false;
  if (m_hotplugDebounceTimer != nullptr) {
    m_hotplugDebounceTimer->stop();
  }
  if (m_refreshButton != nullptr) {
    m_refreshButton->setEnabled(false);
  }
  if (m_statusBar != nullptr) {
    m_statusBar->showMessage(QStringLiteral("正在扫描 USB 设备..."));
  }
  if (m_viewModel == nullptr) {
    return;
  }
  m_viewModel->setBusy(true);
  const auto backend = m_backend;
  m_scanWatcher.setFuture(QtConcurrent::run([backend] {
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
  }));
}

void MainWorkspaceController::handleDeviceChange() {
  if (m_shuttingDown || !m_deviceMonitoringError.isEmpty()) {
    return;
  }
  m_hotplugRefreshPending = true;
  if (!usbOperationRunning() && m_hotplugDebounceTimer != nullptr) {
    m_hotplugDebounceTimer->start();
  }
}

void MainWorkspaceController::handleDeviceMonitoringError(
    const zzrk::transport::UsbHotplugError &error) {
  if (m_shuttingDown) {
    return;
  }
  m_deviceMonitoringError = QString::fromStdString(error.message);
  m_hotplugRefreshPending = false;
  if (m_hotplugDebounceTimer != nullptr) {
    m_hotplugDebounceTimer->stop();
  }
  if (m_connectionText != nullptr) {
    m_connectionText->setProperty("hotplugAvailable", false);
    m_connectionText->setText(QStringLiteral("设备热插拔检测不可用"));
    m_connectionText->setToolTip(m_deviceMonitoringError);
  }
  if (m_statusBar != nullptr) {
    m_statusBar->showMessage(
        QStringLiteral("设备热插拔检测不可用：%1；请使用手动刷新")
            .arg(m_deviceMonitoringError));
  }
  emit deviceMonitoringError(m_deviceMonitoringError);
}

void MainWorkspaceController::processPendingDeviceRefresh() {
  if (m_shuttingDown || !m_hotplugRefreshPending || usbOperationRunning()) {
    return;
  }
  refreshDevices();
}

void MainWorkspaceController::handleScanFinished() {
  if (m_shuttingDown) {
    return;
  }
  QPointer<MainWorkspaceController> guard{this};
  if (m_viewModel != nullptr) {
    m_viewModel->setBusy(false);
    if (guard == nullptr)
      return;
  }
  applyScanResult(m_scanWatcher.result());
  if (guard == nullptr)
    return;
  if (m_refreshButton != nullptr) {
    m_refreshButton->setEnabled(true);
  }
  processPendingDeviceRefresh();
  emit scanCompleted();
}

void MainWorkspaceController::applyScanResult(
    const zzrk::transport::ScanResult &result) {
  m_lastScanResult = result;
  if (m_devicePage != nullptr) {
    m_devicePage->setDevices(deviceRows(result));
  }
  if (m_downloadPage != nullptr) {
    const QSignalBlocker blocker{m_downloadPage->deviceCombo()};
    const QString selected = m_downloadPage->selectedPortPath();
    m_downloadPage->deviceCombo()->clear();
    for (const auto &device : result.devices) {
      const bool loader = isSupportedManagementDevice(device);
      const bool maskrom = isSupportedFlashDevice(device) && !loader;
      if (!loader && !maskrom)
        continue;
      const QString port = QString::fromStdString(device.portPath);
      const QString mode =
          loader ? QStringLiteral("Loader") : QStringLiteral("Maskrom");
      m_downloadPage->deviceCombo()->addItem(
          QStringLiteral("%1 (%2)").arg(port, mode), port);
      const int index = m_downloadPage->deviceCombo()->count() - 1;
      m_downloadPage->deviceCombo()->setItemData(index, loader,
                                                 Qt::UserRole + 1);
      m_downloadPage->deviceCombo()->setItemData(
          index, static_cast<int>(device.mode), Qt::UserRole + 2);
    }
    const int restored = m_downloadPage->deviceCombo()->findData(selected);
    if (restored >= 0)
      m_downloadPage->deviceCombo()->setCurrentIndex(restored);
  }
  if (m_lastScanText != nullptr) {
    m_lastScanText->setText(QStringLiteral("更新于 %1")
                                .arg(QDateTime::currentDateTime().toString(
                                    QStringLiteral("HH:mm:ss"))));
  }
  if (m_connectionText != nullptr) {
    const auto count = static_cast<qulonglong>(result.devices.size());
    m_connectionText->setText(
        result.error.has_value()
            ? (count == 0U
                   ? QStringLiteral("设备扫描失败")
                   : QStringLiteral("已检测到 %1 台设备（结果可能不完整）")
                         .arg(count))
            : (count == 0U
                   ? QStringLiteral("未检测到 Rockchip 设备")
                   : QStringLiteral("已连接 %1 台 Rockchip 设备").arg(count)));
  }
  QPointer<MainWorkspaceController> guard{this};
  emit devicesChanged(result);
  if (guard == nullptr)
    return;
  handleDownloadDeviceChanged();
  if (guard == nullptr)
    return;
  handleDeviceSelectionChanged();
}

void MainWorkspaceController::handleDeviceSelectionChanged() {
  if (m_devicePage == nullptr)
    return;
  const QString port = m_devicePage->selectedPortPath();
  const bool changed =
      m_viewModel != nullptr && m_viewModel->selectedPortPath() != port;
  QPointer<MainWorkspaceController> guard{this};
  if (m_viewModel != nullptr) {
    m_viewModel->setSelectedPortPath(port);
    if (guard == nullptr)
      return;
  }
  if (changed) {
    clearDeviceInspection();
    setDevicePartitionTable(nullptr);
    m_partitionTableStatus = QStringLiteral("尚未读取分区表");
    clearPartitionTable();
    emit devicePartitionTableChanged();
    if (guard == nullptr)
      return;
  }
  if (!port.isEmpty()) {
    syncDownloadDeviceFromTable();
    if (guard == nullptr)
      return;
  }
  if (port.isEmpty()) {
    m_devicePage->setInspection(QStringLiteral("请选择 Loader 设备"));
  } else if (!selectedDeviceManageable()) {
    m_devicePage->setInspection(QStringLiteral("当前模式不支持详情与分区操作"));
  }
  updateDownloadControls();
  updateUpgradeControls();
  updatePartitionControls();
  if (changed)
    emit selectedDeviceChanged();
}

void MainWorkspaceController::clearDeviceInspection() {
  if (m_viewModel != nullptr)
    m_viewModel->setInspectedPort({});
  if (m_devicePage != nullptr) {
    m_devicePage->setInspection(QStringLiteral("尚未读取详情"));
  }
}

void MainWorkspaceController::inspectSelectedDevice() {
  if (m_devicePage == nullptr || usbOperationRunning())
    return;
  const QString port = selectedPortPath();
  if (port.isEmpty())
    return;
  m_devicePage->setInspection(QStringLiteral("正在读取设备详情..."));
  if (m_statusBar != nullptr)
    m_statusBar->showMessage(QStringLiteral("正在读取设备详情"));
  const auto backend = m_backend;
  m_inspectionWatcher.setFuture(QtConcurrent::run([backend, port] {
    try {
      return backend->inspectDevice(port.toStdString());
    } catch (...) {
      return zzrk::workflow::DeviceInspectionResult{
          unexpectedDeviceManagementError(port.toStdString())};
    }
  }));
  setPagesBusy(true);
}

void MainWorkspaceController::handleInspectionFinished() {
  if (m_shuttingDown || m_devicePage == nullptr)
    return;
  const auto result = m_inspectionWatcher.result();
  if (const auto *error =
          std::get_if<zzrk::workflow::DeviceManagementError>(&result)) {
    clearDeviceInspection();
    const QString message = deviceManagementErrorText(*error);
    m_devicePage->setInspection(message);
    if (m_statusBar != nullptr)
      m_statusBar->showMessage(QStringLiteral("设备详情读取失败"));
    if (m_showModalDialogs)
      QMessageBox::critical(m_devicePage, QStringLiteral("读取失败"), message);
    setPagesBusy(false);
    processPendingDeviceRefresh();
    emit deviceInspectionCompleted(false);
    return;
  }
  const auto &inspection = std::get<zzrk::workflow::DeviceInspection>(result);
  const QString port = QString::fromStdString(inspection.device.portPath);
  QPointer<MainWorkspaceController> guard{this};
  if (m_viewModel != nullptr) {
    m_viewModel->setInspectedPort(port);
    if (guard == nullptr)
      return;
  }
  const auto sectors = inspection.flashInfo.legacy.sectorCount;
  const auto &usb = inspection.interfaceInfo;
  m_devicePage->setInspection(
      QStringLiteral("端口 %1，详情读取完成").arg(port),
      hexBytes(inspection.chipInfo.bytes),
      hexBytes(inspection.capability.rawBytes),
      QStringLiteral("%1 [%2]").arg(asciiBytes(inspection.flashId.rawBytes),
                                    hexBytes(inspection.flashId.rawBytes)),
      QStringLiteral("%1（%2 扇区）")
          .arg(dataSizeText(static_cast<std::uint64_t>(sectors) * 512U))
          .arg(sectors),
      QStringLiteral("原始 %1；块 %2，页 %3，ECC %4")
          .arg(hexBytes(inspection.flashInfo.rawBytes))
          .arg(inspection.flashInfo.legacy.blockSizeCode)
          .arg(inspection.flashInfo.legacy.pageSizeCode)
          .arg(inspection.flashInfo.legacy.eccBits),
      QStringLiteral("接口 %1/%2；IN 0x%3 (%4 B)，OUT 0x%5 (%6 B)")
          .arg(usb.interfaceNumber)
          .arg(usb.alternateSetting)
          .arg(usb.bulkInEndpoint, 2, 16, QLatin1Char('0'))
          .arg(usb.bulkInMaxPacketSize)
          .arg(usb.bulkOutEndpoint, 2, 16, QLatin1Char('0'))
          .arg(usb.bulkOutMaxPacketSize)
          .toUpper());
  if (m_statusBar != nullptr)
    m_statusBar->showMessage(QStringLiteral("设备详情已更新"));
  setPagesBusy(false);
  processPendingDeviceRefresh();
  emit deviceInspectionCompleted(true);
}

void MainWorkspaceController::readSelectedDevicePartitions() {
  if (m_shuttingDown || usbOperationRunning() || !selectedDeviceManageable())
    return;
  const QString port = selectedPortPath();
  if (port.isEmpty())
    return;
  m_partitionTableStatus = QStringLiteral("正在读取并校验 GPT...");
  if (m_partitionPage != nullptr) {
    m_partitionPage->setStatus(m_partitionTableStatus);
    m_partitionPage->setProgress(0, true);
  }
  if (m_downloadPage != nullptr)
    m_downloadPage->setStatus(m_partitionTableStatus);
  if (m_statusBar != nullptr)
    m_statusBar->showMessage(QStringLiteral("正在读取 GPT 分区表"));
  const auto backend = m_backend;
  m_partitionTableWatcher.setFuture(QtConcurrent::run([backend, port] {
    try {
      return backend->readPartitions(port.toStdString());
    } catch (...) {
      return zzrk::workflow::DevicePartitionTableResult{
          unexpectedDeviceManagementError(port.toStdString())};
    }
  }));
  setPagesBusy(true);
}

void MainWorkspaceController::handlePartitionTableFinished() {
  if (m_shuttingDown)
    return;
  auto result = m_partitionTableWatcher.result();
  if (const auto *error =
          std::get_if<zzrk::workflow::DeviceManagementError>(&result)) {
    setDevicePartitionTable(nullptr);
    m_partitionTableStatus = deviceManagementErrorText(*error);
    clearPartitionTable();
    if (m_partitionPage != nullptr)
      m_partitionPage->setStatus(m_partitionTableStatus);
    if (m_downloadPage != nullptr)
      m_downloadPage->setStatus(m_partitionTableStatus);
    if (m_statusBar != nullptr)
      m_statusBar->showMessage(QStringLiteral("GPT 分区表读取失败"));
    if (m_showModalDialogs && m_downloadPage != nullptr) {
      QMessageBox::critical(m_downloadPage, QStringLiteral("读取失败"),
                            m_partitionTableStatus);
    }
    setPagesBusy(false);
    processPendingDeviceRefresh();
    QPointer<MainWorkspaceController> guard{this};
    emit devicePartitionTableChanged();
    if (guard != nullptr)
      emit partitionTableCompleted(false);
    return;
  }

  const auto partitions =
      std::get<zzrk::workflow::DevicePartitionTable>(std::move(result));
  setDevicePartitionTable(&partitions);
  QPointer<MainWorkspaceController> guard{this};
  if (m_viewModel != nullptr) {
    m_viewModel->setPartitionPort(
        QString::fromStdString(partitions.device.portPath));
    if (guard == nullptr)
      return;
  }
  const auto backup = zzrk::storage::toString(partitions.table.backupStatus);
  m_partitionTableStatus =
      QStringLiteral("端口 %1：%2 个分区，eMMC %3，备份 GPT %4")
          .arg(QString::fromStdString(partitions.device.portPath))
          .arg(static_cast<qulonglong>(partitions.table.partitions.size()))
          .arg(dataSizeText(partitions.deviceSectors * 512U))
          .arg(QString::fromLatin1(backup.data(),
                                   static_cast<qsizetype>(backup.size())));
  if (m_partitionPage != nullptr) {
    std::vector<PartitionPage::PartitionRow> rows;
    rows.reserve(partitions.table.partitions.size());
    for (const auto &partition : partitions.table.partitions) {
      rows.push_back({.name = QString::fromStdString(partition.name),
                      .firstLba = partition.firstLba,
                      .lastLba = partition.lastLba,
                      .attributes = partition.attributes});
    }
    m_partitionPage->setPartitions(std::move(rows));
    m_partitionPage->setProgress(0);
    m_partitionPage->setStatus(m_partitionTableStatus);
  }
  if (m_statusBar != nullptr)
    m_statusBar->showMessage(QStringLiteral("GPT 分区表已更新"));
  setPagesBusy(false);
  processPendingDeviceRefresh();
  emit devicePartitionTableChanged();
  if (guard != nullptr)
    emit partitionTableCompleted(true);
}

void MainWorkspaceController::clearPartitionTable() {
  if (m_viewModel != nullptr)
    m_viewModel->setPartitionPort({});
  if (m_partitionPage != nullptr) {
    m_partitionPage->clearPartitions();
    m_partitionPage->setProgress(0);
    m_partitionPage->setStatus(QStringLiteral("尚未读取分区表"));
  }
  updatePartitionControls();
}

void MainWorkspaceController::backupSelectedPartition() {
  if (m_partitionPage == nullptr || usbOperationRunning())
    return;
  const auto partition = m_partitionPage->selectedPartition();
  if (!partition.has_value())
    return;
  const QString path = QFileDialog::getSaveFileName(
      m_partitionPage, QStringLiteral("备份分区"),
      partition->name + QStringLiteral(".img"),
      QStringLiteral("分区镜像 (*.img);;所有文件 (*)"));
  if (path.isEmpty())
    return;
  if (QFileInfo::exists(path)) {
    const QString message = QStringLiteral("输出文件已存在，备份不会覆盖该文件。");
    if (m_statusBar != nullptr) m_statusBar->showMessage(message);
    if (m_showModalDialogs)
      QMessageBox::warning(m_partitionPage, QStringLiteral("无法备份"), message);
    return;
  }
  startPartitionOperation(zzrk::workflow::PartitionOperationPhase::read,
                          filesystemPath(path));
}

void MainWorkspaceController::writeSelectedPartition() {
  if (m_partitionPage == nullptr || usbOperationRunning())
    return;
  const auto partition = m_partitionPage->selectedPartition();
  if (!partition.has_value())
    return;
  const QString path = QFileDialog::getOpenFileName(
      m_partitionPage, QStringLiteral("选择分区镜像"), {},
      QStringLiteral("分区镜像 (*.img *.bin);;所有文件 (*)"));
  if (path.isEmpty())
    return;
  const auto partitionBytes =
      (partition->lastLba - partition->firstLba + 1U) * 512U;
  const QFileInfo inputInfo{path};
  if (inputInfo.size() <= 0 ||
      static_cast<qulonglong>(inputInfo.size()) > partitionBytes) {
    const QString message = QStringLiteral("镜像大小无效或超过目标分区容量。");
    if (m_statusBar != nullptr) m_statusBar->showMessage(message);
    if (m_showModalDialogs)
      QMessageBox::critical(m_partitionPage, QStringLiteral("无法写入"), message);
    return;
  }
  if (m_showModalDialogs) {
    const auto answer = QMessageBox::warning(
        m_partitionPage, QStringLiteral("确认写入分区"),
        QStringLiteral("端口：%1\n分区：%2\nLBA：%3 - %4\n分区容量：%5\n"
                       "镜像：%6\n回读校验：%7\n\n该操作会覆盖目标分区现有数据。")
            .arg(selectedPortPath(), partition->name)
            .arg(partition->firstLba)
            .arg(partition->lastLba)
            .arg(dataSizeText(partitionBytes), inputInfo.fileName(),
                 m_partitionPage->verifyWrite() ? QStringLiteral("启用")
                                                : QStringLiteral("关闭")),
        QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
    if (answer != QMessageBox::Yes)
      return;
  }
  startPartitionOperation(zzrk::workflow::PartitionOperationPhase::write,
                          filesystemPath(path));
}

void MainWorkspaceController::eraseSelectedPartition() {
  if (m_partitionPage == nullptr || usbOperationRunning())
    return;
  const auto partition = m_partitionPage->selectedPartition();
  if (!partition.has_value())
    return;
  const auto partitionBytes =
      (partition->lastLba - partition->firstLba + 1U) * 512U;
  if (m_showModalDialogs) {
    const auto answer = QMessageBox::critical(
        m_partitionPage, QStringLiteral("确认擦除分区"),
        QStringLiteral("端口：%1\n分区：%2\nLBA：%3 - %4\n容量：%5\n\n"
                       "该分区的全部数据将被擦除，且无法恢复。")
            .arg(selectedPortPath(), partition->name)
            .arg(partition->firstLba)
            .arg(partition->lastLba)
            .arg(dataSizeText(partitionBytes)),
        QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
    if (answer != QMessageBox::Yes)
      return;
  }
  startPartitionOperation(zzrk::workflow::PartitionOperationPhase::erase);
}

void MainWorkspaceController::startPartitionOperation(
    zzrk::workflow::PartitionOperationPhase phase,
    const std::filesystem::path &path) {
  if (m_partitionPage == nullptr || usbOperationRunning())
    return;
  const auto partition = m_partitionPage->selectedPartition();
  const QString port = selectedPortPath();
  if (!partition.has_value() || port.isEmpty() || m_viewModel == nullptr ||
      m_viewModel->partitionPort() != port)
    return;
  m_partitionOperationPhase = phase;
  const auto cancellation = m_viewModel->partitionCancellation();
  if (!cancellation)
    return;
  cancellation->store(false);
  m_partitionPage->setProgress(0);
  m_partitionPage->setStatus(QStringLiteral("正在%1分区 %2...")
                                 .arg(partitionPhaseText(phase), partition->name));
  if (m_statusBar != nullptr)
    m_statusBar->showMessage(QStringLiteral("分区操作进行中"));
  const auto backend = m_backend;
  const bool verify = m_partitionPage->verifyWrite();
  QPointer<MainWorkspaceController> guard{this};
  m_partitionOperationWatcher.setFuture(QtConcurrent::run(
      [guard, backend, cancellation, phase, port, partition = *partition,
       path, verify] {
        const auto progress = [guard](
                                  const zzrk::workflow::PartitionOperationProgress &event) {
          if (guard != nullptr)
            QMetaObject::invokeMethod(
                guard,
                [guard, event] {
                  if (guard != nullptr)
                    guard->handlePartitionOperationProgress(event);
                },
                Qt::QueuedConnection);
        };
        const auto cancelled = [cancellation] { return cancellation->load(); };
        try {
          switch (phase) {
          case zzrk::workflow::PartitionOperationPhase::read:
            return backend->readPartition(port.toStdString(),
                                          partition.name.toStdString(), path,
                                          progress, cancelled);
          case zzrk::workflow::PartitionOperationPhase::write:
            return backend->writePartition(port.toStdString(),
                                           partition.name.toStdString(), path,
                                           verify, progress, cancelled);
          case zzrk::workflow::PartitionOperationPhase::erase:
            return backend->erasePartition(port.toStdString(),
                                           partition.name.toStdString(),
                                           progress, cancelled);
          case zzrk::workflow::PartitionOperationPhase::verify:
            break;
          }
        } catch (...) {
        }
        return zzrk::workflow::PartitionOperationResult{
            unexpectedDeviceManagementError(partition.name.toStdString())};
      }));
  setPagesBusy(true);
}

void MainWorkspaceController::handlePartitionOperationProgress(
    const zzrk::workflow::PartitionOperationProgress &progress) {
  if (m_shuttingDown || m_partitionPage == nullptr ||
      !m_partitionOperationWatcher.isRunning())
    return;
  const int percent = progress.totalBytes == 0U
                          ? 0
                          : static_cast<int>(std::min<std::uint64_t>(
                                100U, progress.completedBytes * 100U /
                                          progress.totalBytes));
  m_partitionPage->setProgress(percent, progress.totalBytes == 0U);
  m_partitionPage->setStatus(
      QStringLiteral("正在%1分区 %2：%3 / %4")
          .arg(partitionPhaseText(progress.phase),
               QString::fromStdString(progress.partition),
               dataSizeText(progress.completedBytes),
               dataSizeText(progress.totalBytes)));
}

void MainWorkspaceController::cancelPartitionOperation() {
  const auto cancellation =
      m_viewModel != nullptr ? m_viewModel->partitionCancellation() : nullptr;
  if (!cancellation || !m_partitionOperationWatcher.isRunning() ||
      m_partitionPage == nullptr)
    return;
  cancellation->store(true);
  m_partitionPage->setStatus(QStringLiteral("正在等待当前 USB 分块完成..."));
  updatePartitionControls();
}

void MainWorkspaceController::handlePartitionOperationFinished() {
  if (m_shuttingDown || m_partitionPage == nullptr)
    return;
  const auto result = m_partitionOperationWatcher.result();
  bool successValue = false;
  m_partitionPage->setProgress(0);
  if (const auto *error =
          std::get_if<zzrk::workflow::DeviceManagementError>(&result)) {
    const QString message = deviceManagementErrorText(*error);
    m_partitionPage->setStatus(message);
    if (m_statusBar != nullptr)
      m_statusBar->showMessage(
          error->code == zzrk::workflow::DeviceManagementErrorCode::cancelled
              ? QStringLiteral("分区操作已取消")
              : QStringLiteral("分区操作失败"));
    if (m_showModalDialogs &&
        error->code != zzrk::workflow::DeviceManagementErrorCode::cancelled)
      QMessageBox::critical(m_partitionPage, QStringLiteral("操作失败"), message);
  } else {
    const auto &success =
        std::get<zzrk::workflow::PartitionOperationSuccess>(result);
    m_partitionPage->setProgress(100);
    const QString message =
        QStringLiteral("%1完成：分区 %2，处理 %3%4")
            .arg(partitionPhaseText(success.phase),
                 QString::fromStdString(success.partition),
                 dataSizeText(success.bytesProcessed),
                 success.verified ? QStringLiteral("，回读校验通过") : QString{});
    m_partitionPage->setStatus(message);
    if (m_statusBar != nullptr)
      m_statusBar->showMessage(QStringLiteral("分区操作完成"));
    if (m_showModalDialogs)
      QMessageBox::information(m_partitionPage, QStringLiteral("操作完成"), message);
    successValue = true;
  }
  setPagesBusy(false);
  processPendingDeviceRefresh();
  emit partitionOperationCompleted(successValue);
}

void MainWorkspaceController::updatePartitionControls() {
  if (m_partitionPage == nullptr)
    return;
  const bool busy = usbOperationRunning();
  const bool canRead = selectedDeviceManageable() && !selectedPortPath().isEmpty();
  const bool canOperate = m_partitionPage->selectedPartition().has_value() &&
                          m_viewModel != nullptr &&
                          m_viewModel->partitionPort() == selectedPortPath();
  const auto cancellation =
      m_viewModel != nullptr ? m_viewModel->partitionCancellation() : nullptr;
  m_partitionPage->setBusy(busy, canRead, canOperate,
                           m_partitionOperationWatcher.isRunning() &&
                               cancellation && !cancellation->load());
}

void MainWorkspaceController::chooseFirmwareImage() {
  if (m_upgradePage == nullptr || m_prepareWatcher.isRunning() ||
      m_upgradeWatcher.isRunning())
    return;
  const QString path = QFileDialog::getOpenFileName(
      m_upgradePage, QStringLiteral("选择 Rockchip 固件"), {},
      QStringLiteral("Rockchip 固件 (*.img);;所有文件 (*)"));
  if (!path.isEmpty())
    loadFirmwareImage(path);
}

void MainWorkspaceController::loadFirmwareImage(const QString &path) {
  if (m_upgradePage == nullptr || path.isEmpty() ||
      m_prepareWatcher.isRunning() || m_upgradeWatcher.isRunning() ||
      usbOperationRunning())
    return;
  m_preparedUpgrade.reset();
  m_progressTracker.reset();
  m_firmwarePath = path;
  m_upgradePage->setFirmwarePath(path);
  m_upgradePage->setPlanPreview({});
  m_upgradePage->setStatus(QStringLiteral("正在解析固件并校验 MD5..."));
  m_upgradePage->setOverallProgress(0, true);
  m_upgradePage->setOperationProgress(0, true);
  const auto backend = m_backend;
  const auto imagePath = filesystemPath(path);
  m_prepareWatcher.setFuture(QtConcurrent::run([backend, imagePath] {
    try {
      return backend->prepareFirmware(imagePath);
    } catch (...) {
      return zzrk::workflow::PreparedFirmwareUpgradeResult{
          unexpectedFirmwareUpgradeError(imagePath.string())};
    }
  }));
  setPagesBusy(true);
}

void MainWorkspaceController::handlePrepareFinished() {
  if (m_shuttingDown || m_upgradePage == nullptr)
    return;
  auto result = m_prepareWatcher.result();
  if (const auto *error =
          std::get_if<zzrk::workflow::FirmwareUpgradeError>(&result)) {
    m_preparedUpgrade.reset();
    m_upgradePage->setPlanPreview({});
    m_upgradePage->setOverallProgress(0);
    m_upgradePage->setOperationProgress(0);
    m_upgradePage->setStatus(workflowErrorText(*error));
    if (m_statusBar != nullptr)
      m_statusBar->showMessage(QStringLiteral("固件预检失败"));
    setPagesBusy(false);
    processPendingDeviceRefresh();
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
               .arg(prepared.plan.minimumDeviceSectors)
               .arg(prepared.plan.totalPartitionBytes);
  lines << QStringLiteral("步骤：%1；MD5：%2")
               .arg(prepared.plan.steps.size())
               .arg(QString::fromLatin1(prepared.md5.data()));
  for (const auto &step : prepared.plan.steps) {
    if (!step.target.empty())
      lines << QStringLiteral("- %1（%2 字节）")
                   .arg(QString::fromStdString(step.target))
                   .arg(step.outputSize);
  }
  m_upgradePage->setPlanPreview(lines.join(QLatin1Char('\n')));
  m_upgradePage->setOverallProgress(0);
  m_upgradePage->setOperationProgress(0);
  m_upgradePage->setStatus(QStringLiteral("固件预检通过"));
  if (m_statusBar != nullptr)
    m_statusBar->showMessage(QStringLiteral("升级计划已生成"));
  setPagesBusy(false);
  updateUpgradeControls();
  processPendingDeviceRefresh();
  emit firmwarePreparationCompleted(true);
}

void MainWorkspaceController::startUpgrade() {
  if (m_upgradePage == nullptr || !m_preparedUpgrade.has_value() ||
      m_prepareWatcher.isRunning() || m_upgradeWatcher.isRunning() ||
      usbOperationRunning())
    return;
  const QString port = selectedPortPath();
  if (port.isEmpty() || !selectedDeviceFlashable())
    return;
  if (m_showModalDialogs) {
    const auto answer = QMessageBox::warning(
        m_upgradePage, QStringLiteral("确认完整升级"),
        QStringLiteral("将向端口 %1 的 eMMC 写入 Loader、GPT 和固件中的全部分区。")
            .arg(port),
        QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
    if (answer != QMessageBox::Yes)
      return;
  }
  const auto cancellation =
      m_viewModel != nullptr ? m_viewModel->upgradeCancellation() : nullptr;
  if (!cancellation)
    return;
  cancellation->store(false);
  QPointer<MainWorkspaceController> guard{this};
  m_viewModel->setProgress(0, 0);
  if (guard == nullptr)
    return;
  m_upgradePage->setOverallProgress(0);
  m_upgradePage->setOperationProgress(0, true);
  m_upgradePage->setStatus(QStringLiteral("正在启动完整升级..."));
  if (m_statusBar != nullptr)
    m_statusBar->showMessage(QStringLiteral("固件升级进行中"));
  const auto prepared = *m_preparedUpgrade;
  const auto verifyMode = m_upgradePage->verifyMode();
  m_progressTracker.emplace(prepared.plan, verifyMode);
  beginHistoryEntry(port);
  const zzrk::workflow::FirmwareUpgradeRequest request{
      .portPath = port.toStdString(),
      .imagePath = prepared.image.sourcePath,
      .verifyMode = verifyMode,
      .reboot = m_upgradePage->rebootAfterFlash(),
      .prepareOnly = false};
  m_activeReportBuilder =
      std::make_shared<zzrk::workflow::FirmwareUpgradeReportBuilder>(request);
  m_activeReportBuilder->setPrepared(prepared);
  const auto reportBuilder = m_activeReportBuilder;
  const auto backend = m_backend;
  m_upgradeWatcher.setFuture(QtConcurrent::run(
      [guard, backend, request, prepared, cancellation,
       reportBuilder]() mutable {
        const auto progress = [guard, reportBuilder](
                                  const zzrk::workflow::FirmwareUpgradeEvent &event) {
          reportBuilder->observe(event);
          if (guard != nullptr)
            QMetaObject::invokeMethod(
                guard,
                [guard, event] {
                  if (guard != nullptr) guard->handleUpgradeProgress(event);
                },
                Qt::QueuedConnection);
        };
        try {
          return backend->executeUpgrade(
              request, std::move(prepared), progress,
              [cancellation] { return cancellation->load(); });
        } catch (...) {
          return zzrk::workflow::FirmwareUpgradeResult{
              unexpectedFirmwareUpgradeError(request.portPath)};
        }
      }));
  setPagesBusy(true);
}

void MainWorkspaceController::cancelUpgrade() {
  const auto cancellation =
      m_viewModel != nullptr ? m_viewModel->upgradeCancellation() : nullptr;
  if (!cancellation || !m_upgradeWatcher.isRunning() ||
      m_upgradePage == nullptr)
    return;
  cancellation->store(true);
  m_upgradePage->setStatus(QStringLiteral("正在等待当前 USB 分块完成..."));
  if (m_activeHistoryRow >= 0)
    m_upgradePage->updateHistoryResult(m_activeHistoryRow,
                                       QStringLiteral("正在取消"));
  updateUpgradeControls();
}

void MainWorkspaceController::handleUpgradeProgress(
    const zzrk::workflow::FirmwareUpgradeEvent &event) {
  if (m_shuttingDown || m_upgradePage == nullptr ||
      !m_upgradeWatcher.isRunning() || !m_progressTracker.has_value())
    return;
  const auto snapshot = m_progressTracker->update(event);
  m_upgradePage->setOverallProgress(snapshot.overallPercent);
  m_upgradePage->setOperationProgress(snapshot.operationPercent,
                                      !snapshot.operationDeterminate);
  QPointer<MainWorkspaceController> guard{this};
  if (m_viewModel != nullptr) {
    m_viewModel->setProgress(snapshot.overallPercent,
                             snapshot.operationPercent);
    if (guard == nullptr)
      return;
  }
  const auto stageText = zzrk::workflow::toString(event.stage);
  const QString stage = QString::fromLatin1(
      stageText.data(), static_cast<qsizetype>(stageText.size()));
  m_upgradePage->setStatus(
      event.target.empty()
          ? stage
          : QStringLiteral("%1：%2").arg(stage,
                                          QString::fromStdString(event.target)));
  emit upgradeProgressChanged(snapshot.overallPercent,
                              snapshot.operationPercent);
}

void MainWorkspaceController::handleUpgradeFinished() {
  if (m_shuttingDown || m_upgradePage == nullptr)
    return;
  const auto result = m_upgradeWatcher.result();
  if (m_activeReportBuilder != nullptr)
    m_activeReportBuilder->finish(result);
  auto report = std::move(*m_activeReportBuilder).takeReport();
  m_activeReportBuilder.reset();
  m_upgradePage->setOverallProgress(
      m_viewModel != nullptr ? m_viewModel->overallProgress() : 0);
  m_upgradePage->setOperationProgress(
      m_viewModel != nullptr ? m_viewModel->operationProgress() : 0);
  if (const auto *error =
          std::get_if<zzrk::workflow::FirmwareUpgradeError>(&result)) {
    const QString message = workflowErrorText(*error);
    m_upgradePage->setStatus(message);
    if (m_statusBar != nullptr)
      m_statusBar->showMessage(QStringLiteral("固件升级未完成"));
    finishHistoryEntry(
        error->code == zzrk::workflow::FirmwareUpgradeErrorCode::cancelled
            ? QStringLiteral("已取消")
            : QStringLiteral("失败"),
        message, std::move(report));
    if (m_showModalDialogs &&
        error->code != zzrk::workflow::FirmwareUpgradeErrorCode::cancelled)
      QMessageBox::critical(m_upgradePage, QStringLiteral("升级失败"), message);
  } else {
    const auto &success = std::get<zzrk::workflow::FirmwareUpgradeSuccess>(result);
    const auto snapshot = m_progressTracker.has_value()
                              ? m_progressTracker->complete()
                              : UpgradeProgressSnapshot{100, 100, true};
    m_upgradePage->setOverallProgress(snapshot.overallPercent);
    m_upgradePage->setOperationProgress(snapshot.operationPercent);
    QPointer<MainWorkspaceController> guard{this};
    if (m_viewModel != nullptr) {
      m_viewModel->setProgress(snapshot.overallPercent,
                               snapshot.operationPercent);
      if (guard == nullptr)
        return;
    }
    const QString message =
        QStringLiteral("升级完成：%1 字节，已校验 %2 个分区")
            .arg(success.partitionBytesWritten)
            .arg(success.partitionsVerified);
    m_upgradePage->setStatus(message);
    if (m_statusBar != nullptr)
      m_statusBar->showMessage(QStringLiteral("固件升级完成"));
    finishHistoryEntry(QStringLiteral("已完成"), message, std::move(report));
    if (m_showModalDialogs)
      QMessageBox::information(m_upgradePage, QStringLiteral("升级完成"), message);
  }
  m_progressTracker.reset();
  setPagesBusy(false);
  processPendingDeviceRefresh();
  emit upgradeCompleted();
}

void MainWorkspaceController::beginHistoryEntry(const QString &portPath) {
  if (m_upgradePage == nullptr)
    return;
  m_upgradePage->prependHistoryEntry(
      {.startedAt = QDateTime::currentDateTime().toString(
           QStringLiteral("yyyy-MM-dd HH:mm:ss")),
       .portPath = portPath,
       .imageName = QFileInfo{m_firmwarePath}.fileName(),
       .result = QStringLiteral("进行中"), .detail = {}, .reportId = {}});
  m_activeHistoryRow = 0;
  constexpr int maximumHistoryRows = 50;
  while (m_upgradePage->historyCount() > maximumHistoryRows) {
    const auto removed = m_upgradePage->removeLastHistoryEntry();
    if (removed.has_value() && m_viewModel != nullptr)
      m_viewModel->removeHistoryReport(*removed);
  }
  updateReportExportControls();
}

void MainWorkspaceController::finishHistoryEntry(
    const QString &result, const QString &detail,
    zzrk::workflow::FirmwareUpgradeReport report) {
  if (m_upgradePage == nullptr || m_activeHistoryRow < 0 ||
      m_viewModel == nullptr)
    return;
  const auto reportId = m_nextReportId++;
  QPointer<MainWorkspaceController> guard{this};
  m_viewModel->setHistoryReport(reportId, std::move(report));
  if (guard == nullptr)
    return;
  m_upgradePage->updateHistoryResult(m_activeHistoryRow, result, detail,
                                     reportId);
  m_upgradePage->selectHistoryRow(m_activeHistoryRow);
  m_activeHistoryRow = -1;
  updateReportExportControls();
}

void MainWorkspaceController::updateReportExportControls() {
  updateUpgradeControls();
}

void MainWorkspaceController::exportSelectedReport() {
  if (m_upgradePage == nullptr || m_viewModel == nullptr)
    return;
  const auto reportId = m_upgradePage->selectedReportId();
  if (!reportId.has_value())
    return;
  const auto found = m_viewModel->historyReports().find(*reportId);
  if (found == m_viewModel->historyReports().end())
    return;
  const QString suggested =
      QStringLiteral("%1-%2.json")
          .arg(m_upgradePage->selectedReportImageName(),
               QDateTime::currentDateTime().toString(
                   QStringLiteral("yyyyMMdd-HHmmss")));
  const QString path = QFileDialog::getSaveFileName(
      m_upgradePage, QStringLiteral("导出升级报告"), suggested,
      QStringLiteral("JSON 报告 (*.json)"));
  if (path.isEmpty())
    return;
  const auto error = zzrk::qt::writeFirmwareUpgradeReport(
      found->second, filesystemPath(path),
      {.includeFirmwarePath = false, .applicationVersion = ZZRK_VERSION});
  if (error.has_value()) {
    const QString message = QStringLiteral("无法导出报告：%1")
                                .arg(QString::fromStdString(error->message));
    if (m_statusBar != nullptr) m_statusBar->showMessage(message);
    if (m_showModalDialogs)
      QMessageBox::critical(m_upgradePage, QStringLiteral("导出失败"), message);
    return;
  }
  if (m_statusBar != nullptr)
    m_statusBar->showMessage(QStringLiteral("升级报告已导出"));
}

void MainWorkspaceController::updateUpgradeControls() {
  if (m_upgradePage == nullptr)
    return;
  const bool busy = usbOperationRunning();
  const bool canStart = m_preparedUpgrade.has_value() &&
                        selectedDeviceFlashable() &&
                        !selectedPortPath().isEmpty();
  bool canExport = false;
  if (m_viewModel != nullptr) {
    const auto reportId = m_upgradePage->selectedReportId();
    canExport = reportId.has_value() &&
                m_viewModel->historyReports().contains(*reportId);
  }
  const auto cancellation =
      m_viewModel != nullptr ? m_viewModel->upgradeCancellation() : nullptr;
  m_upgradePage->setBusy(busy, canStart,
                         m_upgradeWatcher.isRunning() && cancellation &&
                             !cancellation->load(),
                         canExport);
}

void MainWorkspaceController::syncDownloadDeviceFromTable() {
  if (m_downloadPage == nullptr || m_devicePage == nullptr)
    return;
  const int index = m_downloadPage->deviceCombo()->findData(selectedPortPath());
  if (index < 0 || index == m_downloadPage->deviceCombo()->currentIndex())
    return;
  const QSignalBlocker blocker{m_downloadPage->deviceCombo()};
  m_downloadPage->deviceCombo()->setCurrentIndex(index);
  handleDownloadDeviceChanged();
}

void MainWorkspaceController::syncDeviceTableFromDownload() {
  if (m_downloadPage != nullptr && m_devicePage != nullptr) {
    m_devicePage->selectPort(downloadPortPath());
  }
}

void MainWorkspaceController::handleDownloadDeviceChanged() {
  if (m_downloadPage == nullptr)
    return;
  QPointer<MainWorkspaceController> guard{this};
  syncDeviceTableFromDownload();
  if (guard == nullptr)
    return;
  if (m_downloadPage->layoutCombo()->currentIndex() == 0 &&
      m_viewModel != nullptr &&
      m_viewModel->downloadLayoutPort() != downloadPortPath()) {
    m_downloadLayout.reset();
    m_viewModel->setDownloadLayoutPort({});
    m_downloadPage->layoutPathEdit()->clear();
    rebuildDownloadTable();
  }
  updateDownloadControls();
}

void MainWorkspaceController::handleDownloadLayoutChanged() {
  if (m_downloadPage == nullptr)
    return;
  if (m_downloadPage->layoutCombo()->currentIndex() == 1) {
    if (!m_downloadParameterPath.empty()) {
      loadDownloadParameter(filesystemPathText(m_downloadParameterPath));
      return;
    }
    m_downloadLayout.reset();
    if (m_viewModel != nullptr)
      m_viewModel->setDownloadLayoutPort({});
    m_downloadPage->layoutPathEdit()->clear();
  } else if (m_devicePartitions.has_value() &&
             QString::fromStdString(m_devicePartitions->device.portPath) ==
                 downloadPortPath()) {
    setDevicePartitionTable(&*m_devicePartitions);
    return;
  } else {
    m_downloadLayout.reset();
    if (m_viewModel != nullptr)
      m_viewModel->setDownloadLayoutPort({});
    m_downloadPage->layoutPathEdit()->clear();
  }
  rebuildDownloadTable();
  matchDownloadDirectory();
}

void MainWorkspaceController::handleDownloadPlanChanged() {
  if (m_downloadPage == nullptr)
    return;
  for (const auto &choice : m_downloadPage->planChoices()) {
    const std::string key = choice.target.toStdString();
    m_downloadSelections.insert_or_assign(key, choice.selected);
    m_downloadVerifications.insert_or_assign(key, choice.verify);
  }
  updateDownloadControls();
}

void MainWorkspaceController::chooseDownloadParameter() {
  if (m_downloadPage == nullptr)
    return;
  const QString path = QFileDialog::getOpenFileName(
      m_downloadPage, QStringLiteral("选择 parameter.txt"), {},
      QStringLiteral("Rockchip parameter (parameter.txt *.txt);;所有文件 (*)"));
  if (!path.isEmpty())
    loadDownloadParameter(path);
}

void MainWorkspaceController::loadDownloadParameter(const QString &path) {
  if (m_downloadPage == nullptr || path.isEmpty() ||
      m_downloadPrepareWatcher.isRunning() ||
      m_imageDownloadWatcher.isRunning())
    return;
  const auto parameterPath = filesystemPath(path);
  auto parsed = zzrk::image::parseRockchipParameterFile(parameterPath);
  if (const auto *error =
          std::get_if<zzrk::image::FirmwareImageError>(&parsed)) {
    m_downloadPage->setStatus(
        QStringLiteral("parameter.txt 解析失败（错误 %1）")
            .arg(static_cast<int>(error->code)));
    return;
  }
  {
    const QSignalBlocker blocker{m_downloadPage->layoutCombo()};
    m_downloadPage->layoutCombo()->setCurrentIndex(1);
  }
  m_downloadParameterPath = parameterPath;
  m_downloadLayout =
      std::get<zzrk::image::RockchipParameterLayout>(std::move(parsed));
  if (m_viewModel != nullptr)
    m_viewModel->setDownloadLayoutPort({});
  m_downloadPage->layoutPathEdit()->setText(path);
  if (m_downloadDirectoryPath.empty()) {
    m_downloadDirectoryPath = parameterPath.parent_path();
    m_downloadPage->directoryEdit()->setText(
        filesystemPathText(m_downloadDirectoryPath));
  }
  rebuildDownloadTable();
  matchDownloadDirectory();
  m_downloadPage->setStatus(
      QStringLiteral("已加载 parameter.txt，共 %1 个分区")
          .arg(static_cast<qulonglong>(m_downloadLayout->partitions.size())));
}

void MainWorkspaceController::chooseDownloadDirectory() {
  if (m_downloadPage == nullptr)
    return;
  const QString path = QFileDialog::getExistingDirectory(
      m_downloadPage, QStringLiteral("选择镜像目录"),
      m_downloadPage->directoryEdit()->text());
  if (!path.isEmpty())
    loadDownloadDirectory(path);
}

void MainWorkspaceController::loadDownloadDirectory(const QString &path) {
  if (m_downloadPage == nullptr || path.isEmpty() ||
      m_downloadPrepareWatcher.isRunning() ||
      m_imageDownloadWatcher.isRunning())
    return;
  const QDir directory{path};
  if (!directory.exists()) {
    m_downloadPage->setStatus(QStringLiteral("镜像目录不存在"));
    return;
  }
  m_downloadDirectoryPath = filesystemPath(directory.absolutePath());
  m_downloadPage->directoryEdit()->setText(directory.absolutePath());
  matchDownloadDirectory();
}

void MainWorkspaceController::matchDownloadDirectory() {
  if (m_downloadPage == nullptr || m_downloadDirectoryPath.empty()) {
    updateDownloadControls();
    return;
  }
  const QDir directory{filesystemPathText(m_downloadDirectoryPath)};
  if (!directory.exists())
    return;
  std::unordered_map<std::string, std::filesystem::path> files;
  for (const QFileInfo &entry :
       directory.entryInfoList(QDir::Files | QDir::Readable, QDir::Name)) {
    files.insert_or_assign(entry.fileName().toLower().toStdString(),
                           filesystemPath(entry.absoluteFilePath()));
  }
  m_downloadImages.clear();
  m_downloadSelections.clear();
  m_validDownloadImages.clear();
  const auto findCandidate = [&](const std::string &stem) {
    for (const std::string &name : {stem + ".img", stem + ".bin", stem}) {
      const auto found =
          files.find(QString::fromStdString(name).toLower().toStdString());
      if (found != files.end())
        return found->second;
    }
    return std::filesystem::path{};
  };
  const auto loader = files.find("miniloaderall.bin");
  if (loader != files.end())
    assignDownloadImage("Loader", loader->second, false, false);
  if (m_downloadLayout.has_value()) {
    for (const auto &partition : m_downloadLayout->partitions) {
      auto image = findCandidate(partition.name);
      if (image.empty())
        image = findCandidate(slotBaseTarget(partition.name));
      if (!image.empty())
        assignDownloadImage(partition.name, image, false, false);
    }
  }
  rebuildDownloadTable();
  m_downloadPage->setStatus(
      QStringLiteral("已匹配 %1 个可下载目标")
          .arg(static_cast<qulonglong>(m_validDownloadImages.size())));
}

void MainWorkspaceController::assignDownloadImage(
    const std::string &target, const std::filesystem::path &path,
    const bool applySlotPair, const bool rebuild) {
  std::vector<std::string> targets{target};
  if (applySlotPair && target != "Loader" && slotBaseTarget(target) != target &&
      QFileInfo{filesystemPathText(path)}.completeBaseName().compare(
          QString::fromStdString(slotBaseTarget(target)),
          Qt::CaseInsensitive) == 0 &&
      m_downloadLayout.has_value()) {
    targets.clear();
    for (const auto &partition : m_downloadLayout->partitions) {
      if (slotBaseTarget(partition.name) == slotBaseTarget(target))
        targets.push_back(partition.name);
    }
  }
  std::optional<zzrk::image::FirmwarePayloadSource> payload;
  bool loaderValid = false;
  if (target == "Loader") {
    auto parsed = zzrk::image::parseRkLoaderImage(path);
    if (const auto *loader = std::get_if<zzrk::image::RkLoaderImage>(&parsed)) {
      loaderValid = loader->info.chipType == 0x33353838U;
    }
  } else {
    auto inspected = zzrk::image::inspectFirmwarePayload(path);
    if (const auto *source =
            std::get_if<zzrk::image::FirmwarePayloadSource>(&inspected)) {
      if (source->format == zzrk::image::PayloadFormat::raw ||
          source->format == zzrk::image::PayloadFormat::androidSparse)
        payload = *source;
    }
  }
  for (const auto &matched : targets) {
    bool valid = loaderValid;
    if (matched != "Loader" && payload.has_value() &&
        m_downloadLayout.has_value()) {
      const auto partition =
          std::ranges::find(m_downloadLayout->partitions, matched,
                            &zzrk::image::FirmwarePartition::name);
      valid = partition != m_downloadLayout->partitions.end();
      if (valid && partition->sectorCount.has_value()) {
        valid = *partition->sectorCount <=
                    std::numeric_limits<std::uint64_t>::max() / 512U &&
                payload->logicalSize <= *partition->sectorCount * 512U;
      }
    }
    m_downloadImages.insert_or_assign(matched, path);
    m_downloadSelections.insert_or_assign(matched, true);
    m_downloadVerifications.insert_or_assign(matched, true);
    if (valid)
      m_validDownloadImages.insert(matched);
    else
      m_validDownloadImages.erase(matched);
  }
  if (rebuild)
    rebuildDownloadTable();
}

void MainWorkspaceController::rebuildDownloadTable() {
  if (m_downloadPage == nullptr)
    return;
  const int partitionRows =
      m_downloadLayout.has_value()
          ? static_cast<int>(m_downloadLayout->partitions.size())
          : 0;
  const bool parameterLayout =
      m_downloadPage->layoutCombo()->currentIndex() == 1 &&
      m_downloadLayout.has_value();
  std::vector<ImageDownloadPage::PlanRow> rows;
  rows.reserve(
      static_cast<std::size_t>(1 + (parameterLayout ? 1 : 0) + partitionRows));
  const auto addRow = [&](const zzrk::workflow::ImageDownloadItemKind kind,
                          const std::string &target, const QString &lba,
                          const std::uint64_t capacity, const bool forced) {
    QString file = QStringLiteral("-");
    QString format = QStringLiteral("-");
    QString expanded = QStringLiteral("-");
    QString status = QStringLiteral("未选择");
    if (kind == zzrk::workflow::ImageDownloadItemKind::partitionTable) {
      file = filesystemPathText(m_downloadParameterPath);
      format = payloadFormatText(zzrk::image::PayloadFormat::rockchipParameter);
      expanded = dataSizeText(67U * 512U);
      status = QStringLiteral("就绪");
    } else if (const auto found = m_downloadImages.find(target);
               found != m_downloadImages.end()) {
      file = filesystemPathText(found->second);
      if (kind == zzrk::workflow::ImageDownloadItemKind::loader) {
        auto parsed = zzrk::image::parseRkLoaderImage(found->second);
        if (const auto *loader =
                std::get_if<zzrk::image::RkLoaderImage>(&parsed)) {
          format = QStringLiteral("Loader");
          expanded = dataSizeText(loader->info.idBlockSize);
        }
      } else {
        auto inspected = zzrk::image::inspectFirmwarePayload(found->second);
        if (const auto *source =
                std::get_if<zzrk::image::FirmwarePayloadSource>(&inspected)) {
          format = payloadFormatText(source->format);
          expanded = dataSizeText(source->logicalSize);
        }
      }
      status = m_validDownloadImages.contains(target)
                   ? QStringLiteral("就绪")
                   : QStringLiteral("镜像无效或超过分区容量");
    }
    rows.push_back({
        .kind = pageRowKind(kind),
        .target = QString::fromStdString(target),
        .lba = lba,
        .capacity =
            capacity == 0U ? QStringLiteral("-") : dataSizeText(capacity),
        .file = file,
        .format = format,
        .expandedSize = expanded,
        .status = status,
        .selected = forced || (m_downloadSelections.contains(target) &&
                               m_downloadSelections.at(target)),
        .selectionForced = forced,
        .verify = !m_downloadVerifications.contains(target) ||
                  m_downloadVerifications.at(target),
    });
  };
  addRow(zzrk::workflow::ImageDownloadItemKind::loader, "Loader",
         QStringLiteral("64"), 0U, false);
  if (parameterLayout)
    addRow(zzrk::workflow::ImageDownloadItemKind::partitionTable, "GPT",
           QStringLiteral("主 / 备"), 67U * 512U, true);
  if (m_downloadLayout.has_value()) {
    for (const auto &partition : m_downloadLayout->partitions) {
      const std::uint64_t capacity =
          partition.sectorCount.has_value() &&
                  *partition.sectorCount <=
                      std::numeric_limits<std::uint64_t>::max() / 512U
              ? *partition.sectorCount * 512U
              : 0U;
      addRow(zzrk::workflow::ImageDownloadItemKind::partition, partition.name,
             QString::number(static_cast<qulonglong>(partition.firstLba)),
             capacity, false);
    }
  }
  m_downloadPage->setPlan(std::move(rows));
  updateDownloadControls();
}

void MainWorkspaceController::chooseSelectedDownloadImage() {
  if (m_downloadPage == nullptr)
    return;
  const auto selected = m_downloadPage->currentTarget();
  if (!selected.has_value() ||
      selected->kind == ImageDownloadPage::RowKind::partitionTable)
    return;
  const std::string target = selected->target.toStdString();
  const QString path = QFileDialog::getOpenFileName(
      m_downloadPage,
      target == "Loader" ? QStringLiteral("选择 MiniLoaderAll.bin")
                         : QStringLiteral("选择分区镜像"),
      m_downloadPage->directoryEdit()->text(),
      target == "Loader"
          ? QStringLiteral(
                "Rockchip Loader (MiniLoaderAll.bin *.bin);;所有文件 (*)")
          : QStringLiteral("分区镜像 (*.img *.bin);;所有文件 (*)"));
  if (!path.isEmpty())
    assignDownloadImage(target, filesystemPath(path));
}

void MainWorkspaceController::clearSelectedDownloadImage() {
  if (m_downloadPage == nullptr)
    return;
  handleDownloadPlanChanged();
  const auto selected = m_downloadPage->currentTarget();
  if (!selected.has_value() ||
      selected->kind == ImageDownloadPage::RowKind::partitionTable)
    return;
  const std::string target = selected->target.toStdString();
  m_downloadImages.erase(target);
  m_downloadSelections.erase(target);
  m_downloadVerifications.erase(target);
  m_validDownloadImages.erase(target);
  rebuildDownloadTable();
}

std::optional<zzrk::workflow::ImageDownloadPrepareRequest>
MainWorkspaceController::imageDownloadPrepareRequest() const {
  if (m_downloadPage == nullptr)
    return std::nullopt;
  zzrk::workflow::ImageDownloadPrepareRequest request{};
  request.layoutSource =
      m_downloadPage->layoutCombo()->currentIndex() == 1
          ? zzrk::workflow::ImageDownloadLayoutSource::parameterFile
          : zzrk::workflow::ImageDownloadLayoutSource::deviceGpt;
  if (request.layoutSource ==
      zzrk::workflow::ImageDownloadLayoutSource::parameterFile) {
    if (!m_downloadLayout.has_value() || m_downloadParameterPath.empty())
      return std::nullopt;
    request.parameterPath = m_downloadParameterPath;
  } else if (m_downloadLayout.has_value()) {
    request.devicePartitions = m_downloadLayout->partitions;
    if (m_devicePartitions.has_value() && m_viewModel != nullptr &&
        m_viewModel->downloadLayoutPort() == downloadPortPath()) {
      request.deviceSectors = m_devicePartitions->deviceSectors;
    }
  }
  for (const auto &choice : m_downloadPage->planChoices()) {
    if (!choice.selected)
      continue;
    const std::string target = choice.target.toStdString();
    if (choice.kind == ImageDownloadPage::RowKind::partitionTable) {
      request.verifyPartitionTable = choice.verify;
      continue;
    }
    const auto path = m_downloadImages.find(target);
    if (path == m_downloadImages.end() ||
        !m_validDownloadImages.contains(target))
      return std::nullopt;
    if (choice.kind == ImageDownloadPage::RowKind::loader) {
      request.loaderPath = path->second;
      request.verifyLoader = choice.verify;
    } else {
      request.partitions.push_back({.target = target,
                                    .imagePath = path->second,
                                    .verify = choice.verify});
    }
  }
  if (request.layoutSource ==
          zzrk::workflow::ImageDownloadLayoutSource::deviceGpt &&
      request.loaderPath.empty() && request.partitions.empty())
    return std::nullopt;
  return request;
}

void MainWorkspaceController::startImageDownload() {
  if (m_downloadPage == nullptr || m_downloadPrepareWatcher.isRunning() ||
      m_imageDownloadWatcher.isRunning() || usbOperationRunning())
    return;
  const QString port = downloadPortPath();
  const auto request = imageDownloadPrepareRequest();
  if (port.isEmpty() || !request.has_value())
    return;
  if (m_showModalDialogs) {
    QStringList targets;
    if (!request->loaderPath.empty())
      targets << QStringLiteral("Loader");
    if (request->layoutSource ==
        zzrk::workflow::ImageDownloadLayoutSource::parameterFile) {
      targets << QStringLiteral("主/备 GPT");
    }
    for (const auto &partition : request->partitions)
      targets << QString::fromStdString(partition.target);
    const QString layout =
        request->layoutSource ==
                zzrk::workflow::ImageDownloadLayoutSource::parameterFile
            ? filesystemPathText(request->parameterPath)
            : QStringLiteral("设备当前 GPT");
    const auto answer = QMessageBox::warning(
        m_downloadPage, QStringLiteral("确认下载镜像"),
        QStringLiteral("端口：%1\n布局：%2\n目标：%3\n完成后重启：%4\n\n"
                       "所选目标的现有数据会被覆盖。")
            .arg(port, layout, targets.join(QStringLiteral("、")),
                 m_downloadPage->rebootCheckBox()->isChecked()
                     ? QStringLiteral("是")
                     : QStringLiteral("否")),
        QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
    if (answer != QMessageBox::Yes)
      return;
  }
  m_pendingDownloadRequest = {
      .portPath = port.toStdString(),
      .reboot = m_downloadPage->rebootCheckBox()->isChecked()};
  m_preparedDownload.reset();
  m_downloadPage->setProgress(0, 0, true, true);
  m_downloadPage->setStatus(QStringLiteral("正在检查镜像并计算 MD5..."));
  const auto backend = m_backend;
  m_downloadPrepareWatcher.setFuture(
      QtConcurrent::run([backend, request = *request] {
        try {
          return backend->prepareImageDownload(request);
        } catch (...) {
          return zzrk::workflow::PreparedImageDownloadResult{
              unexpectedImageDownloadError("images")};
        }
      }));
  updateDownloadControls();
}

void MainWorkspaceController::handleDownloadPrepareFinished() {
  if (m_shuttingDown || m_downloadPage == nullptr)
    return;
  auto result = m_downloadPrepareWatcher.result();
  if (const auto *error =
          std::get_if<zzrk::workflow::ImageDownloadError>(&result)) {
    m_downloadPage->setProgress(0, 0);
    m_downloadPage->setStatus(imageDownloadErrorText(*error));
    updateDownloadControls();
    processPendingDeviceRefresh();
    emit imageDownloadCompleted(false);
    return;
  }
  if (usbOperationRunning()) {
    m_downloadPage->setStatus(QStringLiteral("另一个 USB 操作正在运行"));
    updateDownloadControls();
    emit imageDownloadCompleted(false);
    return;
  }
  m_preparedDownload =
      std::get<zzrk::workflow::PreparedImageDownloadPlan>(std::move(result));
  const auto prepared = *m_preparedDownload;
  const auto cancellation =
      m_viewModel != nullptr ? m_viewModel->downloadCancellation() : nullptr;
  if (!cancellation) {
    emit imageDownloadCompleted(false);
    return;
  }
  cancellation->store(false);
  m_downloadPage->setProgress(0, 0, false, true);
  m_downloadPage->setStatus(QStringLiteral("正在启动镜像下载..."));
  const auto backend = m_backend;
  const auto request = m_pendingDownloadRequest;
  QPointer<MainWorkspaceController> guard{this};
  m_imageDownloadWatcher.setFuture(QtConcurrent::run(
      [guard, backend, request, prepared, cancellation]() mutable {
        const auto progress =
            [guard](const zzrk::workflow::ImageDownloadEvent &event) {
              if (guard != nullptr)
                QMetaObject::invokeMethod(
                    guard,
                    [guard, event] {
                      if (guard != nullptr)
                        guard->handleDownloadProgress(event);
                    },
                    Qt::QueuedConnection);
            };
        try {
          return backend->executeImageDownload(
              request, std::move(prepared), progress,
              [cancellation] { return cancellation->load(); });
        } catch (...) {
          return zzrk::workflow::ImageDownloadResult{
              unexpectedImageDownloadError(request.portPath)};
        }
      }));
  updateDownloadControls();
}

void MainWorkspaceController::cancelImageDownload() {
  const auto cancellation =
      m_viewModel != nullptr ? m_viewModel->downloadCancellation() : nullptr;
  if (!cancellation || !m_imageDownloadWatcher.isRunning() ||
      m_downloadPage == nullptr)
    return;
  cancellation->store(true);
  m_downloadPage->setStatus(QStringLiteral("正在等待当前 USB 分块完成..."));
  updateDownloadControls();
}

void MainWorkspaceController::handleDownloadProgress(
    const zzrk::workflow::ImageDownloadEvent &event) {
  if (m_shuttingDown || m_downloadPage == nullptr ||
      !m_imageDownloadWatcher.isRunning())
    return;
  const auto percent = [](const std::uint64_t done, const std::uint64_t total) {
    return total == 0U ? 0
                       : static_cast<int>(std::min<std::uint64_t>(
                             100U, done * 100U / total));
  };
  const int operation = percent(event.completedBytes, event.totalBytes);
  const int overall = percent(event.completedPlanBytes, event.totalPlanBytes);
  m_downloadPage->setProgress(overall, operation, false,
                              event.totalBytes == 0U);
  const QString stage = imageDownloadStageText(event.stage);
  m_downloadPage->setStatus(
      event.target.empty() ? stage
                           : QStringLiteral("%1：%2").arg(
                                 stage, QString::fromStdString(event.target)));
  if (event.itemIndex.has_value() && m_preparedDownload.has_value() &&
      *event.itemIndex < m_preparedDownload->items.size()) {
    const QString target = QString::fromStdString(
        m_preparedDownload->items[*event.itemIndex].target);
    m_downloadPage->setTargetStatus(
        target, event.totalBytes == 0U
                    ? stage
                    : QStringLiteral("%1 %2%").arg(stage).arg(operation));
  }
  if (m_viewModel != nullptr)
    m_viewModel->setProgress(overall, operation);
  emit imageDownloadProgressChanged(overall, operation);
}

void MainWorkspaceController::handleImageDownloadFinished() {
  if (m_shuttingDown || m_downloadPage == nullptr)
    return;
  const auto result = m_imageDownloadWatcher.result();
  m_downloadPage->setProgress(
      m_viewModel != nullptr ? m_viewModel->overallProgress() : 0,
      m_viewModel != nullptr ? m_viewModel->operationProgress() : 0);
  bool succeeded = false;
  if (const auto *error =
          std::get_if<zzrk::workflow::ImageDownloadError>(&result)) {
    const QString message = imageDownloadErrorText(*error);
    m_downloadPage->setStatus(message);
    if (error->itemIndex.has_value() && m_preparedDownload.has_value() &&
        *error->itemIndex < m_preparedDownload->items.size()) {
      const QString target = QString::fromStdString(
          m_preparedDownload->items[*error->itemIndex].target);
      m_downloadPage->setTargetStatus(
          target,
          error->code == zzrk::workflow::ImageDownloadErrorCode::cancelled
              ? QStringLiteral("已取消")
              : QStringLiteral("失败"));
    }
    if (m_showModalDialogs &&
        error->code != zzrk::workflow::ImageDownloadErrorCode::cancelled)
      QMessageBox::critical(m_downloadPage, QStringLiteral("下载失败"),
                            message);
  } else {
    const auto &success =
        std::get<zzrk::workflow::ImageDownloadSuccess>(result);
    m_downloadPage->setProgress(100, 100);
    m_downloadPage->setSelectedTargetsStatus(QStringLiteral("完成"));
    const QString message =
        QStringLiteral("下载完成：写入 %1 项，校验 %2 项，共 %3")
            .arg(success.itemsWritten)
            .arg(success.itemsVerified)
            .arg(dataSizeText(success.partitionBytesWritten));
    m_downloadPage->setStatus(message);
    succeeded = true;
    if (m_showModalDialogs)
      QMessageBox::information(m_downloadPage, QStringLiteral("下载完成"),
                               message);
  }
  m_preparedDownload.reset();
  updateDownloadControls();
  processPendingDeviceRefresh();
  emit imageDownloadCompleted(succeeded);
}

void MainWorkspaceController::updateDownloadControls() {
  const bool preparing = m_downloadPrepareWatcher.isRunning();
  const bool running = m_imageDownloadWatcher.isRunning();
  const bool busy = usbOperationRunning() || preparing;
  QPointer<MainWorkspaceController> guard{this};
  if (m_viewModel != nullptr) {
    m_viewModel->setBusy(busy);
    if (guard == nullptr)
      return;
  }
  if (m_downloadPage == nullptr)
    return;
  const auto request = imageDownloadPrepareRequest();
  bool maskromHasLoader = true;
  if (m_downloadPage->selectedDeviceMode() ==
      static_cast<int>(zzrk::transport::DeviceMode::Maskrom)) {
    maskromHasLoader = request.has_value() && !request->loaderPath.empty();
  }
  m_downloadPage->setBusy(busy,
                          !downloadPortPath().isEmpty() &&
                              request.has_value() && maskromHasLoader,
                          running);
  if (m_devicePage != nullptr)
    m_devicePage->setBusy(busy, selectedDeviceManageable());
  if (m_refreshButton != nullptr)
    m_refreshButton->setEnabled(!busy);
}

void MainWorkspaceController::setDevicePartitionTable(
    const zzrk::workflow::DevicePartitionTable *partitions) {
  if (partitions == nullptr) {
    m_devicePartitions.reset();
    if (m_downloadPage != nullptr &&
        m_downloadPage->layoutCombo()->currentIndex() == 0) {
      m_downloadLayout.reset();
      if (m_viewModel != nullptr)
        m_viewModel->setDownloadLayoutPort({});
      m_downloadPage->layoutPathEdit()->clear();
      rebuildDownloadTable();
    }
    return;
  }
  m_devicePartitions = *partitions;
  const QString port = QString::fromStdString(partitions->device.portPath);
  if (m_downloadPage == nullptr ||
      m_downloadPage->layoutCombo()->currentIndex() != 0 ||
      port != downloadPortPath())
    return;
  zzrk::image::RockchipParameterLayout layout{};
  layout.model = "device-gpt";
  for (const auto &partition : partitions->table.partitions) {
    layout.partitions.push_back({.name = partition.name,
                                 .firstLba = partition.firstLba,
                                 .sectorCount = partition.sectorCount(),
                                 .grow = false});
  }
  m_downloadLayout = std::move(layout);
  QPointer<MainWorkspaceController> guard{this};
  if (m_viewModel != nullptr) {
    m_viewModel->setDownloadLayoutPort(port);
    if (guard == nullptr)
      return;
  }
  m_downloadPage->layoutPathEdit()->setText(
      QStringLiteral("%1 / 设备 GPT").arg(port));
  rebuildDownloadTable();
  matchDownloadDirectory();
  m_downloadPage->setStatus(
      QStringLiteral("已读取设备 GPT，共 %1 个分区")
          .arg(static_cast<qulonglong>(m_downloadLayout->partitions.size())));
}

const std::optional<zzrk::workflow::DevicePartitionTable> &
MainWorkspaceController::devicePartitionTable() const noexcept {
  return m_devicePartitions;
}

const QString &MainWorkspaceController::partitionTableStatus() const noexcept {
  return m_partitionTableStatus;
}

void MainWorkspaceController::shutdown() noexcept {
  if (m_shuttingDown) {
    return;
  }
  m_shuttingDown = true;
  m_hotplugRefreshPending = false;
  if (m_hotplugDebounceTimer != nullptr) {
    m_hotplugDebounceTimer->stop();
  }
  if (m_backend && m_monitoringStarted) {
    m_backend->stopDeviceMonitoring();
    m_monitoringStarted = false;
  }
  if (m_viewModel != nullptr) {
    if (const auto token = m_viewModel->upgradeCancellation()) {
      token->store(true);
    }
    if (const auto token = m_viewModel->downloadCancellation()) {
      token->store(true);
    }
    if (const auto token = m_viewModel->partitionCancellation()) {
      token->store(true);
    }
    m_viewModel->setBusy(false);
  }
  disconnect(&m_scanWatcher, nullptr, this, nullptr);
  disconnect(&m_downloadPrepareWatcher, nullptr, this, nullptr);
  disconnect(&m_imageDownloadWatcher, nullptr, this, nullptr);
  disconnect(&m_inspectionWatcher, nullptr, this, nullptr);
  disconnect(&m_partitionTableWatcher, nullptr, this, nullptr);
  disconnect(&m_partitionOperationWatcher, nullptr, this, nullptr);
  disconnect(&m_prepareWatcher, nullptr, this, nullptr);
  disconnect(&m_upgradeWatcher, nullptr, this, nullptr);
  m_scanWatcher.waitForFinished();
  m_downloadPrepareWatcher.waitForFinished();
  m_imageDownloadWatcher.waitForFinished();
  m_inspectionWatcher.waitForFinished();
  m_partitionTableWatcher.waitForFinished();
  m_partitionOperationWatcher.waitForFinished();
  m_prepareWatcher.waitForFinished();
  m_upgradeWatcher.waitForFinished();
  if (m_lifecycle != nullptr) {
    m_lifecycle->m_monitoringStopped.store(true);
    m_lifecycle->m_allWatchersStopped.store(true);
    m_lifecycle->m_shutdownComplete.store(true);
  }
}

bool MainWorkspaceController::usbOperationRunning() const noexcept {
  return m_scanWatcher.isRunning() || m_downloadPrepareWatcher.isRunning() ||
         m_imageDownloadWatcher.isRunning() ||
         m_inspectionWatcher.isRunning() ||
         m_partitionTableWatcher.isRunning() || m_partitionOperationWatcher.isRunning() ||
         m_prepareWatcher.isRunning() || m_upgradeWatcher.isRunning() ||
         (m_externalBusyCallback && m_externalBusyCallback());
}

QString MainWorkspaceController::selectedPortPath() const {
  return m_viewModel != nullptr ? m_viewModel->selectedPortPath() : QString{};
}

QString MainWorkspaceController::downloadPortPath() const {
  return m_downloadPage != nullptr ? m_downloadPage->selectedPortPath()
                                   : QString{};
}

bool MainWorkspaceController::selectedDeviceFlashable() const {
  const auto *device = findDevice(m_lastScanResult, selectedPortPath());
  return device != nullptr && isSupportedFlashDevice(*device);
}

bool MainWorkspaceController::selectedDeviceManageable() const {
  const auto *device = findDevice(m_lastScanResult, selectedPortPath());
  return device != nullptr && isSupportedManagementDevice(*device);
}

void MainWorkspaceController::setPagesBusy(const bool busy) {
  if (m_devicePage != nullptr)
    m_devicePage->setBusy(busy, selectedDeviceManageable());
  updateDownloadControls();
  updateUpgradeControls();
  updatePartitionControls();
}

ZzFluentUI::ZzTabWidget *MainWorkspaceController::mainTabs() const noexcept {
  return m_tabs;
}

QLabel *MainWorkspaceController::connectionStatusText() const noexcept {
  return m_connectionText;
}

QToolButton *MainWorkspaceController::refreshButton() const noexcept {
  return m_refreshButton;
}

QLabel *MainWorkspaceController::lastScanText() const noexcept {
  return m_lastScanText;
}

} // namespace zzrk::gui
