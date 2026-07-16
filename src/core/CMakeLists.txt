# SPDX-License-Identifier: MIT

add_library(zzrk_core INTERFACE)
add_library(zzrk::core ALIAS zzrk_core)

target_include_directories(
    zzrk_core
    INTERFACE
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
)
target_link_libraries(zzrk_core INTERFACE zzrk_project_options)
