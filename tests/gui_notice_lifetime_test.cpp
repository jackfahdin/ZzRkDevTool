// SPDX-License-Identifier: MIT
#include "WorkspaceUi.hpp"

#include <QApplication>
#include <QResizeEvent>
#include <QVBoxLayout>
#include <QWidget>
#include <ZzFluentUI/ZzInfoBar.h>

#include <iostream>

int main(int argc, char **argv) {
  QApplication application(argc, argv);
  // QWidget 析构会移除内部布局；此时 ZzInfoBar 的私有数据已经释放，
  // 但安装在它上面的子对象事件过滤器还活着，不能再调用派生类方法。
  for (const bool open : {false, true}) {
    QWidget page;
    auto *layout = new QVBoxLayout(&page);
    auto *notice = zzrk::gui::ui::addErrorNotice(layout);
    notice->setOpen(open);
    // 布局在 QWidget 基类析构阶段先被销毁；补发过滤器实际处理的事件，
    // 同时验证类型检查，而不只是忽略常见的 ChildRemoved 事件。
    QObject::connect(notice->layout(), &QObject::destroyed, &application,
                     [notice] {
                       QResizeEvent resize(QSize(320, 40), QSize(640, 40));
                       QApplication::sendEvent(notice, &resize);
                       QEvent layoutRequest(QEvent::LayoutRequest);
                       QApplication::sendEvent(notice, &layoutRequest);
                     });
    delete notice;
  }
  std::cout << "Open and closed notices can be destroyed safely\n";
  return 0;
}
