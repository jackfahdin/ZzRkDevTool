// SPDX-License-Identifier: MIT

#include "WorkspaceUi.hpp"
#include "GuiTranslations.hpp"

#include <QApplication>
#include <ZzFluentUI/ZzButtonAppearance.h>
#include <ZzFluentUI/ZzControlAppearance.h>
#include <ZzFluentUI/ZzInfoBar.h>
#include <ZzFluentUI/ZzPushButton.h>
#include <ZzFluentUI/ZzSegoeIconFont.h>
#include <ZzFluentUI/ZzThemeController.h>
#include <ZzFluentUI/ZzThemeSnapshot.h>
#include <ZzFluentUI/ZzTypographyToken.h>
#include <ZzPureTools/ZzPureApplication.h>

#include <QEvent>
#include <QFont>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPainter>
#include <QPalette>
#include <QProgressBar>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <optional>

namespace zzrk::gui::ui {
namespace {

// 卡片仅负责绘制。颜色来自框架调色板，切换主题时不需要业务代码同步颜色。
class Surface final : public QWidget {
public:
  explicit Surface(QWidget *parent) : QWidget(parent) {}

  void setCompact(bool compact) {
    if (layout() == nullptr)
      return;
    if (!m_originalMargins)
      m_originalMargins = layout()->contentsMargins();
    const auto margins = *m_originalMargins;
    layout()->setContentsMargins(
        compact ? qMin(12, margins.left()) : margins.left(),
        compact ? qMin(12, margins.top()) : margins.top(),
        compact ? qMin(12, margins.right()) : margins.right(),
        compact ? qMin(12, margins.bottom()) : margins.bottom());
  }

protected:
  void paintEvent(QPaintEvent *) override {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    QColor outline = palette().color(QPalette::Mid);
    outline.setAlpha(65);
    painter.setPen(outline);
    painter.setBrush(palette().color(QPalette::Base));
    painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 10,
                            10);
  }

private:
  std::optional<QMargins> m_originalMargins;
};

// 小窗口自动收紧留白，优先给清单和任务按钮留空间；恢复尺寸后恢复原间距。
class PageDensity final : public QObject {
public:
  PageDensity(QWidget *page, QVBoxLayout *layout)
      : QObject(page), m_page(page), m_layout(layout) {
    page->installEventFilter(this);
  }

protected:
  bool eventFilter(QObject *watched, QEvent *event) override {
    if (event->type() == QEvent::Resize || event->type() == QEvent::Show) {
      const bool compact = m_page->width() < 900 || m_page->height() < 680;
      const int margin = compact ? 12 : PageMargin;
      m_layout->setContentsMargins(margin, margin, margin, margin);
      m_layout->setSpacing(compact ? 10 : SectionSpacing);
      for (auto *child : m_page->findChildren<QWidget *>())
        if (auto *surface = dynamic_cast<Surface *>(child))
          surface->setCompact(compact);
    }
    return QObject::eventFilter(watched, event);
  }

private:
  QWidget *m_page;
  QVBoxLayout *m_layout;
};

// 长路径会让通知换行；窗口改变宽度后同步最小高度，防止只保护首行。
class NoticeSizing final : public QObject {
public:
  explicit NoticeSizing(ZzFluentUI::ZzInfoBar *notice)
      : QObject(notice), m_notice(notice) {
    notice->installEventFilter(this);
  }

protected:
  bool eventFilter(QObject *watched, QEvent *event) override {
    if (m_notice->isOpen() && (event->type() == QEvent::Resize ||
                               event->type() == QEvent::LayoutRequest)) {
      const int height = m_notice->heightForWidth(m_notice->width());
      if (m_notice->minimumHeight() != height)
        m_notice->setMinimumHeight(height);
    }
    return QObject::eventFilter(watched, event);
  }

private:
  ZzFluentUI::ZzInfoBar *m_notice;
};

// Qt 在主题快照通知之后才把新字体传播到子控件；依据表格自己的
// FontChange 调整高度，避免使用上一轮字体度量导致大字号被裁切。
class TableSizing final : public QObject {
public:
  explicit TableSizing(QTableWidget *table) : QObject(table), m_table(table) {
    table->installEventFilter(this);
    update();
  }

protected:
  bool eventFilter(QObject *watched, QEvent *event) override {
    if (event->type() == QEvent::FontChange)
      update();
    return QObject::eventFilter(watched, event);
  }

private:
  void update() {
    m_table->verticalHeader()->setDefaultSectionSize(
        qMax(TableRowHeight, m_table->fontMetrics().height() + 16));
    m_table->horizontalHeader()->setMinimumHeight(
        qMax(ControlHeight, m_table->fontMetrics().height() + 12));
  }
  QTableWidget *m_table;
};

QLabel *label(const QString &text, QWidget *parent,
              ZzFluentUI::ZzTypographyToken role) {
  auto *result = new QLabel(text, parent);
  // 显式使用纯文本，固件名称或错误消息中的尖括号不应被解释为 HTML。
  result->setTextFormat(Qt::PlainText);
  // 角色字体来自框架；监听快照才能让已创建的标签随全局字体即时更新。
  if (auto *app = qobject_cast<ZzPureTools::ZzPureApplication *>(qApp)) {
    auto *theme = app->themeController();
    auto update = [result, theme, role] {
      result->setFont(theme->snapshot()->font(role));
    };
    update();
    QObject::connect(theme, &ZzFluentUI::ZzThemeController::snapshotChanged,
                     result, update);
  }
  result->setWordWrap(true);
  return result;
}
} // namespace

