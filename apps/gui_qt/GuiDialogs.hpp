// SPDX-License-Identifier: MIT
#pragma once

#include "GuiTranslations.hpp"

class QFileDialog;
class QWidget;

namespace zzrk::gui::dialogs {
// 业务提示统一走框架，调用方只关心用户是否明确确认。
void notify(QWidget *parent, GuiText title, GuiText text);
bool confirm(QWidget *parent, GuiText title, GuiText text,
             GuiText action = guiText("继续"));

// 仅桥接该文件浏览器内部的 Qt 提示，文件删除/覆盖仍交给 Qt 执行。
void adaptFileMessages(QFileDialog *browser);
} // namespace zzrk::gui::dialogs
