// SPDX-License-Identifier: MIT
#pragma once
#include <QFont>
#include <QObject>
#include <memory>
class QColor;
namespace ZzFluentUI {
class ZzThemeController;
}
namespace zzrk::gui {
class GuiSettingsStore;
// 外观应用的统一入口；系统默认字体只在启动时记录，恢复默认不依赖当前字体。
class GuiAppearanceSettings final : public QObject {
  Q_OBJECT
public:
  GuiAppearanceSettings(std::shared_ptr<GuiSettingsStore> settings,
                        ZzFluentUI::ZzThemeController *theme, QObject *parent);
  void applyStored();
  void setFontFamily(const QString &family);
  void setFontPointSize(double size);
  void setAccentColor(QColor color);
  void resetFont();
  void resetAccent();
signals:
  void changed();

private:
  void applyFont();
  void applyAccent();
  std::shared_ptr<GuiSettingsStore> m_settings;
  ZzFluentUI::ZzThemeController *m_theme;
  QFont m_systemFont;
};
} // namespace zzrk::gui
