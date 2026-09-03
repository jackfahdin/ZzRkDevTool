# 任务 9 验收报告

## ASan 阻断问题诊断（2026-09-03）

本节记录诊断证据、根因假设以及随后验证的修复。

### 构建和复现命令

```bash
cmake -S . -B build-asan -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_PREFIX_PATH=/home/zz/Qt/6.11.1/gcc_64 \
  -DZZRK_BUILD_GUI=ON \
  -DZZRK_BUILD_CLI=ON \
  -DZZRK_BUILD_TRANSPORT=ON \
  -DZZRK_BUILD_TESTS=ON \
  -DZZRK_LIBUSB_ROOT="$PWD/thirdParty/libusb" \
  -DZZRK_WARNINGS_AS_ERRORS=ON \
  -DZZRK_ENABLE_SANITIZERS=ON \
  -DFETCHCONTENT_SOURCE_DIR_FMT="$PWD/build/linux-debug/_deps/fmt-src"
cmake --build build-asan --parallel 4
ctest --test-dir build-asan --output-on-failure \
  -R 'gui_framework|gui_upgrade|gui_image_download|gui_smoke'
ctest --test-dir build-asan --output-on-failure -R '^gui_smoke$'
ctest --test-dir build-asan --output-on-failure -R '^gui_upgrade$'
```

`FETCHCONTENT_SOURCE_DIR_FMT` 只复用此前已校验的 fmt 12.1.0 源码，避免受当前受限网络影响，不改变项目源文件或编译选项。

复现结果：

| 测试 | 首次重点集合 | 独立复现 1 | 独立复现 2 | 独立复现 3 |
| --- | --- | --- | --- | --- |
| `gui_smoke` | UAF | UAF | UAF | UAF |
| `gui_upgrade` | UAF | UAF | UAF | UAF |
| `gui_framework` | 通过 | 未重复 | 未重复 | 未重复 |
| `gui_image_download` | 通过 | 未重复 | 未重复 | 未重复 |

首次重点集合的完整结尾：

```text
    Start 19: gui_smoke
1/4 Test #19: gui_smoke ........................***Failed    0.59 sec
    Start 20: gui_framework
2/4 Test #20: gui_framework ....................   Passed    0.33 sec
    Start 22: gui_upgrade
3/4 Test #22: gui_upgrade ......................***Failed    0.92 sec
    Start 23: gui_image_download
4/4 Test #23: gui_image_download ...............   Passed    0.25 sec
Errors while running CTest

50% tests passed, 2 tests failed out of 4

The following tests FAILED:
         19 - gui_smoke (Failed)
         22 - gui_upgrade (Failed)
```

### 代表性完整 ASan 堆栈

以下为 `gui_smoke` 独立复现的完整读栈、释放栈和分配栈。地址每次运行不同，但文件、行号和调用链稳定一致。

