# SPDX-License-Identifier: MIT

add_library(zzrk_storage STATIC src/GptPartitionTable.cpp)
add_library(zzrk::storage ALIAS zzrk_storage)

target_include_directories(
    zzrk_storage
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
)
target_link_libraries(
    zzrk_storage
    PUBLIC zzrk::core zzrk::session zzrk_project_options
    PRIVATE zzrk_project_warnings
)
