// SPDX-License-Identifier: MIT
#pragma once

#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileDialog>
#include <QFileInfo>
#include <QLineEdit>
#include <QPushButton>
#include <QThread>
#include <QTreeView>

namespace zzrk::gui::test {

inline QPushButton *filePickerAcceptButton(QFileDialog *browser) {
  auto *buttons = browser->findChild<QDialogButtonBox *>();
  if (buttons)
    for (auto *button : buttons->findChildren<QPushButton *>())
      if (buttons->buttonRole(button) == QDialogButtonBox::AcceptRole)
        return button;
  return nullptr;
}

inline bool enterFilePickerPath(QFileDialog *browser, const QString &path) {
  auto *edit = browser->findChild<QLineEdit *>(QStringLiteral("fileNameEdit"));
  auto *tree = browser->findChild<QTreeView *>(QStringLiteral("treeView"));
  auto *accept = filePickerAcceptButton(browser);
  if (!edit || !tree || !accept)
    return false;

  const QFileInfo target(path);
  browser->setDirectory(target.absolutePath());
  QElapsedTimer timer;
  timer.start();
  const auto targetIsListed = [&] {
    if (!target.exists())
      return true; // 保存目标在调用方写入文件之前没有模型行。
    for (int row = 0; row < tree->model()->rowCount(tree->rootIndex()); ++row) {
      const auto name =
          tree->model()->index(row, 0, tree->rootIndex()).data().toString();
      if (name.normalized(QString::NormalizationForm_C) ==
          target.fileName().normalized(QString::NormalizationForm_C))
        return true;
    }
    return false;
  };
  while (!targetIsListed() && timer.elapsed() < 2000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    QThread::msleep(1);
  }
  if (!targetIsListed())
    return false;

  // Cocoa 下可见输入框获得焦点后，selectFile() 不会更新文件名。
  // 模型加载完成后，使用用户实际编辑的输入框填写完整路径。
  edit->setText(path);
  const auto ready = [&] {
    const auto selected = browser->selectedFiles();
    if (selected.size() != 1 || !accept->isEnabled())
      return false;
    const QFileInfo actual(selected.first());
    // macOS 的 /var 别名和文件系统 Unicode 规范化不改变文件身份。
    const auto expectedPath = target.exists() ? target.canonicalFilePath()
                                              : target.absoluteFilePath();
    const auto actualPath = actual.exists() ? actual.canonicalFilePath()
                                            : actual.absoluteFilePath();
    return actualPath.normalized(QString::NormalizationForm_C) ==
           expectedPath.normalized(QString::NormalizationForm_C);
  };
  while (!ready() && timer.elapsed() < 2000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    QThread::msleep(1);
  }
  return ready();
}

} // namespace zzrk::gui::test
