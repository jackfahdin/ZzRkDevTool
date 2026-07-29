// SPDX-License-Identifier: MIT

#pragma once

#include "GuiOperationBackend.hpp"
#include "UpgradeProgressTracker.hpp"

#include <zzrk/workflow/FirmwareUpgradeReport.hpp>

#include <QFutureWatcher>
#include <QMainWindow>

#include <atomic>
#include <filesystem>
#include <memory>
#include <optional>
#include <unordered_map>
#include <unordered_set>

class QCheckBox;
class QLabel;
class QComboBox;
class QLineEdit;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QTableWidget;
class QTabWidget;
class QTimer;
class QToolButton;

class MainWindow final : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    MainWindow(std::shared_ptr<zzrk::gui::IGuiOperationBackend> backend,
               bool showModalDialogs,
               QWidget *parent = nullptr);
    ~MainWindow() override;

    void loadFirmwareImage(const QString &path);
    void loadDownloadParameter(const QString &path);
    void loadDownloadDirectory(const QString &path);

signals:
    void scanCompleted();
    void firmwarePreparationCompleted(bool success);
    void upgradeProgressChanged(int overallPercent, int operationPercent);
    void upgradeCompleted();
    void deviceInspectionCompleted(bool success);
    void partitionTableCompleted(bool success);
    void partitionOperationCompleted(bool success);
    void imageDownloadCompleted(bool success);
    void imageDownloadProgressChanged(int overallPercent,
                                      int operationPercent);

