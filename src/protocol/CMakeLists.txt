# SPDX-License-Identifier: MIT

add_library(zzrk_protocol STATIC src/RockusbCodec.cpp)
add_library(zzrk::protocol ALIAS zzrk_protocol)

target_include_directories(
    zzrk_protocol
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
)
target_link_libraries(
    zzrk_protocol
    PUBLIC zzrk_project_options
    PRIVATE zzrk_project_warnings
)
