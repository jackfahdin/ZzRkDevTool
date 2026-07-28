# SPDX-License-Identifier: MIT

add_library(
    zzrk_workflow STATIC
    src/FirmwareUpgradeWorkflow.cpp
    src/LibusbFirmwareUpgradeBackend.cpp
)
add_library(zzrk::workflow ALIAS zzrk_workflow)

target_include_directories(
    zzrk_workflow
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_link_libraries(
    zzrk_workflow
    PUBLIC
        zzrk::image
        zzrk::session
        zzrk::transport
    PRIVATE zzrk_project_warnings
)
