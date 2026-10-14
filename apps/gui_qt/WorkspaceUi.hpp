// SPDX-License-Identifier: MIT

#pragma once

#include "GuiTranslations.hpp"
#include <QIcon>
#include <QString>

class QLabel;
class QProgressBar;
class QPushButton;
class QTableWidget;
class QToolButton;
class QVBoxLayout;
class QWidget;
namespace ZzFluentUI {
class ZzInfoBar;
}

namespace zzrk::gui::ui {

// 统一视觉参数，均为 Qt 逻辑像素；想调整密度时优先修改这里。
inline constexpr int PageMargin = 20;
inline constexpr int SectionSpacing = 16;
inline constexpr int CardPadding = 16;
inline constexpr int ControlHeight = 34;
inline constexpr int TableRowHeight = 38;
inline constexpr int IconSize = 18;

// 页面只使用操作语义，不关心字体码点，便于以后统一更换图标。
enum class Icon {
  Folder,
  Refresh,
  Flash,
  Cancel,
  Clear,
  Save,
  Info,
  Device,
  File,
  Back,
  Forward,
  Up,
  NewFolder,
  List,
  Details
};
[[nodiscard]] QIcon icon(Icon role);

void configurePage(QWidget *page, QVBoxLayout *layout);
[[nodiscard]] QWidget *card(QWidget *parent);
[[nodiscard]] QLabel *sectionTitle(const GuiText &text, QWidget *parent);
[[nodiscard]] QLabel *secondaryText(const GuiText &text, QWidget *parent);
[[nodiscard]] QPushButton *button(const GuiText &text, Icon role,
                                  QWidget *parent, bool accent = false);
void configureToolButton(QToolButton *button, Icon role);
void configureTable(QTableWidget *table);

// 百分比单独显示，避免框架细线进度条隐藏文本；保留原 QProgressBar 对象。
void addProgressRow(QVBoxLayout *layout, const GuiText &title,
                    QProgressBar *progress, bool primary = false);
void refreshProgressText(QProgressBar *progress);
[[nodiscard]] ZzFluentUI::ZzInfoBar *addErrorNotice(QVBoxLayout *layout);

} // namespace zzrk::gui::ui
