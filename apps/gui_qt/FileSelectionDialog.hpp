// SPDX-License-Identifier: MIT

#pragma once

#include "GuiTranslations.hpp"
#include <QString>

class QWidget;

namespace zzrk::gui {

// 全部文件入口共用这里：Qt 负责文件选择语义，框架负责 Fluent 弹窗外观。
class FileSelectionDialog final {
public:
  [[nodiscard]] static QString getOpenFileName(QWidget *parent,
                                               const GuiText &title,
                                               const QString &directory = {},
                                               const QString &filter = {},
                                               const QString &purpose = {});
  [[nodiscard]] static QString getSaveFileName(QWidget *parent,
                                               const GuiText &title,
                                               const QString &directory = {},
                                               const QString &filter = {},
                                               const QString &purpose = {});
  [[nodiscard]] static QString
  getExistingDirectory(QWidget *parent, const GuiText &title,
                       const QString &directory = {},
                       const QString &purpose = {});
};

} // namespace zzrk::gui
