// SPDX-License-Identifier: MIT

#include "MainWindow.hpp"
#include "pages/DevicePage.hpp"
#include "pages/FullUpgradePage.hpp"
#include "pages/ImageDownloadPage.hpp"
#include "pages/PartitionPage.hpp"

#include <QFrame>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QStatusBar>
#include <QStyle>
#include <QTabWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

#include <utility>

MainWindow::MainWindow(QWidget *parent)
    : MainWindow(zzrk::gui::createGuiOperationBackend(), true, parent) {}

MainWindow::MainWindow(std::shared_ptr<zzrk::gui::IGuiOperationBackend> backend,
                       bool showModalDialogs, QWidget *parent)
    : QMainWindow(parent),
      m_workspaceController(std::make_unique<zzrk::gui::MainWorkspaceController>(
          std::move(backend), showModalDialogs)) {
  setWindowTitle(QStringLiteral("ZzRkDevTool"));
  resize(980, 820);
  setMinimumSize(800, 700);
  auto *central = new QWidget(this);
  auto *layout = new QVBoxLayout(central);
  layout->setContentsMargins(14, 12, 14, 10);
  layout->setSpacing(10);
  auto *tools = new QHBoxLayout;
  auto *icon = new QLabel(central);
  icon->setObjectName(QStringLiteral("connectionStatusIcon"));
  icon->setFixedSize(18, 18);
  icon->setAccessibleName(QStringLiteral("连接状态"));
  auto *connection = new QLabel(QStringLiteral("正在检测设备..."), central);
  connection->setObjectName(QStringLiteral("connectionStatusText"));
  connection->setProperty("hotplugAvailable", true);
  auto font = connection->font();
  font.setBold(true);
  connection->setFont(font);
  auto *refresh = new QToolButton(central);
  refresh->setObjectName(QStringLiteral("refreshButton"));
  refresh->setIcon(style()->standardIcon(QStyle::SP_BrowserReload));
  refresh->setToolTip(QStringLiteral("立即刷新设备列表"));
  refresh->setAccessibleName(QStringLiteral("刷新设备列表"));
  refresh->setFixedSize(30, 30);
  tools->addWidget(icon);
  tools->addWidget(connection);
  tools->addStretch();
  tools->addWidget(refresh);
  layout->addLayout(tools);
  auto *separator = new QFrame(central);
  separator->setFrameShape(QFrame::HLine);
  separator->setFrameShadow(QFrame::Sunken);
  layout->addWidget(separator);
  auto *tabs = new QTabWidget(central);
  tabs->setObjectName(QStringLiteral("mainTabs"));
  auto *download = zzrk::gui::ImageDownloadPage::buildUi(tabs);
  auto *device = zzrk::gui::DevicePage::buildUi(tabs);
  auto *upgrade = zzrk::gui::FullUpgradePage::buildUi(tabs);
  auto *partition = zzrk::gui::PartitionPage::buildUi(tabs);
  tabs->addTab(download, QStringLiteral("下载镜像"));
  tabs->addTab(device, QStringLiteral("设备"));
  tabs->addTab(upgrade, QStringLiteral("完整升级"));
  tabs->addTab(partition, QStringLiteral("分区"));
  layout->addWidget(tabs, 1);
  setCentralWidget(central);
  auto *lastScan = new QLabel(QStringLiteral("尚未扫描"), this);
  lastScan->setObjectName(QStringLiteral("lastScanText"));
  statusBar()->showMessage(QStringLiteral("准备就绪"));
  auto *controller = m_workspaceController.get();
  controller->attachWorkspace(nullptr, statusBar());
  controller->attachPages(download, device, upgrade, partition);
  connect(refresh, &QToolButton::clicked, controller,
          &zzrk::gui::MainWorkspaceController::refreshDevices);
  connect(controller, &zzrk::gui::MainWorkspaceController::scanCompleted, this,
          &MainWindow::scanCompleted);
  connect(controller,
          &zzrk::gui::MainWorkspaceController::firmwarePreparationCompleted,
          this, &MainWindow::firmwarePreparationCompleted);
  connect(controller,
          &zzrk::gui::MainWorkspaceController::upgradeProgressChanged, this,
          &MainWindow::upgradeProgressChanged);
  connect(controller, &zzrk::gui::MainWorkspaceController::upgradeCompleted,
          this, &MainWindow::upgradeCompleted);
  connect(controller,
          &zzrk::gui::MainWorkspaceController::deviceInspectionCompleted, this,
          &MainWindow::deviceInspectionCompleted);
  connect(controller,
          &zzrk::gui::MainWorkspaceController::partitionTableCompleted, this,
          &MainWindow::partitionTableCompleted);
  connect(controller,
          &zzrk::gui::MainWorkspaceController::partitionOperationCompleted,
          this, &MainWindow::partitionOperationCompleted);
  connect(controller,
          &zzrk::gui::MainWorkspaceController::imageDownloadCompleted, this,
          &MainWindow::imageDownloadCompleted);
  connect(controller,
          &zzrk::gui::MainWorkspaceController::imageDownloadProgressChanged,
          this, &MainWindow::imageDownloadProgressChanged);
  Q_UNUSED(lastScan);
}

MainWindow::~MainWindow() {
  if (m_workspaceController != nullptr)
    m_workspaceController->shutdown();
}

void MainWindow::loadFirmwareImage(const QString &path) {
  m_workspaceController->loadFirmwareImage(path);
}
void MainWindow::loadDownloadParameter(const QString &path) {
  m_workspaceController->loadDownloadParameter(path);
}
void MainWindow::loadDownloadDirectory(const QString &path) {
  m_workspaceController->loadDownloadDirectory(path);
}
