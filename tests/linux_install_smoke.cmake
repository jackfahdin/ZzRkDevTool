# SPDX-License-Identifier: MIT

foreach(_required_variable IN ITEMS BUILD_DIR SOURCE_DIR INSTALL_CONFIG)
    if(NOT DEFINED ${_required_variable} OR "${${_required_variable}}" STREQUAL "")
        message(FATAL_ERROR "${_required_variable} is required")
    endif()
endforeach()

set(_install_root "${BUILD_DIR}/install-smoke")
set(_install_prefix "${_install_root}/usr")
set(_cli "${_install_prefix}/bin/zzrkdevtool-cli")
set(_gui "${_install_prefix}/bin/ZzRkDevTool")

file(REMOVE_RECURSE "${_install_root}")
execute_process(
    COMMAND
        "${CMAKE_COMMAND}" --install "${BUILD_DIR}"
        --config "${INSTALL_CONFIG}" --prefix "${_install_prefix}"
    RESULT_VARIABLE _install_result
    OUTPUT_VARIABLE _install_output
    ERROR_VARIABLE _install_error
)
if(NOT _install_result EQUAL 0)
    message(
        FATAL_ERROR
        "Temporary installation failed (${_install_result}):\n"
        "${_install_output}${_install_error}"
    )
endif()

set(
    _required_files
    "${_cli}"
    "${_gui}"
    "${_install_prefix}/bin/qt.conf"
    "${_install_prefix}/lib/udev/rules.d/60-zzrkdevtool.rules"
    "${_install_prefix}/lib/zzrkdevtool/libQt6Core.so.6"
    "${_install_prefix}/lib/zzrkdevtool/libusb-1.0.so.0"
    "${_install_prefix}/lib/zzrkdevtool/plugins/platforms/libqxcb.so"
    "${_install_prefix}/lib/zzrkdevtool/plugins/platforms/libqoffscreen.so"
    "${_install_prefix}/share/doc/zzrkdevtool/THIRD_PARTY_NOTICES.md"
    "${_install_prefix}/share/doc/zzrkdevtool/sbom.spdx.json"
    "${_install_prefix}/share/doc/zzrkdevtool/licenses/Qt/LGPL-3.0-only.txt"
    "${_install_prefix}/share/doc/zzrkdevtool/licenses/libusb/LGPL-2.1-or-later.txt"
)
foreach(_required_file IN LISTS _required_files)
    if(NOT EXISTS "${_required_file}")
        message(FATAL_ERROR "Installed file is missing: ${_required_file}")
    endif()
endforeach()

execute_process(
    COMMAND "${_cli}" --version
    RESULT_VARIABLE _cli_result
    OUTPUT_VARIABLE _cli_output
    ERROR_VARIABLE _cli_error
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
if(NOT _cli_result EQUAL 0 OR NOT _cli_output MATCHES "^ZzRkDevTool [0-9]+\\.[0-9]+\\.[0-9]+$")
    message(
        FATAL_ERROR
        "Installed CLI startup failed (${_cli_result}): "
        "${_cli_output}${_cli_error}"
    )
endif()

execute_process(
    COMMAND
        "${CMAKE_COMMAND}" -E env QT_QPA_PLATFORM=offscreen
        "${_gui}" --smoke-test
    RESULT_VARIABLE _gui_result
    OUTPUT_VARIABLE _gui_output
    ERROR_VARIABLE _gui_error
)
if(NOT _gui_result EQUAL 0)
    message(
        FATAL_ERROR
        "Installed GUI startup failed (${_gui_result}):\n"
        "${_gui_output}${_gui_error}"
    )
endif()

find_program(_readelf readelf REQUIRED)
foreach(_executable IN ITEMS "${_cli}" "${_gui}")
    execute_process(
        COMMAND "${_readelf}" -d "${_executable}"
        RESULT_VARIABLE _readelf_result
        OUTPUT_VARIABLE _dynamic_section
        ERROR_VARIABLE _readelf_error
    )
    if(NOT _readelf_result EQUAL 0)
        message(FATAL_ERROR "readelf failed: ${_readelf_error}")
    endif()
    if(NOT _dynamic_section MATCHES "\\$ORIGIN/../lib/zzrkdevtool")
        message(FATAL_ERROR "Installed RPATH is not relative: ${_executable}")
    endif()
    if(_dynamic_section MATCHES "${SOURCE_DIR}" OR
       _dynamic_section MATCHES "${BUILD_DIR}")
        message(FATAL_ERROR "Installed RPATH exposes the build tree: ${_executable}")
    endif()
endforeach()

file(REMOVE_RECURSE "${_install_root}")
if(EXISTS "${_install_root}")
    message(FATAL_ERROR "Temporary installation could not be removed")
endif()
