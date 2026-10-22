# SPDX-License-Identifier: MIT
include_guard(GLOBAL)

option(ZZRK_ENABLE_WINDOWS_DRIVER_MANAGEMENT
    "Build and package the Windows WinUSB driver installer" ${WIN32})
if(NOT WIN32 OR NOT ZZRK_ENABLE_WINDOWS_DRIVER_MANAGEMENT)
    return()
endif()
if(NOT CMAKE_HOST_WIN32)
    message(FATAL_ERROR "libwdi packaging requires a native Windows build host (VS 2022+ with v143).")
endif()

# 上游的 DLL 工程只有 Win32/x64；不能把 ARM64 应用与 x64 DLL 混装。
if(CMAKE_GENERATOR_PLATFORM MATCHES "^[Aa][Rr][Mm]" OR
   CMAKE_SYSTEM_PROCESSOR MATCHES "^(ARM64|arm64|aarch64)$" OR
   CMAKE_CXX_COMPILER_ARCHITECTURE_ID MATCHES "^[Aa][Rr][Mm]")
    message(FATAL_ERROR "libwdi v1.5.1 packaging currently supports Win32 and x64 only.")
endif()
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(_zzrk_wdi_platform x64)
elseif(CMAKE_SIZEOF_VOID_P EQUAL 4)
    set(_zzrk_wdi_platform Win32)
else()
    message(FATAL_ERROR "Cannot determine the application architecture for libwdi.")
endif()

find_program(ZZRK_LIBWDI_POWERSHELL NAMES pwsh powershell REQUIRED)
set(ZZRK_LIBWDI_MSBUILD "${CMAKE_VS_MSBUILD_COMMAND}" CACHE FILEPATH
    "MSBuild with MSVC v143; empty means discover with vswhere at build time")

include(FetchContent)
FetchContent_Declare(zzrk_libwdi_source
    URL https://codeload.github.com/pbatard/libwdi/tar.gz/9b23b82a2dd1cbffc16d46c212f92c6bf8c0c602
    URL_HASH SHA256=a65be68e20f0db155c98fa57f7748088165b2a8d0c19c2968e01c9ea5b2dfe4e
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(zzrk_libwdi_source)
set(ZZRK_LIBWDI_INCLUDE_DIR "${zzrk_libwdi_source_SOURCE_DIR}/libwdi")

# 固定使用 Release 的 libwdi/installer，避免发布包依赖 MSVC Debug CRT。
# 每个应用配置使用独立副本，VS 多配置并行构建也不会写同一 embedded.h。
set(_zzrk_wdi_root "${CMAKE_BINARY_DIR}/_deps/libwdi-build/$<CONFIG>")
set(ZZRK_LIBWDI_DLL "${_zzrk_wdi_root}/output/libwdi.dll")
set(_zzrk_wdi_script "${CMAKE_CURRENT_LIST_DIR}/../scripts/build-libwdi.ps1")
add_custom_command(
    OUTPUT "${ZZRK_LIBWDI_DLL}"
    COMMAND "${ZZRK_LIBWDI_POWERSHELL}" -NoProfile -NonInteractive
        -ExecutionPolicy Bypass -File "${_zzrk_wdi_script}"
        -SourceDir "${zzrk_libwdi_source_SOURCE_DIR}"
        -BuildDir "${_zzrk_wdi_root}" -Platform "${_zzrk_wdi_platform}"
        -MSBuildPath "${ZZRK_LIBWDI_MSBUILD}"
    DEPENDS "${_zzrk_wdi_script}" "${CMAKE_CURRENT_LIST_DIR}/../scripts/patch-libwdi.ps1"
        "${ZZRK_LIBWDI_INCLUDE_DIR}/libwdi.h"
    BYPRODUCTS "${_zzrk_wdi_root}/output/WDF-License.rtf"
        "${_zzrk_wdi_root}/output/WDF-redist.txt"
        "${_zzrk_wdi_root}/output/BUILD-INFO.txt"
    COMMENT "Building pinned libwdi v1.5.1 and embedding WinUSB resources"
    VERBATIM USES_TERMINAL
)
add_custom_target(zzrk_libwdi ALL DEPENDS "${ZZRK_LIBWDI_DLL}")

# helper 按应用目录绝对路径加载 DLL，因此安装位置固定为应用根目录。
install(FILES "${ZZRK_LIBWDI_DLL}" DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(FILES "${CMAKE_CURRENT_LIST_DIR}/../packaging/windows/README.md"
    DESTINATION . RENAME README.zh-CN.md)
install(FILES
    "${zzrk_libwdi_source_SOURCE_DIR}/COPYING"
    "${zzrk_libwdi_source_SOURCE_DIR}/COPYING-LGPL"
    "${CMAKE_CURRENT_LIST_DIR}/../thirdParty/libwdi/README.md"
    "${_zzrk_wdi_root}/output/WDF-License.rtf"
    "${_zzrk_wdi_root}/output/WDF-redist.txt"
    "${_zzrk_wdi_root}/output/BUILD-INFO.txt"
    DESTINATION share/licenses/libwdi
)
# 附完整对应源码和构建配方，允许替换 LGPL 动态库并复现本项目的资源配置。
install(DIRECTORY "${zzrk_libwdi_source_SOURCE_DIR}/"
    DESTINATION share/licenses/libwdi/source PATTERN ".git" EXCLUDE)
install(FILES "${_zzrk_wdi_script}" "${CMAKE_CURRENT_LIST_DIR}/../scripts/patch-libwdi.ps1" "${CMAKE_CURRENT_LIST_FILE}"
    DESTINATION share/licenses/libwdi/rebuild)
