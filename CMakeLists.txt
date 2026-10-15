# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.25)

project(
    ZzRkDevTool
    VERSION 0.1.0
    DESCRIPTION "Cross-platform Rockchip flashing tool"
    LANGUAGES CXX
)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

option(ZZRK_BUILD_GUI "Build the Qt Widgets application" ON)
option(ZZRK_BUILD_CLI "Build the command-line application" ON)
option(ZZRK_BUILD_TRANSPORT "Build the libusb transport" ON)
option(ZZRK_BUILD_TESTS "Build unit tests" ON)
option(ZZRK_WARNINGS_AS_ERRORS "Treat project warnings as errors" OFF)
option(ZZRK_ENABLE_SANITIZERS "Enable address and undefined sanitizers" OFF)
option(ZZRK_LINUX_PORTABLE "Use the relocatable Linux archive layout" OFF)
set(_zzrk_bundle_runtime_default OFF)
if((CMAKE_SYSTEM_NAME STREQUAL "Linux" OR WIN32 OR APPLE) AND
   ZZRK_BUILD_GUI)
    set(_zzrk_bundle_runtime_default ON)
endif()
option(
    ZZRK_BUNDLE_RUNTIME
    "Bundle Qt, its plugins, and non-system runtime libraries when installing"
    ${_zzrk_bundle_runtime_default}
)
set(
    ZZRK_MACOS_CODESIGN_IDENTITY
    ""
    CACHE STRING
    "Optional Developer ID Application identity used by macdeployqt"
)
unset(_zzrk_bundle_runtime_default)

if(ZZRK_LINUX_PORTABLE)
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
        message(FATAL_ERROR "ZZRK_LINUX_PORTABLE is only supported on Linux")
    endif()
    if(NOT ZZRK_BUNDLE_RUNTIME)
        message(FATAL_ERROR "The Linux portable package requires ZZRK_BUNDLE_RUNTIME=ON")
    endif()
endif()

set(_zzrk_transport_consumers)
if(ZZRK_BUILD_CLI)
    list(APPEND _zzrk_transport_consumers ZZRK_BUILD_CLI)
endif()
if(ZZRK_BUILD_GUI)
    list(APPEND _zzrk_transport_consumers ZZRK_BUILD_GUI)
endif()
if(_zzrk_transport_consumers AND NOT ZZRK_BUILD_TRANSPORT)
    list(JOIN _zzrk_transport_consumers ", " _zzrk_transport_consumer_list)
    message(
        FATAL_ERROR
        "${_zzrk_transport_consumer_list} require ZZRK_BUILD_TRANSPORT=ON. "
        "Enable transport or disable the application targets."
    )
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Windows 和 Linux 便携包将程序放在根目录；Linux DEB 使用 GNU 标准目录，
# macOS 的 GUI 和 CLI 则由安装脚本放入同一个 .app。
# 缓存值允许用户覆盖；Windows 预设也显式设置，便于更新已有构建目录。
# 只初始化未定义的值，避免把命令行传入的相对路径转换为源码绝对路径。
if(WIN32 AND NOT DEFINED CMAKE_INSTALL_BINDIR)
    set(CMAKE_INSTALL_BINDIR "." CACHE PATH "Executable installation directory")
endif()
# 便携布局固定使用相对目录，不受发行版 lib64/multiarch 默认值影响。
# 与 DEB 使用独立预设和构建目录，切换布局时不会相互覆盖。
if(ZZRK_LINUX_PORTABLE)
    set(CMAKE_INSTALL_BINDIR ".")
    set(CMAKE_INSTALL_LIBDIR "lib")
endif()
include(GNUInstallDirs)

add_library(zzrk_project_options INTERFACE)
target_compile_features(zzrk_project_options INTERFACE cxx_std_20)
target_compile_definitions(
    zzrk_project_options
    INTERFACE ZZRK_VERSION="${PROJECT_VERSION}"
)

if(ZZRK_ENABLE_SANITIZERS)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(
            zzrk_project_options
            INTERFACE -fsanitize=address,undefined -fno-omit-frame-pointer
        )
        target_link_options(
            zzrk_project_options
            INTERFACE -fsanitize=address,undefined
        )
    else()
        message(WARNING "Sanitizers are not configured for this compiler")
    endif()
endif()

add_library(zzrk_project_warnings INTERFACE)
if(MSVC)
    target_compile_options(zzrk_project_warnings INTERFACE /W4 /permissive-)
    if(ZZRK_WARNINGS_AS_ERRORS)
        target_compile_options(zzrk_project_warnings INTERFACE /WX)
    endif()
