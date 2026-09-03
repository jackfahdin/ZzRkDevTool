// SPDX-License-Identifier: MIT

#pragma once

#include "GuiOperationBackend.hpp"

#include <ZzCore/ZzError.h>
#include <ZzCore/ZzResult.h>
#include <ZzPureTools/ZzPureApplication.h>

#include <memory>

namespace zzrk::gui {

struct GuiApplicationOptions {
  std::shared_ptr<IGuiOperationBackend> backend;
  bool showModalDialogs = true;
  bool smokeTest = false;
};

void configureApplicationMetadata(
    ZzPureTools::ZzPureApplication &application);

[[nodiscard]] ZzCore::ZzResult<ZzPureTools::ZzApplicationWindow *>
buildGuiApplication(ZzPureTools::ZzPureApplication &application,
                    GuiApplicationOptions options = {});

void reportStartupFailure(const char *stage,
                          const ZzCore::ZzError &error);

} // namespace zzrk::gui
