// SPDX-License-Identifier: MIT

#include "GuiWindowShell.hpp"

#include "GuiSettings.hpp"
#include "GuiTranslations.hpp"
#include "MainWorkspaceController.hpp"
#include "MainWorkspaceViewModel.hpp"
#include "pages/ImageDownloadPage.hpp"
#include "pages/SettingsPage.hpp"
#include "GuiAppearanceSettings.hpp"

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
#include <ZzFluentUI/ZzTabBarAppearance.h>
#include <ZzFluentUI/ZzTabWidget.h>
#include <ZzFluentUI/ZzThemeController.h>
#include <ZzPureTools/ZzApplicationWindow.h>
#include <ZzPureTools/ZzPageHost.h>
#include <ZzPureTools/ZzPureApplication.h>
#include <ZzPureTools/ZzWorkspaceCenterMode.h>
#include <ZzPureTools/ZzWorkspacePanelId.h>
#include <ZzPureTools/ZzWorkspaceShell.h>
#include <ZzPureTools/ZzWorkspaceTitleMode.h>
#include <ZzWindowKit/ZzWindowAgent.h>

#include <QApplication>
#include <QBoxLayout>
#include <QCoreApplication>
#include <QEvent>
#include <QScreen>
#include <QStatusBar>
#include <QThread>
#include <QTimer>
#include <QWidget>
#include <algorithm>

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
[[nodiscard]] ZzCore::ZzResult<Value> shellFailure(ZzCore::ZzErrorCode code,
                                                   QString message) {
  return ZzCore::ZzResult<Value>::failure(
      ZzCore::ZzError(code, std::move(message)));
}

} // namespace

ZzCore::ZzResult<void>
GuiWindowShell::attach(ZzPureTools::ZzApplicationWindow &window,
                       std::shared_ptr<IGuiOperationBackend> backend,
                       const bool showModalDialogs,
                       std::shared_ptr<GuiSettingsStore> settings) {
  if (attachedTo(window) != nullptr) {
    return shellFailure<void>(ZzCore::ZzErrorCode::InvalidState,
                              guiText("GUI 窗口壳层只能装配一次"));
  }

  try {
    auto shell = std::unique_ptr<GuiWindowShell>(new GuiWindowShell(
        window, std::move(backend), showModalDialogs, std::move(settings)));
    auto initialized = shell->initialize();
    if (!initialized) {
      return initialized;
    }
    [[maybe_unused]] auto *attachedShell = shell.release();
    return ZzCore::ZzResult<void>::success();
  } catch (const std::exception &exception) {
    return shellFailure<void>(
        ZzCore::ZzErrorCode::Unknown,
        guiText("无法创建 GUI 窗口壳层：%1")
            .arg(QString::fromLocal8Bit(exception.what())));
  } catch (...) {
    return shellFailure<void>(ZzCore::ZzErrorCode::Unknown,
                              guiText("无法创建 GUI 窗口壳层"));
  }
}

GuiWindowShell *
GuiWindowShell::attachedTo(ZzPureTools::ZzApplicationWindow &window) noexcept {
  return window.findChild<GuiWindowShell *>(QStringLiteral("guiWindowShell"),
                                            Qt::FindDirectChildrenOnly);
}

GuiWindowShell::GuiWindowShell(ZzPureTools::ZzApplicationWindow &window,
                               std::shared_ptr<IGuiOperationBackend> backend,
                               const bool showModalDialogs,
                               std::shared_ptr<GuiSettingsStore> settings)
    : QObject(&window), window_(&window), backend_(std::move(backend)),
      showModalDialogs_(showModalDialogs), settings_(std::move(settings)) {
  setObjectName(QStringLiteral("guiWindowShell"));
  saveTimer_ = new QTimer(this);
  saveTimer_->setSingleShot(true);
  saveTimer_->setInterval(400);
  connect(saveTimer_, &QTimer::timeout, this, [this] { savePreferences(); });
  window.installEventFilter(this);
}

GuiWindowShell::~GuiWindowShell() = default;