QIcon icon(const Icon role) {
  // 框架已内嵌完整 Segoe Fluent Icons 字体，但命名枚举只覆盖部分字形。
  // 缺少的标准码点集中在这里；框架补齐枚举后可直接替换对应映射。
  char16_t glyph = 0xe8b7; // Folder
  switch (role) {
  case Icon::Folder:
    glyph = 0xe8b7;
    break;
  case Icon::Refresh:
    glyph = 0xe72c;
    break;
  case Icon::Flash:
    glyph = 0xe896;
    break; // Download
  case Icon::Cancel:
    glyph = 0xe894;
    break; // Close
  case Icon::Clear:
    glyph = 0xe74d;
    break; // Delete
  case Icon::Save:
    glyph = 0xe74e;
    break;
  case Icon::Info:
    glyph = 0xe946;
    break;
  case Icon::Device:
    glyph = 0xe772;
    break; // Devices
  case Icon::File:
    glyph = 0xe8a5;
    break; // Page
  case Icon::Back:
    glyph = 0xe72b;
    break;
  case Icon::Forward:
    glyph = 0xe72a;
    break;
  case Icon::Up:
    glyph = 0xe74a;
    break;
  case Icon::NewFolder:
    glyph = 0xe8f4;
    break;
  case Icon::List:
    glyph = 0xe8fd;
    break;
  case Icon::Details:
    glyph = 0xe8a9;
    break;
  }
  return ZzFluentUI::ZzSegoeIconFont::icon(
      static_cast<ZzFluentUI::ZzSegoeIcon>(glyph));
}

void configurePage(QWidget *page, QVBoxLayout *layout) {
  layout->setContentsMargins(PageMargin, PageMargin, PageMargin, PageMargin);
  layout->setSpacing(SectionSpacing);
  new PageDensity(page, layout);
}

QWidget *card(QWidget *parent) { return new Surface(parent); }

QLabel *sectionTitle(const GuiText &text, QWidget *parent) {
  auto *result = label(text, parent, ZzFluentUI::ZzTypographyToken::BodyStrong);
  setGuiText(result, "text", text);
  return result;
}

QLabel *secondaryText(const GuiText &text, QWidget *parent) {
  auto *result = label(text, parent, ZzFluentUI::ZzTypographyToken::Caption);
  setGuiText(result, "text", text);
  result->setForegroundRole(QPalette::PlaceholderText);
  return result;
}

