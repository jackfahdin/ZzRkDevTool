# SPDX-License-Identifier: MIT

foreach(_required_variable IN ITEMS SOURCE_DIR BINARY_ROOT)
    if(NOT DEFINED ${_required_variable} OR "${${_required_variable}}" STREQUAL "")
        message(FATAL_ERROR "${_required_variable} is required")
    endif()
endforeach()

set(_source_dir "${BINARY_ROOT}/source")
set(_build_dir "${BINARY_ROOT}/build")
set(_install_dir "${BINARY_ROOT}/install")
file(REMOVE_RECURSE "${BINARY_ROOT}")
file(MAKE_DIRECTORY "${_source_dir}/thirdParty")
file(COPY
    "${SOURCE_DIR}/CMakeLists.txt"
    "${SOURCE_DIR}/LICENSE"
    "${SOURCE_DIR}/NOTICE.md"
    "${SOURCE_DIR}/apps"
    "${SOURCE_DIR}/cmake"
    "${SOURCE_DIR}/packaging"
    "${SOURCE_DIR}/src"
    DESTINATION "${_source_dir}"
)
file(COPY "${SOURCE_DIR}/thirdParty/spdlog"
          "${SOURCE_DIR}/thirdParty/Zzlibusb"
     DESTINATION "${_source_dir}/thirdParty")
if(DEFINED FMT_SOURCE AND EXISTS "${FMT_SOURCE}")
    file(MAKE_DIRECTORY "${_source_dir}/thirdParty/fmt")
    file(COPY "${FMT_SOURCE}/" DESTINATION "${_source_dir}/thirdParty/fmt")
endif()

set(_fmt_override)
if(DEFINED FMT_SOURCE AND EXISTS "${FMT_SOURCE}/CMakeLists.txt")
    set(_fmt_override "-DFETCHCONTENT_SOURCE_DIR_FMT=${FMT_SOURCE}")
endif()

execute_process(
    COMMAND
        "${CMAKE_COMMAND}" -S "${_source_dir}" -B "${_build_dir}" -G Ninja
        -DCMAKE_BUILD_TYPE=Release
        -DZZRK_BUILD_GUI=OFF
        -DZZRK_BUILD_CLI=ON
        -DZZRK_BUILD_TRANSPORT=ON
        -DZZRK_BUILD_TESTS=OFF
        -DZZRK_BUNDLE_RUNTIME=OFF
        ${_fmt_override}
    RESULT_VARIABLE _configure_result
    OUTPUT_VARIABLE _configure_output
    ERROR_VARIABLE _configure_error
)
if(NOT _configure_result EQUAL 0)
    message(FATAL_ERROR
        "CLI-only configure failed (${_configure_result}):\n"
        "${_configure_output}${_configure_error}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" --build "${_build_dir}" --parallel 4
    RESULT_VARIABLE _build_result
    OUTPUT_VARIABLE _build_output
    ERROR_VARIABLE _build_error
)
if(NOT _build_result EQUAL 0)
    message(FATAL_ERROR
        "CLI-only build failed (${_build_result}):\n"
        "${_build_output}${_build_error}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" --install "${_build_dir}"
            --prefix "${_install_dir}"
    RESULT_VARIABLE _install_result
    OUTPUT_VARIABLE _install_output
    ERROR_VARIABLE _install_error
)
if(NOT _install_result EQUAL 0)
    message(FATAL_ERROR
        "CLI-only install failed (${_install_result}):\n"
        "${_install_output}${_install_error}")
endif()

if(NOT EXISTS "${_install_dir}/bin/zzrkdevtool-cli")
    message(FATAL_ERROR "CLI-only install did not produce zzrkdevtool-cli")
endif()
foreach(_framework_license IN ITEMS ZzPureTools ZzLog QWindowKit)
    if(EXISTS "${_install_dir}/share/doc/zzrkdevtool/licenses/${_framework_license}")
        message(FATAL_ERROR
            "CLI-only install contains GUI framework license ${_framework_license}")
    endif()
endforeach()

file(REMOVE_RECURSE "${BINARY_ROOT}")
