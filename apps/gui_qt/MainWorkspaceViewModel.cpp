// SPDX-License-Identifier: MIT

#include "MainWorkspaceViewModel.hpp"

#include <algorithm>
#include <utility>

namespace zzrk::gui {

MainWorkspaceViewModel::MainWorkspaceViewModel(QObject *parent)
    : QObject(parent)
{
}

const QString &MainWorkspaceViewModel::selectedPortPath() const noexcept
{
    return m_selectedPortPath;
}

void MainWorkspaceViewModel::setSelectedPortPath(QString path)
{
    if (m_selectedPortPath == path) {
        return;
    }
    m_selectedPortPath = std::move(path);
    emit deviceSelectionChanged();
}

const QString &MainWorkspaceViewModel::inspectedPort() const noexcept
{
    return m_inspectedPort;
}

void MainWorkspaceViewModel::setInspectedPort(QString path)
{
    if (m_inspectedPort == path) {
        return;
    }
    m_inspectedPort = std::move(path);
    emit deviceSelectionChanged();
}

const QString &MainWorkspaceViewModel::partitionPort() const noexcept
{
    return m_partitionPort;
}

void MainWorkspaceViewModel::setPartitionPort(QString path)
{
    if (m_partitionPort == path) {
        return;
    }
    m_partitionPort = std::move(path);
    emit deviceSelectionChanged();
}

const QString &MainWorkspaceViewModel::downloadLayoutPort() const noexcept
{
    return m_downloadLayoutPort;
}

void MainWorkspaceViewModel::setDownloadLayoutPort(QString path)
{
    if (m_downloadLayoutPort == path) {
        return;
    }
    m_downloadLayoutPort = std::move(path);
    emit deviceSelectionChanged();
}

std::shared_ptr<std::atomic_bool>
MainWorkspaceViewModel::upgradeCancellation() const noexcept
{
    return m_upgradeCancellation;
}

void MainWorkspaceViewModel::setUpgradeCancellation(
    std::shared_ptr<std::atomic_bool> token)
{
    m_upgradeCancellation = std::move(token);
}

std::shared_ptr<std::atomic_bool>
MainWorkspaceViewModel::downloadCancellation() const noexcept
{
    return m_downloadCancellation;
}

void MainWorkspaceViewModel::setDownloadCancellation(
    std::shared_ptr<std::atomic_bool> token)
{
    m_downloadCancellation = std::move(token);
}

std::shared_ptr<std::atomic_bool>
MainWorkspaceViewModel::partitionCancellation() const noexcept
{
    return m_partitionCancellation;
}

void MainWorkspaceViewModel::setPartitionCancellation(
    std::shared_ptr<std::atomic_bool> token)
{
    m_partitionCancellation = std::move(token);
}

bool MainWorkspaceViewModel::busy() const noexcept
{
    return m_busy;
}

void MainWorkspaceViewModel::setBusy(const bool busy)
{
    if (m_busy == busy) {
        return;
    }
    m_busy = busy;
    emit busyChanged(m_busy);
}

int MainWorkspaceViewModel::overallProgress() const noexcept
{
    return m_overallProgress;
}

int MainWorkspaceViewModel::operationProgress() const noexcept
{
    return m_operationProgress;
}

void MainWorkspaceViewModel::setProgress(const int overallPercent,
                                         const int operationPercent)
{
    const int overall = std::clamp(overallPercent, 0, 100);
    const int operation = std::clamp(operationPercent, 0, 100);
    if (m_overallProgress == overall && m_operationProgress == operation) {
        return;
    }
    m_overallProgress = overall;
    m_operationProgress = operation;
    emit progressChanged(m_overallProgress, m_operationProgress);
}

} // namespace zzrk::gui