QPushButton *button(const GuiText &text, const Icon role, QWidget *parent,
                    const bool accent) {
  auto *result = new ZzFluentUI::ZzPushButton(text, parent);
  setGuiText(result, "text", text);
  result->setAppearance(accent ? ZzFluentUI::ZzButtonAppearance::Accent
                               : ZzFluentUI::ZzButtonAppearance::Standard);
  result->setIcon(icon(role));
  result->setIconSize(QSize(IconSize, IconSize));
  result->setMinimumHeight(ControlHeight);
  return result;
}

void configureToolButton(QToolButton *button, const Icon role) {
  button->setIcon(icon(role));
  button->setIconSize(QSize(IconSize, IconSize));
  button->setMinimumSize(ControlHeight, ControlHeight);
  button->setAutoRaise(true);
}

void configureTable(QTableWidget *table) {
  table->setAlternatingRowColors(false);
  table->setShowGrid(false);
  table->setFrameShape(QFrame::NoFrame);
  table->setWordWrap(false);
  table->setTextElideMode(Qt::ElideMiddle);
  table->verticalHeader()->setVisible(false);
  table->verticalHeader()->setDefaultSectionSize(TableRowHeight);
  table->horizontalHeader()->setMinimumHeight(ControlHeight);
  table->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft |
                                                 Qt::AlignVCenter);
  new TableSizing(table);
  // 表格交给框架绘制，不使用 palette(...) 样式表。
  // Qt 样式表会固定部分调色板角色，导致运行中切换主题后仍残留浅色背景。
}

void addProgressRow(QVBoxLayout *layout, const GuiText &title,
                    QProgressBar *progress, const bool primary) {
  auto *row = new QHBoxLayout;
  row->addWidget(secondaryText(title, layout->parentWidget()));
  row->addStretch();
  auto *percent = label(QStringLiteral("0%"), layout->parentWidget(),
                        ZzFluentUI::ZzTypographyToken::Caption);
  percent->setObjectName(progress->objectName() + QStringLiteral("Percent"));
  row->addWidget(percent);
  layout->addLayout(row);
  progress->setTextVisible(false);
  progress->setFixedHeight(8);
  ZzFluentUI::ZzControlAppearance::setProgressBarAppearance(
      progress, primary ? ZzFluentUI::ZzProgressBarAppearance::Thick
                        : ZzFluentUI::ZzProgressBarAppearance::Thin);
  layout->addWidget(progress);
  QObject::connect(progress, &QProgressBar::valueChanged, percent,
                   [progress](int) { refreshProgressText(progress); });
  refreshProgressText(progress);
  bindGuiTranslation(progress, [progress] { refreshProgressText(progress); });
}

void refreshProgressText(QProgressBar *progress) {
  auto *parent = progress->parentWidget();
  auto *percent = parent != nullptr
                      ? parent->findChild<QLabel *>(progress->objectName() +
                                                    QStringLiteral("Percent"))
                      : nullptr;
  if (percent != nullptr)
    percent->setText(
        progress->minimum() == 0 && progress->maximum() == 0
            ? guiText("进行中")
            : QStringLiteral("%1%").arg(qMax(0, progress->value())));
}

ZzFluentUI::ZzInfoBar *addErrorNotice(QVBoxLayout *layout) {
  auto *notice = new ZzFluentUI::ZzInfoBar(layout->parentWidget());
  notice->setSeverity(ZzFluentUI::ZzInfoBar::Error);
  setGuiText(notice, "title", guiText("操作未完成"));
  notice->setClosable(false);
  notice->setAnimationEnabled(false);
  for (auto *label : notice->findChildren<QLabel *>())
    label->setTextFormat(Qt::PlainText);
  // 框架提供 heightForWidth；固定纵向策略使任务布局优先保留通知文字高度。
  notice->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
  new NoticeSizing(notice);
  layout->addWidget(notice);
  return notice;
}

} // namespace zzrk::gui::ui
