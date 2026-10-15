// SPDX-License-Identifier: MIT
#include "FileSelectionDialog.hpp"
#include "GuiTestHarness.hpp"

#include <QAction>
#include <QApplication>
#include <QContextMenuEvent>
#include <QDialogButtonBox>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFileInfo>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QTabBar>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QTreeView>
#include <QWindow>
#include <QtTest/QTest>
#include <ZzFluentUI/ZzContentDialog.h>
#include <ZzPureTools/ZzApplicationWindow.h>

#include <cstdlib>
#include <functional>
#include <iostream>

namespace {
int failures = 0;
int editorWarnings = 0;
const char *stage = "startup";

void expect(bool condition, const char *message) {
  if (!condition) {
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
  }
}

bool waitFor(const std::function<bool()> &condition) {
  QElapsedTimer timer;
  timer.start();
  while (!condition() && timer.elapsed() < 2000)
    QTest::qWait(5);
  return condition();
}

void messages(QtMsgType, const QMessageLogContext &, const QString &text) {
  if (text.contains("editor that does not belong to this view"))
    ++editorWarnings;
  std::cerr << text.toStdString() << '\n';
}

void click(QWidget *widget, const QPoint &position) {
  // 走 QWindow 输入分发；QPushButton::click/done
  // 会绕过模态阻塞，不能证明界面可点。
  QTest::mouseClick(widget->window()->windowHandle(), Qt::LeftButton,
                    Qt::NoModifier, widget->mapTo(widget->window(), position));
}
void click(QWidget *widget) { click(widget, widget->rect().center()); }
} // namespace

