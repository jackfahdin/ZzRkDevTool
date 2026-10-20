// SPDX-License-Identifier: MIT
#include "GuiApplicationBootstrap.hpp"
#include "GuiSettings.hpp"

#include <ZzPureTools/ZzApplicationWindow.h>
#include <ZzWindowKit/ZzWindowKitBootstrap.h>

#include <QApplication>
#include <QElapsedTimer>
#include <QEvent>
#include <QResizeEvent>
#include <QScreen>
#include <QTemporaryDir>
#include <QThread>
#include <QWindow>

#include <functional>
#include <iostream>

namespace {
bool waitFor(const std::function<bool()> &condition) {
  QElapsedTimer timer;
  timer.start();
  while (!condition() && timer.elapsed() < 2000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    QThread::msleep(1);
  }
  return condition();
}

// 模拟 Cocoa 在首次显示时回写原生窗口的旧尺寸，走真实 QWidgetWindow 分发。
// 如果启动尺寸只在隐藏阶段设置，旧回写就会覆盖用户配置。
class NativeResizeBeforeShow final : public QObject {
public:
  bool applied = false;

protected:
  bool eventFilter(QObject *watched, QEvent *event) override {
    auto *window = qobject_cast<ZzPureTools::ZzApplicationWindow *>(watched);
    if (!applied && window && event->type() == QEvent::Show) {
      applied = true;
      auto *native = window->windowHandle();
      native->resize(800, 720);
      QResizeEvent resize(native->size(), QSize(980, 820));
      QCoreApplication::sendEvent(native, &resize);
    }
    return false;
  }
};
} // namespace

int main(int argc, char **argv) {
  if (!ZzWindowKit::ZzWindowKitBootstrap::prepare())
    return 1;
  ZzPureTools::ZzPureApplication application(argc, argv);
  zzrk::gui::configureApplicationMetadata(application);
  NativeResizeBeforeShow interference;
  application.installEventFilter(&interference);

  QTemporaryDir fixture;
  if (!fixture.isValid())
    return 1;
  zzrk::gui::GuiApplicationOptions options;
  options.showModalDialogs = false;
  const bool restored = argc > 1;
  const bool maximized = argc > 2;
  if (restored) {
    options.settings = std::make_shared<zzrk::gui::GuiSettingsStore>(
        fixture.filePath("user"), fixture.filePath("app"));
    if (!options.settings->initialize(zzrk::gui::GuiSettingsLocation::User))
      return 1;
    options.settings->setValue("window/geometry", QRect(0, 0, 800, 700));
    options.settings->setValue("window/maximized", maximized);
  }
  auto built = zzrk::gui::buildGuiApplication(application, options);
  if (!built)
    return 1;
  auto *window = built.value();
  QApplication::processEvents();
  // 小屏幕（包括 macOS Runner）可以缩小默认尺寸，但不能小于业务窗口下限。
  const auto available = window->screen()->availableGeometry().size();
  const QSize expected = (restored ? QSize(800, 700) : QSize(980, 820))
                             .boundedTo(available)
                             .expandedTo(QSize(800, 700));
  bool passed = waitFor([&] {
    return interference.applied &&
           (maximized ? window->isMaximized() &&
                            window->normalGeometry().size() == expected
                      : window->size() == expected);
  });
  if (!passed)
    std::cerr << "FAIL: native startup resize overwrote "
              << (restored ? "restored" : "default") << " geometry: "
              << window->width() << 'x' << window->height() << '\n';

  // 启动修正只能执行一次；之后用户改变尺寸、隐藏再显示时不能恢复旧尺寸。
  window->showNormal();
  passed = waitFor([&] {
             return !window->isMaximized() && window->size() == expected;
           }) && passed;
  window->hide();
  const auto userSize = QSize(910, 710)
                            .boundedTo(available)
                            .expandedTo(QSize(800, 700));
  window->resize(userSize);
  window->show();
  if (!waitFor([&] { return window->size() == userSize; })) {
    passed = false;
    std::cerr << "FAIL: showing an existing window overwrote user geometry\n";
  }
  application.beginShutdown();
  return passed ? 0 : 1;
}
