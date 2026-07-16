# SPDX-License-Identifier: MIT

find_package(Qt6 6.11 REQUIRED COMPONENTS Concurrent)

qt_add_executable(
    ZzRkDevTool
    MANUAL_FINALIZATION
    main.cpp
    MainWindow.cpp
    MainWindow.hpp
)

target_link_libraries(
    ZzRkDevTool
    PRIVATE
        Qt6::Core
        Qt6::Concurrent
        Qt6::Gui
        Qt6::Widgets
        zzrk::transport
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
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
)
qt_finalize_executable(ZzRkDevTool)
