# SPDX-License-Identifier: MIT

add_library(
    zzrk_cli_support STATIC
    DeviceListPrinter.cpp
    ProbeSupport.cpp
)
target_include_directories(zzrk_cli_support PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(
    zzrk_cli_support
    PUBLIC zzrk::transport
    PRIVATE zzrk_project_warnings
)

add_executable(zzrkdevtool-cli main.cpp)
target_link_libraries(
    zzrkdevtool-cli
    PRIVATE
        zzrk_cli_support
        zzrk::logging
        zzrk::platform
        zzrk_project_warnings
)

set_target_properties(
    zzrkdevtool-cli
    PROPERTIES
        OUTPUT_NAME zzrkdevtool-cli
        BUILD_RPATH "${ZZRK_LIBUSB_LIBRARY_DIR}"
)
