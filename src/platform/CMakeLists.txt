# SPDX-License-Identifier: MIT

set(ZZRK_PLATFORM_SOURCE)
if(WIN32)
    set(ZZRK_PLATFORM_SOURCE windows/WindowsPlatformServices.cpp)
elseif(APPLE)
    set(ZZRK_PLATFORM_SOURCE macos/MacPlatformServices.cpp)
elseif(UNIX)
    set(ZZRK_PLATFORM_SOURCE linux/LinuxPlatformServices.cpp)
else()
    message(FATAL_ERROR "Unsupported host platform")
endif()

add_library(zzrk_platform STATIC ${ZZRK_PLATFORM_SOURCE})
add_library(zzrk::platform ALIAS zzrk_platform)

target_include_directories(
    zzrk_platform
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
)
target_link_libraries(
    zzrk_platform
    PUBLIC zzrk_project_options
    PRIVATE zzrk_project_warnings
)
