// SPDX-License-Identifier: MIT

#pragma once

#include "GuiOperationBackend.hpp"

#include <ZzCore/ZzResult.h>
#include <ZzPureTools/ZzPageInstance.h>

#include <QObject>
#include <QPointer>
#include <QRect>

#include <memory>
#include <optional>
class QTimer;

class QWidget;

namespace ZzPureTools {
class ZzApplicationWindow;
class ZzWorkspaceShell;
} // namespace ZzPureTools

namespace zzrk::gui {

class MainWorkspaceController;
class GuiSettingsStore;
class SettingsPage;
class GuiAppearanceSettings;

class GuiWindowShell final : public QObject {
  Q_OBJECT

public:
  [[nodiscard]] static ZzCore::ZzResult<void>
  attach(ZzPureTools::ZzApplicationWindow &window,
         std::shared_ptr<IGuiOperationBackend> backend,
         bool showModalDialogs = true,
         std::shared_ptr<GuiSettingsStore> settings = {});

  [[nodiscard]] static GuiWindowShell *
  attachedTo(ZzPureTools::ZzApplicationWindow &window) noexcept;

  ~GuiWindowShell() override;

  GuiWindowShell(const GuiWindowShell &) = delete;
  GuiWindowShell &operator=(const GuiWindowShell &) = delete;

  [[nodiscard]] MainWorkspaceController *controller() const noexcept;
  [[nodiscard]] std::shared_ptr<GuiSettingsStore> settings() const noexcept;
  bool savePreferences();

  [[nodiscard]] ZzCore::ZzResult<std::unique_ptr<ZzPureTools::ZzPageInstance>>
  createInitialDownloadPage(QWidget *pageParent);

private:
  GuiWindowShell(ZzPureTools::ZzApplicationWindow &window,
                 std::shared_ptr<IGuiOperationBackend> backend,
                 bool showModalDialogs,
                 std::shared_ptr<GuiSettingsStore> settings);

  [[nodiscard]] ZzCore::ZzResult<void> initialize();
  void configureFixedTabs();
  void updateWindowTitle();
  void restorePreferences();
  void scheduleSave();
  bool eventFilter(QObject *watched, QEvent *event) override;

  QPointer<ZzPureTools::ZzApplicationWindow> window_;
  std::shared_ptr<IGuiOperationBackend> backend_;
  bool showModalDialogs_ = true;
  std::unique_ptr<ZzPureTools::ZzWorkspaceShell> workspace_;
  QPointer<MainWorkspaceController> controller_;
  std::shared_ptr<GuiSettingsStore> settings_;
  QPointer<SettingsPage> settingsPage_;
  GuiAppearanceSettings *appearance_ = nullptr;
  QTimer *saveTimer_ = nullptr;
  bool restoring_ = false;
  QSize initialWindowSize_;
  std::optional<QRect> initialRestoredGeometry_;
  bool initialWindowShown_ = false;
};

} // namespace zzrk::gui
