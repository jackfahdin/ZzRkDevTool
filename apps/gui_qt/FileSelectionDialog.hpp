// SPDX-License-Identifier: MIT

#pragma once

#include <QString>

class QWidget;

namespace zzrk::gui {

// 全部文件入口共用这里：Qt 负责文件选择语义，框架负责 Fluent 弹窗外观。
class FileSelectionDialog final {
public:
  [[nodiscard]] static QString getOpenFileName(
      QWidget *parent, const QString &title, const QString &directory = {},
      const QString &filter = {});
  [[nodiscard]] static QString getSaveFileName(
      QWidget *parent, const QString &title, const QString &directory = {},
      const QString &filter = {});
  [[nodiscard]] static QString getExistingDirectory(
      QWidget *parent, const QString &title, const QString &directory = {});
};

} // namespace zzrk::gui