ZzCore::ZzResult<void> GuiWindowShell::initialize() {
  if (window_ == nullptr || backend_ == nullptr) {
    return shellFailure<void>(ZzCore::ZzErrorCode::InvalidArgument,
                              guiText("GUI 窗口壳层需要窗口和操作后端"));
  }
  if (QThread::currentThread() != window_->thread()) {
    return shellFailure<void>(ZzCore::ZzErrorCode::InvalidState,
                              guiText("GUI 窗口壳层必须在 GUI 线程装配"));
  }

  const ApplicationBodySnapshot applicationBody =
      captureApplicationBody(*window_);
  if (!applicationBody.isValid()) {
    return shellFailure<void>(ZzCore::ZzErrorCode::InvalidState,
                              guiText("GUI 应用窗口中央导航结构不可用"));
  }

  auto workspaceResult = ZzPureTools::ZzWorkspaceShell::create(
      window_, window_->titleBar(), ZzPureTools::ZzWorkspaceCenterMode::Tabbed);
  if (!workspaceResult) {
    return ZzCore::ZzResult<void>::failure(workspaceResult.error());
  }
  auto workspace = std::move(workspaceResult).value();
  workspace->setApplicationTitle(QStringLiteral("ZzRkDevTool"));
  workspace->setTitleMode(ZzPureTools::ZzWorkspaceTitleMode::Application);

  auto integrated = workspace->integrateApplicationNavigation(
      kNavigationPanelId, guiText("内部导航"),
      ZzFluentUI::ZzIconDescriptor::fromFontIcon(
          ZzFluentUI::ZzFontIcon::Download),
      ZzFluentUI::ZzActivityArea::LeftPrimary, guiText("下载镜像"));
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
      return shellFailure<void>(ZzCore::ZzErrorCode::InvalidState,
                                guiText("GUI 工作区侧栏表面不可用"));
    }
    activityBar->hide();
    sidePane->hide();
  }
  if (window_->navigationPane() == nullptr) {
    rollbackApplicationNavigation(*window_, *workspace, applicationBody);
    return shellFailure<void>(ZzCore::ZzErrorCode::InvalidState,
                              guiText("GUI 导航面板不可用"));
  }
  window_->navigationPane()->hide();

  workspace_ = std::move(workspace);
  // 框架内置组合标题采用“页面 - 应用”顺序；这里通过公开接口统一为“应用-页面”。
  workspace_->setTitleMode(ZzPureTools::ZzWorkspaceTitleMode::Custom);
  connect(workspace_->tabWidget(), &QTabWidget::currentChanged, this,
          &GuiWindowShell::updateWindowTitle);
  updateWindowTitle();
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
        guiText("下载镜像页面需要当前框架窗口的页面父对象"));
  }
  if (controller_ != nullptr) {
    return shellFailure<PageInstance>(ZzCore::ZzErrorCode::InvalidState,
                                      guiText("下载镜像页面已经创建"));
  }
  auto *tabs = workspace_->tabWidget();
  if (tabs == nullptr || window_->statusBar() == nullptr) {
    return shellFailure<PageInstance>(ZzCore::ZzErrorCode::InvalidState,
                                      guiText("GUI 工作区标签或状态栏不可用"));
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
  auto *application = qobject_cast<ZzPureTools::ZzPureApplication *>(qApp);
  // 框架标题栏固定 32 px，且不响应 FontChange 重新计算标题省略范围。
  // 暂保留系统窗口装饰字体；工作区和文件弹窗使用用户字体。
  // 框架支持动态标题栏高度与字体布局后，可删除这段固定装饰字体的适配。
  auto chromeFont = QApplication::font();
  chromeFont.setFamily(chromeFont.family());
  if (chromeFont.pointSizeF() > 0)
    chromeFont.setPointSizeF(chromeFont.pointSizeF());
  else if (chromeFont.pixelSize() > 0)
    chromeFont.setPixelSize(chromeFont.pixelSize());
  window_->titleBar()->setFont(chromeFont);
  appearance_ = new GuiAppearanceSettings(settings_, application->themeController(), this);
  settingsPage_ =
      new SettingsPage(settings_, application->themeController(), appearance_, tabs);
  connect(appearance_, &GuiAppearanceSettings::changed, this,
          &GuiWindowShell::scheduleSave);
  tabs->addTab(settingsPage_, guiText("设置"));
  settingsPage_->setFlushCallback([this] { return savePreferences(); });
  settingsPage_->setLanguageCallback([](const QString &language) {
    auto *translations = GuiTranslations::active();
    return translations && translations->install(language);
  });
  connect(settingsPage_, &SettingsPage::saveRequested, this,
          &GuiWindowShell::scheduleSave);
  connect(settingsPage_, &SettingsPage::resetRequested, this, [this] {
    restorePreferences();
    settingsPage_->refresh();
  });
  connect(controller_, &MainWorkspaceController::preferencesChanged, this,
          &GuiWindowShell::scheduleSave);
  connect(controller_->viewModel(), &MainWorkspaceViewModel::busyChanged,
          settingsPage_, &SettingsPage::setBusy);
  connect(tabs, &QTabWidget::currentChanged, this,
          &GuiWindowShell::scheduleSave);
  connect(application->themeController(),
          &ZzFluentUI::ZzThemeController::snapshotChanged, this,
          &GuiWindowShell::scheduleSave);
  connect(window_->titleBar(),
          &ZzFluentUI::ZzFluentTitleBar::alwaysOnTopChanged, this,
          &GuiWindowShell::scheduleSave);
  controller_->setSettings(settings_);
  restorePreferences();
  configureFixedTabs();
  workspace_->setApplicationTitle(QStringLiteral("ZzRkDevTool"));
  updateWindowTitle();
  return instance;
}

