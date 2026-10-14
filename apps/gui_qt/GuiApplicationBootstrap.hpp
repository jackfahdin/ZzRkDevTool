// SPDX-License-Identifier: MIT

#pragma once

#include "GuiOperationBackend.hpp"

#include <ZzCore/ZzError.h>
#include <ZzCore/ZzResult.h>
#include <ZzPureTools/ZzPureApplication.h>

#include <memory>

namespace zzrk::gui {
class GuiSettingsStore;

struct GuiApplicationOptions {
  std::shared_ptr<IGuiOperationBackend> backend;
  bool showModalDialogs = true;
  bool smokeTest = false;
  // 测试注入临时存储；未提供时保持无持久化，避免访问用户真实配置。
  std::shared_ptr<GuiSettingsStore> settings;
};

void configureApplicationMetadata(
    ZzPureTools::ZzPureApplication &application);

[[nodiscard]] ZzCore::ZzResult<ZzPureTools::ZzApplicationWindow *>
buildGuiApplication(ZzPureTools::ZzPureApplication &application,
                    GuiApplicationOptions options = {});

void reportStartupFailure(const char *stage,
                          const ZzCore::ZzError &error);

} // namespace zzrk::gui
