// SPDX-License-Identifier: MIT
#include "GuiAppearanceSettings.hpp"
#include "GuiSettings.hpp"
#include <QApplication>
#include <QColor>
#include <QFontDatabase>
#include <ZzFluentUI/ZzThemeController.h>
#include <cmath>

namespace zzrk::gui {
GuiAppearanceSettings::GuiAppearanceSettings(
    std::shared_ptr<GuiSettingsStore> settings,
    ZzFluentUI::ZzThemeController *theme, QObject *parent)
    : QObject(parent), m_settings(std::move(settings)), m_theme(theme),
      m_systemFont(QApplication::font()) {}

void GuiAppearanceSettings::applyStored() {
  if (!m_settings)
    return;
  applyFont();
  applyAccent();
  emit changed();
}

void GuiAppearanceSettings::applyFont() {
  QFont font = m_systemFont;
  auto family = m_settings->value("appearance/fontFamily").toString();
  // 配置可在不同机器间迁移，缺失字体回到当前系统默认，不保留失效的选择。
  if (!family.isEmpty() && !QFontDatabase::families().contains(family))
    family.clear();
  if (!family.isEmpty())
    font.setFamily(family);
  double size = m_settings->value("appearance/fontPointSize").toDouble();
  if (!std::isfinite(size) || size < 8 || size > 24)
    size = 0;
  if (size > 0)
    font.setPointSizeF(size);
  m_settings->setValue("appearance/fontFamily", family);
  m_settings->setValue("appearance/fontPointSize", size);
  QApplication::setFont(font);
}

void GuiAppearanceSettings::applyAccent() {
  QColor color(m_settings->value("appearance/accentColor").toString());
  if (color.isValid())
    color.setAlpha(255);
  m_settings->setValue("appearance/accentColor",
                       color.isValid() ? color.name() : QString{});
  // 无效 QColor 让框架自行恢复默认强调色，业务层不复制框架的默认蓝色。
  m_theme->setAccentColor(color);
}
void GuiAppearanceSettings::setFontFamily(const QString &family) {
  if (m_settings) {
    m_settings->setValue("appearance/fontFamily", family);
    applyFont();
    emit changed();
  }
}
void GuiAppearanceSettings::setFontPointSize(double size) {
  if (m_settings) {
    m_settings->setValue("appearance/fontPointSize", size);
    applyFont();
    emit changed();
  }
}
void GuiAppearanceSettings::setAccentColor(QColor color) {
  if (m_settings) {
    m_settings->setValue("appearance/accentColor",
                         color.isValid() ? color.name() : QString{});
    // 色谱拖动会高频更新颜色，不需要反复枚举字体或重设应用字体。
    applyAccent();
    emit changed();
  }
}
void GuiAppearanceSettings::resetFont() {
  if (m_settings) {
    m_settings->setValue("appearance/fontFamily", QString{});
    m_settings->setValue("appearance/fontPointSize", 0);
    applyFont();
    emit changed();
  }
}
void GuiAppearanceSettings::resetAccent() { setAccentColor(QColor{}); }
} // namespace zzrk::gui
