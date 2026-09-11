// SPDX-License-Identifier: MIT

#pragma once

#include <zzrk/workflow/FirmwareUpgradeReport.hpp>

#include <QObject>
#include <QString>

#include <atomic>
#include <cstdint>
#include <memory>
#include <unordered_map>

namespace zzrk::gui {

/**
 * @brief 跨工作区页面共享的 GUI 状态。
 *
 * 该对象只保存值和取消标志，不持有任何 QWidget。setter 约定在对象所属的
 * GUI 线程调用；后台线程只通过原子取消标志观察取消请求。
 */
class MainWorkspaceViewModel final : public QObject
{
    Q_OBJECT

public:
    explicit MainWorkspaceViewModel(QObject *parent = nullptr);
    ~MainWorkspaceViewModel() override = default;

    [[nodiscard]] const QString &selectedPortPath() const noexcept;
    void setSelectedPortPath(QString path);

    [[nodiscard]] const QString &inspectedPort() const noexcept;
    void setInspectedPort(QString path);

    [[nodiscard]] const QString &partitionPort() const noexcept;
    void setPartitionPort(QString path);

    [[nodiscard]] const QString &downloadLayoutPort() const noexcept;
    void setDownloadLayoutPort(QString path);

    [[nodiscard]] std::shared_ptr<std::atomic_bool>
    upgradeCancellation() const noexcept;
    void setUpgradeCancellation(std::shared_ptr<std::atomic_bool> token);

    [[nodiscard]] std::shared_ptr<std::atomic_bool>
    downloadCancellation() const noexcept;
    void setDownloadCancellation(std::shared_ptr<std::atomic_bool> token);

    [[nodiscard]] std::shared_ptr<std::atomic_bool>
    partitionCancellation() const noexcept;
    void setPartitionCancellation(std::shared_ptr<std::atomic_bool> token);

    [[nodiscard]] bool busy() const noexcept;
    void setBusy(bool busy);

    [[nodiscard]] int overallProgress() const noexcept;
    [[nodiscard]] int operationProgress() const noexcept;
    void setProgress(int overallPercent, int operationPercent);

    using HistoryReports = std::unordered_map<
        std::uint64_t, zzrk::workflow::FirmwareUpgradeReport>;

    [[nodiscard]] const HistoryReports &historyReports() const noexcept;
    void setHistoryReports(HistoryReports reports);
    void setHistoryReport(std::uint64_t id,
                          zzrk::workflow::FirmwareUpgradeReport report);
    void removeHistoryReport(std::uint64_t id);
    void clearHistoryReports();

signals:
    void deviceSelectionChanged();
    void busyChanged(bool busy);
    void progressChanged(int overallPercent, int operationPercent);
    void historyChanged();

private:
    QString m_selectedPortPath;
    QString m_inspectedPort;
    QString m_partitionPort;
    QString m_downloadLayoutPort;
    std::shared_ptr<std::atomic_bool> m_upgradeCancellation;
    std::shared_ptr<std::atomic_bool> m_downloadCancellation;
    std::shared_ptr<std::atomic_bool> m_partitionCancellation;
    bool m_busy = false;
    int m_overallProgress = 0;
    int m_operationProgress = 0;
    HistoryReports m_historyReports;
};

} // namespace zzrk::gui
