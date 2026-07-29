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
set(_zzrk_bundle_runtime_default OFF)
if((CMAKE_SYSTEM_NAME STREQUAL "Linux" OR WIN32 OR APPLE) AND
   (ZZRK_BUILD_CLI OR ZZRK_BUILD_GUI))
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
    set(_zzrk_default_libusb_root)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set(_zzrk_default_libusb_root "${CMAKE_CURRENT_SOURCE_DIR}/thirdParty/libusb")
    endif()
    set(
        ZZRK_LIBUSB_ROOT
        "${_zzrk_default_libusb_root}"
        CACHE PATH
        "Root of a libusb installation"
    )
    find_package(LibUSB1 1.0.30 REQUIRED)

    get_filename_component(ZZRK_LIBUSB_LIBRARY_DIR "${LIBUSB1_LIBRARY}" DIRECTORY)
    if(UNIX AND NOT APPLE)
        list(APPEND CMAKE_BUILD_RPATH "${ZZRK_LIBUSB_LIBRARY_DIR}")
    endif()
endif()

add_subdirectory(src)

if(ZZRK_BUILD_CLI OR ZZRK_BUILD_GUI)
    find_package(Qt6 6.11 REQUIRED COMPONENTS Core)
    add_subdirectory(src/qt_support)
endif()

if(ZZRK_BUILD_CLI)
    add_subdirectory(apps/cli)
endif()

if(ZZRK_BUILD_GUI)
    find_package(Qt6 6.11 REQUIRED COMPONENTS Gui Widgets)
    add_subdirectory(apps/gui)
endif()

if(ZZRK_BUILD_TESTS)
    include(CTest)
    enable_testing()
    add_subdirectory(tests)
endif()

include(cmake/InstallAndPackage.cmake)
