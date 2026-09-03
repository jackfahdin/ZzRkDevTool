# SPDX-License-Identifier: MIT

set(ZZRK_PRIVATE_LIBDIR "${CMAKE_INSTALL_LIBDIR}/zzrkdevtool")
if(APPLE AND TARGET ZzRkDevTool)
    set(
        ZZRK_INSTALL_DOCDIR
        "ZzRkDevTool.app/Contents/Resources/doc/zzrkdevtool"
    )
else()
    set(ZZRK_INSTALL_DOCDIR "${CMAKE_INSTALL_DATAROOTDIR}/doc/zzrkdevtool")
endif()
set(
    CMAKE_INSTALL_DEFAULT_DIRECTORY_PERMISSIONS
    OWNER_READ OWNER_WRITE OWNER_EXECUTE
    GROUP_READ GROUP_EXECUTE
    WORLD_READ WORLD_EXECUTE
)
file(
    RELATIVE_PATH
    _zzrk_private_libdir_from_bindir
    "/${CMAKE_INSTALL_BINDIR}"
    "/${ZZRK_PRIVATE_LIBDIR}"
)
set(ZZRK_INSTALL_RPATH "\$ORIGIN/${_zzrk_private_libdir_from_bindir}")

# This file is included after the application directories, so install-time
# runtime properties can be applied to targets configured earlier.
foreach(_zzrk_application_target IN ITEMS zzrkdevtool-cli ZzRkDevTool)
    if(TARGET ${_zzrk_application_target})
        if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
            set_target_properties(
                ${_zzrk_application_target}
                PROPERTIES INSTALL_RPATH "${ZZRK_INSTALL_RPATH}"
            )
        elseif(APPLE)
            set_target_properties(
                ${_zzrk_application_target}
                PROPERTIES INSTALL_RPATH "@executable_path/../Frameworks"
            )
        endif()
    endif()
endforeach()

if(TARGET ZzRkDevTool)
    install(
        TARGETS ZzRkDevTool
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
        BUNDLE DESTINATION "."
    )
endif()
if(TARGET zzrkdevtool-cli)
    if(APPLE AND TARGET ZzRkDevTool)
        install(
            TARGETS zzrkdevtool-cli
            RUNTIME DESTINATION "ZzRkDevTool.app/Contents/MacOS"
        )
    else()
        install(
            TARGETS zzrkdevtool-cli
            RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
        )
    endif()
endif()

