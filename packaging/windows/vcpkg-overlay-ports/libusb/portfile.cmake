# SPDX-License-Identifier: MIT

vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO libusb/libusb
    REF "v${VERSION}"
    SHA512 3251c9f41e900efa13caf981483f46886c8434bf4c30f3fd3073921b06be9977cebcf4a18f7bc46db86e33d7f19752296377be9050abfb8d887ffeb377864647
    HEAD_REF master
)

if(VCPKG_LIBRARY_LINKAGE STREQUAL "dynamic")
    set(LIBUSB_PROJECT_TYPE dll)
else()
    set(LIBUSB_PROJECT_TYPE static)
endif()

# The release archive contains a README.md symlink that MSBuild cannot copy.
file(REMOVE "${SOURCE_PATH}/README.md")

vcpkg_msbuild_install(
    SOURCE_PATH "${SOURCE_PATH}"
    PROJECT_SUBPATH "msvc/libusb_${LIBUSB_PROJECT_TYPE}.vcxproj"
    OPTIONS "/p:EnableWindowsHotplug=true"
)

file(
    INSTALL "${SOURCE_PATH}/libusb/libusb.h"
    DESTINATION "${CURRENT_PACKAGES_DIR}/include/libusb-1.0"
)
set(prefix "")
set(exec_prefix [[${prefix}]])
set(libdir [[${prefix}/lib]])
set(includedir [[${prefix}/include]])
configure_file(
    "${SOURCE_PATH}/libusb-1.0.pc.in"
    "${CURRENT_PACKAGES_DIR}/lib/pkgconfig/libusb-1.0.pc"
    @ONLY
)
vcpkg_replace_string(
    "${CURRENT_PACKAGES_DIR}/lib/pkgconfig/libusb-1.0.pc"
    " -lusb-1.0"
    " -llibusb-1.0"
)
if(NOT VCPKG_BUILD_TYPE)
    set(includedir [[${prefix}/../include]])
    configure_file(
        "${SOURCE_PATH}/libusb-1.0.pc.in"
        "${CURRENT_PACKAGES_DIR}/debug/lib/pkgconfig/libusb-1.0.pc"
        @ONLY
    )
    vcpkg_replace_string(
        "${CURRENT_PACKAGES_DIR}/debug/lib/pkgconfig/libusb-1.0.pc"
        " -lusb-1.0"
        " -llibusb-1.0"
    )
endif()

vcpkg_fixup_pkgconfig()
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/COPYING")
