// SPDX-License-Identifier: MIT

#pragma once

#include <QFutureWatcher>
#include <QMainWindow>

#include <zzrk/transport/UsbDeviceScanner.hpp>

class QCheckBox;
class QLabel;
class QTableWidget;
class QTimer;
class QToolButton;

class MainWindow final : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

signals:
    void scanCompleted();

private:
    void refreshDevices();
    void handleScanFinished();
    void applyScanResult(const zzrk::transport::ScanResult &result);
    void setAutoRefreshEnabled(bool enabled);

    zzrk::transport::UsbDeviceScanner m_scanner;
    QFutureWatcher<zzrk::transport::ScanResult> m_scanWatcher;
    QTableWidget *m_deviceTable = nullptr;
    QLabel *m_connectionIcon = nullptr;
    QLabel *m_connectionText = nullptr;
    QLabel *m_lastScanText = nullptr;
    QToolButton *m_refreshButton = nullptr;
    QCheckBox *m_autoRefreshCheckBox = nullptr;
    QTimer *m_autoRefreshTimer = nullptr;
    bool m_hasCompletedScan = false;
    bool m_shuttingDown = false;
};
