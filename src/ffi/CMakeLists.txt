# SPDX-License-Identifier: MIT

option(ZZRK_FFI_SHARED "Build zzrk_ffi as a shared library" OFF)

if(ZZRK_FFI_SHARED)
    add_library(zzrk_ffi SHARED src/ZzrkFfi.cpp)
else()
    add_library(zzrk_ffi STATIC src/ZzrkFfi.cpp)
endif()
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

if(ZZRK_FFI_SHARED)
    set_target_properties(zzrk_ffi PROPERTIES POSITION_INDEPENDENT_CODE ON)
    target_compile_definitions(
        zzrk_ffi
        PRIVATE ZZRK_FFI_BUILD
        PUBLIC ZZRK_FFI_SHARED
    )
endif()
