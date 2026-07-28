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

if(TARGET zzrk_logging)
    add_executable(logging_test logging_test.cpp)
    target_link_libraries(
        logging_test
        PRIVATE zzrk::logging zzrk_project_warnings
    )
    add_test(NAME logging COMMAND logging_test)
endif()

if(TARGET zzrk_transport)
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

if(TARGET ZzRkDevTool)
    add_test(NAME gui_smoke COMMAND ZzRkDevTool --smoke-test)
    set_tests_properties(
        gui_smoke
        PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )
endif()