int main() {
  auto harness = zzrk::gui::test::GuiTestHarness::create();
  if (!harness)
    return EXIT_FAILURE;
  auto *window = harness->window();
  window->resize(800, 700);
  window->move(0, 0);
  window->show();
  const auto previousHandler = qInstallMessageHandler(messages);
  QTimer::singleShot(12000, window, [] {
    std::cerr << "FAIL: input timed out at " << stage << '\n';
    std::exit(EXIT_FAILURE);
  });
  for (bool confirmDeletion : {false, true}) {
    QTemporaryDir fixture;
    if (!fixture.isValid())
      return EXIT_FAILURE;
    // 等待已知目录出现在模型中，避免初始异步扫描尚未结束就创建目录。
    QDir(fixture.path()).mkdir(QStringLiteral("existing-folder"));
    QTimer::singleShot(0, window, [&] {
      stage = "create directory";
      auto *picker = qobject_cast<QDialog *>(QApplication::activeModalWidget());
      auto *browser = picker ? picker->findChild<QFileDialog *>() : nullptr;
      auto *tree =
          browser ? browser->findChild<QTreeView *>("treeView") : nullptr;
      auto *create = browser
                         ? browser->findChild<QAction *>("qt_new_folder_action")
                         : nullptr;
      expect(tree && create, "parameter browser offers directory creation");
      if (!tree || !create) {
        if (picker)
          picker->reject();
        return;
      }
      expect(waitFor([&] {
               return tree->rootIndex().isValid() &&
                      tree->model()->rowCount(tree->rootIndex()) > 0;
             }),
             "parameter directory model has loaded");
      create->trigger();
      QLineEdit *editor = nullptr;
      expect(waitFor([&] {
               for (auto *candidate : tree->findChildren<QLineEdit *>())
                 if (candidate->isVisible()) {
                   editor = candidate;
                   return true;
                 }
               return false;
             }),
             "new directory opens its inline name editor");
      if (!editor) {
        picker->reject();
        return;
      }
      editor->selectAll();
      QTest::keyClicks(editor, "temporary-folder");
      QTest::keyClick(editor, Qt::Key_Return);
      const auto directory = fixture.filePath("temporary-folder");
      expect(waitFor([&] { return QFileInfo::exists(directory); }),
             "new directory is renamed through the file model");
      // 重命名后模型可能排序，重新按名称定位目标，避免点击旧行。
      QModelIndex directoryIndex;
      expect(waitFor([&] {
               for (int row = 0;
                    row < tree->model()->rowCount(tree->rootIndex()); ++row) {
                 const auto candidate =
                     tree->model()->index(row, 0, tree->rootIndex());
                 if (candidate.data().toString() == "temporary-folder") {
                   directoryIndex = candidate;
                   return true;
                 }
               }
               return false;
             }),
             "renamed directory remains in the parameter browser");
      tree->selectionModel()->setCurrentIndex(
          directoryIndex,
          QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
      stage = "open context menu";
      const auto index = tree->currentIndex();
      const auto position = tree->visualRect(index).center();
      bool messageHandled = false;
      QTimer::singleShot(
          0, picker, [picker, browser, confirmDeletion, &messageHandled] {
            auto *menu =
                qobject_cast<QMenu *>(QApplication::activePopupWidget());
            auto *remove = browser->findChild<QAction *>("qt_delete_action");
            expect(menu && remove && remove->isEnabled(),
                   "right-click menu offers deletion");
            if (!menu || !remove || !remove->isEnabled()) {
              messageHandled = true;
              return;
            }
            stage = "confirm directory deletion";
            auto *poll = new QTimer(picker);
            poll->setInterval(10);
            QObject::connect(
                poll, &QTimer::timeout, picker,
                [picker, poll, confirmDeletion] {
                  auto *confirmation =
                      qobject_cast<ZzFluentUI::ZzContentDialog *>(
                          QApplication::activeModalWidget());
                  if (!confirmation || confirmation == picker)
                    return;
                  poll->stop();
                  auto *button = confirmation->findChild<QPushButton *>(
                      confirmDeletion ? "zzContentDialogPrimaryButton"
                                      : "zzContentDialogCloseButton");
                  expect(button && button->isVisible(),
                         "directory confirmation has a visible action");
                  if (button)
                    QTimer::singleShot(0, confirmation,
                                       [button] { click(button); });
                });
            poll->start();
            QTest::mouseClick(menu->windowHandle(), Qt::LeftButton,
                              Qt::NoModifier,
                              menu->actionGeometry(remove).center());
            messageHandled = true;
          });
      QContextMenuEvent context(QContextMenuEvent::Mouse, position,
                                tree->viewport()->mapToGlobal(position));
      QApplication::sendEvent(tree->viewport(), &context);
      expect(waitFor([&] { return messageHandled; }),
             "window input completes directory confirmation");
      expect(QFileInfo::exists(directory) != confirmDeletion,
             "only confirming deletion removes the directory");
      stage = "close browser after deletion";
      expect(QApplication::activeModalWidget() == picker,
             "deletion leaves only the file picker modal");
      auto *buttons = browser->findChild<QDialogButtonBox *>();
      expect(buttons != nullptr, "browser has a cancel button");
      if (buttons) {
        for (auto *button : buttons->findChildren<QPushButton *>())
          if (buttons->buttonRole(button) == QDialogButtonBox::RejectRole) {
            QTimer::singleShot(0, picker, [button] { click(button); });
            break;
          }
      }
    });
    const auto selected = zzrk::gui::FileSelectionDialog::getOpenFileName(
        window, QStringLiteral("选择 parameter.txt"), fixture.path(),
        QStringLiteral("Parameter (parameter*.txt);;All files (*)"));
    expect(selected.isEmpty(), "mouse cancellation closes the browser");
    expect(QApplication::activeModalWidget() == nullptr &&
               QGuiApplication::modalWindow() == nullptr,
           "all modals are released after closing the browser");
  }
  stage = "main window input after deletion";
  auto *tabs = window->findChild<QTabWidget *>("mainTabs");
  expect(tabs && tabs->count() > 1, "main window has multiple pages");
  if (tabs && tabs->count() > 1) {
    const int target = (tabs->currentIndex() + 1) % tabs->count();
    click(tabs->tabBar(), tabs->tabBar()->tabRect(target).center());
    expect(tabs->currentIndex() == target,
           "main window still receives mouse input");
  }
  expect(editorWarnings == 0,
         "inline directory editing has no foreign-editor warnings");
  qInstallMessageHandler(previousHandler);
  return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