install(
    FILES
        "${PROJECT_SOURCE_DIR}/LICENSE"
        "${PROJECT_SOURCE_DIR}/NOTICE.md"
    DESTINATION "${ZZRK_INSTALL_DOCDIR}"
)
install(
    FILES "${PROJECT_SOURCE_DIR}/packaging/licenses/fmt-MIT.txt"
    DESTINATION "${ZZRK_INSTALL_DOCDIR}/licenses/fmt"
)
install(
    FILES "${PROJECT_SOURCE_DIR}/thirdParty/spdlog/LICENSE"
    DESTINATION "${ZZRK_INSTALL_DOCDIR}/licenses/spdlog"
)
if(ZZRK_BUILD_GUI AND TARGET ZzRkDevTool)
    install(
        FILES "${PROJECT_SOURCE_DIR}/packaging/licenses/ZzPureTools-MIT.txt"
        DESTINATION "${ZZRK_INSTALL_DOCDIR}/licenses/ZzPureTools"
    )
    install(
        FILES "${PROJECT_SOURCE_DIR}/thirdParty/ZzPureTools/ZzThirdParty/ZzLog/LICENSE"
        DESTINATION "${ZZRK_INSTALL_DOCDIR}/licenses/ZzLog"
    )
    install(
        FILES "${PROJECT_SOURCE_DIR}/packaging/licenses/QWindowKit-Apache-2.0.txt"
        DESTINATION "${ZZRK_INSTALL_DOCDIR}/licenses/QWindowKit"
    )
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    install(
        FILES
            "${PROJECT_SOURCE_DIR}/packaging/linux/THIRD_PARTY_NOTICES.md"
            "${PROJECT_SOURCE_DIR}/packaging/linux/sbom.spdx.json"
        DESTINATION "${ZZRK_INSTALL_DOCDIR}"
    )
    install(
        FILES "${PROJECT_SOURCE_DIR}/packaging/linux/60-zzrkdevtool.rules"
        DESTINATION "lib/udev/rules.d"
    )

    if(TARGET ZzRkDevTool)
        install(
            FILES "${PROJECT_SOURCE_DIR}/packaging/linux/zzrkdevtool.desktop"
            DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/applications"
        )
    endif()

    if(ZZRK_BUNDLE_RUNTIME)
        if(NOT TARGET Qt6::Core)
            message(
                FATAL_ERROR
                "ZZRK_BUNDLE_RUNTIME requires at least one Qt application target"
            )
        endif()

        set(_zzrk_primary_deploy_target)
        set(_zzrk_additional_deploy_executables)
        if(TARGET ZzRkDevTool)
            set(_zzrk_primary_deploy_target ZzRkDevTool)
            if(TARGET zzrkdevtool-cli)
                set(
                    _zzrk_additional_deploy_executables
                    "ADDITIONAL_EXECUTABLES \"$<TARGET_FILE:zzrkdevtool-cli>\""
                )
            endif()
        elseif(TARGET zzrkdevtool-cli)
            set(_zzrk_primary_deploy_target zzrkdevtool-cli)
        endif()

        qt_generate_deploy_script(
            TARGET ${_zzrk_primary_deploy_target}
            OUTPUT_SCRIPT _zzrk_runtime_deploy_script
            CONTENT
"set(QT_DEPLOY_LIB_DIR \"${ZZRK_PRIVATE_LIBDIR}\")
set(QT_DEPLOY_PLUGINS_DIR \"${ZZRK_PRIVATE_LIBDIR}/plugins\")
qt_deploy_runtime_dependencies(
    EXECUTABLE \"$<TARGET_FILE:${_zzrk_primary_deploy_target}>\"
    ${_zzrk_additional_deploy_executables}
    GENERATE_QT_CONF
    NO_TRANSLATIONS
    INCLUDE_PLUGINS qoffscreen
    EXCLUDE_PLUGIN_TYPES
        egldeviceintegrations
        generic
        iconengines
        imageformats
        networkinformation
        platforminputcontexts
        platformthemes
        qmltooling
        sqldrivers
        tls
        vectorimageformats
        wayland-decoration-client
        wayland-graphics-integration-client
        wayland-shell-integration
        xcbglintegrations
)"
        )
        install(SCRIPT "${_zzrk_runtime_deploy_script}")
        install(
            CODE
"file(
    CHMOD \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}/qt.conf\"
    PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ
)"
        )

    endif()
endif()

if(ZZRK_BUNDLE_RUNTIME)
    install(
        FILES "${PROJECT_SOURCE_DIR}/packaging/licenses/LGPL-2.1-or-later.txt"
        DESTINATION "${ZZRK_INSTALL_DOCDIR}/licenses/libusb"
    )
    install(
        FILES
            "${PROJECT_SOURCE_DIR}/packaging/licenses/LGPL-3.0-only.txt"
            "${PROJECT_SOURCE_DIR}/packaging/licenses/GPL-3.0-only.txt"
        DESTINATION "${ZZRK_INSTALL_DOCDIR}/licenses/Qt"
    )
    install(
        FILES "${PROJECT_SOURCE_DIR}/packaging/licenses/ICU.txt"
        DESTINATION "${ZZRK_INSTALL_DOCDIR}/licenses/ICU"
    )
endif()

