# SPDX-License-Identifier: MIT

foreach(_required_variable IN ITEMS BUILD_DIR SOURCE_DIR)
    if(NOT DEFINED ${_required_variable} OR "${${_required_variable}}" STREQUAL "")
        message(FATAL_ERROR "${_required_variable} is required")
    endif()
endforeach()

set(_install_root "${BUILD_DIR}/install-smoke")
set(_install_prefix "${_install_root}/usr")
set(_bindir "bin")
set(_libdir "lib/zzrkdevtool")
set(_plugindir "${_libdir}/plugins")
set(_docdir "share/doc/zzrkdevtool")
set(_udev_rule "lib/udev/rules.d/60-zzrkdevtool.rules")
set(_expected_rpath "$ORIGIN/../lib/zzrkdevtool")
if(PORTABLE_LAYOUT)
    set(_bindir ".")
    set(_libdir "lib")
    set(_plugindir "plugins")
    set(_docdir "docs")
    set(_udev_rule "usb/60-zzrkdevtool.rules")
    set(_expected_rpath "$ORIGIN/lib")
endif()

file(REMOVE_RECURSE "${_install_root}")
set(_install_command "${CMAKE_COMMAND}" --install "${BUILD_DIR}")
if(DEFINED INSTALL_CONFIG AND NOT "${INSTALL_CONFIG}" STREQUAL "")
    list(APPEND _install_command --config "${INSTALL_CONFIG}")
endif()
list(APPEND _install_command --prefix "${_install_prefix}")
execute_process(
    COMMAND ${_install_command}
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

# 移动到带空格的目录后再启动，验证便携包不依赖原安装位置。
if(PORTABLE_LAYOUT)
    set(_relocated_prefix "${_install_root}/relocated folder/ZzRkDevTool")
    file(MAKE_DIRECTORY "${_install_root}/relocated folder")
    file(RENAME "${_install_prefix}" "${_relocated_prefix}")
    set(_install_prefix "${_relocated_prefix}")
endif()
set(_cli "${_install_prefix}/${_bindir}/zzrkdevtool-cli")
set(_gui "${_install_prefix}/${_bindir}/ZzRkDevTool")

set(
    _required_files
    "${_cli}"
    "${_gui}"
    "${_install_prefix}/${_bindir}/qt.conf"
    "${_install_prefix}/${_udev_rule}"
    "${_install_prefix}/${_libdir}/libQt6Core.so.6"
    "${_install_prefix}/${_libdir}/libQt6Widgets.so.6"
    "${_install_prefix}/${_libdir}/libQt6Svg.so.6"
    "${_install_prefix}/${_plugindir}/platforms/libqxcb.so"
    "${_install_prefix}/${_plugindir}/platforms/libqoffscreen.so"
    "${_install_prefix}/${_docdir}/THIRD_PARTY_NOTICES.md"
    "${_install_prefix}/${_docdir}/sbom.spdx.json"
    "${_install_prefix}/${_docdir}/licenses/Qt/LGPL-3.0-only.txt"
    "${_install_prefix}/${_docdir}/licenses/libusb/LGPL-2.1-or-later.txt"
    "${_install_prefix}/${_docdir}/licenses/ZzPureTools/ZzPureTools-MIT.txt"
    "${_install_prefix}/${_docdir}/licenses/ZzLog/LICENSE"
    "${_install_prefix}/${_docdir}/licenses/QWindowKit/QWindowKit-Apache-2.0.txt"
)
foreach(_required_file IN LISTS _required_files)
    if(NOT EXISTS "${_required_file}")
        message(FATAL_ERROR "Installed file is missing: ${_required_file}")
    endif()
endforeach()

set(
    _framework_license_pairs
    "${SOURCE_DIR}/thirdParty/ZzPureTools/LICENSE|${_install_prefix}/${_docdir}/licenses/ZzPureTools/ZzPureTools-MIT.txt"
    "${SOURCE_DIR}/thirdParty/ZzPureTools/ZzThirdParty/ZzLog/LICENSE|${_install_prefix}/${_docdir}/licenses/ZzLog/LICENSE"
    "${SOURCE_DIR}/thirdParty/ZzPureTools/ZzThirdParty/qwindowkit/LICENSE|${_install_prefix}/${_docdir}/licenses/QWindowKit/QWindowKit-Apache-2.0.txt"
)
foreach(_framework_license_pair IN LISTS _framework_license_pairs)
    string(REPLACE "|" ";" _framework_license_paths "${_framework_license_pair}")
    list(GET _framework_license_paths 0 _framework_license_source)
    list(GET _framework_license_paths 1 _framework_license_installed)
    file(READ "${_framework_license_source}" _framework_license_source_content)
    file(READ "${_framework_license_installed}" _framework_license_installed_content)
    if(NOT _framework_license_source_content STREQUAL _framework_license_installed_content)
        message(FATAL_ERROR "Installed framework license differs from source: ${_framework_license_installed}")
    endif()
endforeach()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env --unset=LD_LIBRARY_PATH
        --unset=QT_PLUGIN_PATH --unset=QT_QPA_PLATFORM_PLUGIN_PATH
        "${_cli}" --version
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
        "${CMAKE_COMMAND}" -E env --unset=LD_LIBRARY_PATH
        --unset=QT_PLUGIN_PATH --unset=QT_QPA_PLATFORM_PLUGIN_PATH
        QT_QPA_PLATFORM=offscreen
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
    string(FIND "${_dynamic_section}" "${_expected_rpath}" _rpath_position)
    if(_rpath_position EQUAL -1)
        message(FATAL_ERROR "Installed RPATH is not relative: ${_executable}")
    endif()
    if(_dynamic_section MATCHES "${SOURCE_DIR}" OR
       _dynamic_section MATCHES "${BUILD_DIR}")
        message(FATAL_ERROR "Installed RPATH exposes the build tree: ${_executable}")
    endif()
    # 源码静态链接后不应再依赖系统或旧仓库里的 libusb 动态库。
    if(_dynamic_section MATCHES "NEEDED[^\n]*libusb")
        message(FATAL_ERROR "Installed executable still depends on shared libusb: ${_executable}")
    endif()
endforeach()

file(REMOVE_RECURSE "${_install_root}")
if(EXISTS "${_install_root}")
    message(FATAL_ERROR "Temporary installation could not be removed")
endif()
