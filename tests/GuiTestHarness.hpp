// SPDX-License-Identifier: MIT

#pragma once

#include "../apps/gui_qt/GuiApplicationBootstrap.hpp"

#include <ZzPureTools/ZzPureApplication.h>

#include <QPointer>
#include <QString>

#include <memory>

namespace ZzPureTools {
class ZzApplicationWindow;
}

namespace zzrk::gui {
class MainWorkspaceController;
}

namespace zzrk::gui::test {

class GuiTestHarness final {
public:
  [[nodiscard]] static std::unique_ptr<GuiTestHarness> create(
      std::shared_ptr<IGuiOperationBackend> backend = {},
      std::shared_ptr<GuiSettingsStore> settings = {});

  ~GuiTestHarness();

  GuiTestHarness(const GuiTestHarness &) = delete;
  GuiTestHarness &operator=(const GuiTestHarness &) = delete;

  [[nodiscard]] ZzPureTools::ZzPureApplication *application() const noexcept;
  [[nodiscard]] ZzPureTools::ZzApplicationWindow *window() const noexcept;
  [[nodiscard]] MainWorkspaceController *controller() const noexcept;
  [[nodiscard]] const GuiApplicationOptions &options() const noexcept;

  void loadFirmwareImage(const QString &path);
  void loadDownloadParameter(const QString &path);
  void loadDownloadDirectory(const QString &path);

private:
  GuiTestHarness(std::unique_ptr<ZzPureTools::ZzPureApplication> application,
                 GuiApplicationOptions options,
                 ZzPureTools::ZzApplicationWindow *window);

  std::unique_ptr<ZzPureTools::ZzPureApplication> application_;
  GuiApplicationOptions options_;
  QPointer<ZzPureTools::ZzApplicationWindow> window_;
};

} // namespace zzrk::gui::test
