// SPDX-License-Identifier: MIT

#include "GuiTestHarness.hpp"

#include <ZzWindowKit/ZzWindowKitBootstrap.h>

#include <QCoreApplication>

namespace zzrk::gui::test {

std::unique_ptr<GuiTestHarness> GuiTestHarness::create(
    std::shared_ptr<IGuiOperationBackend> backend)
{
  if (QCoreApplication::instance() != nullptr) {
    return nullptr;
  }

  const auto bootstrap = ZzWindowKit::ZzWindowKitBootstrap::prepare();
  if (!bootstrap) {
    return nullptr;
  }

  static char applicationName[] = "gui_test";
  static char *applicationArguments[] = {applicationName, nullptr};
  int argumentCount = 1;
  auto application = std::make_unique<ZzPureTools::ZzPureApplication>(
      argumentCount, applicationArguments);

  GuiApplicationOptions options;
  options.backend = std::move(backend);
  configureApplicationMetadata(*application);
  return std::unique_ptr<GuiTestHarness>(
      new GuiTestHarness(std::move(application), std::move(options)));
}

GuiTestHarness::GuiTestHarness(
    std::unique_ptr<ZzPureTools::ZzPureApplication> application,
    GuiApplicationOptions options)
    : application_(std::move(application)), options_(std::move(options))
{
}

GuiTestHarness::~GuiTestHarness()
{
  if (application_ != nullptr) {
    application_->beginShutdown();
    application_.reset();
  }
}

ZzPureTools::ZzPureApplication *GuiTestHarness::application() const noexcept
{
  return application_.get();
}

const GuiApplicationOptions &GuiTestHarness::options() const noexcept
{
  return options_;
}

} // namespace zzrk::gui::test
