# SPDX-License-Identifier: MIT

find_package(Qt6 6.11 REQUIRED COMPONENTS Concurrent)

qt_add_library(
    zzrk_gui_support STATIC
    GuiOperationBackend.cpp
    GuiOperationBackend.hpp
    GuiApplicationBootstrap.cpp
    GuiApplicationBootstrap.hpp
    GuiWindowShell.cpp
    GuiWindowShell.hpp
    MainWorkspaceController.cpp
    MainWorkspaceController.hpp
    MainWorkspaceSettings.cpp
    MainWorkspaceViewModel.cpp
    MainWorkspaceViewModel.hpp
    WorkspaceUi.cpp
    WorkspaceUi.hpp
    FileSelectionDialog.cpp
    FileSelectionDialog.hpp
    GuiDialogs.cpp
    GuiDialogs.hpp
    GuiSettings.cpp
    GuiSettings.hpp
    GuiAppearanceSettings.cpp
    GuiAppearanceSettings.hpp
    GuiTranslations.cpp
    GuiTranslations.hpp
    SettingsLocationDialog.cpp
    SettingsLocationDialog.hpp
    pages/SettingsPage.cpp
    pages/SettingsPage.hpp
    pages/DevicePage.cpp
    pages/DevicePage.hpp
    pages/ImageDownloadPage.cpp
    pages/ImageDownloadPage.hpp
    pages/FullUpgradePage.cpp
    pages/FullUpgradePage.hpp
    pages/PartitionPage.cpp
    pages/PartitionPage.hpp
    UpgradeProgressTracker.cpp
    UpgradeProgressTracker.hpp
)

target_link_libraries(
    zzrk_gui_support
    PRIVATE
        Qt6::Concurrent
        Zz::WindowKit
        Zz::FluentUI
        zzrk::logging
        zzrk_project_warnings
    PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Zz::Core
        Zz::PureTools
        zzrk::transport
        zzrk::workflow
)
target_include_directories(zzrk_gui_support PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
set_target_properties(zzrk_gui_support PROPERTIES AUTOMOC ON)
add_library(zzrk::gui ALIAS zzrk_gui_support)
find_package(Qt6 REQUIRED COMPONENTS LinguistTools)
qt_add_lrelease(zzrk_gui_support TS_FILES translations/zzrkdevtool_en.ts
                translations/zzrkdevtool_zh_CN.ts
                QM_FILES_OUTPUT_VARIABLE gui_qm)
qt_add_resources(zzrk_gui_support gui_translations PREFIX /translations
                 BASE "${CMAKE_CURRENT_BINARY_DIR}" FILES ${gui_qm})

qt_add_executable(
    ZzRkDevTool
    MANUAL_FINALIZATION
    main.cpp
)

target_link_libraries(
    ZzRkDevTool
    PRIVATE
        Zz::WindowKit
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
