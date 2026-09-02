// SPDX-License-Identifier: MIT

#include "MainWorkspaceController.hpp"

#include <ZzFluentUI/ZzTabWidget.h>

#include <QDateTime>
#include <QLabel>
#include <QMetaObject>
#include <QStatusBar>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>
#include <QtConcurrent/QtConcurrentRun>

#include <ZzCore/ZzErrorCode.h>

#include <exception>
#include <stdexcept>
#include <utility>

namespace zzrk::gui {

namespace {

[[nodiscard]] ZzCore::ZzResult<std::unique_ptr<ZzPureTools::ZzPageInstance>>
pageFailure(QString message)
{
    return ZzCore::ZzResult<std::unique_ptr<ZzPureTools::ZzPageInstance>>::failure(
        ZzCore::ZzError(ZzCore::ZzErrorCode::InvalidArgument,
                        std::move(message)));
}

} // namespace

MainWorkspaceController::MainWorkspaceController(
    std::shared_ptr<IGuiOperationBackend> backend,
    const bool showModalDialogs,
    QObject *parent)
    : QObject(parent)
    , m_backend(std::move(backend))
    , m_viewModelOwner(std::make_unique<MainWorkspaceViewModel>())
    , m_viewModel(m_viewModelOwner.get())
    , m_showModalDialogs(showModalDialogs)
{
    if (!m_backend) {
        throw std::invalid_argument("MainWorkspaceController requires a GUI backend");
    }
    m_viewModel->setUpgradeCancellation(
        std::make_shared<std::atomic_bool>(false));
    m_viewModel->setDownloadCancellation(
        std::make_shared<std::atomic_bool>(false));
    m_viewModel->setPartitionCancellation(
        std::make_shared<std::atomic_bool>(false));

    connect(&m_scanWatcher,
            &QFutureWatcher<zzrk::transport::ScanResult>::finished,
            this, &MainWorkspaceController::handleScanFinished);
}

MainWorkspaceController::~MainWorkspaceController()
{
    shutdown();
}

IGuiOperationBackend *MainWorkspaceController::backend() const noexcept
{
    return m_backend.get();
}

MainWorkspaceViewModel *MainWorkspaceController::viewModel() const noexcept
{
    return m_viewModel.data();
}

bool MainWorkspaceController::showModalDialogs() const noexcept
{
    return m_showModalDialogs;
}

void MainWorkspaceController::setShowModalDialogs(const bool show)
{
    m_showModalDialogs = show;
}

void MainWorkspaceController::attachWorkspace(
    ZzFluentUI::ZzTabWidget *tabs, QStatusBar *statusBar)
{
    if (m_shuttingDown) {
        return;
    }
    m_tabs = tabs;
    if (m_tabs != nullptr) {
        m_tabs->setObjectName(QStringLiteral("mainTabs"));
    }
    m_statusBar = statusBar;

    QWidget *host = tabs != nullptr && tabs->parentWidget() != nullptr
        ? tabs->parentWidget()
        : (statusBar != nullptr && statusBar->parentWidget() != nullptr
               ? statusBar->parentWidget()
               : (statusBar != nullptr ? statusBar
                                        : static_cast<QWidget *>(tabs)));
    if (host == nullptr) {
        return;
    }
    m_connectionText = host->findChild<QLabel *>(
        QStringLiteral("connectionStatusText"));
    if (m_connectionText == nullptr) {
        m_connectionText = new QLabel(QStringLiteral("正在检测设备..."), host);
        m_connectionText->setObjectName(QStringLiteral("connectionStatusText"));
        m_connectionText->setProperty("hotplugAvailable", true);
        m_connectionText->setAccessibleName(QStringLiteral("连接状态"));
    }

    m_refreshButton = host->findChild<QToolButton *>(
        QStringLiteral("refreshButton"));
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
    connect(m_refreshButton, &QToolButton::clicked,
            this, &MainWorkspaceController::refreshDevices);
    connect(m_hotplugDebounceTimer, &QTimer::timeout,
            this, &MainWorkspaceController::processPendingDeviceRefresh);

    if (m_statusBar != nullptr) {
        m_statusBar->addPermanentWidget(m_lastScanText);
    }
    startMonitoring();
    QTimer::singleShot(0, this, &MainWorkspaceController::refreshDevices);
}

void MainWorkspaceController::setExternalBusyCallback(
    std::function<bool()> callback)
{
    m_externalBusyCallback = std::move(callback);
}

ZzCore::ZzResult<std::unique_ptr<ZzPureTools::ZzPageInstance>>
MainWorkspaceController::createInitialDownloadPage(QWidget *pageParent)
{
    if (pageParent == nullptr) {
        return pageFailure(QStringLiteral("下载镜像页面父对象不能为空"));
    }

    auto *view = new QWidget(pageParent);
    view->setObjectName(QStringLiteral("downloadImagePage"));
    auto *layout = new QVBoxLayout(view);
    layout->setContentsMargins(12, 12, 12, 12);
    auto *title = new QLabel(QStringLiteral("下载镜像"), view);
    title->setObjectName(QStringLiteral("downloadImagePageTitle"));
    layout->addWidget(title);
    layout->addStretch();

    if (m_viewModel == nullptr || !m_viewModelOwner) {
        return pageFailure(QStringLiteral("共享工作区状态已被页面接管"));
    }
    std::unique_ptr<QObject> pageViewModel{m_viewModelOwner.release()};
    auto presenter = std::make_unique<QObject>();
    auto result = ZzPureTools::ZzPageInstance::create(
        pageParent, view, std::move(pageViewModel), std::move(presenter));
    if (!result) {
        return pageFailure(QStringLiteral("无法创建下载镜像页面"));
    }
    QPointer<MainWorkspaceController> guard{this};
    if (result) {
        result.value()->addCancellation([guard] {
            if (guard != nullptr) {
                guard->shutdown();
            }
        });
    }
    return result;
}

void MainWorkspaceController::startMonitoring()
{
    if (m_monitoringStarted || m_shuttingDown || !m_backend) {
        return;
    }
    m_monitoringStarted = true;
    const auto monitoringError = m_backend->startDeviceMonitoring(
        [this](zzrk::transport::UsbHotplugEvent) {
            QMetaObject::invokeMethod(
                this, [this] { handleDeviceChange(); }, Qt::QueuedConnection);
        },
        [this](const zzrk::transport::UsbHotplugError &error) {
            QMetaObject::invokeMethod(
                this,
                [this, error] { handleDeviceMonitoringError(error); },
                Qt::QueuedConnection);
        });
    if (monitoringError.has_value()) {
        handleDeviceMonitoringError(*monitoringError);
    }
}

void MainWorkspaceController::refreshDevices()
{
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
            result.error = zzrk::transport::ScanError{-1,
                                                       "Unknown USB scan failure"};
            return result;
        }
    }));
}

