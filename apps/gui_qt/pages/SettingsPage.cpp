// SPDX-License-Identifier: MIT
#include "SettingsPage.hpp"
#include "FileSelectionDialog.hpp"
#include "GuiSettings.hpp"
#include "GuiTranslations.hpp"
#include "SettingsLocationDialog.hpp"
#include "WorkspaceUi.hpp"
#include <QComboBox>
#include <QDesktopServices>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QUrl>
#include <QVBoxLayout>
#include <ZzFluentUI/ZzContentDialog.h>
#include <ZzFluentUI/ZzContentDialogResult.h>
#include <ZzFluentUI/ZzThemeController.h>

namespace zzrk::gui {
SettingsPage::SettingsPage(std::shared_ptr<GuiSettingsStore> settings,
                           ZzFluentUI::ZzThemeController *theme,
                           QWidget *parent)
    : QWidget(parent), m_settings(std::move(settings)), m_theme(theme) {
  setObjectName(QStringLiteral("settingsPage"));
  auto *layout = new QVBoxLayout(this);
  ui::configurePage(this, layout);
  auto *appearance = ui::card(this);
  auto *appearanceLayout = new QVBoxLayout(appearance);
  appearanceLayout->setContentsMargins(ui::CardPadding, ui::CardPadding,
                                       ui::CardPadding, ui::CardPadding);
  appearanceLayout->addWidget(
      ui::sectionTitle(guiText("外观与语言"), appearance));
  auto *appearanceForm = new QFormLayout;
  m_themeCombo = new QComboBox(appearance);
  m_themeCombo->setObjectName(QStringLiteral("settingsThemeCombo"));
  m_themeCombo->addItem(guiText("跟随系统"), QStringLiteral("system"));
  m_themeCombo->addItem(guiText("浅色"), QStringLiteral("light"));
  m_themeCombo->addItem(guiText("深色"), QStringLiteral("dark"));
  m_languageCombo = new QComboBox(appearance);
  m_languageCombo->setObjectName(QStringLiteral("settingsLanguageCombo"));
  m_languageCombo->addItem(guiText("跟随系统"), QStringLiteral("system"));
  m_languageCombo->addItem(QStringLiteral("简体中文"), QStringLiteral("zh_CN"));
  m_languageCombo->addItem(QStringLiteral("English"), QStringLiteral("en"));
  for (auto *combo : {m_themeCombo, m_languageCombo})
    combo->setMinimumHeight(ui::ControlHeight);
  appearanceForm->addRow(guiLabel(guiText("主题"), appearance), m_themeCombo);
  appearanceForm->addRow(guiLabel(guiText("语言"), appearance),
                         m_languageCombo);
  bindGuiTranslation(m_themeCombo, [this] {
    const QSignalBlocker blocker(m_themeCombo);
    m_themeCombo->setItemText(0, guiText("跟随系统"));
    m_themeCombo->setItemText(1, guiText("浅色"));
    m_themeCombo->setItemText(2, guiText("深色"));
  });
  bindGuiTranslation(m_languageCombo, [this] {
    const QSignalBlocker blocker(m_languageCombo);
    m_languageCombo->setItemText(0, guiText("跟随系统"));
  });
  appearanceLayout->addLayout(appearanceForm);
  layout->addWidget(appearance);

  auto *paths = ui::card(this);
  auto *pathsLayout = new QVBoxLayout(paths);
  pathsLayout->setContentsMargins(ui::CardPadding, ui::CardPadding,
                                  ui::CardPadding, ui::CardPadding);
  pathsLayout->addWidget(ui::sectionTitle(guiText("路径与存储"), paths));
  auto *form = new QFormLayout;
  m_configPath = new QLineEdit(paths);
  m_configPath->setObjectName(QStringLiteral("settingsConfigPath"));
  m_configPath->setReadOnly(true);
  auto *configRow = new QHBoxLayout;
  configRow->addWidget(m_configPath, 1);
  m_changeDirectory = ui::button(guiText("修改"), ui::Icon::Folder, paths);
  m_changeDirectory->setObjectName(
      QStringLiteral("changeSettingsDirectoryButton"));
  configRow->addWidget(m_changeDirectory);
  form->addRow(guiLabel(guiText("配置目录"), paths), configRow);
  m_backupPath = new QLineEdit(paths);
  m_backupPath->setObjectName(QStringLiteral("settingsBackupDirectory"));
  m_backupPath->setReadOnly(true);
  setGuiText(m_backupPath, "placeholderText", guiText("默认使用上次保存目录"));
  auto *backupRow = new QHBoxLayout;
  backupRow->addWidget(m_backupPath, 1);
  auto *chooseBackup = ui::button(guiText("浏览"), ui::Icon::Folder, paths);
  auto *clearBackup = ui::button(guiText("清除"), ui::Icon::Clear, paths);
  backupRow->addWidget(chooseBackup);
  backupRow->addWidget(clearBackup);
  form->addRow(guiLabel(guiText("备份保存目录"), paths), backupRow);
  pathsLayout->addLayout(form);
  auto *open = ui::button(guiText("打开配置目录"), ui::Icon::Folder, paths);
  pathsLayout->addWidget(open, 0, Qt::AlignLeft);
  layout->addWidget(paths);
  auto *manage = ui::card(this);
  auto *manageLayout = new QVBoxLayout(manage);
  manageLayout->setContentsMargins(ui::CardPadding, ui::CardPadding,
                                   ui::CardPadding, ui::CardPadding);
  manageLayout->addWidget(ui::sectionTitle(guiText("配置管理"), manage));
  m_reset = ui::button(guiText("恢复默认设置"), ui::Icon::Refresh, manage);
  m_reset->setObjectName(QStringLiteral("resetSettingsButton"));
  manageLayout->addWidget(m_reset, 0, Qt::AlignLeft);
  layout->addWidget(manage);
  m_notice = ui::secondaryText({}, this);
  m_notice->setObjectName(QStringLiteral("settingsNotice"));
  m_notice->setWordWrap(true);
  layout->addWidget(m_notice);
  layout->addStretch();
  refresh();
  connect(m_themeCombo, &QComboBox::currentIndexChanged, this, [this] {
    const auto theme = m_themeCombo->currentData().toString();
    m_theme->setMode(
        theme == QStringLiteral("dark")    ? ZzFluentUI::ZzThemeMode::Dark
        : theme == QStringLiteral("light") ? ZzFluentUI::ZzThemeMode::Light
                                           : ZzFluentUI::ZzThemeMode::System);
    if (m_settings)
      m_settings->setValue(QStringLiteral("appearance/theme"), theme);
    emit saveRequested();
  });
  connect(
      m_theme, &ZzFluentUI::ZzThemeController::snapshotChanged, this, [this] {
        // 标题栏和设置页观察同一个控制器，任何入口切换主题都同步。
        const QSignalBlocker blocker(m_themeCombo);
        const auto mode = m_theme->mode();
        m_themeCombo->setCurrentIndex(mode == ZzFluentUI::ZzThemeMode::Dark ? 2
                                      : mode == ZzFluentUI::ZzThemeMode::Light
                                          ? 1
                                          : 0);
      });
  connect(m_languageCombo, &QComboBox::currentIndexChanged, this, [this] {
    const auto language = m_languageCombo->currentData().toString();
    if (!m_changeLanguage || !m_changeLanguage(language)) {
      refresh();
      setGuiText(m_notice, "text", guiText("语言切换失败，请重试。"));
      return;
    }
    if (m_settings)
      m_settings->setValue(QStringLiteral("appearance/language"), language);
    setGuiText(m_notice, "text", guiText("语言已切换。"));
    emit saveRequested();
  });
  connect(chooseBackup, &QPushButton::clicked, this, [this] {
    const auto path = FileSelectionDialog::getExistingDirectory(
        this, guiText("选择备份保存目录"), m_backupPath->text());
    if (path.isEmpty() || !m_settings)
      return;
    m_settings->setValue(QStringLiteral("paths/backup"), path);
    m_backupPath->setText(path);
    emit saveRequested();
  });
  connect(clearBackup, &QPushButton::clicked, this, [this] {
    if (m_settings)
      m_settings->setValue(QStringLiteral("paths/backup"), QString{});
    m_backupPath->clear();
    emit saveRequested();
  });
  connect(open, &QPushButton::clicked, this, [this] {
    if (m_settings && !QDesktopServices::openUrl(
                          QUrl::fromLocalFile(m_settings->directory())))
      setGuiText(m_notice, "text", guiText("无法打开配置目录。"));
  });
  connect(m_changeDirectory, &QPushButton::clicked, this, [this] {
    if (!m_settings || (m_flush && !m_flush()))
      return;
    if (chooseSettingsLocation(*m_settings, this, true)) {
      refresh();
      setGuiText(m_notice, "text",
                 guiText("配置目录已更新，原配置文件已保留。"));
    }
  });
  connect(m_reset, &QPushButton::clicked, this, [this] {
    ZzFluentUI::ZzContentDialog dialog(window());
    dialog.setTitle(guiText("恢复默认设置"));
    dialog.setText(
        guiText("重置界面偏好和镜像配置草稿，保留当前配置目录。是否继续？"));
    dialog.setPrimaryButtonText(guiText("恢复默认"));
    dialog.setPrimaryButtonVisible(true);
    dialog.setCloseButtonText(guiText("取消"));
    dialog.exec();
    if (dialog.dialogResult() != ZzFluentUI::ZzContentDialogResult::Primary ||
        !m_settings)
      return;
    // 模态确认期间仍会处理热插拔和后台完成事件，确认后再次检查实际忙碌状态。
    if (m_busy) {
      setGuiText(m_notice, "text",
                 guiText("设备操作进行中，请稍后恢复默认设置。"));
      return;
    }
    const auto result = m_settings->reset();
    if (!result) {
      setGuiText(
          m_notice, "text",
          guiText("无法保存配置：%1").arg(result.error().technicalMessage()));
      return;
    }
    emit resetRequested();
    refresh();
    setGuiText(m_notice, "text", guiText("已恢复默认设置。"));
  });
  if (!m_settings) {
    setEnabled(false);
    setGuiText(m_notice, "text", guiText("当前运行未启用配置存储。"));
  }
}
void SettingsPage::setFlushCallback(std::function<bool()> flush) {
  m_flush = std::move(flush);
}
void SettingsPage::setLanguageCallback(
    std::function<bool(const QString &)> change) {
  m_changeLanguage = std::move(change);
}
void SettingsPage::setBusy(bool busy) {
  m_busy = busy;
  m_changeDirectory->setEnabled(!busy && m_settings != nullptr);
  m_reset->setEnabled(!busy && m_settings != nullptr);
}
void SettingsPage::refresh() {
  const QSignalBlocker themeBlock(m_themeCombo), languageBlock(m_languageCombo);
  const auto mode = m_theme->mode();
  m_themeCombo->setCurrentIndex(mode == ZzFluentUI::ZzThemeMode::Dark    ? 2
                                : mode == ZzFluentUI::ZzThemeMode::Light ? 1
                                                                         : 0);
  m_languageCombo->setCurrentIndex(m_languageCombo->findData(
      m_settings ? m_settings->value(QStringLiteral("appearance/language"),
                                     QStringLiteral("system"))
                 : QVariant(QStringLiteral("system"))));
  m_configPath->setText(m_settings ? m_settings->directory() : QString{});
  m_backupPath->setText(
      m_settings ? m_settings->value(QStringLiteral("paths/backup")).toString()
                 : QString{});
}
} // namespace zzrk::gui
