# SPDX-License-Identifier: MIT

add_library(
    zzrk_session STATIC
    src/ReadChipInfo.cpp
    src/ReadFlashInfo.cpp
    src/RockusbSession.cpp
)
add_library(zzrk::session ALIAS zzrk_session)

target_include_directories(
    zzrk_session
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
)
target_link_libraries(
    zzrk_session
    PUBLIC zzrk::protocol zzrk_project_options
    PRIVATE zzrk_project_warnings
)
