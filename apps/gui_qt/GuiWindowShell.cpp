// SPDX-License-Identifier: MIT

#include "GuiWindowShell.hpp"

#include "MainWorkspaceController.hpp"
#include "MainWorkspaceViewModel.hpp"
#include "pages/ImageDownloadPage.hpp"

#include <ZzCore/ZzError.h>
#include <ZzCore/ZzErrorCode.h>
#include <ZzFluentUI/ZzActivityBar.h>
#include <ZzFluentUI/ZzFluentTitleBar.h>
#include <ZzFluentUI/ZzFontIcon.h>
#include <ZzFluentUI/ZzIconDescriptor.h>
#include <ZzFluentUI/ZzNavigationPane.h>
#include <ZzFluentUI/ZzSidePane.h>
#include <ZzFluentUI/ZzSidePaneEdge.h>
#include <ZzFluentUI/ZzTabBar.h>
#include <ZzFluentUI/ZzTabWidget.h>
#include <ZzFluentUI/ZzThemeController.h>
#include <ZzFluentUI/ZzThemeMode.h>
#include <ZzPureTools/ZzApplicationWindow.h>
#include <ZzPureTools/ZzPureApplication.h>
#include <ZzPureTools/ZzPageHost.h>
#include <ZzPureTools/ZzWorkspaceCenterMode.h>
#include <ZzPureTools/ZzWorkspacePanelId.h>
#include <ZzPureTools/ZzWorkspaceShell.h>
#include <ZzPureTools/ZzWorkspaceTitleMode.h>

#include <QBoxLayout>
#include <QApplication>
#include <QCoreApplication>
#include <QEvent>
#include <QStatusBar>
#include <QThread>
#include <QWidget>

#include <array>
#include <exception>
#include <utility>

namespace zzrk::gui {

namespace {

const ZzPureTools::ZzWorkspacePanelId kNavigationPanelId{
    QStringLiteral("zzrk-internal-navigation")};

struct ApplicationBodySnapshot final {
  QPointer<QWidget> body;
  QPointer<QBoxLayout> layout;
  QPointer<ZzFluentUI::ZzNavigationPane> navigation;
  QPointer<ZzPureTools::ZzPageHost> pageHost;
  int navigationIndex = -1;
  int pageHostIndex = -1;
  int navigationStretch = 0;
  int pageHostStretch = 0;
  Qt::Alignment navigationAlignment;
  Qt::Alignment pageHostAlignment;

