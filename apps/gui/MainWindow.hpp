// SPDX-License-Identifier: MIT

#pragma once

#include "GuiOperationBackend.hpp"
#include "MainWorkspaceController.hpp"
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

    [[nodiscard]] zzrk::gui::MainWorkspaceController *workspaceController()
        const noexcept
    {
        return m_workspaceController.get();
    }

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
    void readSelectedDevicePartitions();
    void handlePartitionTableFinished();
    void backupSelectedPartition();
    void writeSelectedPartition();
    void eraseSelectedPartition();
    void cancelPartitionOperation();
    void handlePartitionOperationProgress(
        const zzrk::workflow::PartitionOperationProgress &progress);
    void handlePartitionOperationFinished();
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
    [[nodiscard]] bool usbOperationRunning() const;

    std::shared_ptr<zzrk::gui::IGuiOperationBackend> m_backend;
    // 保留旧页面实现期间的工作区协调器；页面迁移完成后由其独占业务状态。
    std::unique_ptr<zzrk::gui::MainWorkspaceController> m_workspaceController;
    QFutureWatcher<zzrk::workflow::PreparedFirmwareUpgradeResult>
        m_prepareWatcher;
    QFutureWatcher<zzrk::workflow::FirmwareUpgradeResult> m_upgradeWatcher;
    QFutureWatcher<zzrk::workflow::DevicePartitionTableResult>
        m_partitionTableWatcher;
    QFutureWatcher<zzrk::workflow::PartitionOperationResult>
        m_partitionOperationWatcher;
    QTabWidget *m_tabs = nullptr;
    QLabel *m_connectionIcon = nullptr;
    QLabel *m_connectionText = nullptr;
    QLabel *m_lastScanText = nullptr;
    QToolButton *m_refreshButton = nullptr;
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
    std::optional<zzrk::workflow::DevicePartitionTable> m_partitions;
    std::optional<zzrk::gui::UpgradeProgressTracker> m_progressTracker;
    std::shared_ptr<zzrk::workflow::FirmwareUpgradeReportBuilder>
        m_activeReportBuilder;
    std::unordered_map<std::uint64_t,
                       zzrk::workflow::FirmwareUpgradeReport>
        m_historyReports;
    std::uint64_t m_nextReportId{1U};
    int m_activeHistoryRow = -1;
    QString m_partitionPort;
    QString m_deviceMonitoringError;
    zzrk::workflow::PartitionOperationPhase m_partitionOperationPhase{
        zzrk::workflow::PartitionOperationPhase::read};
    bool m_showModalDialogs = true;
    bool m_shuttingDown = false;
};
