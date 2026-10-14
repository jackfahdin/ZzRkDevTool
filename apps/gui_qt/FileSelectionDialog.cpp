// SPDX-License-Identifier: MIT

#include "FileSelectionDialog.hpp"
#include "GuiSettings.hpp"
#include "GuiTranslations.hpp"
#include "GuiWindowShell.hpp"
#include "WorkspaceUi.hpp"
#include <QStatusBar>
#include <ZzPureTools/ZzApplicationWindow.h>

#include <ZzFluentUI/ZzContentDialog.h>
#include <ZzFluentUI/ZzSegoeIconFont.h>

#include <QAbstractItemView>
#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QEvent>
#include <QFileDialog>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QGridLayout>
#include <QHeaderView>
#include <QIdentityProxyModel>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QScreen>
#include <QSplitter>
#include <QStandardPaths>
#include <QStyledItemDelegate>
#include <QToolButton>
#include <QTreeView>
#include <QUrl>

#include <algorithm>
#include <array>

namespace zzrk::gui {
namespace {

enum class SelectionMode { OpenFile, SaveFile, Directory };

// 避免平台文件图标和彩色文件夹混入线性图标；QFileDialog 不接管 provider。
class FileIcons final : public QFileIconProvider {
public:
  QIcon icon(IconType type) const override {
    if (type == Folder)
      return ui::icon(ui::Icon::Folder);
    if (type == Computer || type == Drive || type == Network)
      return ui::icon(ui::Icon::Device);
    return ui::icon(ui::Icon::File);
  }
  QIcon icon(const QFileInfo &info) const override {
    return ui::icon(info.isDir() ? ui::Icon::Folder : ui::Icon::File);
  }
  QString type(const QFileInfo &info) const override {
    if (info.isDir())
      return guiText("文件夹");
    const auto suffix = info.suffix().toLower();
    if (suffix == QStringLiteral("img"))
      return guiText("镜像文件");
    if (suffix == QStringLiteral("bin"))
      return guiText("二进制文件");
    if (suffix == QStringLiteral("txt"))
      return guiText("文本文件");
    return suffix.isEmpty() ? guiText("文件")
                            : suffix.toUpper() + guiText(" 文件");
  }
};

// 只翻译表头，不改变 Qt 文件模型的路径、排序和选择逻辑。
class FileHeaders final : public QIdentityProxyModel {
public:
  using QIdentityProxyModel::QIdentityProxyModel;
  QVariant headerData(int section, Qt::Orientation orientation,
                      int role) const override {
    const QStringList labels{guiText("名称"), guiText("大小"), guiText("类型"),
                             guiText("修改时间")};
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole &&
        section >= 0 && section < labels.size())
      return labels.at(section);
    return QIdentityProxyModel::headerData(section, orientation, role);
  }
};

class FileRows : public QStyledItemDelegate {
public:
  using QStyledItemDelegate::QStyledItemDelegate;
  QSize sizeHint(const QStyleOptionViewItem &option,
                 const QModelIndex &index) const override {
    auto size = QStyledItemDelegate::sizeHint(option, index);
    size.setHeight(std::max(size.height(), ui::TableRowHeight));
    return size;
  }
};

// Qt 会异步重写常用位置名称；在绘制层翻译，避免改动路径模型或与它反复写数据。
class PlaceRows final : public FileRows {
public:
  PlaceRows(QFileDialog *browser, QWidget *parent)
      : FileRows(parent), m_browser(browser) {}

protected:
  void initStyleOption(QStyleOptionViewItem *option,
                       const QModelIndex &index) const override {
    FileRows::initStyleOption(option, index);
    const auto urls = m_browser->sidebarUrls();
    if (index.row() < 0 || index.row() >= urls.size())
      return;
    const QString path = urls.at(index.row()).toLocalFile();
    if (path == QDir::homePath())
      option->text = guiText("主目录");
    else if (path ==
             QStandardPaths::writableLocation(QStandardPaths::DesktopLocation))
      option->text = guiText("桌面");
    else if (path ==
             QStandardPaths::writableLocation(QStandardPaths::DownloadLocation))
      option->text = guiText("下载");
    else if (path == QStandardPaths::writableLocation(
                         QStandardPaths::DocumentsLocation))
      option->text = guiText("文档");
  }

private:
  QFileDialog *m_browser;
};

// ContentDialog 的默认宽度适合短提示；文件浏览需要更宽的工作区。
// 主题刷新后会重设框架宽度约束，所以布局请求时再次应用浏览窗口的宽度范围。
class BrowserWidth final : public QObject {
public:
  BrowserWidth(ZzFluentUI::ZzContentDialog *dialog, int maximumWidth)
      : QObject(dialog), m_dialog(dialog), m_maximumWidth(maximumWidth) {
    dialog->installEventFilter(this);
    updateWidth();
  }

protected:
  bool eventFilter(QObject *watched, QEvent *event) override {
    if (event->type() == QEvent::LayoutRequest)
      updateWidth();
    return QObject::eventFilter(watched, event);
  }

private:
  void updateWidth() {
    const int minimumWidth = std::min(640, m_maximumWidth);
    if (m_dialog->maximumWidth() != m_maximumWidth)
      m_dialog->setMaximumWidth(m_maximumWidth);
    if (m_dialog->minimumWidth() != minimumWidth)
      m_dialog->setMinimumWidth(minimumWidth);
  }
  ZzFluentUI::ZzContentDialog *m_dialog;
  int m_maximumWidth;
};

void configureBrowser(QFileDialog *browser, SelectionMode mode) {
  browser->setViewMode(QFileDialog::Detail);
  bindGuiTranslation(browser, [browser, mode] {
    browser->setLabelText(QFileDialog::LookIn, guiText("位置"));
    browser->setLabelText(QFileDialog::FileName,
                          mode == SelectionMode::Directory ? guiText("目录")
                                                           : guiText("文件名"));
    browser->setLabelText(QFileDialog::FileType, guiText("文件类型"));
    browser->setLabelText(QFileDialog::Accept, mode == SelectionMode::SaveFile
                                                   ? guiText("保存")
                                                   : guiText("选择"));
    browser->setLabelText(QFileDialog::Reject, guiText("取消"));
  });
  browser->setProxyModel(new FileHeaders(browser));
  browser->setItemDelegate(new FileRows(browser));
  auto font = browser->font();
  font.setPixelSize(14);
  browser->setFont(font);
  if (browser->layout() != nullptr) {
    browser->layout()->setContentsMargins(0, 0, 0, 0);
    browser->layout()->setSpacing(12);
  }
  for (auto *label : browser->findChildren<QLabel *>())
    label->setTextFormat(Qt::PlainText);
  for (auto *edit : browser->findChildren<QLineEdit *>())
    edit->setMinimumHeight(ui::ControlHeight);
  for (auto *view : browser->findChildren<QAbstractItemView *>()) {
    view->setIconSize(QSize(ui::IconSize, ui::IconSize));
    view->setFrameShape(QFrame::NoFrame);
    if (auto *tree = qobject_cast<QTreeView *>(view)) {
      tree->setRootIsDecorated(false);
      tree->setAlternatingRowColors(false);
      tree->header()->setStretchLastSection(false);
      tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
      for (int section = 1; section < 4; ++section)
        tree->header()->setSectionResizeMode(section,
                                             QHeaderView::ResizeToContents);
    }
  }
  if (auto *sidebar =
          browser->findChild<QAbstractItemView *>(QStringLiteral("sidebar")))
    sidebar->setMinimumWidth(150);
  if (auto *splitter = browser->findChild<QSplitter *>()) {
    splitter->setHandleWidth(1);
    splitter->setSizes({170, 630});
  }
  // 地址框使用自己的路径模型，需要单独替换缓存的平台文件夹图标。
  if (auto *locations =
          browser->findChild<QComboBox *>(QStringLiteral("lookInCombo"))) {
    const QIcon folderIcon = ui::icon(ui::Icon::Folder);
    const auto updateIcons = [locations, folderIcon] {
      for (int index = 0; index < locations->count(); ++index)
        if (locations->itemIcon(index).cacheKey() != folderIcon.cacheKey())
          locations->setItemIcon(index, folderIcon);
    };
    updateIcons();
    QObject::connect(browser, &QFileDialog::directoryEntered, locations,
                     updateIcons);
    QObject::connect(locations->model(), &QAbstractItemModel::rowsInserted,
                     locations, updateIcons);
    QObject::connect(locations->model(), &QAbstractItemModel::dataChanged,
                     locations, updateIcons);
    QObject::connect(locations->model(), &QAbstractItemModel::modelReset,
                     locations, updateIcons);
    locations->setMinimumHeight(ui::ControlHeight);
  }

  // Qt 标准文件浏览器的工具按钮名集中适配于此，不在业务页面依赖内部布局。
  struct Tool {
    const char *name;
    ui::Icon icon;
    const char *tooltip;
  };
  const std::array tools{
      Tool{"backButton", ui::Icon::Back, "后退"},
      Tool{"forwardButton", ui::Icon::Forward, "前进"},
      Tool{"toParentButton", ui::Icon::Up, "上一级目录"},
      Tool{"newFolderButton", ui::Icon::NewFolder, "新建文件夹"},
      Tool{"listModeButton", ui::Icon::List, "列表视图"},
      Tool{"detailModeButton", ui::Icon::Details, "详细视图"}};
  for (const auto &tool : tools)
    if (auto *button =
            browser->findChild<QToolButton *>(QString::fromLatin1(tool.name))) {
      ui::configureToolButton(button, tool.icon);
      setGuiText(button, "toolTip", guiText(tool.tooltip));
      setGuiText(button, "accessibleName", guiText(tool.tooltip));
    }
  if (auto *buttons = browser->findChild<QDialogButtonBox *>()) {
    // 将 Qt 默认右侧竖排按钮移到底部，文件名和筛选字段可以占满整行。
    buttons->setOrientation(Qt::Horizontal);
    if (auto *grid = qobject_cast<QGridLayout *>(browser->layout())) {
      grid->removeWidget(buttons);
      const int columns = grid->columnCount();
      for (const auto &name :
           {QStringLiteral("fileNameEdit"), QStringLiteral("fileTypeCombo")}) {
        if (auto *field = browser->findChild<QWidget *>(name)) {
          int row = 0, column = 0, rowSpan = 0, columnSpan = 0;
          const int index = grid->indexOf(field);
          if (index < 0)
            continue;
          grid->getItemPosition(index, &row, &column, &rowSpan, &columnSpan);
          grid->removeWidget(field);
          grid->addWidget(field, row, column, rowSpan, columns - column);
          field->setMinimumHeight(ui::ControlHeight);
        }
      }
      grid->addWidget(buttons, grid->rowCount(), 0, 1, columns, Qt::AlignRight);
    }
    for (auto *button : buttons->findChildren<QPushButton *>()) {
      button->setMinimumHeight(ui::ControlHeight);
      const bool accept =
          buttons->buttonRole(button) == QDialogButtonBox::AcceptRole;
      button->setProperty("accent", accept);
      const auto icon =
          ui::icon(accept ? (mode == SelectionMode::SaveFile ? ui::Icon::Save
                                                             : ui::Icon::Folder)
                          : ui::Icon::Cancel);
      button->setIcon(
          accept ? ZzFluentUI::ZzSegoeIconFont::withForegroundColor(
                       icon, button->palette().color(QPalette::HighlightedText))
                 : icon);
      button->setIconSize(QSize(ui::IconSize, ui::IconSize));
    }
  }
  if (mode == SelectionMode::Directory) {
    for (const auto &name :
         {QStringLiteral("fileTypeLabel"), QStringLiteral("fileTypeCombo")})
      if (auto *widget = browser->findChild<QWidget *>(name))
        widget->hide();
  }

  QList<QUrl> places{QUrl::fromLocalFile(QDir::homePath())};
  for (const auto location :
       {QStandardPaths::DesktopLocation, QStandardPaths::DownloadLocation,
        QStandardPaths::DocumentsLocation}) {
    const auto path = QStandardPaths::writableLocation(location);
    const auto url = QUrl::fromLocalFile(path);
    if (QDir(path).exists() && !places.contains(url))
      places.append(url);
  }
  for (const auto &drive : QDir::drives())
    places.append(QUrl::fromLocalFile(drive.absoluteFilePath()));
  browser->setSidebarUrls(places);
  if (auto *sidebar =
          browser->findChild<QAbstractItemView *>(QStringLiteral("sidebar")))
    sidebar->setItemDelegate(new PlaceRows(browser, sidebar));
}

QString selectPath(QWidget *parent, const GuiText &title,
                   const QString &directory, const QString &filter,
                   SelectionMode mode, const QString &purpose) {
  auto *window =
      parent
          ? qobject_cast<ZzPureTools::ZzApplicationWindow *>(parent->window())
          : nullptr;
  QPointer<GuiWindowShell> shell =
      window ? GuiWindowShell::attachedTo(*window) : nullptr;
  const auto settings = shell ? shell->settings() : nullptr;
  const QString key =
      QStringLiteral("fileDialog/") +
      (purpose.isEmpty()
           ? mode == SelectionMode::SaveFile    ? QStringLiteral("save")
             : mode == SelectionMode::Directory ? QStringLiteral("directory")
                                                : QStringLiteral("open")
           : purpose);
  QString initial = directory;
  if (settings) {
    QString previous = settings->value(key).toString();
    if (mode == SelectionMode::SaveFile &&
        !settings->value(QStringLiteral("paths/backup")).toString().isEmpty())
      previous = settings->value(QStringLiteral("paths/backup")).toString();
    if (QDir(previous).exists() && !previous.isEmpty()) {
      if (initial.isEmpty())
        initial = previous;
      else if (mode == SelectionMode::SaveFile &&
               QFileInfo(initial).isRelative())
        initial = QDir(previous).filePath(initial);
    }
  }
  FileIcons icons; // 必须比浏览器活得久：关闭并销毁弹窗后才释放图标提供器。
  QPointer<ZzFluentUI::ZzContentDialog> dialog =
      new ZzFluentUI::ZzContentDialog(parent != nullptr ? parent->window()
                                                        : nullptr);
  dialog->setObjectName(QStringLiteral("fileSelectionDialog"));
  setGuiText(dialog, "title", title);
  dialog->setPrimaryButtonVisible(false);
  dialog->setSecondaryButtonVisible(false);
  dialog->setCloseButtonVisible(false);
  auto *browser = new QFileDialog(dialog);
  browser->setObjectName(QStringLiteral("fileSelectionBrowser"));
  // 显式使用 Qt 控件实现，保证三平台都能由框架绘制而不跳到系统文件窗口。
  browser->setOption(QFileDialog::DontUseNativeDialog, true);
  browser->setAcceptMode(mode == SelectionMode::SaveFile
                             ? QFileDialog::AcceptSave
                             : QFileDialog::AcceptOpen);
  browser->setFileMode(mode == SelectionMode::Directory ? QFileDialog::Directory
                       : mode == SelectionMode::SaveFile
                           ? QFileDialog::AnyFile
                           : QFileDialog::ExistingFile);
  browser->setOption(QFileDialog::ShowDirsOnly,
                     mode == SelectionMode::Directory);
  browser->setIconProvider(&icons);
  browser->setNameFilter(filter.isEmpty() ? guiText("所有文件 (*)") : filter);
  if (!initial.isEmpty()) {
    if (QFileInfo(initial).isDir())
      browser->setDirectory(initial);
    else {
      browser->setDirectory(QFileInfo(initial).absolutePath());
      browser->selectFile(QFileInfo(initial).fileName());
    }
  }
  configureBrowser(browser, mode);
  browser->setWindowFlags(Qt::Widget);
  dialog->setContentWidget(browser);
  QSize available = parent != nullptr
                        ? parent->window()->size()
                        : dialog->screen()->availableGeometry().size();
  const QSize size(std::min(900, available.width() - 40),
                   std::min(640, available.height() - 60));
  new BrowserWidth(dialog, std::max(640, size.width()));
  dialog->resize(size);
  QString selected;
  QObject::connect(browser, &QDialog::accepted, dialog, [&] {
    const auto files = browser->selectedFiles();
    if (!files.isEmpty())
      selected = files.first();
    dialog->accept();
  });
  QObject::connect(browser, &QDialog::rejected, dialog, &QDialog::reject);
  // 浏览器自己管理确认/取消和文件合法性；外围对话框只管理模态与主题。
  // 父窗口关闭可能销毁弹窗，使用 QPointer 避免关闭后再次释放失效对象。
  dialog->exec();
  delete dialog.data();
  if (settings && !selected.isEmpty()) {
    settings->setValue(key, mode == SelectionMode::Directory
                                ? selected
                                : QFileInfo(selected).absolutePath());
    // 最近目录在退出弹窗时保存，即使随后未执行烧录也不会丢失。
    const auto saved = settings->sync();
    if (!saved && shell && shell->parent()) {
      auto *host =
          qobject_cast<ZzPureTools::ZzApplicationWindow *>(shell->parent());
      if (host)
        setGuiStatus(
            host->statusBar(),
            guiText("配置保存失败：%1").arg(saved.error().technicalMessage()));
    }
  }
  return selected;
}

} // namespace

QString FileSelectionDialog::getOpenFileName(QWidget *parent,
                                             const GuiText &title,
                                             const QString &directory,
                                             const QString &filter,
                                             const QString &purpose) {
  return selectPath(parent, title, directory, filter, SelectionMode::OpenFile,
                    purpose);
}
QString FileSelectionDialog::getSaveFileName(QWidget *parent,
                                             const GuiText &title,
                                             const QString &directory,
                                             const QString &filter,
                                             const QString &purpose) {
  return selectPath(parent, title, directory, filter, SelectionMode::SaveFile,
                    purpose);
}
QString FileSelectionDialog::getExistingDirectory(QWidget *parent,
                                                  const GuiText &title,
                                                  const QString &directory,
                                                  const QString &purpose) {
  return selectPath(parent, title, directory, {}, SelectionMode::Directory,
                    purpose);
}

} // namespace zzrk::gui
