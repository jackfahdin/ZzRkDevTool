// SPDX-License-Identifier: MIT

#include "GuiTestHarness.hpp"
#include "GuiSettings.hpp"
#include "GuiTranslations.hpp"
#include "GuiWindowShell.hpp"
#include "MainWorkspaceController.hpp"

#include <ZzPureTools/ZzApplicationWindow.h>
#include <ZzWindowKit/ZzWindowKitBootstrap.h>

#include <QCoreApplication>

namespace zzrk::gui::test {

std::unique_ptr<GuiTestHarness>
GuiTestHarness::create(std::shared_ptr<IGuiOperationBackend> backend,
                       std::shared_ptr<GuiSettingsStore> settings) {
  if (QCoreApplication::instance() != nullptr) {
    return nullptr;
  }

  const auto bootstrap = ZzWindowKit::ZzWindowKitBootstrap::prepare();
  if (!bootstrap) {
    return nullptr;
  }

  static char applicationName[] = "gui_test";
  static char *applicationArguments[] = {applicationName, nullptr};
  // QApplication 在整个生命周期内持有 argc 的引用；不能引用工厂函数的局部变量。
  static int argumentCount = 1;
  auto application = std::make_unique<ZzPureTools::ZzPureApplication>(
      argumentCount, applicationArguments);

  GuiApplicationOptions options;
  options.backend = std::move(backend);
  options.showModalDialogs = false;
  options.settings = std::move(settings);
  configureApplicationMetadata(*application);
  auto *translations = new GuiTranslations(application.get());
  const auto language = options.settings
                            ? options.settings
                                  ->value(QStringLiteral("appearance/language"),
                                          QStringLiteral("zh_CN"))
                                  .toString()
                            : QStringLiteral("zh_CN");
  if (!translations->install(language))
    return nullptr;
  auto buildResult = buildGuiApplication(*application, options);
  if (!buildResult) {
    application->beginShutdown();
    return nullptr;
  }
  return std::unique_ptr<GuiTestHarness>(new GuiTestHarness(
      std::move(application), std::move(options), buildResult.value()));
}

GuiTestHarness::GuiTestHarness(
    std::unique_ptr<ZzPureTools::ZzPureApplication> application,
    GuiApplicationOptions options, ZzPureTools::ZzApplicationWindow *window)
    : application_(std::move(application)), options_(std::move(options)),
      window_(window) {}

GuiTestHarness::~GuiTestHarness() {
  if (application_ != nullptr) {
    application_->beginShutdown();
    application_.reset();
  }
}

ZzPureTools::ZzPureApplication *GuiTestHarness::application() const noexcept {
  return application_.get();
}

ZzPureTools::ZzApplicationWindow *GuiTestHarness::window() const noexcept {
  return window_.data();
}

MainWorkspaceController *GuiTestHarness::controller() const noexcept {
  auto *shell =
      window_ != nullptr ? GuiWindowShell::attachedTo(*window_) : nullptr;
  return shell != nullptr ? shell->controller() : nullptr;
}

const GuiApplicationOptions &GuiTestHarness::options() const noexcept {
  return options_;
}

void GuiTestHarness::loadFirmwareImage(const QString &path) {
  if (auto *workspaceController = controller(); workspaceController != nullptr)
    workspaceController->loadFirmwareImage(path);
}

void GuiTestHarness::loadDownloadParameter(const QString &path) {
  if (auto *workspaceController = controller(); workspaceController != nullptr)
    workspaceController->loadDownloadParameter(path);
}

void GuiTestHarness::loadDownloadDirectory(const QString &path) {
  if (auto *workspaceController = controller(); workspaceController != nullptr)
    workspaceController->loadDownloadDirectory(path);
}

} // namespace zzrk::gui::test