  [[nodiscard]] bool isValid() const noexcept {
    return body != nullptr && layout != nullptr && navigation != nullptr &&
           pageHost != nullptr && navigationIndex >= 0 && pageHostIndex >= 0;
  }
};

[[nodiscard]] ApplicationBodySnapshot
captureApplicationBody(ZzPureTools::ZzApplicationWindow &window) {
  ApplicationBodySnapshot snapshot;
  snapshot.body = window.centralWidget();
  snapshot.layout = snapshot.body != nullptr
                        ? qobject_cast<QBoxLayout *>(snapshot.body->layout())
                        : nullptr;
  snapshot.navigation = window.navigationPane();
  snapshot.pageHost = window.pageHost();
  if (snapshot.layout == nullptr)
    return snapshot;

  snapshot.navigationIndex = snapshot.layout->indexOf(snapshot.navigation);
  snapshot.pageHostIndex = snapshot.layout->indexOf(snapshot.pageHost);
  if (snapshot.navigationIndex >= 0) {
    snapshot.navigationStretch =
        snapshot.layout->stretch(snapshot.navigationIndex);
    snapshot.navigationAlignment =
        snapshot.layout->itemAt(snapshot.navigationIndex)->alignment();
  }
  if (snapshot.pageHostIndex >= 0) {
    snapshot.pageHostStretch = snapshot.layout->stretch(snapshot.pageHostIndex);
    snapshot.pageHostAlignment =
        snapshot.layout->itemAt(snapshot.pageHostIndex)->alignment();
  }
  return snapshot;
}

void rollbackApplicationNavigation(
    ZzPureTools::ZzApplicationWindow &window,
    ZzPureTools::ZzWorkspaceShell &workspace,
    const ApplicationBodySnapshot &snapshot) noexcept {
  if (!snapshot.isValid())
    return;

  QCoreApplication::removePostedEvents(snapshot.body, QEvent::DeferredDelete);

  if (auto *tabs = workspace.tabWidget(); tabs != nullptr) {
    const int pageIndex = tabs->indexOf(snapshot.pageHost);
    if (pageIndex >= 0)
      tabs->removeTab(pageIndex);
  }
  if (snapshot.pageHost->parent() != nullptr)
    snapshot.pageHost->setParent(nullptr);

  auto navigationResult = workspace.takePanel(kNavigationPanelId);
  QWidget *const takenNavigation =
      navigationResult ? navigationResult.value() : nullptr;
  if (takenNavigation != nullptr && takenNavigation != snapshot.navigation)
    takenNavigation->setParent(nullptr);
  if (snapshot.navigation->parent() != nullptr)
    snapshot.navigation->setParent(nullptr);

  QWidget *const workspaceRoot = workspace.workspaceWidget();
  if (window.centralWidget() == workspaceRoot)
    static_cast<void>(window.takeCentralWidget());
  if (workspaceRoot != nullptr && workspaceRoot->parent() != nullptr)
    workspaceRoot->setParent(nullptr);

  const auto restoreWidget = [&snapshot](const int index, QWidget *widget,
                                         const int stretch,
                                         const Qt::Alignment alignment) {
    if (snapshot.layout->indexOf(widget) < 0)
      snapshot.layout->insertWidget(index, widget, stretch, alignment);
  };
  if (snapshot.navigationIndex < snapshot.pageHostIndex) {
    restoreWidget(snapshot.navigationIndex, snapshot.navigation,
                  snapshot.navigationStretch, snapshot.navigationAlignment);
    restoreWidget(snapshot.pageHostIndex, snapshot.pageHost,
                  snapshot.pageHostStretch, snapshot.pageHostAlignment);
  } else {
    restoreWidget(snapshot.pageHostIndex, snapshot.pageHost,
                  snapshot.pageHostStretch, snapshot.pageHostAlignment);
    restoreWidget(snapshot.navigationIndex, snapshot.navigation,
                  snapshot.navigationStretch, snapshot.navigationAlignment);
  }
  window.setCentralWidget(snapshot.body);
}

template <typename Value>
[[nodiscard]] ZzCore::ZzResult<Value> shellFailure(
    ZzCore::ZzErrorCode code, QString message) {
  return ZzCore::ZzResult<Value>::failure(
      ZzCore::ZzError(code, std::move(message)));
}

} // namespace

ZzCore::ZzResult<void>
GuiWindowShell::attach(ZzPureTools::ZzApplicationWindow &window,
                       std::shared_ptr<IGuiOperationBackend> backend,
                       const bool showModalDialogs) {
  if (attachedTo(window) != nullptr) {
    return shellFailure<void>(ZzCore::ZzErrorCode::InvalidState,
                              QStringLiteral("GUI 窗口壳层只能装配一次"));
  }

  try {
    auto shell = std::unique_ptr<GuiWindowShell>(new GuiWindowShell(
        window, std::move(backend), showModalDialogs));
    auto initialized = shell->initialize();
    if (!initialized) {
      return initialized;
    }
    [[maybe_unused]] auto *attachedShell = shell.release();
    return ZzCore::ZzResult<void>::success();
  } catch (const std::exception &exception) {
    return shellFailure<void>(
        ZzCore::ZzErrorCode::Unknown,
        QStringLiteral("无法创建 GUI 窗口壳层：%1")
            .arg(QString::fromLocal8Bit(exception.what())));
  } catch (...) {
    return shellFailure<void>(ZzCore::ZzErrorCode::Unknown,
                              QStringLiteral("无法创建 GUI 窗口壳层"));
  }
}

GuiWindowShell *
GuiWindowShell::attachedTo(ZzPureTools::ZzApplicationWindow &window) noexcept {
  return window.findChild<GuiWindowShell *>(QStringLiteral("guiWindowShell"),
                                            Qt::FindDirectChildrenOnly);
}

GuiWindowShell::GuiWindowShell(
    ZzPureTools::ZzApplicationWindow &window,
    std::shared_ptr<IGuiOperationBackend> backend,
    const bool showModalDialogs)
    : QObject(&window), window_(&window), backend_(std::move(backend)),
      showModalDialogs_(showModalDialogs) {
  setObjectName(QStringLiteral("guiWindowShell"));
}

GuiWindowShell::~GuiWindowShell() = default;

ZzCore::ZzResult<void> GuiWindowShell::initialize() {
  if (window_ == nullptr || backend_ == nullptr) {
    return shellFailure<void>(
        ZzCore::ZzErrorCode::InvalidArgument,
        QStringLiteral("GUI 窗口壳层需要窗口和操作后端"));
  }
  if (QThread::currentThread() != window_->thread()) {
    return shellFailure<void>(ZzCore::ZzErrorCode::InvalidState,
                              QStringLiteral("GUI 窗口壳层必须在 GUI 线程装配"));
  }

  const ApplicationBodySnapshot applicationBody =
      captureApplicationBody(*window_);
  if (!applicationBody.isValid()) {
    return shellFailure<void>(
        ZzCore::ZzErrorCode::InvalidState,
        QStringLiteral("GUI 应用窗口中央导航结构不可用"));
  }

  auto workspaceResult = ZzPureTools::ZzWorkspaceShell::create(
      window_, window_->titleBar(),
      ZzPureTools::ZzWorkspaceCenterMode::Tabbed);
  if (!workspaceResult) {
    return ZzCore::ZzResult<void>::failure(workspaceResult.error());
  }
  auto workspace = std::move(workspaceResult).value();
  workspace->setApplicationTitle(QStringLiteral("ZzRkDevTool"));
  workspace->setTitleMode(ZzPureTools::ZzWorkspaceTitleMode::Application);

  auto integrated = workspace->integrateApplicationNavigation(
      kNavigationPanelId, QStringLiteral("内部导航"),
      ZzFluentUI::ZzIconDescriptor::fromFontIcon(
          ZzFluentUI::ZzFontIcon::Download),
      ZzFluentUI::ZzActivityArea::LeftPrimary, QStringLiteral("下载镜像"));
  if (!integrated) {
    return integrated;
  }

  window_->setCentralWidget(workspace->workspaceWidget());

  auto hidden = workspace->showPanel(kNavigationPanelId, false);
  if (!hidden) {
    rollbackApplicationNavigation(*window_, *workspace, applicationBody);
    return hidden;
  }
  constexpr std::array edges{ZzFluentUI::ZzSidePaneEdge::Left,
                             ZzFluentUI::ZzSidePaneEdge::Right};
  for (const auto edge : edges) {
    auto *activityBar = workspace->activityBar(edge);
    auto *sidePane = workspace->sidePane(edge);
    if (activityBar == nullptr || sidePane == nullptr) {
      rollbackApplicationNavigation(*window_, *workspace, applicationBody);
      return shellFailure<void>(
          ZzCore::ZzErrorCode::InvalidState,
          QStringLiteral("GUI 工作区侧栏表面不可用"));
    }
    activityBar->hide();
    sidePane->hide();
  }
  if (window_->navigationPane() == nullptr) {
    rollbackApplicationNavigation(*window_, *workspace, applicationBody);
    return shellFailure<void>(ZzCore::ZzErrorCode::InvalidState,
                              QStringLiteral("GUI 导航面板不可用"));
  }
  window_->navigationPane()->hide();

  auto *application =
      qobject_cast<ZzPureTools::ZzPureApplication *>(QApplication::instance());
  auto *themeController =
      application != nullptr ? application->themeController() : nullptr;
  if (themeController == nullptr) {
    rollbackApplicationNavigation(*window_, *workspace, applicationBody);
    return shellFailure<void>(ZzCore::ZzErrorCode::InvalidState,
                              QStringLiteral("GUI 应用主题控制器不可用"));
  }
  const QPointer<QWidget> titleBar = window_->titleBar();
  const QPointer<QWidget> tabWidget = workspace->tabWidget();
  const QPointer<QWidget> statusBar = window_->statusBar();
  const auto refreshThemePalette = [titleBar, tabWidget, statusBar] {
    const QPalette palette = QApplication::palette();
    if (titleBar != nullptr)
      titleBar->setPalette(palette);
    if (tabWidget != nullptr)
      tabWidget->setPalette(palette);
    if (statusBar != nullptr)
      statusBar->setPalette(palette);
  };
  refreshThemePalette();
  connect(themeController, &ZzFluentUI::ZzThemeController::snapshotChanged,
          window_, refreshThemePalette);
  connect(window_->titleBar(),
          &ZzFluentUI::ZzFluentTitleBar::themeToggleRequested, window_,
          [themeController] {
            themeController->setMode(
                themeController->mode() == ZzFluentUI::ZzThemeMode::Dark
                    ? ZzFluentUI::ZzThemeMode::Light
                    : ZzFluentUI::ZzThemeMode::Dark);
          });

  workspace_ = std::move(workspace);
  return ZzCore::ZzResult<void>::success();
}

MainWorkspaceController *GuiWindowShell::controller() const noexcept {
  return controller_.data();
}

ZzCore::ZzResult<std::unique_ptr<ZzPureTools::ZzPageInstance>>
GuiWindowShell::createInitialDownloadPage(QWidget *pageParent) {
  using PageInstance = std::unique_ptr<ZzPureTools::ZzPageInstance>;

  if (pageParent == nullptr || window_ == nullptr || workspace_ == nullptr ||
      pageParent->window() != window_) {
    return shellFailure<PageInstance>(
        ZzCore::ZzErrorCode::InvalidArgument,
        QStringLiteral("下载镜像页面需要当前框架窗口的页面父对象"));
  }
  if (controller_ != nullptr) {
    return shellFailure<PageInstance>(
        ZzCore::ZzErrorCode::InvalidState,
        QStringLiteral("下载镜像页面已经创建"));
  }
  auto *tabs = workspace_->tabWidget();
  if (tabs == nullptr || window_->statusBar() == nullptr) {
    return shellFailure<PageInstance>(
        ZzCore::ZzErrorCode::InvalidState,
        QStringLiteral("GUI 工作区标签或状态栏不可用"));
  }

  auto viewModel = std::make_unique<MainWorkspaceViewModel>();
  auto controller = std::make_unique<MainWorkspaceController>(
      backend_, viewModel.get(), showModalDialogs_);
  auto view = std::unique_ptr<ImageDownloadPage>(
      ImageDownloadPage::buildUi(pageParent));

  auto attached =
      controller->attachWorkspace(tabs, window_->statusBar(), view.get());
  if (!attached) {
    return ZzCore::ZzResult<PageInstance>::failure(attached.error());
  }

  MainWorkspaceController *const controllerObserver = controller.get();
  QWidget *const viewObserver = view.release();
  auto instance = ZzPureTools::ZzPageInstance::create(
      pageParent, viewObserver, std::move(viewModel), std::move(controller));
  if (!instance) {
    return instance;
  }
  controller_ = controllerObserver;
  configureFixedTabs();
  workspace_->setApplicationTitle(QStringLiteral("ZzRkDevTool"));
  return instance;
}

void GuiWindowShell::configureFixedTabs() {
  auto *tabs = workspace_ != nullptr ? workspace_->tabWidget() : nullptr;
  if (tabs == nullptr) {
    return;
  }
  tabs->setTabsClosable(false);
  tabs->setMovable(false);
  auto *bar = tabs->fluentTabBar();
  if (bar == nullptr) {
    return;
  }
  bar->setTearOffEnabled(false);
  bar->setTabTransferEnabled(false);
  if (auto *button = bar->newTabButton(); button != nullptr) {
    button->hide();
  }
}

} // namespace zzrk::gui
