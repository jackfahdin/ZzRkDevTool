// SPDX-License-Identifier: MIT
#include "GuiDialogs.hpp"

#include <QAbstractButton>
#include <QApplication>
#include <QEvent>
#include <QFileDialog>
#include <QLabel>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QTimer>
#include <ZzFluentUI/ZzContentDialog.h>

namespace zzrk::gui::dialogs {
namespace {
using ZzFluentUI::ZzContentDialog;
using ZzFluentUI::ZzContentDialogButton;
using ZzFluentUI::ZzContentDialogResult;

ZzContentDialog *create(QWidget *parent, GuiText title, GuiText text) {
  auto *dialog = new ZzContentDialog(parent ? parent->window() : nullptr);
  dialog->setObjectName(QStringLiteral("guiMessageDialog"));
  setGuiText(dialog, "title", std::move(title));
  setGuiText(dialog, "text", std::move(text));
  // 文件名、设备错误可能包含尖括号，不能被 QLabel 当成 HTML。
  for (auto *label : dialog->findChildren<QLabel *>())
    label->setTextFormat(Qt::PlainText);
  dialog->setDefaultButton(ZzContentDialogButton::Close);
  return dialog;
}

GuiText buttonText(QMessageBox *box, QAbstractButton *button) {
  switch (box->standardButton(button)) {
  case QMessageBox::Ok:
    return guiText("确定");
  case QMessageBox::Yes:
    return guiText("是");
  case QMessageBox::No:
    return guiText("否");
  case QMessageBox::Cancel:
    return guiText("取消");
  case QMessageBox::Save:
    return guiText("保存");
  case QMessageBox::Retry:
    return guiText("重试");
  default:
    return button->text();
  }
}

class FileMessageBridge final : public QObject {
public:
  explicit FileMessageBridge(QFileDialog *browser)
      : QObject(browser), m_browser(browser) {
    qApp->installEventFilter(this);
  }

protected:
  bool eventFilter(QObject *watched, QEvent *event) override {
    if (event->type() != QEvent::Polish && event->type() != QEvent::Show)
      return false;
    auto *box = qobject_cast<QMessageBox *>(watched);
    if (!box)
      return false;
    // QWidget::isAncestorOf 会在顶层窗口处停止；QMessageBox 本身就是窗口。
    auto *owner = box->parent();
    while (owner && owner != m_browser)
      owner = owner->parent();
    if (!owner)
      return false;
    // 必须在 Qt 真正映射窗口前设置；原模态循环保留，但旧窗口不显示到屏幕。
    box->setOption(QMessageBox::Option::DontUseNativeDialog);
    // QMessageBox::setVisible(false) 会重置 WA_DontShowOnScreen，导致 Qt
    // 跳过离屏窗口的模态清理。使用透明窗口保留成对的 show/hide 生命周期。
    box->setWindowOpacity(0);
    if (event->type() == QEvent::Show &&
        !box->property("fluentMessageQueued").toBool()) {
      box->setProperty("fluentMessageQueued", true);
      // exec() 在 show() 之后才进入事件循环，不能直接在 Show 中结束原窗口。
      QTimer::singleShot(0, box, [guard = QPointer<QMessageBox>(box)] {
        if (guard)
          present(guard);
      });
    }
    return false;
  }

private:
  static void present(QPointer<QMessageBox> box) {
    QString text = box->text();
    if (!box->informativeText().isEmpty())
      text += QStringLiteral("\n\n") + box->informativeText();
    if (!box->detailedText().isEmpty())
      text += QStringLiteral("\n\n") + box->detailedText();
    // 先正常隐藏原窗口并解除模态阻塞，再显示 Fluent 确认窗口。
    box->hide();
    QPointer<ZzContentDialog> dialog =
        create(box->parentWidget(), box->windowTitle(), text);
    dialog->setObjectName(QStringLiteral("fileMessageDialog"));
    QPointer<QAbstractButton> primary;
    QPointer<QAbstractButton> secondary;
    QPointer<QAbstractButton> close;
    for (auto *button : box->buttons()) {
      const auto role = box->buttonRole(button);
      if (role == QMessageBox::RejectRole || role == QMessageBox::NoRole) {
        close = button;
        setGuiText(dialog, "closeButtonText", buttonText(box, button));
      } else if (!primary) {
        primary = button;
        setGuiText(dialog, "primaryButtonText", buttonText(box, button));
        dialog->setPrimaryButtonVisible(true);
      } else {
        secondary = button;
        setGuiText(dialog, "secondaryButtonText", buttonText(box, button));
        dialog->setSecondaryButtonVisible(true);
      }
    }
    // 单个 OK 提示只展示一个关闭按钮；Escape 也返回同一个 OK。
    if (!close && primary && !secondary) {
      close = primary;
      primary.clear();
      dialog->setPrimaryButtonVisible(false);
      setGuiText(dialog, "closeButtonText", buttonText(box, close));
    }
    if (!close)
      setGuiText(dialog, "closeButtonText", guiText("取消"));
    dialog->exec();
    const auto result =
        dialog ? dialog->dialogResult() : ZzContentDialogResult::Close;
    delete dialog.data();
    if (!box)
      return;
    auto *selected = result == ZzContentDialogResult::Primary ? primary.data()
                     : result == ZzContentDialogResult::Secondary
                         ? secondary.data()
                         : close.data();
    // 点击原按钮同时保留 Qt 的返回码和 clickedButton，不能仅调用 done(1)。
    if (selected)
      selected->click();
    else
      box->reject();
  }

  QFileDialog *m_browser;
};
} // namespace

void notify(QWidget *parent, GuiText title, GuiText text) {
  QPointer<ZzContentDialog> dialog =
      create(parent, std::move(title), std::move(text));
  setGuiText(dialog, "closeButtonText", guiText("确定"));
  dialog->exec();
  delete dialog.data();
}

bool confirm(QWidget *parent, GuiText title, GuiText text, GuiText action) {
  QPointer<ZzContentDialog> dialog =
      create(parent, std::move(title), std::move(text));
  setGuiText(dialog, "primaryButtonText", std::move(action));
  dialog->setPrimaryButtonVisible(true);
  setGuiText(dialog, "closeButtonText", guiText("取消"));
  dialog->exec();
  const bool confirmed =
      dialog && dialog->dialogResult() == ZzContentDialogResult::Primary;
  delete dialog.data();
  return confirmed;
}

void adaptFileMessages(QFileDialog *browser) { new FileMessageBridge(browser); }
} // namespace zzrk::gui::dialogs