```text
==4052733==ERROR: AddressSanitizer: heap-use-after-free on address 0x6e2c8d007680
READ of size 8 at 0x6e2c8d007680 thread T0
    #0 zzrk::gui::MainWorkspaceController::handleScanFinished()
       apps/gui/MainWorkspaceController.cpp:836
    #1 QtPrivate::FunctorCall<... MainWorkspaceController::*()>::call(...)
       QtCore/qobjectdefs_impl.h:128
    #2 QtPrivate::FunctorCallBase::call_internal<...>(...)
       QtCore/qobjectdefs_impl.h:66
    #3 QtPrivate::FunctorCall<... MainWorkspaceController::*()>::call(...)
       QtCore/qobjectdefs_impl.h:127
    #4 QtPrivate::FunctionPointer<...>::call<...>(...)
       QtCore/qobjectdefs_impl.h:175
    #5 QtPrivate::QCallableObject<...>::impl(...)
       QtCore/qobjectdefs_impl.h:546
    #6 QtPrivate::QSlotObjectBase::call(QObject*, void**)
       qtbase/src/corelib/kernel/qobjectdefs_impl.h:462
    #7 doActivate<false>(QObject*, int, void**)
       qtbase/src/corelib/kernel/qobject.cpp:4372
    #8 QFutureWatcherBase::event(QEvent*)
       qtbase/src/corelib/thread/qfuturewatcher.cpp:393
    #9 QApplicationPrivate::notify_helper(QObject*, QEvent*)
       qtbase/src/widgets/kernel/qapplication.cpp:3276
    #10 QCoreApplication::notifyInternal2(QObject*, QEvent*)
        qtbase/src/corelib/kernel/qcoreapplication.cpp:1114
    #11 QCoreApplicationPrivate::sendPostedEvents(QObject*, int, QThreadData*)
        qtbase/src/corelib/kernel/qcoreapplication.cpp:1914
    #12 postEventSourceDispatch
        qtbase/src/corelib/kernel/qeventdispatcher_glib.cpp:248
    #13 libglib-2.0.so.0
    #14 libglib-2.0.so.0
    #15 g_main_context_iteration
    #16 QEventDispatcherGlib::processEvents(...)
        qtbase/src/corelib/kernel/qeventdispatcher_glib.cpp:402
    #17 QEventLoop::exec(...)
        qtbase/src/corelib/kernel/qeventloop.cpp:198
    #18 QCoreApplication::exec()
        qtbase/src/corelib/kernel/qcoreapplication.cpp:1457
    #19 main
        apps/gui/main.cpp:141
    #20 __libc_start_call_main
    #21 __libc_start_main_impl
    #22 _start

0x6e2c8d007680 is located 0 bytes inside of 2176-byte region
[0x6e2c8d007680,0x6e2c8d007f00)
freed by thread T0 here:
    #0 operator delete(void*, unsigned long)
       asan_new_delete.cpp:190
    #1 zzrk::gui::MainWorkspaceController::~MainWorkspaceController()
       apps/gui/MainWorkspaceController.cpp:520
    #2 std::default_delete<QObject>::operator()(QObject*) const
       bits/unique_ptr.h:92
    #3 std::__uniq_ptr_impl<QObject, ...>::reset(QObject*)
       bits/unique_ptr.h:204
    #4 std::unique_ptr<QObject, ...>::reset(QObject*)
       bits/unique_ptr.h:511
    #5 ZzPureTools::ZzPageInstancePrivate::prepareForDestruction()
       thirdParty/ZzPureTools/ZzPureTools/widgets/src/private/
       ZzPageInstancePrivate.cpp:80
    #6 ZzPureTools::ZzPageInstance::prepareForDestruction()
       thirdParty/ZzPureTools/ZzPureTools/widgets/src/ZzPageInstance.cpp:115
    #7 ZzPureTools::ZzPageHostPrivate::~ZzPageHostPrivate()
       thirdParty/ZzPureTools/ZzPureTools/widgets/src/private/
       ZzPageHostPrivate.cpp:112
    #8 std::default_delete<ZzPureTools::ZzPageHostPrivate>::operator()(...)
       bits/unique_ptr.h:92
    #9 std::__uniq_ptr_impl<ZzPureTools::ZzPageHostPrivate, ...>::reset(...)
       bits/unique_ptr.h:204
    #10 std::unique_ptr<ZzPureTools::ZzPageHostPrivate, ...>::reset(...)
        bits/unique_ptr.h:511
    #11 ZzPureTools::ZzPageHost::~ZzPageHost()
        thirdParty/ZzPureTools/ZzPureTools/widgets/src/ZzPageHost.cpp:17
    #12 ZzPureTools::ZzPageHost::~ZzPageHost()
        thirdParty/ZzPureTools/ZzPureTools/widgets/src/ZzPageHost.cpp:18
    #13 QObjectPrivate::deleteChildren()
        qtbase/src/corelib/kernel/qobject.cpp:2279

previously allocated by thread T0 here:
    #0 operator new(unsigned long)
       asan_new_delete.cpp:109
    #1 std::make_unique<zzrk::gui::MainWorkspaceController>(...)
       bits/unique_ptr.h:1084
    #2 zzrk::gui::GuiWindowShell::createInitialDownloadPage(QWidget*)
       apps/gui/GuiWindowShell.cpp:330
    #3 zzrk::gui::buildGuiApplication(...)::{lambda(QWidget*)#1}::operator()(...)
       apps/gui/GuiApplicationBootstrap.cpp:68
    #4 std::__invoke_impl<...>(...)
       bits/invoke.h:63
    #5 std::__invoke_r<...>(...)
       bits/invoke.h:118
    #6 std::_Function_handler<...>::_M_invoke(...)
       bits/std_function.h:293
    #7 std::function<...>::operator()(QWidget*) const
       bits/std_function.h:593
    #8 ZzPureTools::ZzPageHostPrivate::createPage(...)
       thirdParty/ZzPureTools/ZzPureTools/widgets/src/private/
       ZzPageHostPrivate.cpp:279
    #9 ZzPureTools::ZzPageHostPrivate::activate(...)
       thirdParty/ZzPureTools/ZzPureTools/widgets/src/private/
       ZzPageHostPrivate.cpp:155
    #10 ZzPureTools::ZzPageHost::activate(...)
        thirdParty/ZzPureTools/ZzPureTools/widgets/src/ZzPageHost.cpp:23
    #11 ZzPureTools::ZzNavigationControllerPrivate::navigate(...)
        thirdParty/ZzPureTools/ZzPureTools/widgets/src/private/
        ZzNavigationControllerPrivate.cpp:151
    #12 ZzPureTools::ZzNavigationController::navigate(...)
        thirdParty/ZzPureTools/ZzPureTools/widgets/src/
        ZzNavigationController.cpp:30
    #13 ZzPureTools::ZzApplicationWindowPrivate::initialize(...)
        thirdParty/ZzPureTools/ZzPureTools/widgets/src/private/
        ZzApplicationWindowPrivate.cpp:242
    #14 ZzPureTools::ZzApplicationWindow::initialize(...)
        thirdParty/ZzPureTools/ZzPureTools/widgets/src/
        ZzApplicationWindow.cpp:61
    #15 ZzPureTools::ZzApplicationWindow::create(...)
        thirdParty/ZzPureTools/ZzPureTools/widgets/src/
        ZzApplicationWindow.cpp:40
    #16 ZzPureTools::ZzApplicationBuilderPrivate::build(...)
        thirdParty/ZzPureTools/ZzPureTools/widgets/src/private/
        ZzApplicationBuilderPrivate.cpp:398
    #17 ZzPureTools::ZzApplicationBuilder::build(...)
        thirdParty/ZzPureTools/ZzPureTools/widgets/src/
        ZzApplicationBuilder.cpp:90
    #18 zzrk::gui::buildGuiApplication(...)
        apps/gui/GuiApplicationBootstrap.cpp:118
    #19 main
        apps/gui/main.cpp:86
    #20 __libc_start_call_main
    #21 __libc_start_main_impl
    #22 _start

SUMMARY: AddressSanitizer: heap-use-after-free
apps/gui/MainWorkspaceController.cpp:836 in
zzrk::gui::MainWorkspaceController::handleScanFinished()
```

