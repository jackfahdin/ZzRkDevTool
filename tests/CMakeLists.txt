# SPDX-License-Identifier: MIT

add_executable(protocol_codec_test protocol_codec_test.cpp)
target_link_libraries(
    protocol_codec_test
    PRIVATE zzrk::protocol zzrk_project_warnings
)
add_test(NAME protocol_codec COMMAND protocol_codec_test)

add_executable(storage_units_test storage_units_test.cpp)
target_link_libraries(
    storage_units_test
    PRIVATE zzrk::core zzrk_project_warnings
)
add_test(NAME storage_units COMMAND storage_units_test)

add_executable(rockusb_session_test rockusb_session_test.cpp)
target_include_directories(
    rockusb_session_test
    PRIVATE "${PROJECT_SOURCE_DIR}/src/session/src"
)
target_link_libraries(
    rockusb_session_test
    PRIVATE zzrk::session zzrk_project_warnings
)
add_test(NAME rockusb_session COMMAND rockusb_session_test)

add_executable(read_chip_info_test read_chip_info_test.cpp)
target_link_libraries(
    read_chip_info_test
    PRIVATE zzrk::session zzrk_project_warnings
)
add_test(NAME read_chip_info COMMAND read_chip_info_test)

add_executable(read_flash_id_test read_flash_id_test.cpp)
target_link_libraries(
    read_flash_id_test
    PRIVATE zzrk::session zzrk_project_warnings
)
add_test(NAME read_flash_id COMMAND read_flash_id_test)

add_executable(read_flash_info_test read_flash_info_test.cpp)
target_link_libraries(
    read_flash_info_test
    PRIVATE zzrk::session zzrk_project_warnings
)
add_test(NAME read_flash_info COMMAND read_flash_info_test)

add_executable(rk_maskrom_loader_test rk_maskrom_loader_test.cpp)
target_link_libraries(
    rk_maskrom_loader_test
    PRIVATE zzrk::image zzrk_project_warnings
)
add_test(NAME rk_maskrom_loader COMMAND rk_maskrom_loader_test)

add_executable(standalone_image_test standalone_image_test.cpp)
target_link_libraries(
    standalone_image_test
    PRIVATE zzrk::image zzrk_project_warnings
)
add_test(NAME standalone_image COMMAND standalone_image_test)

if(TARGET zzrk_logging)
    add_executable(logging_test logging_test.cpp)
    target_link_libraries(
        logging_test
        PRIVATE zzrk::logging zzrk_project_warnings
    )
    add_test(NAME logging COMMAND logging_test)
endif()

if(TARGET zzrk_transport)
    find_package(Threads REQUIRED)

    add_executable(
        usb_device_scanner_test
        usb_device_scanner_test.cpp
        "${PROJECT_SOURCE_DIR}/src/transport/LibusbDeviceScanner.cpp"
    )
    target_include_directories(
        usb_device_scanner_test
        PRIVATE
            "${PROJECT_SOURCE_DIR}/src/transport/include"
    )
    target_include_directories(
        usb_device_scanner_test
        SYSTEM PRIVATE "${LIBUSB1_INCLUDE_DIR}"
    )
    target_link_libraries(
        usb_device_scanner_test
        PRIVATE zzrk_project_options zzrk_project_warnings
    )
    add_test(NAME usb_device_scanner COMMAND usb_device_scanner_test)

    add_executable(
        libusb_hotplug_monitor_test
        libusb_hotplug_monitor_test.cpp
        "${PROJECT_SOURCE_DIR}/src/transport/LibusbHotplugMonitor.cpp"
    )
    target_include_directories(
        libusb_hotplug_monitor_test
        PRIVATE "${PROJECT_SOURCE_DIR}/src/transport/include"
    )
    target_include_directories(
        libusb_hotplug_monitor_test
        SYSTEM PRIVATE "${LIBUSB1_INCLUDE_DIR}"
    )
    target_link_libraries(
        libusb_hotplug_monitor_test
        PRIVATE Threads::Threads zzrk_project_options zzrk_project_warnings
    )
    add_test(NAME libusb_hotplug_monitor COMMAND libusb_hotplug_monitor_test)

    add_executable(
        libusb_bulk_transport_test
        libusb_bulk_transport_test.cpp
        "${PROJECT_SOURCE_DIR}/src/transport/LibusbBulkTransport.cpp"
    )
    target_include_directories(
        libusb_bulk_transport_test
        PRIVATE
            "${PROJECT_SOURCE_DIR}/src/transport/include"
            "${PROJECT_SOURCE_DIR}/src/session/include"
    )
    target_include_directories(
        libusb_bulk_transport_test
        SYSTEM PRIVATE "${LIBUSB1_INCLUDE_DIR}"
    )
    target_link_libraries(
        libusb_bulk_transport_test
        PRIVATE zzrk_project_options zzrk_project_warnings
    )
    add_test(NAME libusb_bulk_transport COMMAND libusb_bulk_transport_test)

    if(TARGET zzrk_workflow)
        add_executable(firmware_upgrade_workflow_test
                       firmware_upgrade_workflow_test.cpp)
        target_link_libraries(
            firmware_upgrade_workflow_test
            PRIVATE zzrk::workflow zzrk_project_warnings
        )
        add_test(NAME firmware_upgrade_workflow
                 COMMAND firmware_upgrade_workflow_test)

        add_executable(device_management_workflow_test
                       device_management_workflow_test.cpp)
        target_link_libraries(
            device_management_workflow_test
            PRIVATE zzrk::workflow zzrk_project_warnings
        )
        add_test(NAME device_management_workflow
                 COMMAND device_management_workflow_test)

        add_executable(image_download_workflow_test
                       image_download_workflow_test.cpp)
        target_link_libraries(
            image_download_workflow_test
            PRIVATE zzrk::workflow zzrk_project_warnings
        )
        add_test(NAME image_download_workflow
                 COMMAND image_download_workflow_test)
    endif()