std::shared_ptr<GuiSettingsStore> GuiWindowShell::settings() const noexcept {
  return settings_;
}

void GuiWindowShell::scheduleSave() {
  if (settings_ && !restoring_)
    saveTimer_->start();
}

bool GuiWindowShell::eventFilter(QObject *watched, QEvent *event) {
  if (watched == window_ && event->type() == QEvent::LanguageChange &&
      workspace_) {
    // 固定标签只修改标题，页面实例、当前索引及后台任务保持不变。
    auto *tabs = workspace_->tabWidget();
    const std::array names{"下载镜像", "设备", "完整升级", "分区", "设置"};
    for (int index = 0;
         index < tabs->count() && index < static_cast<int>(names.size());
         ++index)
      tabs->setTabText(index, guiText(names[static_cast<std::size_t>(index)]));
    updateWindowTitle();
  }
  if (watched == window_ && settings_ && controller_ && !restoring_) {
    if (event->type() == QEvent::Close)
      savePreferences();
    else if (event->type() == QEvent::Move || event->type() == QEvent::Resize ||
             event->type() == QEvent::WindowStateChange)
      scheduleSave();
  }
  return QObject::eventFilter(watched, event);
}

bool GuiWindowShell::savePreferences() {
  if (!settings_ || restoring_ || !window_ || !controller_)
    return true;
  saveTimer_->stop();
  controller_->captureSettings();
  auto *application = qobject_cast<ZzPureTools::ZzPureApplication *>(qApp);
  const auto mode = application->themeController()->mode();
  settings_->setValue(
      QStringLiteral("appearance/theme"),
      mode == ZzFluentUI::ZzThemeMode::Dark    ? QStringLiteral("dark")
      : mode == ZzFluentUI::ZzThemeMode::Light ? QStringLiteral("light")
                                               : QStringLiteral("system"));
  settings_->setValue(QStringLiteral("window/tab"),
                      workspace_->tabWidget()->currentIndex());
  settings_->setValue(QStringLiteral("window/geometry"),
                      window_->isMaximized() || window_->isMinimized()
                          ? window_->normalGeometry()
                          : window_->geometry());
  settings_->setValue(QStringLiteral("window/maximized"),
                      window_->isMaximized());
  settings_->setValue(QStringLiteral("window/pinned"),
                      window_->titleBar()->isAlwaysOnTop());
  const auto result = settings_->sync();
  if (!result) {
    setGuiStatus(window_->statusBar(),
        guiText("配置保存失败：%1").arg(result.error().technicalMessage()));
    return false;
  }
  return true;
}

