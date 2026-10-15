# SPDX-License-Identifier: MIT

set(ZZRK_PRIVATE_LIBDIR "${CMAKE_INSTALL_LIBDIR}/zzrkdevtool")
set(ZZRK_INSTALL_PLUGINDIR "${ZZRK_PRIVATE_LIBDIR}/plugins")
if(ZZRK_LINUX_PORTABLE)
    set(ZZRK_PRIVATE_LIBDIR "lib")
    set(ZZRK_INSTALL_PLUGINDIR "plugins")
    set(ZZRK_INSTALL_DOCDIR "docs")
elseif(APPLE AND TARGET ZzRkDevTool)
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
    if(ZZRK_LINUX_PORTABLE)
        # 便携包只携带规则与安装说明，不自动修改系统 udev 配置。
        install(FILES "${PROJECT_SOURCE_DIR}/packaging/linux/60-zzrkdevtool.rules"
                DESTINATION "usb")
        install(FILES "${PROJECT_SOURCE_DIR}/packaging/linux/PORTABLE.zh-CN.md"
                DESTINATION "." RENAME "README.zh-CN.md")
    else()
        install(FILES "${PROJECT_SOURCE_DIR}/packaging/linux/60-zzrkdevtool.rules"
                DESTINATION "lib/udev/rules.d")
    endif()

    if(TARGET ZzRkDevTool AND NOT ZZRK_LINUX_PORTABLE)
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
        find_package(Qt6 6.8 REQUIRED COMPONENTS Core)

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

        # 精细插件筛选参数在 Qt 6.10 才引入；6.8/6.9 使用默认完整部署。
        set(_zzrk_linux_plugin_options)
        if(Qt6_VERSION VERSION_GREATER_EQUAL "6.10")
            set(_zzrk_linux_plugin_options
"INCLUDE_PLUGINS qoffscreen
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
        xcbglintegrations"
            )
        endif()

        # Linux 从构建产物分析依赖，保留其中指向所选 Qt 的 BUILD_RPATH。
        # 已安装程序的 RPATH 已改为包内路径，此时依赖尚未复制，分析它会误用系统 Qt。
        # 使用 Qt 6 接口，避免无版本接口转发参数时提前展开安装阶段的变量。
        qt6_generate_deploy_script(
            TARGET ${_zzrk_primary_deploy_target}
            OUTPUT_SCRIPT _zzrk_runtime_deploy_script
            CONTENT
"set(QT_DEPLOY_LIB_DIR \"${ZZRK_PRIVATE_LIBDIR}\")
set(QT_DEPLOY_PLUGINS_DIR \"${ZZRK_INSTALL_PLUGINDIR}\")
qt_deploy_runtime_dependencies(
    EXECUTABLE \"$<TARGET_FILE:${_zzrk_primary_deploy_target}>\"
    ${_zzrk_additional_deploy_executables}
    GENERATE_QT_CONF
    NO_TRANSLATIONS
    ${_zzrk_linux_plugin_options}
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
    find_package(Qt6 6.8 REQUIRED COMPONENTS Core)

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
    set(_zzrk_cross_plugin_options)
    if(Qt6_VERSION VERSION_GREATER_EQUAL "6.10")
        set(_zzrk_cross_plugin_options "INCLUDE_PLUGINS qoffscreen")
    endif()
    if(APPLE AND ZZRK_MACOS_CODESIGN_IDENTITY)
        set(
            _zzrk_deploy_tool_options
            "DEPLOY_TOOL_OPTIONS -hardened-runtime \"-codesign=${ZZRK_MACOS_CODESIGN_IDENTITY}\""
        )
    endif()

    # 保留 QT_DEPLOY_BIN_DIR，等安装脚本加载 Qt 部署环境后再展开。
    qt6_generate_deploy_script(
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

    # Zzlibusb 已静态链接，不再从旧缓存路径复制预编译 DLL/dylib。
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
    if(ZZRK_LINUX_PORTABLE)
        set(CPACK_GENERATOR "TGZ")
        set(CPACK_PACKAGING_INSTALL_PREFIX "/")
        set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY ON)
        set(CPACK_PACKAGE_FILE_NAME "ZzRkDevTool-${PROJECT_VERSION}-linux-${CMAKE_SYSTEM_PROCESSOR}-portable")
    else()
        set(CPACK_GENERATOR "DEB")
        set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
        set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
        set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
        set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
        set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
        set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS_PRIVATE_DIRS "${ZZRK_PRIVATE_LIBDIR}")
        set(CPACK_DEBIAN_PACKAGE_DEPENDS "adduser, udev")
        set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA
            "${PROJECT_SOURCE_DIR}/packaging/linux/postinst;${PROJECT_SOURCE_DIR}/packaging/linux/postrm")
        # 同一份编译产物生成两种布局；TGZ 仅整理自己的 CPack 暂存副本。
        configure_file(
            "${PROJECT_SOURCE_DIR}/cmake/LinuxPackageConfig.cmake.in"
            "${PROJECT_BINARY_DIR}/LinuxPackageConfig.cmake" @ONLY
        )
        configure_file(
            "${PROJECT_SOURCE_DIR}/cmake/LinuxPortableStage.cmake.in"
            "${PROJECT_BINARY_DIR}/LinuxPortableStage.cmake" @ONLY
        )
        set(CPACK_PROJECT_CONFIG_FILE "${PROJECT_BINARY_DIR}/LinuxPackageConfig.cmake")
    endif()
elseif(WIN32)
    set(CPACK_GENERATOR "TGZ")
    set(CPACK_PACKAGE_FILE_NAME "ZzRkDevTool-${PROJECT_VERSION}-windows-x86_64")
elseif(APPLE)
    set(CPACK_GENERATOR "DragNDrop")
    set(CPACK_DMG_VOLUME_NAME "ZzRkDevTool ${PROJECT_VERSION}")
    set(_zzrk_macos_package_arch "${CMAKE_OSX_ARCHITECTURES}")
    if(NOT _zzrk_macos_package_arch)
        set(_zzrk_macos_package_arch "${CMAKE_SYSTEM_PROCESSOR}")
    endif()
    string(REPLACE ";" "-" _zzrk_macos_package_arch "${_zzrk_macos_package_arch}")
    set(CPACK_PACKAGE_FILE_NAME "ZzRkDevTool-${PROJECT_VERSION}-macos-${_zzrk_macos_package_arch}")
endif()

include(CPack)
