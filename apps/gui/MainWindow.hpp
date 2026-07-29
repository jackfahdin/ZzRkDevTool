// SPDX-License-Identifier: MIT

#pragma once

#include "GuiOperationBackend.hpp"
#include "UpgradeProgressTracker.hpp"

#include <QFutureWatcher>
#include <QMainWindow>

#include <atomic>
#include <memory>
#include <optional>

class QCheckBox;
class QLabel;
class QComboBox;
class QLineEdit;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QTableWidget;
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

signals:
    void scanCompleted();
    void firmwarePreparationCompleted(bool success);
    void upgradeProgressChanged(int overallPercent, int operationPercent);
    void upgradeCompleted();

private:
    void refreshDevices();
    void handleScanFinished();
    void applyScanResult(const zzrk::transport::ScanResult &result);
    void setAutoRefreshEnabled(bool enabled);
    void chooseFirmwareImage();
    void handlePrepareFinished();
    void startUpgrade();
    void cancelUpgrade();
    void handleUpgradeProgress(
        const zzrk::workflow::FirmwareUpgradeEvent &event);
    void handleUpgradeFinished();
    void updateUpgradeControls();
    void beginHistoryEntry(const QString &portPath);
    void finishHistoryEntry(const QString &result, const QString &detail);
    [[nodiscard]] QString selectedPortPath() const;

    std::shared_ptr<zzrk::gui::IGuiOperationBackend> m_backend;
    QFutureWatcher<zzrk::transport::ScanResult> m_scanWatcher;
    QFutureWatcher<zzrk::workflow::PreparedFirmwareUpgradeResult>
        m_prepareWatcher;
    QFutureWatcher<zzrk::workflow::FirmwareUpgradeResult> m_upgradeWatcher;
    QTableWidget *m_deviceTable = nullptr;
    QLabel *m_connectionIcon = nullptr;
    QLabel *m_connectionText = nullptr;
    QLabel *m_lastScanText = nullptr;
    QToolButton *m_refreshButton = nullptr;
    QCheckBox *m_autoRefreshCheckBox = nullptr;
    QTimer *m_autoRefreshTimer = nullptr;
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
    std::optional<zzrk::workflow::PreparedFirmwareUpgrade> m_preparedUpgrade;
    std::optional<zzrk::gui::UpgradeProgressTracker> m_progressTracker;
    std::shared_ptr<std::atomic_bool> m_upgradeCancellation;
    int m_activeHistoryRow = -1;
    bool m_showModalDialogs = true;
    bool m_hasCompletedScan = false;
    bool m_shuttingDown = false;
};