endif()

if(TARGET zzrk_storage)
    add_executable(gpt_partition_table_test gpt_partition_table_test.cpp)
    target_link_libraries(
        gpt_partition_table_test
        PRIVATE zzrk::storage zzrk_project_warnings
    )
    add_test(NAME gpt_partition_table COMMAND gpt_partition_table_test)
endif()

if(TARGET zzrk_cli_support)
    add_executable(cli_output_test cli_output_test.cpp)
    target_link_libraries(
        cli_output_test
        PRIVATE zzrk_cli_support zzrk_project_warnings
    )
    add_test(NAME cli_output COMMAND cli_output_test)
endif()

if(TARGET zzrk_workflow)
    add_executable(firmware_upgrade_report_test
                   firmware_upgrade_report_test.cpp)
    target_link_libraries(
        firmware_upgrade_report_test
        PRIVATE zzrk::workflow zzrk_project_warnings
    )
    add_test(NAME firmware_upgrade_report
             COMMAND firmware_upgrade_report_test)

    add_executable(firmware_md5_verifier_test firmware_md5_verifier_test.cpp)
    target_link_libraries(
        firmware_md5_verifier_test
        PRIVATE zzrk::workflow zzrk_project_warnings
    )
    add_test(NAME firmware_md5_verifier COMMAND firmware_md5_verifier_test)
endif()

if(TARGET ZzRkDevTool)
    add_test(NAME gui_smoke COMMAND ZzRkDevTool --smoke-test)
    set_tests_properties(
        gui_smoke
        PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )
endif()

if(TARGET zzrk_gui_support)
    function(zzrk_add_gui_test test_name source)
        add_executable(
            "${test_name}_test"
            "${source}"
            GuiTestHarness.cpp
            GuiTestHarness.hpp
        )
        target_include_directories(
            "${test_name}_test"
            PRIVATE
                "${PROJECT_SOURCE_DIR}/apps/gui_qt"
                "${CMAKE_CURRENT_SOURCE_DIR}"
        )
        target_link_libraries(
            "${test_name}_test"
            PRIVATE
                Zz::WindowKit
                Zz::FluentUI
                zzrk::gui
                zzrk_project_warnings
        )
        add_test(NAME "${test_name}" COMMAND "${test_name}_test")
        set_tests_properties(
            "${test_name}"
            PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        )
    endfunction()

    zzrk_add_gui_test(gui_framework gui_framework_test.cpp)

    add_executable(upgrade_progress_tracker_test
                   upgrade_progress_tracker_test.cpp)
    target_link_libraries(
        upgrade_progress_tracker_test
        PRIVATE zzrk::gui zzrk_project_warnings
    )
    add_test(NAME upgrade_progress_tracker
             COMMAND upgrade_progress_tracker_test)

    zzrk_add_gui_test(gui_upgrade gui_upgrade_test.cpp)
    zzrk_add_gui_test(gui_image_download gui_image_download_test.cpp)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND TARGET ZzRkDevTool
   AND TARGET zzrkdevtool-cli)
    add_test(
        NAME linux_install_smoke
        COMMAND
            "${CMAKE_COMMAND}"
            "-DBUILD_DIR=${CMAKE_BINARY_DIR}"
            "-DSOURCE_DIR=${PROJECT_SOURCE_DIR}"
            "-DINSTALL_CONFIG=$<CONFIG>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/linux_install_smoke.cmake"
    )
    set_tests_properties(
        linux_install_smoke
        PROPERTIES
            LABELS packaging
            RUN_SERIAL TRUE
            TIMEOUT 60
    )
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND TARGET zzrkdevtool-cli)
    add_test(
        NAME cli_only_install_smoke
        COMMAND
            "${CMAKE_COMMAND}"
            "-DSOURCE_DIR=${PROJECT_SOURCE_DIR}"
            "-DBINARY_ROOT=${CMAKE_BINARY_DIR}/cli-only-install-smoke"
            "-DLIBUSB_ROOT=${ZZRK_LIBUSB_ROOT}"
            "-DFMT_SOURCE=${CMAKE_BINARY_DIR}/_deps/fmt-src"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cli_only_install_smoke.cmake"
    )
    set_tests_properties(
        cli_only_install_smoke
        PROPERTIES LABELS packaging RUN_SERIAL TRUE TIMEOUT 180
    )
endif()
