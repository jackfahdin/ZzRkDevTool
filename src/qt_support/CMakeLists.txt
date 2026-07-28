# SPDX-License-Identifier: MIT

add_library(
    zzrk_qt_support STATIC
    src/FirmwareMd5Verifier.cpp
)
add_library(zzrk::qt_support ALIAS zzrk_qt_support)

target_include_directories(
    zzrk_qt_support
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_link_libraries(
    zzrk_qt_support
    PUBLIC
        zzrk::workflow
        Qt6::Core
    PRIVATE zzrk_project_warnings
)
