# SPDX-License-Identifier: MIT

find_package(Threads REQUIRED)

add_library(
    zzrk_transport STATIC
    LibusbBulkTransport.cpp
    LibusbDeviceScanner.cpp
    LibusbHotplugMonitor.cpp
)
add_library(zzrk::transport ALIAS zzrk_transport)

target_include_directories(
    zzrk_transport
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
)
target_link_libraries(
    zzrk_transport
    PUBLIC zzrk::core zzrk::session zzrk_project_options
    PRIVATE LibUSB1::LibUSB1 Threads::Threads zzrk_project_warnings
)
if(WIN32)
    target_compile_definitions(zzrk_transport PRIVATE NOMINMAX)
endif()