void GuiWindowShell::restorePreferences() {
  if (!settings_ || !window_ || !controller_)
    return;
  restoring_ = true;
  saveTimer_->stop();
  auto *application = qobject_cast<ZzPureTools::ZzPureApplication *>(qApp);
  const auto theme =
      settings_
          ->value(QStringLiteral("appearance/theme"), QStringLiteral("system"))
          .toString();
  if (auto *translations = GuiTranslations::active())
    translations->install(settings_
                              ->value(QStringLiteral("appearance/language"),
                                      QStringLiteral("system"))
                              .toString());
  application->themeController()->setMode(
      theme == QStringLiteral("dark")    ? ZzFluentUI::ZzThemeMode::Dark
      : theme == QStringLiteral("light") ? ZzFluentUI::ZzThemeMode::Light
                                         : ZzFluentUI::ZzThemeMode::System);
  appearance_->applyStored();
  auto geometry = settings_->value(QStringLiteral("window/geometry")).toRect();
  if (geometry.isValid()) {
    QScreen *screen = window_->screen();
    for (auto *candidate : QGuiApplication::screens())
      if (candidate->availableGeometry().intersects(geometry)) {
        screen = candidate;
        break;
      }
    const auto available = screen->availableGeometry();
    geometry.setSize(geometry.size()
                         .expandedTo(window_->minimumSize())
                         .boundedTo(available.size()));
    geometry.moveLeft(std::clamp(geometry.left(), available.left(),
                                 available.right() - geometry.width() + 1));
    geometry.moveTop(std::clamp(geometry.top(), available.top(),
                                available.bottom() - geometry.height() + 1));
    window_->setGeometry(geometry);
  }
  if (settings_->value(QStringLiteral("window/maximized"), false).toBool())
    window_->setWindowState(Qt::WindowMaximized);
  else
    window_->setWindowState(Qt::WindowNoState);
  const auto pinResult = window_->windowAgent()->setAlwaysOnTop(
      settings_->value(QStringLiteral("window/pinned"), false).toBool());
  if (pinResult)
    window_->titleBar()->setAlwaysOnTop(
        window_->windowFlags().testFlag(Qt::WindowStaysOnTopHint));
  controller_->restoreSettings();
  auto *tabs = workspace_->tabWidget();
  tabs->setCurrentIndex(
      std::clamp(settings_->value(QStringLiteral("window/tab"), 0).toInt(), 0,
                 tabs->count() - 1));
  if (settingsPage_)
    settingsPage_->refresh();
  restoring_ = false;
}

void GuiWindowShell::updateWindowTitle() {
  auto *tabs = workspace_ != nullptr ? workspace_->tabWidget() : nullptr;
  if (tabs == nullptr)
    return;
  const QString applicationTitle = workspace_->applicationTitle();
  const QString pageTitle = tabs->tabText(tabs->currentIndex());
  // 页面名称取自顶部标签，后续改名只需修改标签文案；系统窗口与框架标题栏同步更新。
  workspace_->setCustomTitle(
      pageTitle.isEmpty() ? applicationTitle
                          : applicationTitle + QStringLiteral("-") + pageTitle);
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
  // 标签风格的统一修改入口；固定功能标签不需要浏览器式关闭或拖拽。
  bar->setAppearance(ZzFluentUI::ZzTabBarAppearance::PivotSlide);
  for (int index = 0; index < tabs->count(); ++index)
    tabs->setTabIcon(index, QIcon{});
  bar->setTabTransferEnabled(false);
  if (auto *button = bar->newTabButton(); button != nullptr) {
    button->hide();
  }
}

} // namespace zzrk::gui
