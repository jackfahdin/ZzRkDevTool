// SPDX-License-Identifier: MIT
#pragma once
#include <QWidget>
#include <functional>
#include <memory>
class QComboBox;
class QLineEdit;
class QLabel;
class QPushButton;
namespace ZzFluentUI {
class ZzThemeController;
}
namespace zzrk::gui {
class GuiSettingsStore;
class SettingsPage final : public QWidget {
  Q_OBJECT
public:
  SettingsPage(std::shared_ptr<GuiSettingsStore> settings,
               ZzFluentUI::ZzThemeController *theme, QWidget *parent = nullptr);
  void setFlushCallback(std::function<bool()> flush);
  void setLanguageCallback(std::function<bool(const QString &)> change);
  void setBusy(bool busy);
  void refresh();
signals:
  void saveRequested();
  void resetRequested();

private:
  std::shared_ptr<GuiSettingsStore> m_settings;
  ZzFluentUI::ZzThemeController *m_theme;
  std::function<bool()> m_flush;
  std::function<bool(const QString &)> m_changeLanguage;
  QComboBox *m_themeCombo;
  QComboBox *m_languageCombo;
  QLineEdit *m_configPath;
  QLineEdit *m_backupPath;
  QLabel *m_notice;
  QPushButton *m_changeDirectory;
  QPushButton *m_reset;
  bool m_busy = false;
};
} // namespace zzrk::gui
