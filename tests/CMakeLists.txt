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