void MainWorkspaceController::handleDeviceChange()
{
    if (m_shuttingDown || !m_deviceMonitoringError.isEmpty()) {
        return;
    }
    m_hotplugRefreshPending = true;
    if (!usbOperationRunning() && m_hotplugDebounceTimer != nullptr) {
        m_hotplugDebounceTimer->start();
    }
}

void MainWorkspaceController::handleDeviceMonitoringError(
    const zzrk::transport::UsbHotplugError &error)
{
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

void MainWorkspaceController::processPendingDeviceRefresh()
{
    if (m_shuttingDown || !m_hotplugRefreshPending || usbOperationRunning()) {
        return;
    }
    refreshDevices();
}

void MainWorkspaceController::handleScanFinished()
{
    if (m_shuttingDown) {
        return;
    }
    if (m_viewModel != nullptr) {
        m_viewModel->setBusy(false);
    }
    applyScanResult(m_scanWatcher.result());
    if (m_refreshButton != nullptr) {
        m_refreshButton->setEnabled(true);
    }
    emit scanCompleted();
}

void MainWorkspaceController::applyScanResult(
    const zzrk::transport::ScanResult &result)
{
    m_lastScanResult = result;
    if (m_lastScanText != nullptr) {
        m_lastScanText->setText(
            QStringLiteral("更新于 %1")
                .arg(QDateTime::currentDateTime().toString(
                    QStringLiteral("HH:mm:ss"))));
    }
    if (m_connectionText != nullptr) {
        const auto count = static_cast<qulonglong>(result.devices.size());
        m_connectionText->setText(
            result.error.has_value()
                ? (count == 0U ? QStringLiteral("设备扫描失败")
                               : QStringLiteral("已检测到 %1 台设备（结果可能不完整）")
                                     .arg(count))
                : (count == 0U ? QStringLiteral("未检测到 Rockchip 设备")
                               : QStringLiteral("已连接 %1 台 Rockchip 设备")
                                     .arg(count)));
    }
    emit devicesChanged(result);
}

void MainWorkspaceController::shutdown() noexcept
{
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
    m_scanWatcher.waitForFinished();
}

bool MainWorkspaceController::usbOperationRunning() const noexcept
{
    return m_scanWatcher.isRunning()
        || (m_viewModel != nullptr && m_viewModel->busy())
        || (m_externalBusyCallback && m_externalBusyCallback());
}

QString MainWorkspaceController::selectedPortPath() const
{
    return m_viewModel != nullptr ? m_viewModel->selectedPortPath() : QString{};
}

QString MainWorkspaceController::downloadPortPath() const
{
    return m_viewModel != nullptr ? m_viewModel->downloadLayoutPort() : QString{};
}

ZzFluentUI::ZzTabWidget *MainWorkspaceController::mainTabs() const noexcept
{
    return m_tabs;
}

QLabel *MainWorkspaceController::connectionStatusText() const noexcept
{
    return m_connectionText;
}

QToolButton *MainWorkspaceController::refreshButton() const noexcept
{
    return m_refreshButton;
}

QLabel *MainWorkspaceController::lastScanText() const noexcept
{
    return m_lastScanText;
}

} // namespace zzrk::gui
