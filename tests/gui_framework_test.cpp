// SPDX-License-Identifier: MIT

#include "GuiTestHarness.hpp"

#include <ZzPureTools/ZzPureApplication.h>

#include <QApplication>
#include <QStyle>

#include <iostream>

namespace {

int failures = 0;

void expect(const bool condition, const char *description)
{
  if (!condition) {
    std::cerr << "FAIL: " << description << '\n';
    ++failures;
  }
}

} // namespace

int main()
{
  auto harness = zzrk::gui::test::GuiTestHarness::create();
  expect(harness != nullptr, "GUI test harness creates an application");
  if (harness == nullptr) {
    return 1;
  }

  auto *application = harness->application();
  expect(application != nullptr, "harness exposes the application");
  expect(qApp == application, "qApp is the PureApplication instance");
  expect(qobject_cast<ZzPureTools::ZzPureApplication *>(qApp) == application,
         "qApp can be cast to ZzPureApplication");
  expect(application->themeController() != nullptr,
         "application exposes a theme controller");
  expect(QApplication::style() != nullptr, "Fluent application style is set");

  application->beginShutdown();
  application->beginShutdown();
  return failures;
}
