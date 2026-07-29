# SPDX-License-Identifier: MIT

include(FindPackageHandleStandardArgs)

set(_libusb_no_default_path)
set(_libusb_include_hints)
set(_libusb_library_hints)
set(_libusb_runtime_hints)
set(_libusb_version_metadata)

if(ZZRK_LIBUSB_ROOT)
    list(APPEND _libusb_no_default_path NO_DEFAULT_PATH)
    list(APPEND
        _libusb_include_hints
        "${ZZRK_LIBUSB_ROOT}/include"
        "${ZZRK_LIBUSB_ROOT}/include/libusb-1.0"
    )
    list(APPEND _libusb_library_hints "${ZZRK_LIBUSB_ROOT}/lib")
    list(APPEND
        _libusb_runtime_hints
        "${ZZRK_LIBUSB_ROOT}/bin"
        "${ZZRK_LIBUSB_ROOT}/debug/bin"
    )
    list(APPEND
        _libusb_version_metadata
        "${ZZRK_LIBUSB_ROOT}/lib/pkgconfig/libusb-1.0.pc"
        "${ZZRK_LIBUSB_ROOT}/lib64/pkgconfig/libusb-1.0.pc"
    )
else()
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(PC_LIBUSB1 QUIET libusb-1.0)
        list(APPEND _libusb_include_hints ${PC_LIBUSB1_INCLUDE_DIRS})
        list(APPEND _libusb_library_hints ${PC_LIBUSB1_LIBRARY_DIRS})
        set(LibUSB1_VERSION "${PC_LIBUSB1_VERSION}")
    endif()
endif()

foreach(_libusb_pc_file IN LISTS _libusb_version_metadata)
    if(EXISTS "${_libusb_pc_file}")
        file(
            STRINGS "${_libusb_pc_file}" _libusb_pc_version_line
            REGEX "^[ \t]*Version:[ \t]*[^ \t\r\n]+"
            LIMIT_COUNT 1
        )
        if(_libusb_pc_version_line)
            string(
                REGEX REPLACE
                "^[ \t]*Version:[ \t]*([^ \t\r\n]+).*$"
                "\\1"
                LibUSB1_VERSION
                "${_libusb_pc_version_line}"
            )
            break()
        endif()
    endif()
endforeach()

find_path(
    LIBUSB1_INCLUDE_DIR
    NAMES libusb.h
    HINTS ${_libusb_include_hints}
    PATH_SUFFIXES libusb-1.0
    ${_libusb_no_default_path}
)

find_library(
    LIBUSB1_LIBRARY
    NAMES usb-1.0 libusb-1.0
    HINTS ${_libusb_library_hints}
    ${_libusb_no_default_path}
)

if(WIN32)
    find_file(
        LIBUSB1_RUNTIME_LIBRARY
        NAMES libusb-1.0.dll usb-1.0.dll
        HINTS ${_libusb_runtime_hints}
        ${_libusb_no_default_path}
    )
elseif(APPLE AND LIBUSB1_LIBRARY)
    file(REAL_PATH "${LIBUSB1_LIBRARY}" LIBUSB1_RUNTIME_LIBRARY)
endif()

set(_libusb_required_vars LIBUSB1_LIBRARY LIBUSB1_INCLUDE_DIR)
if(WIN32)
    list(APPEND _libusb_required_vars LIBUSB1_RUNTIME_LIBRARY)
endif()

find_package_handle_standard_args(
    LibUSB1
    REQUIRED_VARS ${_libusb_required_vars}
    VERSION_VAR LibUSB1_VERSION
)

if(LibUSB1_FOUND AND NOT TARGET LibUSB1::LibUSB1)
    if(WIN32)
        add_library(LibUSB1::LibUSB1 SHARED IMPORTED)
        set_target_properties(
            LibUSB1::LibUSB1
            PROPERTIES
                IMPORTED_IMPLIB "${LIBUSB1_LIBRARY}"
                IMPORTED_LOCATION "${LIBUSB1_RUNTIME_LIBRARY}"
                INTERFACE_INCLUDE_DIRECTORIES "${LIBUSB1_INCLUDE_DIR}"
        )
    elseif(APPLE)
        add_library(LibUSB1::LibUSB1 SHARED IMPORTED)
        set_target_properties(
            LibUSB1::LibUSB1
            PROPERTIES
                IMPORTED_LOCATION "${LIBUSB1_RUNTIME_LIBRARY}"
                INTERFACE_INCLUDE_DIRECTORIES "${LIBUSB1_INCLUDE_DIR}"
        )
    else()
        add_library(LibUSB1::LibUSB1 UNKNOWN IMPORTED)
        set_target_properties(
            LibUSB1::LibUSB1
            PROPERTIES
                IMPORTED_LOCATION "${LIBUSB1_LIBRARY}"
                INTERFACE_INCLUDE_DIRECTORIES "${LIBUSB1_INCLUDE_DIR}"
        )
    endif()

    if(UNIX AND NOT APPLE AND LIBUSB1_LIBRARY MATCHES "\\.a$")
        find_library(LIBUDEV_LIBRARY NAMES udev REQUIRED)
        set_property(
            TARGET LibUSB1::LibUSB1
            APPEND PROPERTY INTERFACE_LINK_LIBRARIES "${LIBUDEV_LIBRARY}"
        )
    endif()
endif()

mark_as_advanced(
    LIBUSB1_INCLUDE_DIR
    LIBUSB1_LIBRARY
    LIBUSB1_RUNTIME_LIBRARY
)