`gui_upgrade` 的独立复现具有相同读栈和释放栈，区别仅是测试入口与分配入口：读栈 `#19` 为 `tests/gui_upgrade_test.cpp:776`，分配栈经过 `tests/GuiTestHarness.cpp:36` 和 `tests/gui_upgrade_test.cpp:418`。CTest 原始输出在诊断时同时保存于 `build-asan/Testing/Temporary/LastTest.log`。

### 事件来源和析构顺序证据

1. `refreshDevices()` 用 `QtConcurrent::run` 设置 `m_scanWatcher`；后台 future 完成后，Qt 在 `QFutureWatcherBasePrivate::postCallOutEvent()` 中向 watcher 投递 `QEvent::FutureCallOut`。
2. Qt 6.11.1 的 `QFutureWatcherBase::event()` 在 `qfuturewatcher.cpp:388-395` 消费该事件；`sendCallOutEvent()` 对 `Finished` 同步发出 watcher 的 `finished()` 信号。
3. `MainWorkspaceController` 构造函数把 watcher 的 `finished()` 直接连接到 `handleScanFinished()`。该函数先读取已经完成的 watcher 结果，再在 `MainWorkspaceController.cpp:835` 发出 `scanCompleted()`，最后在第 836 行调用 `processPendingDeviceRefresh()`。
4. `gui_smoke` 的 `scanCompleted` 接收器在第二次扫描时调用 `application.exit(EXIT_SUCCESS)`；`gui_upgrade` 的接收器在最终扫描时调用同一路径。
5. Qt 6.11.1 的 `QCoreApplication::exit()` 在 `qcoreapplication.cpp:1510-1529` 先同步发出 `aboutToQuit()`，再让事件循环退出。它不是等当前 watcher 事件返回后才发信号。
6. `ZzPureApplicationPrivate` 在构造时以 `Qt::DirectConnection` 把 `aboutToQuit()` 连接到 `beginShutdown()`；因此 `exit()` 在 `handleScanFinished()` 的 `scanCompleted()` 发射栈内同步进入关闭流程。
7. `beginShutdown()` 的 `windows.clear()` 同步销毁唯一 `ZzApplicationWindow`。窗口 QObject 子树销毁 `ZzPageHost`；`ZzPageHostPrivate::~ZzPageHostPrivate()` 调用页面实例的 `prepareForDestruction()`；该函数先删除 view，再 `presenter_.reset()`，因此删除作为 presenter 的 `MainWorkspaceController`。`GuiWindowShell` 只用 `QPointer` 观察该 controller，不拥有它。
8. controller 析构调用 `shutdown()`，它断开 watcher 信号并等待所有 future 完成，但这时当前 watcher 的 `Finished` callout 已经在派发，`handleScanFinished()` 也已经位于调用栈上。析构完成返回到原槽函数后，第 836 行再次解引用已释放的 `this`，ASan 报错。

