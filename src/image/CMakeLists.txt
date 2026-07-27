# SPDX-License-Identifier: MIT

add_library(
    zzrk_image STATIC
    src/RkFirmwareImage.cpp
    src/RkFirmwareFlasher.cpp
)
add_library(zzrk::image ALIAS zzrk_image)

target_include_directories(
    zzrk_image
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
)
target_link_libraries(
    zzrk_image
    PUBLIC zzrk::core zzrk::session zzrk_project_options
    PRIVATE zzrk_project_warnings
)
