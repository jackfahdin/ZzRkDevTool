# SPDX-License-Identifier: MIT

add_library(
    zzrk_session STATIC
    src/ReadChipInfo.cpp
    src/ReadCapability.cpp
    src/ReadFlashId.cpp
    src/ReadFlashInfo.cpp
    src/ReadLba.cpp
    src/ResetDevice.cpp
    src/TestUnitReady.cpp
    src/WriteLba.cpp
    src/EraseLba.cpp
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
    PUBLIC zzrk::core zzrk::protocol zzrk_project_options
    PRIVATE zzrk_project_warnings
)