private:
    void refreshDevices();
    void handleDeviceChange();
    void handleDeviceMonitoringError(
        const zzrk::transport::UsbHotplugError &error);
    void processPendingDeviceRefresh();
    void handleScanFinished();
    void applyScanResult(const zzrk::transport::ScanResult &result);
    void handleDeviceSelectionChanged();
    void inspectSelectedDevice();
    void handleInspectionFinished();
    void readSelectedDevicePartitions();
    void handlePartitionTableFinished();
    void backupSelectedPartition();
    void writeSelectedPartition();
    void eraseSelectedPartition();
    void cancelPartitionOperation();
    void handlePartitionOperationProgress(
        const zzrk::workflow::PartitionOperationProgress &progress);
    void handlePartitionOperationFinished();
    void handleDownloadDeviceChanged();
    void handleDownloadLayoutChanged();
    void chooseDownloadParameter();
    void chooseDownloadDirectory();
    void chooseSelectedDownloadImage();
    void clearSelectedDownloadImage();
    void rebuildDownloadTable();
    void matchDownloadDirectory();
    void startImageDownload();
    void cancelImageDownload();
    void handleDownloadPrepareFinished();
    void handleImageDownloadProgress(
        const zzrk::workflow::ImageDownloadEvent &event);
    void handleImageDownloadFinished();
    void updateDownloadControls();
    void chooseFirmwareImage();
    void handlePrepareFinished();
    void startUpgrade();
    void cancelUpgrade();
    void exportSelectedReport();
    void updateReportExportControls();
    void handleUpgradeProgress(
        const zzrk::workflow::FirmwareUpgradeEvent &event);
    void handleUpgradeFinished();
    void updateUpgradeControls();
    void clearDeviceInspection();
    void clearPartitionTable();
    void startPartitionOperation(
        zzrk::workflow::PartitionOperationPhase phase,
        const std::filesystem::path &path = {});
    void beginHistoryEntry(const QString &portPath);
    void finishHistoryEntry(
        const QString &result, const QString &detail,
        zzrk::workflow::FirmwareUpgradeReport report);
    [[nodiscard]] QString selectedPortPath() const;
    [[nodiscard]] QString selectedPartitionName() const;
    [[nodiscard]] QString downloadPortPath() const;
    [[nodiscard]] std::optional<zzrk::workflow::ImageDownloadPrepareRequest>
    imageDownloadPrepareRequest() const;
    void assignDownloadImage(const std::string &target,
                             const std::filesystem::path &path,
                             bool applySlotPair = true,
                             bool rebuild = true);
    void syncDownloadDeviceFromTable();
    void syncDeviceTableFromDownload();
    [[nodiscard]] bool usbOperationRunning() const;

    std::shared_ptr<zzrk::gui::IGuiOperationBackend> m_backend;
    QFutureWatcher<zzrk::transport::ScanResult> m_scanWatcher;
    QFutureWatcher<zzrk::workflow::PreparedFirmwareUpgradeResult>
        m_prepareWatcher;
    QFutureWatcher<zzrk::workflow::FirmwareUpgradeResult> m_upgradeWatcher;
    QFutureWatcher<zzrk::workflow::PreparedImageDownloadResult>
        m_downloadPrepareWatcher;
    QFutureWatcher<zzrk::workflow::ImageDownloadResult> m_imageDownloadWatcher;
    QFutureWatcher<zzrk::workflow::DeviceInspectionResult> m_inspectionWatcher;
    QFutureWatcher<zzrk::workflow::DevicePartitionTableResult>
        m_partitionTableWatcher;
    QFutureWatcher<zzrk::workflow::PartitionOperationResult>
        m_partitionOperationWatcher;
    QTabWidget *m_tabs = nullptr;
    QTableWidget *m_deviceTable = nullptr;
    QLabel *m_connectionIcon = nullptr;
    QLabel *m_connectionText = nullptr;
    QLabel *m_lastScanText = nullptr;
    QToolButton *m_refreshButton = nullptr;
    QTimer *m_hotplugDebounceTimer = nullptr;
    QComboBox *m_downloadDeviceCombo = nullptr;
    QComboBox *m_downloadLayoutCombo = nullptr;
    QLineEdit *m_downloadLayoutPathEdit = nullptr;
    QToolButton *m_chooseDownloadParameterButton = nullptr;
    QToolButton *m_readDownloadGptButton = nullptr;
    QLineEdit *m_downloadDirectoryEdit = nullptr;
    QToolButton *m_chooseDownloadDirectoryButton = nullptr;
    QTableWidget *m_downloadTable = nullptr;
    QPushButton *m_chooseDownloadImageButton = nullptr;
    QPushButton *m_clearDownloadImageButton = nullptr;
    QCheckBox *m_downloadRebootCheckBox = nullptr;
    QPushButton *m_startDownloadButton = nullptr;
    QPushButton *m_cancelDownloadButton = nullptr;
    QProgressBar *m_downloadOverallProgress = nullptr;
    QProgressBar *m_downloadOperationProgress = nullptr;
    QLabel *m_downloadStatus = nullptr;
    QPushButton *m_inspectDeviceButton = nullptr;
    QLabel *m_deviceDetailStatus = nullptr;
    QLabel *m_chipInfoValue = nullptr;
    QLabel *m_capabilityValue = nullptr;
    QLabel *m_flashIdValue = nullptr;
    QLabel *m_capacityValue = nullptr;
    QLabel *m_flashInfoValue = nullptr;
    QLabel *m_usbInterfaceValue = nullptr;
    QLineEdit *m_imagePathEdit = nullptr;
    QToolButton *m_chooseImageButton = nullptr;
    QPlainTextEdit *m_planPreview = nullptr;
    QComboBox *m_verifyModeCombo = nullptr;
    QCheckBox *m_rebootCheckBox = nullptr;
    QPushButton *m_startUpgradeButton = nullptr;
    QPushButton *m_cancelUpgradeButton = nullptr;
    QProgressBar *m_overallProgress = nullptr;
    QProgressBar *m_operationProgress = nullptr;
    QLabel *m_upgradeStatus = nullptr;
    QTableWidget *m_historyTable = nullptr;
    QToolButton *m_exportReportButton = nullptr;
    QToolButton *m_readPartitionsButton = nullptr;
    QTableWidget *m_partitionTable = nullptr;
    QCheckBox *m_verifyPartitionWriteCheckBox = nullptr;
    QPushButton *m_backupPartitionButton = nullptr;
    QPushButton *m_writePartitionButton = nullptr;
    QPushButton *m_erasePartitionButton = nullptr;
    QPushButton *m_cancelPartitionButton = nullptr;
    QProgressBar *m_partitionProgress = nullptr;
    QLabel *m_partitionStatus = nullptr;
    std::optional<zzrk::workflow::PreparedFirmwareUpgrade> m_preparedUpgrade;
    std::optional<zzrk::workflow::PreparedImageDownloadPlan>
        m_preparedDownload;
    std::optional<zzrk::workflow::DevicePartitionTable> m_partitions;
    std::optional<zzrk::image::RockchipParameterLayout> m_downloadLayout;
    std::optional<zzrk::gui::UpgradeProgressTracker> m_progressTracker;
    std::shared_ptr<std::atomic_bool> m_upgradeCancellation;
    std::shared_ptr<std::atomic_bool> m_downloadCancellation;
    std::shared_ptr<std::atomic_bool> m_partitionCancellation;
    std::shared_ptr<zzrk::workflow::FirmwareUpgradeReportBuilder>
        m_activeReportBuilder;
    std::unordered_map<std::uint64_t,
                       zzrk::workflow::FirmwareUpgradeReport>
        m_historyReports;
    std::uint64_t m_nextReportId{1U};
    int m_activeHistoryRow = -1;
    QString m_selectedPortCache;
    QString m_inspectedPort;
    QString m_partitionPort;
    QString m_downloadLayoutPort;
    std::filesystem::path m_downloadParameterPath;
    std::filesystem::path m_downloadDirectoryPath;
    std::unordered_map<std::string, std::filesystem::path> m_downloadImages;
    std::unordered_map<std::string, bool> m_downloadSelections;
    std::unordered_map<std::string, bool> m_downloadVerifications;
    std::unordered_set<std::string> m_validDownloadImages;
    zzrk::workflow::ImageDownloadRequest m_pendingDownloadRequest;
    QString m_deviceMonitoringError;
    zzrk::workflow::PartitionOperationPhase m_partitionOperationPhase{
        zzrk::workflow::PartitionOperationPhase::read};
    bool m_showModalDialogs = true;
    bool m_hotplugRefreshPending = false;
    bool m_shuttingDown = false;
};
