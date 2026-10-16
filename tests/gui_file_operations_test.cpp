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
#include <QFileSystemModel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPersistentModelIndex>
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
      // 同一目录可有不同路径拼写；不要把模型的规范路径文字当成文件身份。
      const auto directory = fixture.filePath("./temporary-folder");
      expect(waitFor([&] { return QFileInfo::exists(directory); }),
             "new directory is renamed through the file model");
      // 切换排序列以完成新目录的待排序批次，再固定重排前的顺序。
      tree->model()->sort(1, Qt::AscendingOrder);
      tree->model()->sort(0, Qt::AscendingOrder);
      expect(waitFor([&] {
               return tree->model()
                          ->index(1, 0, tree->rootIndex())
                          .data()
                          .toString() == "temporary-folder";
             }),
             "renamed directory is sorted after the existing directory");
      // 重命名后模型可能排序，重新按名称定位目标，避免点击旧行。
      QPersistentModelIndex directoryIndex;
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
      // 强制重现异步重命名排序发生在定位目标与选择目标之间的时序。
      tree->model()->sort(0, Qt::DescendingOrder);
      expect(waitFor([&] {
               return tree->model()
                          ->index(0, 0, tree->rootIndex())
                          .data()
                          .toString() == "temporary-folder";
             }),
             "directory reordering moves the intended deletion target");
      // 普通 QModelIndex 重排后可能仍报告原名称，却携带已经失效的行号。
      // 用持久索引保留身份，再走真实窗口输入，并检查 Qt 删除实际消费的行选择。
      // 模拟待处理的视图布局：模型身份已确定，窗口输入前仍可能改变行位置。
      QTimer::singleShot(0, tree, [tree] {
        tree->model()->sort(0, Qt::AscendingOrder);
      });
      QCoreApplication::processEvents();
      // 先处理窗口/表头的布局请求，再取坐标；持久索引只保留身份，不固定位置。
      QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
      tree->doItemsLayout();
      tree->scrollTo(directoryIndex);
      const auto selectionPosition = tree->visualRect(directoryIndex).center();
      expect(tree->indexAt(selectionPosition) == directoryIndex,
             "input coordinates still hit the target after pending layout");
      click(tree->viewport(), selectionPosition);
      const bool targetSelected = waitFor([&] {
        const auto rows = tree->selectionModel()->selectedRows();
        return rows.size() == 1 && rows.constFirst() == directoryIndex;
      });
      expect(
          targetSelected,
          "window input selects the intended directory after model reordering");
      if (!targetSelected) {
        std::cerr << "Selection diagnostics: confirm=" << confirmDeletion
                  << " expected=" << directory.toStdString() << " canonical="
                  << QFileInfo(directory).canonicalFilePath().toStdString()
                  << " hit="
                  << tree->indexAt(selectionPosition)
                         .data(QFileSystemModel::FilePathRole)
                         .toString()
                         .toStdString()
                  << " selected=";
        for (const auto &row : tree->selectionModel()->selectedRows())
          std::cerr << row.data(QFileSystemModel::FilePathRole)
                           .toString()
                           .toStdString()
                    << ';';
        std::cerr << '\n';
        picker->reject();
        return;
      }
      stage = "open context menu";
      const auto position = tree->visualRect(directoryIndex).center();
      bool messageHandled = false;
      int confirmationResult = -1;
      int originalMessageResult = -1;
      QString confirmationText;
      QTimer::singleShot(
          0, picker, [picker, browser, confirmDeletion, &messageHandled,
                      &confirmationResult, &originalMessageResult,
                      &confirmationText] {
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
                [picker, browser, poll, confirmDeletion, &confirmationResult,
                 &originalMessageResult, &confirmationText] {
                  auto *confirmation =
                      qobject_cast<ZzFluentUI::ZzContentDialog *>(
                          QApplication::activeModalWidget());
                  if (!confirmation || confirmation == picker)
                    return;
                  poll->stop();
                  confirmationText = confirmation->text();
                  expect(confirmationText.contains("temporary-folder"),
                         "directory confirmation targets the renamed folder");
                  QObject::connect(
                      confirmation, &QDialog::finished, picker,
                      [confirmation, &confirmationResult] {
                        confirmationResult =
                            static_cast<int>(confirmation->dialogResult());
                      });
                  // 同时观察桥接后的原始返回码，区分输入、按钮映射和文件系统问题。
                  auto *original = browser->findChild<QMessageBox *>();
                  expect(original != nullptr,
                         "directory confirmation bridges a Qt message box");
                  if (original)
                    QObject::connect(original, &QDialog::finished, picker,
                                     [&originalMessageResult](int result) {
                                       originalMessageResult = result;
                                     });
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
      expect(waitFor([&] {
               return confirmationResult != -1 && originalMessageResult != -1;
             }),
             "both confirmation dialogs finish before checking deletion");
      const bool confirmationMatches =
          confirmationResult ==
          static_cast<int>(confirmDeletion
                               ? ZzFluentUI::ZzContentDialogResult::Primary
                               : ZzFluentUI::ZzContentDialogResult::Close);
      const bool originalMessageMatches =
          originalMessageResult ==
          (confirmDeletion ? QMessageBox::Yes : QMessageBox::No);
      expect(confirmationMatches,
             "window input chooses the intended Fluent confirmation action");
      expect(originalMessageMatches,
             "confirmation choice preserves the original Qt return code");
      // 以最终文件状态判断删除结果；取消必须保留原目录。
      const bool directoryStateMatches =
          confirmDeletion
              ? waitFor([&] { return !QFileInfo::exists(directory); })
              : QFileInfo::exists(directory);
      expect(directoryStateMatches,
             "only confirming deletion removes the directory");
      expect(QFileInfo::exists(fixture.filePath("existing-folder")),
             "deletion preserves the unselected directory");
      if (!directoryStateMatches || !confirmationMatches ||
          !originalMessageMatches) {
        std::cerr << "Deletion diagnostics: confirm=" << confirmDeletion
                  << " fluentResult=" << confirmationResult
                  << " qtResult=" << originalMessageResult
                  << " path=" << directory.toStdString()
                  << " exists=" << QFileInfo::exists(directory)
                  << " selection="
                  << browser->selectedFiles().join(',').toStdString()
                  << " prompt=" << confirmationText.toStdString() << '\n';
      }
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