if(ZZRK_BUNDLE_RUNTIME AND (WIN32 OR APPLE))
    if(NOT TARGET Qt6::Core)
        message(
            FATAL_ERROR
            "ZZRK_BUNDLE_RUNTIME requires at least one Qt application target"
        )
    endif()

    set(_zzrk_cross_deploy_target)
    set(_zzrk_cross_deploy_executable)
    set(_zzrk_cross_additional_executables)
    if(TARGET ZzRkDevTool)
        set(_zzrk_cross_deploy_target ZzRkDevTool)
        if(APPLE)
            set(
                _zzrk_cross_deploy_executable
                "$<TARGET_FILE_NAME:ZzRkDevTool>.app"
            )
            if(TARGET zzrkdevtool-cli)
                set(
                    _zzrk_cross_additional_executables
                    "ADDITIONAL_EXECUTABLES \"$<TARGET_FILE_NAME:ZzRkDevTool>.app/Contents/MacOS/$<TARGET_FILE_NAME:zzrkdevtool-cli>\""
                )
            endif()
        else()
            set(
                _zzrk_cross_deploy_executable
                "\${QT_DEPLOY_BIN_DIR}/$<TARGET_FILE_NAME:ZzRkDevTool>"
            )
            if(TARGET zzrkdevtool-cli)
                set(
                    _zzrk_cross_additional_executables
                    "ADDITIONAL_EXECUTABLES \"\${QT_DEPLOY_BIN_DIR}/$<TARGET_FILE_NAME:zzrkdevtool-cli>\""
                )
            endif()
        endif()
    elseif(WIN32 AND TARGET zzrkdevtool-cli)
        set(_zzrk_cross_deploy_target zzrkdevtool-cli)
        set(
            _zzrk_cross_deploy_executable
            "\${QT_DEPLOY_BIN_DIR}/$<TARGET_FILE_NAME:zzrkdevtool-cli>"
        )
    else()
        message(
            FATAL_ERROR
            "Bundled macOS packages require the ZzRkDevTool app target"
        )
    endif()

    set(_zzrk_deploy_tool_options)
    set(_zzrk_cross_plugin_options "INCLUDE_PLUGINS qoffscreen")
    if(APPLE AND ZZRK_MACOS_CODESIGN_IDENTITY)
        set(
            _zzrk_deploy_tool_options
            "DEPLOY_TOOL_OPTIONS -hardened-runtime \"-codesign=${ZZRK_MACOS_CODESIGN_IDENTITY}\""
        )
    endif()

    qt_generate_deploy_script(
        TARGET ${_zzrk_cross_deploy_target}
        OUTPUT_SCRIPT _zzrk_cross_runtime_deploy_script
        CONTENT
"qt_deploy_runtime_dependencies(
    EXECUTABLE \"${_zzrk_cross_deploy_executable}\"
    ${_zzrk_cross_additional_executables}
    GENERATE_QT_CONF
    NO_TRANSLATIONS
    ${_zzrk_cross_plugin_options}
    ${_zzrk_deploy_tool_options}
)"
    )

    if(TARGET LibUSB1::LibUSB1)
        if(APPLE)
            get_filename_component(
                _zzrk_macos_libusb_name
                "${LIBUSB1_RUNTIME_LIBRARY}"
                NAME
            )
            install(
                FILES "${LIBUSB1_RUNTIME_LIBRARY}"
                DESTINATION "ZzRkDevTool.app/Contents/Frameworks"
            )
            configure_file(
                "${PROJECT_SOURCE_DIR}/cmake/FixMacOSLibUSB.cmake.in"
                "${CMAKE_CURRENT_BINARY_DIR}/FixMacOSLibUSB.cmake"
                @ONLY
            )
            install(SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/FixMacOSLibUSB.cmake")
        else()
            install(
                FILES "${LIBUSB1_RUNTIME_LIBRARY}"
                DESTINATION "${CMAKE_INSTALL_BINDIR}"
            )
        endif()
    endif()
    install(SCRIPT "${_zzrk_cross_runtime_deploy_script}")
endif()

set(CPACK_PACKAGE_NAME "zzrkdevtool")
set(CPACK_PACKAGE_VENDOR "ZzRkDevTool contributors")
set(CPACK_PACKAGE_CONTACT "ZzRkDevTool contributors")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Qt Rockchip firmware flashing tool")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_RESOURCE_FILE_LICENSE "${PROJECT_SOURCE_DIR}/LICENSE")
set(CPACK_VERBATIM_VARIABLES YES)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(CPACK_GENERATOR "DEB")
    set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
    set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
    set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
    set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
    set(
        CPACK_DEBIAN_PACKAGE_SHLIBDEPS_PRIVATE_DIRS
        "${ZZRK_PRIVATE_LIBDIR}"
    )
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "adduser, udev")
    set(
        CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA
        "${PROJECT_SOURCE_DIR}/packaging/linux/postinst;${PROJECT_SOURCE_DIR}/packaging/linux/postrm"
    )
elseif(WIN32)
    set(CPACK_GENERATOR "ZIP")
    set(CPACK_PACKAGE_FILE_NAME "zzrkdevtool-${PROJECT_VERSION}-windows-x86_64")
elseif(APPLE)
    set(CPACK_GENERATOR "DragNDrop")
    set(CPACK_DMG_VOLUME_NAME "ZzRkDevTool ${PROJECT_VERSION}")
    set(CPACK_PACKAGE_FILE_NAME "zzrkdevtool-${PROJECT_VERSION}-macos")
endif()

include(CPack)
