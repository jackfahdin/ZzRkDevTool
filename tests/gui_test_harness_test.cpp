// SPDX-License-Identifier: MIT

#include "GuiTestHarness.hpp"

#include <QCoreApplication>
#include <QStringList>

#include <cstddef>
#include <iostream>

namespace {

Q_NEVER_INLINE QStringList argumentsAfterStackReuse() {
  // 复用已返回函数的栈空间，验证仍存活的 QApplication 不再引用失效的 argc。
  volatile int scratch[16384];
  for (std::size_t index = 0; index < 16384; ++index)
    scratch[index] = 0;
  const auto arguments = QCoreApplication::arguments();
  // 在调用公共 Qt 接口期间保留这段有界栈空间。
  if (scratch[0] != 0)
    return {};
  return arguments;
}

} // namespace

int main() {
  auto harness = zzrk::gui::test::GuiTestHarness::create();
  if (!harness) {
    std::cerr << "FAIL: create a GUI harness\n";
    return 1;
  }
  if (argumentsAfterStackReuse() != QStringList{QStringLiteral("gui_test")}) {
    std::cerr << "FAIL: application arguments survive the harness factory call\n";
    return 1;
  }
  std::cout << "GUI harness argument lifetime passed\n";
  return 0;
}
