// SPDX-License-Identifier: MIT

#pragma once

#include "GuiOperationBackend.hpp"
#include "MainWorkspaceController.hpp"

#include <QMainWindow>

#include <memory>

class MainWindow final : public QMainWindow {
  Q_OBJECT

public:
  explicit MainWindow(QWidget *parent = nullptr);
  MainWindow(std::shared_ptr<zzrk::gui::IGuiOperationBackend> backend,
             bool showModalDialogs, QWidget *parent = nullptr);
  ~MainWindow() override;

  void loadFirmwareImage(const QString &path);
  void loadDownloadParameter(const QString &path);
  void loadDownloadDirectory(const QString &path);
  [[nodiscard]] zzrk::gui::MainWorkspaceController *workspaceController()
      const noexcept { return m_workspaceController.get(); }

signals:
  void scanCompleted();
  void firmwarePreparationCompleted(bool success);
  void upgradeProgressChanged(int overallPercent, int operationPercent);
  void upgradeCompleted();
  void deviceInspectionCompleted(bool success);
  void partitionTableCompleted(bool success);
  void partitionOperationCompleted(bool success);
  void imageDownloadCompleted(bool success);
  void imageDownloadProgressChanged(int overallPercent, int operationPercent);

private:
  std::unique_ptr<zzrk::gui::MainWorkspaceController> m_workspaceController;
};
