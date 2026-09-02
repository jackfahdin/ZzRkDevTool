// SPDX-License-Identifier: MIT

#pragma once

#include "../apps/gui/GuiApplicationBootstrap.hpp"

#include <ZzPureTools/ZzPureApplication.h>

#include <memory>

namespace zzrk::gui::test {

class GuiTestHarness final {
public:
  [[nodiscard]] static std::unique_ptr<GuiTestHarness> create(
      std::shared_ptr<IGuiOperationBackend> backend = {});

  ~GuiTestHarness();

  GuiTestHarness(const GuiTestHarness &) = delete;
  GuiTestHarness &operator=(const GuiTestHarness &) = delete;

  [[nodiscard]] ZzPureTools::ZzPureApplication *application() const noexcept;
  [[nodiscard]] const GuiApplicationOptions &options() const noexcept;

private:
  GuiTestHarness(std::unique_ptr<ZzPureTools::ZzPureApplication> application,
                 GuiApplicationOptions options);

  std::unique_ptr<ZzPureTools::ZzPureApplication> application_;
  GuiApplicationOptions options_;
};

} // namespace zzrk::gui::test
