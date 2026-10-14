// SPDX-License-Identifier: MIT
#pragma once
#include <QString>
class QWidget;
namespace zzrk::gui {
class GuiSettingsStore;
// 首次选择和设置页迁移共用一处目录规则；只有保存成功才关闭弹窗。
bool chooseSettingsLocation(GuiSettingsStore &store, QWidget *parent = nullptr,
                            bool migrate = false, const QString &reason = {});
} // namespace zzrk::gui
