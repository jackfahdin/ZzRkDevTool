// SPDX-License-Identifier: MIT

#pragma once

#include "GuiOperationBackend.hpp"
#include "MainWorkspaceViewModel.hpp"

#include <ZzCore/ZzResult.h>
#include <ZzPureTools/ZzPageInstance.h>

#include <QFutureWatcher>
#include <QObject>
#include <QPointer>
#include <QString>

#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <unordered_map>
#include <unordered_set>

class QLabel;
class QStatusBar;
class QTimer;
class QToolButton;
class QWidget;

namespace ZzFluentUI {
class ZzTabWidget;
}

namespace zzrk::gui {

class DevicePage;
class ImageDownloadPage;

/**
 * @brief 工作区级设备协调器。
 *
 * Controller 负责设备扫描、热插拔去抖和跨页面的取消/忙碌协调。页面只观察
 * viewModel()，不直接持有 backend 或 QFutureWatcher。
 */
class MainWorkspaceController final : public QObject {
  Q_OBJECT

public:
  explicit MainWorkspaceController(
      std::shared_ptr<IGuiOperationBackend> backend,
      bool showModalDialogs = true, QObject *parent = nullptr);
  ~MainWorkspaceController() override;

  MainWorkspaceController(const MainWorkspaceController &) = delete;
  MainWorkspaceController &operator=(const MainWorkspaceController &) = delete;

  [[nodiscard]] IGuiOperationBackend *backend() const noexcept;
  [[nodiscard]] MainWorkspaceViewModel *viewModel() const noexcept;
  [[nodiscard]] bool showModalDialogs() const noexcept;
  void setShowModalDialogs(bool show);

  void attachWorkspace(ZzFluentUI::ZzTabWidget *tabs, QStatusBar *statusBar);
  void setExternalBusyCallback(std::function<bool()> callback);
  void attachPages(ImageDownloadPage *downloadPage, DevicePage *devicePage);

  [[nodiscard]] ZzCore::ZzResult<std::unique_ptr<ZzPureTools::ZzPageInstance>>
  createInitialDownloadPage(QWidget *pageParent);

  void refreshDevices();
  void handleDeviceChange();
  void processPendingDeviceRefresh();
  void shutdown() noexcept;

  [[nodiscard]] bool usbOperationRunning() const noexcept;
  [[nodiscard]] QString selectedPortPath() const;
  [[nodiscard]] QString downloadPortPath() const;
  [[nodiscard]] bool selectedDeviceFlashable() const;
  [[nodiscard]] bool selectedDeviceManageable() const;
  void setPagesBusy(bool busy);
  void loadDownloadParameter(const QString &path);
  void loadDownloadDirectory(const QString &path);
  void setDevicePartitionTable(
      const zzrk::workflow::DevicePartitionTable *partitions);

  [[nodiscard]] ZzFluentUI::ZzTabWidget *mainTabs() const noexcept;
  [[nodiscard]] QLabel *connectionStatusText() const noexcept;
  [[nodiscard]] QToolButton *refreshButton() const noexcept;
  [[nodiscard]] QLabel *lastScanText() const noexcept;

signals:
  void scanCompleted();
  void deviceMonitoringError(const QString &message);
  void devicesChanged(const zzrk::transport::ScanResult &result);
  void selectedDeviceChanged();
  void readDownloadGptRequested();
  void imageDownloadCompleted(bool success);
  void imageDownloadProgressChanged(int overallPercent, int operationPercent);
  void deviceInspectionCompleted(bool success);

private slots:
  void handleScanFinished();
  void handleDeviceSelectionChanged();
  void inspectSelectedDevice();
  void handleInspectionFinished();
  void handleDownloadDeviceChanged();
  void handleDownloadLayoutChanged();
  void handleDownloadPlanChanged();
  void chooseDownloadParameter();
  void chooseDownloadDirectory();
  void chooseSelectedDownloadImage();
  void clearSelectedDownloadImage();
  void startImageDownload();
  void cancelImageDownload();
  void handleDownloadPrepareFinished();
  void handleImageDownloadFinished();
  void
  handleDeviceMonitoringError(const zzrk::transport::UsbHotplugError &error);

private:
  void applyScanResult(const zzrk::transport::ScanResult &result);
  void startMonitoring();
  void clearDeviceInspection();
  void syncDownloadDeviceFromTable();
  void syncDeviceTableFromDownload();
  void matchDownloadDirectory();
  void assignDownloadImage(const std::string &target,
                           const std::filesystem::path &path,
                           bool applySlotPair = true, bool rebuild = true);
  void rebuildDownloadTable();
  [[nodiscard]] std::optional<zzrk::workflow::ImageDownloadPrepareRequest>
  imageDownloadPrepareRequest() const;
  void handleDownloadProgress(const zzrk::workflow::ImageDownloadEvent &event);
  void updateDownloadControls();

  std::shared_ptr<IGuiOperationBackend> m_backend;
  std::unique_ptr<MainWorkspaceViewModel> m_viewModelOwner;
  QPointer<MainWorkspaceViewModel> m_viewModel;
  QFutureWatcher<zzrk::transport::ScanResult> m_scanWatcher;
  QFutureWatcher<zzrk::workflow::PreparedImageDownloadResult>
      m_downloadPrepareWatcher;
  QFutureWatcher<zzrk::workflow::ImageDownloadResult> m_imageDownloadWatcher;
  QFutureWatcher<zzrk::workflow::DeviceInspectionResult> m_inspectionWatcher;
  QPointer<ImageDownloadPage> m_downloadPage;
  QPointer<DevicePage> m_devicePage;
  ZzFluentUI::ZzTabWidget *m_tabs = nullptr;
  QStatusBar *m_statusBar = nullptr;
  QLabel *m_connectionText = nullptr;
  QToolButton *m_refreshButton = nullptr;
  QLabel *m_lastScanText = nullptr;
  QTimer *m_hotplugDebounceTimer = nullptr;
  zzrk::transport::ScanResult m_lastScanResult;
  QString m_deviceMonitoringError;
  bool m_hotplugRefreshPending = false;
  bool m_showModalDialogs = true;
  bool m_shuttingDown = false;
  bool m_monitoringStarted = false;
  std::function<bool()> m_externalBusyCallback;
  std::optional<zzrk::workflow::PreparedImageDownloadPlan> m_preparedDownload;
  std::optional<zzrk::workflow::DevicePartitionTable> m_devicePartitions;
  std::optional<zzrk::image::RockchipParameterLayout> m_downloadLayout;
  zzrk::workflow::ImageDownloadRequest m_pendingDownloadRequest;
  std::filesystem::path m_downloadParameterPath;
  std::filesystem::path m_downloadDirectoryPath;
  std::unordered_map<std::string, std::filesystem::path> m_downloadImages;
  std::unordered_map<std::string, bool> m_downloadSelections;
  std::unordered_map<std::string, bool> m_downloadVerifications;
  std::unordered_set<std::string> m_validDownloadImages;
};

} // namespace zzrk::gui
