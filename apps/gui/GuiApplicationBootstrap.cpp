// SPDX-License-Identifier: MIT

#include "GuiApplicationBootstrap.hpp"

#include <zzrk/logging/Logging.hpp>

#include <QCoreApplication>

#include <string>

namespace zzrk::gui {

void configureApplicationMetadata(
    ZzPureTools::ZzPureApplication &application)
{
  Q_UNUSED(application);
  QCoreApplication::setOrganizationName(QStringLiteral("ZzRkDevTool"));
  QCoreApplication::setOrganizationDomain(QStringLiteral("github.com"));
  QCoreApplication::setApplicationName(QStringLiteral("ZzRkDevTool"));
  QCoreApplication::setApplicationVersion(QStringLiteral(ZZRK_VERSION));
}

void reportStartupFailure(const char *stage, const ZzCore::ZzError &error)
{
  const auto stageText = stage == nullptr ? std::string{"unknown"}
                                          : std::string{stage};
  auto message = stageText + ": "
      + error.technicalMessage().toStdString();
  if (!error.context().isEmpty()) {
    message += " (" + error.context().toStdString() + ")";
  }
  zzrk::logging::error(message);
}

} // namespace zzrk::gui
