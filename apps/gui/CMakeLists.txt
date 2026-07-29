# SPDX-License-Identifier: MIT

find_package(Qt6 6.11 REQUIRED COMPONENTS Concurrent)

qt_add_library(
    zzrk_gui_support STATIC
    GuiOperationBackend.cpp
    GuiOperationBackend.hpp
    MainWindow.cpp
    MainWindow.hpp
    UpgradeProgressTracker.cpp
    UpgradeProgressTracker.hpp
)

target_link_libraries(
    zzrk_gui_support
    PRIVATE
        Qt6::Concurrent
        zzrk::logging
        zzrk::qt_support
        zzrk_project_warnings
    PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        zzrk::transport
        zzrk::workflow
)
target_include_directories(zzrk_gui_support PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
set_target_properties(zzrk_gui_support PROPERTIES AUTOMOC ON)
add_library(zzrk::gui ALIAS zzrk_gui_support)

qt_add_executable(
    ZzRkDevTool
    MANUAL_FINALIZATION
    main.cpp
)

target_link_libraries(
    ZzRkDevTool
    PRIVATE
        zzrk::gui
        zzrk::logging
        zzrk_project_warnings
)

set_target_properties(
    ZzRkDevTool
    PROPERTIES
        OUTPUT_NAME ZzRkDevTool
        AUTOMOC ON
        BUILD_RPATH "${ZZRK_LIBUSB_LIBRARY_DIR}"
        WIN32_EXECUTABLE TRUE
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME ZzRkDevTool
        MACOSX_BUNDLE_GUI_IDENTIFIER io.github.zzrkdevtool
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
)
qt_finalize_executable(ZzRkDevTool)
