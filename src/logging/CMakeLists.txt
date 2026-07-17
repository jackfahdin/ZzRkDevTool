# SPDX-License-Identifier: MIT

add_library(zzrk_logging STATIC ConsoleLogging.cpp)
add_library(zzrk::logging ALIAS zzrk_logging)

target_include_directories(
    zzrk_logging
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
)
target_link_libraries(
    zzrk_logging
    PUBLIC zzrk_project_options
    PRIVATE spdlog::spdlog zzrk_project_warnings
)
