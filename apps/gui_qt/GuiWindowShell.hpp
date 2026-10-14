// SPDX-License-Identifier: MIT

#pragma once

#include "GuiOperationBackend.hpp"

#include <ZzCore/ZzResult.h>
#include <ZzPureTools/ZzPageInstance.h>

#include <QObject>
#include <QPointer>

#include <memory>

class QWidget;

namespace ZzPureTools {
class ZzApplicationWindow;
class ZzWorkspaceShell;
}

namespace zzrk::gui {

class MainWorkspaceController;

class GuiWindowShell final : public QObject {
  Q_OBJECT

public:
  [[nodiscard]] static ZzCore::ZzResult<void>
  attach(ZzPureTools::ZzApplicationWindow &window,
         std::shared_ptr<IGuiOperationBackend> backend,
         bool showModalDialogs = true);

  [[nodiscard]] static GuiWindowShell *
  attachedTo(ZzPureTools::ZzApplicationWindow &window) noexcept;

  ~GuiWindowShell() override;

  GuiWindowShell(const GuiWindowShell &) = delete;
  GuiWindowShell &operator=(const GuiWindowShell &) = delete;

  [[nodiscard]] MainWorkspaceController *controller() const noexcept;

  [[nodiscard]] ZzCore::ZzResult<std::unique_ptr<ZzPureTools::ZzPageInstance>>
  createInitialDownloadPage(QWidget *pageParent);

private:
  GuiWindowShell(ZzPureTools::ZzApplicationWindow &window,
                 std::shared_ptr<IGuiOperationBackend> backend,
                 bool showModalDialogs);

  [[nodiscard]] ZzCore::ZzResult<void> initialize();
  void configureFixedTabs();
  void updateWindowTitle();

  QPointer<ZzPureTools::ZzApplicationWindow> window_;
  std::shared_ptr<IGuiOperationBackend> backend_;
  bool showModalDialogs_ = true;
  std::unique_ptr<ZzPureTools::ZzWorkspaceShell> workspace_;
  QPointer<MainWorkspaceController> controller_;
};

} // namespace zzrk::gui
