// SPDX-License-Identifier: MIT
#include "GuiTranslations.hpp"
#include <QApplication>
#include <QEvent>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <iostream>

int main(int argc, char **argv) {
  QApplication app(argc, argv);
  using namespace zzrk::gui;
  // 捕获资源遗漏、未安装翻译器及参数占位符被破坏的问题。
  GuiTranslations translations;
  if (!translations.install(QStringLiteral("en")))
    return 1;
  QLabel label(guiText("设备"));
  QPushButton button(guiText("取消"));
  if (label.text() != QStringLiteral("Device") ||
      button.text() != QStringLiteral("Cancel"))
    return 2;
  if (guiText("已连接 %1 台 Rockchip 设备").arg(3) !=
      QStringLiteral("Connected to 3 Rockchip devices"))
    return 3;
  if (!translations.install(QStringLiteral("zh_CN")))
    return 4;
  QLabel chinese(guiText("设备"));
  if (chinese.text() != QStringLiteral("设备"))
    return 5;
  if (!translations.install(QStringLiteral("invalid")) ||
      guiText("取消") != QStringLiteral("取消"))
    return 6;
  if (!translations.install(QStringLiteral("system")))
    return 7;
  const bool systemEnglish = QLocale::system().language() == QLocale::English;
  if (guiText("设备") !=
      (systemEnglish ? QStringLiteral("Device") : QStringLiteral("设备")))
    return 8;
  translations.install(QStringLiteral("zh_CN"));
  {
    GuiTranslations temporary;
    if (!temporary.install(QStringLiteral("en")))
      return 9;
  }
  // 翻译器销毁后恢复源语言，避免泄漏到同进程的其他测试。
  if (guiText("设备") != QStringLiteral("设备"))
    return 10;
  // 源文案和参数均按值保存，局部变量销毁后仍可重译；普通 QString 日志保留原文。
  QLabel live;
  const auto message = [] {
    const QString port = QStringLiteral("1-5");
    return guiText("端口 %1，详情读取完成").arg(port);
  }();
  const QString history = message;
  setGuiText(&live, "text", message);
  translations.install(QStringLiteral("en"));
  QCoreApplication::sendPostedEvents(nullptr, QEvent::LanguageChange);
  if (live.text() != QStringLiteral("Port 1-5: details loaded") ||
      history != QStringLiteral("端口 1-5，详情读取完成"))
    return 11;
  setGuiText(&live, "text", guiText("已连接 %1 台 Rockchip 设备").arg(3));
  translations.install(QStringLiteral("zh_CN"));
  QCoreApplication::sendPostedEvents(nullptr, QEvent::LanguageChange);
  if (live.text() != QStringLiteral("已连接 3 台 Rockchip 设备"))
    return 12;
  // 替换成普通数据后必须清掉旧文案，不能在下一次事件中恢复旧状态。
  setGuiText(&live, "text", QStringLiteral("/tmp/固件.img"));
  translations.install(QStringLiteral("en"));
  QCoreApplication::sendPostedEvents(nullptr, QEvent::LanguageChange);
  if (live.text() != QStringLiteral("/tmp/固件.img"))
    return 13;
  std::cout << "GUI translation tests passed\n";
  return 0;
}