### 与仓库已有事件清理的对比

- `GuiWindowShell.cpp:101` 的 `removePostedEvents(snapshot.body, QEvent::DeferredDelete)` 只用于工作区装配失败回滚，目标对象和事件类型均与本问题无关。
- Qt 自己在 `QFutureWatcherBasePrivate::callOutInterfaceDisconnected()` 中执行 `removePostedEvents(watcher, QEvent::FutureCallOut)`，可移除尚未派发的 future callout，但无法撤销正在执行的 watcher 事件或已进入的 `handleScanFinished()`。
- 测试和 ZzPureTools 中其他 `sendPostedEvents(..., QEvent::DeferredDelete/MetaCall)` 都是显式冲刷尚未派发事件，没有处理“信号回调同步销毁当前槽接收者”的重入场景。

### 根因假设

当前单一根因假设为：`scanCompleted()` 是同步信号，其测试/程序接收器调用 `QCoreApplication::exit()`；Qt 同步发出 `aboutToQuit()`，ZzPureApplication 又以 DirectConnection 同步销毁窗口和页面 presenter，导致 `MainWorkspaceController` 在自己的 `handleScanFinished()` 尚未返回时被释放。随后第 836 行继续使用 `this`，形成确定性的重入式 UAF。

证据不支持以下替代解释：

- 不是“controller 已销毁后又收到一个旧的 queued `finished` 信号”：ASan 读栈显示当前有效的 `QFutureCallOut` 正在触发 `finished()`，释放发生在该槽执行期间。
- 不是“watcher QObject 已销毁后仍消费事件”：当前 watcher 是 controller 成员，事件进入时 controller/watcher 仍然存活；释放后访问点在 controller 槽的尾部，而不是 `QFutureWatcherBase::event()` 对 watcher 私有数据的后续访问。
- 不是“后台 future 在 controller 释放后访问 controller”：扫描 lambda 只捕获 backend，且到 `Finished` callout 时 future 已完成；ASan 的读写双方均在 GUI 主线程 T0。

受沙箱 `ptrace: Operation not permitted` 限制，无法用 GDB 断点额外打印动态调用栈；上述判断由 4/4 稳定复现、ASan 分配/释放/读取栈以及本机 Qt 6.11.1 对应源码共同支持。

## 修复和回归验证

修复将 `processPendingDeviceRefresh()` 移到各个终态信号之前，使同步关闭接收器不会在槽函数尾部继续访问已析构的 controller。分区表终态的两个信号之间增加 `QPointer<MainWorkspaceController>` 守卫。`tests/gui_framework_test.cpp` 新增 DirectConnection 同步关闭夹具：扫描完成信号直接触发 `beginShutdown()`，并断言 controller 和窗口均被销毁；修复前该夹具在 ASan 下复现同一 UAF，修复后通过。

最终 Linux 构建使用 Qt 6.11.1 GCC、仓库 libusb 和已缓存的 fmt 12.1.0 源码：

