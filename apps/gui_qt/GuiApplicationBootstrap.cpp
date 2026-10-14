// SPDX-License-Identifier: MIT

#include "GuiApplicationBootstrap.hpp"
#include "GuiWindowShell.hpp"
#include "WorkspaceUi.hpp"

#include <zzrk/logging/Logging.hpp>

#include <ZzFluentUI/ZzFontIcon.h>
#include <ZzFluentUI/ZzIconDescriptor.h>
#include <ZzPureTools/ZzApplicationBuilder.h>
#include <ZzPureTools/ZzApplicationWindow.h>
#include <ZzPureTools/ZzNavigationNode.h>
#include <ZzPureTools/ZzPageLifetimePolicy.h>
#include <ZzPureTools/ZzPageRegistration.h>
#include <ZzPureTools/ZzRouteId.h>

#include <QCoreApplication>
#include <QIcon>
#include <QPointer>
#include <QStatusBar>

#include <memory>
#include <string>
#include <utility>

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

ZzCore::ZzResult<ZzPureTools::ZzApplicationWindow *> buildGuiApplication(
    ZzPureTools::ZzPureApplication &application, GuiApplicationOptions options) {
  if (options.backend == nullptr) {
    options.backend = createGuiOperationBackend();
  }

  const ZzPureTools::ZzRouteId initialRoute{
      QStringLiteral("download-image")};
  auto initialWindow =
      std::make_shared<QPointer<ZzPureTools::ZzApplicationWindow>>();

  ZzPureTools::ZzApplicationBuilder builder;
  ZzPureTools::ZzPageRegistration registration;
  registration.routeId = initialRoute;
  registration.lifetime = ZzPureTools::ZzPageLifetimePolicy::Persistent;
  registration.factory = [](QWidget *pageParent) {
    auto *window = pageParent != nullptr
                       ? qobject_cast<ZzPureTools::ZzApplicationWindow *>(
                             pageParent->window())
                       : nullptr;
    auto *shell = window != nullptr ? GuiWindowShell::attachedTo(*window)
                                    : nullptr;
    if (shell == nullptr) {
      using PageInstance =
          std::unique_ptr<ZzPureTools::ZzPageInstance>;
      return ZzCore::ZzResult<PageInstance>::failure(ZzCore::ZzError(
          ZzCore::ZzErrorCode::InvalidState,
          QStringLiteral("下载镜像页面需要已装配的 GUI 窗口壳层")));
    }
    return shell->createInitialDownloadPage(pageParent);
  };
  auto pageResult = builder.addPage(std::move(registration));
  if (!pageResult) {
    return ZzCore::ZzResult<ZzPureTools::ZzApplicationWindow *>::failure(
        pageResult.error());
  }

  ZzPureTools::ZzNavigationNode node{
      initialRoute, QStringLiteral("ZzRkDevTool"),
      QStringLiteral("下载镜像"),
      ZzFluentUI::ZzIconDescriptor::fromFontIcon(
          ZzFluentUI::ZzFontIcon::Download)};
  auto navigationResult = builder.addNavigationNode(std::move(node));
  if (!navigationResult) {
    return ZzCore::ZzResult<ZzPureTools::ZzApplicationWindow *>::failure(
        navigationResult.error());
  }

  auto routeResult = builder.setInitialRoute(initialRoute);
  if (!routeResult) {
    return ZzCore::ZzResult<ZzPureTools::ZzApplicationWindow *>::failure(
        routeResult.error());
  }

  auto setupResult = builder.setWindowSetupCallback(
      [backend = options.backend, showModalDialogs = options.showModalDialogs,
       initialWindow](ZzPureTools::ZzApplicationWindow &window) {
        window.setWindowTitle(QStringLiteral("ZzRkDevTool"));
        window.setWindowIcon(ui::icon(ui::Icon::Device));
        window.setMinimumSize(800, 700);
        window.resize(980, 820);
        window.statusBar()->showMessage(QStringLiteral("准备就绪"));

        auto attached = GuiWindowShell::attach(
            window, backend, showModalDialogs);
        if (!attached) {
          return attached;
        }
        if (initialWindow->isNull()) {
          *initialWindow = &window;
        }
        return ZzCore::ZzResult<void>::success();
      });
  if (!setupResult) {
    return ZzCore::ZzResult<ZzPureTools::ZzApplicationWindow *>::failure(
        setupResult.error());
  }

  auto buildResult = builder.build(application);
  if (!buildResult) {
    return ZzCore::ZzResult<ZzPureTools::ZzApplicationWindow *>::failure(
        buildResult.error());
  }
  if (initialWindow->isNull()) {
    return ZzCore::ZzResult<ZzPureTools::ZzApplicationWindow *>::failure(
        ZzCore::ZzError(ZzCore::ZzErrorCode::InvalidState,
                        QStringLiteral("Builder 未返回初始 GUI 窗口")));
  }
  return ZzCore::ZzResult<ZzPureTools::ZzApplicationWindow *>::success(
      initialWindow->data());
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