else()
    target_compile_options(
        zzrk_project_warnings
        INTERFACE
            -Wall
            -Wextra
            -Wpedantic
            -Wconversion
            -Wsign-conversion
    )
    if(ZZRK_WARNINGS_AS_ERRORS)
        target_compile_options(zzrk_project_warnings INTERFACE -Werror)
    endif()
endif()

if(ZZRK_BUILD_CLI OR ZZRK_BUILD_GUI)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/thirdParty/spdlog/CMakeLists.txt")
        message(
            FATAL_ERROR
            "spdlog is required by the application targets. "
            "Run 'git submodule update --init thirdParty/spdlog' before "
            "configuring."
        )
    endif()

    set(SPDLOG_BUILD_ALL OFF CACHE BOOL "" FORCE)
    set(SPDLOG_BUILD_SHARED OFF CACHE BOOL "" FORCE)
    set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
    set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(SPDLOG_BUILD_BENCH OFF CACHE BOOL "" FORCE)
    set(SPDLOG_BUILD_WARNINGS OFF CACHE BOOL "" FORCE)
    set(SPDLOG_INSTALL OFF CACHE BOOL "" FORCE)
    set(SPDLOG_FMT_EXTERNAL OFF CACHE BOOL "" FORCE)
    set(SPDLOG_SYSTEM_INCLUDES ON CACHE BOOL "" FORCE)

    if(DEFINED BUILD_SHARED_LIBS)
        set(_zzrk_saved_build_shared_libs "${BUILD_SHARED_LIBS}")
        set(_zzrk_build_shared_libs_was_defined TRUE)
    endif()
    set(BUILD_SHARED_LIBS OFF)
    add_subdirectory(thirdParty/spdlog EXCLUDE_FROM_ALL SYSTEM)
    if(_zzrk_build_shared_libs_was_defined)
        set(BUILD_SHARED_LIBS "${_zzrk_saved_build_shared_libs}")
    else()
        unset(BUILD_SHARED_LIBS)
    endif()
    unset(_zzrk_build_shared_libs_was_defined)
    unset(_zzrk_saved_build_shared_libs)
endif()

if(ZZRK_BUILD_TRANSPORT)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/thirdParty/Zzlibusb/CMakeLists.txt")
        message(FATAL_ERROR "Zzlibusb is required. Run git submodule update --init --recursive")
    endif()

    # 只在子目录作用域内设置静态构建，避免影响 FFI 或 UI 框架的链接方式。
    function(zzrk_add_libusb)
        set(BUILD_SHARED_LIBS OFF)
        set(ZZLIBUSB_INSTALL OFF)
        set(LIBUSB_ENABLE_WINDOWS_HOTPLUG ON)
        add_subdirectory(thirdParty/Zzlibusb EXCLUDE_FROM_ALL SYSTEM)
    endfunction()
    zzrk_add_libusb()

    if(NOT TARGET LibUSB1::LibUSB1)
        add_library(LibUSB1::LibUSB1 ALIAS libusb)
    endif()

    # 模拟 USB 函数的测试只需要头文件，不能链接真实 libusb 导致符号冲突。
    add_library(zzrk_libusb_headers INTERFACE)
    target_include_directories(zzrk_libusb_headers SYSTEM INTERFACE
        "$<TARGET_PROPERTY:libusb,INTERFACE_INCLUDE_DIRECTORIES>"
    )
    # libusb.h 在 Windows 会包含 windows.h；模拟测试同样需要禁用 min/max 宏。
    target_compile_definitions(zzrk_libusb_headers INTERFACE
        "$<$<PLATFORM_ID:Windows>:NOMINMAX>"
    )
endif()

add_subdirectory(src)

if(ZZRK_BUILD_CLI)
    add_subdirectory(apps/cli)
endif()

if(ZZRK_BUILD_GUI)
    include(cmake/ZzPureTools.cmake)
    zzrk_capture_puretools_scope_snapshot(_zzrk_puretools_scope)
    zzrk_add_puretools()
    include("${PROJECT_SOURCE_DIR}/tests/zzpuretools_cmake_scope.cmake")
    zzrk_clear_puretools_scope_snapshot(_zzrk_puretools_scope)

    add_subdirectory(apps/gui_qt)
endif()

if(ZZRK_BUILD_TESTS)
    include(CTest)
    enable_testing()
    add_subdirectory(tests)
endif()

include(cmake/InstallAndPackage.cmake)