```text
cmake -S . -B build-debug   -G Ninja -DCMAKE_BUILD_TYPE=Debug   -DCMAKE_PREFIX_PATH=/home/zz/Qt/6.11.1/gcc_64 -DZZRK_BUILD_GUI=ON -DZZRK_BUILD_CLI=ON -DZZRK_BUILD_TRANSPORT=ON -DZZRK_BUILD_TESTS=ON -DZZRK_LIBUSB_ROOT=$PWD/thirdParty/libusb -DZZRK_BUNDLE_RUNTIME=ON -DZZRK_WARNINGS_AS_ERRORS=ON -DZZRK_ENABLE_SANITIZERS=OFF -DFETCHCONTENT_SOURCE_DIR_FMT=$PWD/build/linux-debug/_deps/fmt-src
cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/home/zz/Qt/6.11.1/gcc_64 -DZZRK_BUILD_GUI=ON -DZZRK_BUILD_CLI=ON -DZZRK_BUILD_TRANSPORT=ON -DZZRK_BUILD_TESTS=ON -DZZRK_LIBUSB_ROOT=$PWD/thirdParty/libusb -DZZRK_BUNDLE_RUNTIME=ON -DZZRK_WARNINGS_AS_ERRORS=ON -DZZRK_ENABLE_SANITIZERS=OFF -DFETCHCONTENT_SOURCE_DIR_FMT=$PWD/build/linux-debug/_deps/fmt-src
cmake -S . -B build-asan    -G Ninja -DCMAKE_BUILD_TYPE=Debug   -DCMAKE_PREFIX_PATH=/home/zz/Qt/6.11.1/gcc_64 -DZZRK_BUILD_GUI=ON -DZZRK_BUILD_CLI=ON -DZZRK_BUILD_TRANSPORT=ON -DZZRK_BUILD_TESTS=ON -DZZRK_LIBUSB_ROOT=$PWD/thirdParty/libusb -DZZRK_BUNDLE_RUNTIME=ON -DZZRK_WARNINGS_AS_ERRORS=ON -DZZRK_ENABLE_SANITIZERS=ON  -DFETCHCONTENT_SOURCE_DIR_FMT=$PWD/build/linux-debug/_deps/fmt-src
cmake --build build-debug --parallel 4
cmake --build build-release --parallel 4
cmake --build build-asan --parallel 4
```

三套配置均构建成功。`ctest --test-dir build-debug --output-on-failure`、`build-release` 和 `build-asan` 均为 `100% tests passed, 0 tests failed out of 24`；ASan 重点集合 `gui_framework|gui_upgrade|gui_image_download|gui_smoke` 为 4/4 通过。

## 视觉验收

在临时 `Xvfb :100 -screen 0 1920x1080x24` 的真实 xcb 路径下运行：

```text
DISPLAY=:100 QT_QPA_PLATFORM=xcb ZZRK_GUI_SCREENSHOT_DIR=$PWD/build-debug/visual-final ./build-debug/tests/gui_framework_test
```

命令退出码为 0，生成以下构建产物（未写入源代码目录）：

```text
zzrk-gui-980x820-light.png       75135 bytes
zzrk-gui-minimum-800x700-light.png 73363 bytes
zzrk-gui-980x820-dark.png        75702 bytes
```

三张图均已人工检查：标题栏拖动区、主题/置顶/系统按钮、顶部四个固定标签、设备和分区工具栏、设备表格、选择/清除/下载操作、完整计划与当前分区双进度条、错误提示和底部状态栏均可见且没有重叠或截断；800×700 最小窗口仍可读，深色主题对比度和控件边界正常。

## 跨平台检查边界

本 Linux 主机只有 Qt 6.11.1 `gcc_64`，没有 MSVC、MinGW 或 macOS 工具链，因此本次无法声称 Windows/MSVC、Windows/MinGW 或 macOS 的真实编译已执行。文档已补充对应的可复现配置和 `gui_framework|gui_smoke` 命令；静态检查确认：

- `.github/workflows/cross-platform-apps.yml` 使用 Windows 2022/MSVC 与 macOS 14/clang、递归 submodule、Qt 6.11.1、构建/测试/打包及安装后 offscreen smoke。
- `apps/gui/CMakeLists.txt` 设置 `WIN32_EXECUTABLE`、`MACOSX_BUNDLE`；`cmake/InstallAndPackage.cmake` 包含 Windows/macOS deploy、平台插件和 macOS `@rpath`/签名路径。
- macOS 验收命令检查 `Contents/PlugIns/platforms/libqcocoa.dylib`、GUI/CLI 的 `otool -L`，要求 libusb 使用 `@rpath`；Windows 文档覆盖无边框按钮、DPI、最大化边界和 WinUSB 驱动限制。

## 最终清理

仓库级检查结果：

```text
rg -n "class MainWindow|QApplication application|new QTabWidget|QApplication::setStyle" apps/gui tests
```

没有命中运行时实现；`test ! -e apps/gui/MainWindow.hpp && test ! -e apps/gui/MainWindow.cpp` 通过。`git diff --check` 通过。截图仅保留在 `build-debug/visual` 和 `build-debug/visual-final` 构建目录，不纳入提交。
