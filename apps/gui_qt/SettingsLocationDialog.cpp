// SPDX-License-Identifier: MIT
#include "SettingsLocationDialog.hpp"
#include "FileSelectionDialog.hpp"
#include "GuiSettings.hpp"
#include "GuiTranslations.hpp"
#include "WorkspaceUi.hpp"
#include <QButtonGroup>
#include <QDir>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QVBoxLayout>
#include <ZzFluentUI/ZzContentDialog.h>
#include <vector>

namespace zzrk::gui {
bool chooseSettingsLocation(GuiSettingsStore &store, QWidget *parent,
                            bool migrate, const QString &reason) {
  ZzFluentUI::ZzContentDialog dialog(parent);
  dialog.setObjectName(QStringLiteral("settingsLocationDialog"));
  setGuiText(&dialog, "title", guiText("选择配置文件目录"));
  dialog.setPrimaryButtonVisible(false);
  dialog.setSecondaryButtonVisible(false);
  dialog.setCloseButtonVisible(false);
  auto *body = new QWidget(&dialog);
  auto *layout = new QVBoxLayout(body);
  layout->setSpacing(12);
  layout->addWidget(ui::secondaryText(
      migrate ? guiText("当前设置将保存到新目录，原配置文件会保留。")
              : guiText("请选择保存软件设置的位置，以后可在设置页面修改。"),
      body));
  if (!reason.isEmpty())
    layout->addWidget(ui::secondaryText(reason, body));
  auto *group = new QButtonGroup(body);
  const std::vector<GuiText> labels{guiText("用户配置目录（推荐）"),
                                    guiText("应用所在目录"),
                                    guiText("自定义目录")};
  for (int index = 0; index < static_cast<int>(labels.size()); ++index) {
    auto *radio = new QRadioButton(body);
    setGuiText(radio, "text", labels.at(static_cast<std::size_t>(index)));
    radio->setObjectName(QStringLiteral("settingsLocationOption%1").arg(index));
    group->addButton(radio, index);
    layout->addWidget(radio);
    if (index < 2)
      layout->addWidget(ui::secondaryText(
          index == 0 ? store.userDirectory() : store.applicationDirectory(),
          body));
  }
  auto *custom = new QLineEdit(body);
  custom->setObjectName(QStringLiteral("customSettingsDirectory"));
  setGuiText(custom, "placeholderText", guiText("选择或输入完整目录路径"));
  custom->setMinimumHeight(ui::ControlHeight);
  auto *browse = ui::button(guiText("浏览"), ui::Icon::Folder, body);
  auto *customRow = new QHBoxLayout;
  customRow->addWidget(custom, 1);
  customRow->addWidget(browse);
  layout->addLayout(customRow);
  auto *error = ui::secondaryText({}, body);
  error->setObjectName(QStringLiteral("settingsLocationError"));
  error->setWordWrap(true);
  layout->addWidget(error);
  auto *buttons = new QHBoxLayout;
  buttons->addStretch();
  auto *cancel = ui::button(guiText("取消"), ui::Icon::Cancel, body);
  auto *confirm =
      ui::button(migrate ? guiText("迁移配置") : guiText("开始使用"),
                 ui::Icon::Save, body, true);
  confirm->setObjectName(QStringLiteral("confirmSettingsLocationButton"));
  buttons->addWidget(cancel);
  buttons->addWidget(confirm);
  layout->addLayout(buttons);
  const int initial = migrate ? static_cast<int>(store.location()) : 0;
  group->button(initial)->setChecked(true);
  if (initial == 2)
    custom->setText(store.directory());
  const auto update = [=] {
    custom->setEnabled(group->checkedId() == 2);
    browse->setEnabled(group->checkedId() == 2);
  };
  QObject::connect(group, &QButtonGroup::idToggled, body,
                   [=](int, bool) { update(); });
  update();
  QObject::connect(browse, &QPushButton::clicked, body, [&] {
    const auto path = FileSelectionDialog::getExistingDirectory(
        &dialog, guiText("选择配置文件目录"), custom->text());
    if (!path.isEmpty())
      custom->setText(path);
  });
  QObject::connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
  QObject::connect(confirm, &QPushButton::clicked, body, [&] {
    const auto mode = static_cast<GuiSettingsLocation>(group->checkedId());
    auto result = migrate ? store.relocate(mode, custom->text())
                          : store.initialize(mode, custom->text());
    if (!result) {
      setGuiText(
          error, "text",
          guiText("无法保存配置：%1").arg(result.error().technicalMessage()));
      return;
    }
    dialog.accept();
  });
  dialog.setContentWidget(body);
  dialog.setMinimumWidth(600);
  dialog.resize(640, 500);
  return dialog.exec() == QDialog::Accepted;
}
} // namespace zzrk::gui
