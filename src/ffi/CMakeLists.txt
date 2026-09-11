# SPDX-License-Identifier: MIT

add_library(
    zzrk_ffi STATIC
    src/ZzrkFfi.cpp
)
add_library(zzrk::ffi ALIAS zzrk_ffi)

target_include_directories(
    zzrk_ffi
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_link_libraries(
    zzrk_ffi
    PUBLIC zzrk::workflow
    PRIVATE zzrk_project_warnings
)
