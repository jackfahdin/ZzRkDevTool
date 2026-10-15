# SPDX-License-Identifier: MIT

set(
    ZZRK_PURETOOLS_CONTROLLED_VARIABLES
    BUILD_SHARED_LIBS
    ZZ_BUILD_TESTS
    ZZ_BUILD_EXAMPLES
    ZZ_BUILD_BENCHMARKS
    ZZ_BUILD_FLUENT_QUICK
    ZZ_RELEASE_BUILD
    ZZLOG_BUILD_TESTS
    ZZLOG_BUILD_EXAMPLES
    ZZLOG_INSTALL
    ZZ_ENABLE_ASAN
    ZZ_ENABLE_UBSAN
    ZZ_ENABLE_CLANG_TIDY
    ZZ_ENABLE_MSVC_ANALYZE
    ZZ_WARNINGS_AS_ERRORS
    ZZ_ENABLE_LTO
    ZZ_BUNDLE_GNU_RUNTIME
    ZZ_GNU_RUNTIME_LICENSE_DIR
    ZZ_QT_PREFIX
    ZZ_RELEASE_EVIDENCE_ROOT
    ZZ_RELEASE_FORCED_BLOCKERS
    ZZ_WINDOWKIT_FORCE_QT_CONTEXT
    QWINDOWKIT_BUILD_STATIC
    QWINDOWKIT_BUILD_WIDGETS
    QWINDOWKIT_BUILD_QUICK
    QWINDOWKIT_BUILD_EXAMPLES
    QWINDOWKIT_BUILD_DOCUMENTATIONS
    QWINDOWKIT_INSTALL
    QWINDOWKIT_FORCE_QT_WINDOW_CONTEXT
    QWINDOWKIT_ENABLE_WINDOWS_SYSTEM_BORDERS
    QWINDOWKIT_ENABLE_STYLE_AGENT
    CMAKE_SKIP_INSTALL_RULES
)

function(zzrk_puretools_is_framework_cache_variable variable result_variable)
    if("${variable}" STREQUAL "BUILD_TESTING"
       OR "${variable}" MATCHES "^(ZZ_|ZZLOG_|QWINDOWKIT_)"
       OR "${variable}" STREQUAL "qmsetup_DIR")
        set(${result_variable} TRUE PARENT_SCOPE)
    else()
        set(${result_variable} FALSE PARENT_SCOPE)
    endif()
endfunction()

function(zzrk_capture_puretools_scope_snapshot prefix)
    get_directory_property(_zzrk_puretools_directory_variables VARIABLES)
    set(_zzrk_puretools_normal_variables)
    foreach(_zzrk_puretools_variable IN LISTS ZZRK_PURETOOLS_CONTROLLED_VARIABLES)
        list(FIND _zzrk_puretools_directory_variables
             "${_zzrk_puretools_variable}"
             _zzrk_puretools_variable_index)
        if(NOT _zzrk_puretools_variable_index EQUAL -1)
            list(APPEND _zzrk_puretools_normal_variables
                 "${_zzrk_puretools_variable}")
            set(
                "${prefix}_NORMAL_${_zzrk_puretools_variable}"
                "${${_zzrk_puretools_variable}}"
                PARENT_SCOPE
            )
        endif()
    endforeach()
    set("${prefix}_NORMAL_VARIABLES"
        "${_zzrk_puretools_normal_variables}"
        PARENT_SCOPE)

    get_cmake_property(_zzrk_puretools_cache_variables CACHE_VARIABLES)
    set(_zzrk_puretools_matching_cache_variables)
    foreach(_zzrk_puretools_cache_variable IN LISTS _zzrk_puretools_cache_variables)
        zzrk_puretools_is_framework_cache_variable(
            "${_zzrk_puretools_cache_variable}"
            _zzrk_puretools_is_framework_cache_variable
        )
        if(_zzrk_puretools_is_framework_cache_variable)
            list(APPEND _zzrk_puretools_matching_cache_variables
                 "${_zzrk_puretools_cache_variable}")
            foreach(_zzrk_puretools_cache_property IN ITEMS VALUE TYPE HELPSTRING ADVANCED)
                get_property(
                    _zzrk_puretools_cache_property_value
                    CACHE "${_zzrk_puretools_cache_variable}"
                    PROPERTY ${_zzrk_puretools_cache_property}
                )
                set(
                    "${prefix}_CACHE_${_zzrk_puretools_cache_variable}_${_zzrk_puretools_cache_property}"
                    "${_zzrk_puretools_cache_property_value}"
                    PARENT_SCOPE
                )
            endforeach()
        endif()
    endforeach()
    set("${prefix}_CACHE_VARIABLES"
        "${_zzrk_puretools_matching_cache_variables}"
        PARENT_SCOPE)
endfunction()

function(zzrk_restore_puretools_scope_snapshot prefix)
    get_cmake_property(_zzrk_puretools_cache_variables CACHE_VARIABLES)
    foreach(_zzrk_puretools_cache_variable IN LISTS _zzrk_puretools_cache_variables)
        zzrk_puretools_is_framework_cache_variable(
            "${_zzrk_puretools_cache_variable}"
            _zzrk_puretools_is_framework_cache_variable
        )
        if(_zzrk_puretools_is_framework_cache_variable)
            list(FIND "${prefix}_CACHE_VARIABLES"
                 "${_zzrk_puretools_cache_variable}"
                 _zzrk_puretools_cache_variable_index)
            if(_zzrk_puretools_cache_variable_index EQUAL -1)
                unset("${_zzrk_puretools_cache_variable}" CACHE)
            endif()
        endif()
    endforeach()

    foreach(_zzrk_puretools_cache_variable IN LISTS "${prefix}_CACHE_VARIABLES")
        set(
            "${_zzrk_puretools_cache_variable}"
            "${${prefix}_CACHE_${_zzrk_puretools_cache_variable}_VALUE}"
            CACHE "${${prefix}_CACHE_${_zzrk_puretools_cache_variable}_TYPE}"
            "${${prefix}_CACHE_${_zzrk_puretools_cache_variable}_HELPSTRING}"
            FORCE
        )
        set_property(
            CACHE "${_zzrk_puretools_cache_variable}"
            PROPERTY ADVANCED
            "${${prefix}_CACHE_${_zzrk_puretools_cache_variable}_ADVANCED}"
        )
    endforeach()

    foreach(_zzrk_puretools_variable IN LISTS ZZRK_PURETOOLS_CONTROLLED_VARIABLES)
        list(FIND "${prefix}_NORMAL_VARIABLES"
             "${_zzrk_puretools_variable}"
             _zzrk_puretools_variable_index)
        if(_zzrk_puretools_variable_index EQUAL -1)
            unset("${_zzrk_puretools_variable}")
        else()
            set(
                "${_zzrk_puretools_variable}"
                "${${prefix}_NORMAL_${_zzrk_puretools_variable}}"
            )
        endif()
    endforeach()
endfunction()

function(zzrk_clear_puretools_scope_snapshot prefix)
    foreach(_zzrk_puretools_variable IN LISTS ZZRK_PURETOOLS_CONTROLLED_VARIABLES)
        unset("${prefix}_NORMAL_${_zzrk_puretools_variable}" PARENT_SCOPE)
    endforeach()
    foreach(_zzrk_puretools_cache_variable IN LISTS "${prefix}_CACHE_VARIABLES")
        foreach(_zzrk_puretools_cache_property IN ITEMS VALUE TYPE HELPSTRING ADVANCED)
            unset(
                "${prefix}_CACHE_${_zzrk_puretools_cache_variable}_${_zzrk_puretools_cache_property}"
                PARENT_SCOPE
            )
        endforeach()
    endforeach()
    unset("${prefix}_NORMAL_VARIABLES" PARENT_SCOPE)
    unset("${prefix}_CACHE_VARIABLES" PARENT_SCOPE)
endfunction()

function(zzrk_add_puretools)
    zzrk_capture_puretools_scope_snapshot(_zzrk_puretools_saved)

    set(BUILD_SHARED_LIBS OFF)
    set(ZZ_BUILD_TESTS OFF)
    set(ZZ_BUILD_EXAMPLES OFF)
    set(ZZ_BUILD_BENCHMARKS OFF)
    set(ZZ_BUILD_FLUENT_QUICK OFF)
    set(ZZ_RELEASE_BUILD OFF)
    set(ZZLOG_BUILD_TESTS OFF)
    set(ZZLOG_BUILD_EXAMPLES OFF)
    set(ZZLOG_INSTALL OFF)
    set(ZZ_ENABLE_ASAN OFF)
    set(ZZ_ENABLE_UBSAN OFF)
    set(ZZ_ENABLE_CLANG_TIDY OFF)
    set(ZZ_ENABLE_MSVC_ANALYZE OFF)
    set(ZZ_WARNINGS_AS_ERRORS OFF)
    set(ZZ_ENABLE_LTO OFF)
    set(ZZ_BUNDLE_GNU_RUNTIME OFF)
    set(ZZ_GNU_RUNTIME_LICENSE_DIR "")
    set(ZZ_QT_PREFIX "")
    set(ZZ_RELEASE_EVIDENCE_ROOT "")
    set(ZZ_RELEASE_FORCED_BLOCKERS "")
    set(ZZ_WINDOWKIT_FORCE_QT_CONTEXT OFF)
    set(QWINDOWKIT_BUILD_STATIC ON)
    set(QWINDOWKIT_BUILD_WIDGETS ON)
    set(QWINDOWKIT_BUILD_QUICK OFF)
    set(QWINDOWKIT_BUILD_EXAMPLES OFF)
    set(QWINDOWKIT_BUILD_DOCUMENTATIONS OFF)
    set(QWINDOWKIT_INSTALL OFF)
    set(QWINDOWKIT_FORCE_QT_WINDOW_CONTEXT OFF)
    set(QWINDOWKIT_ENABLE_WINDOWS_SYSTEM_BORDERS ON)
    set(QWINDOWKIT_ENABLE_STYLE_AGENT OFF)
    set(CMAKE_SKIP_INSTALL_RULES ON)

    find_package(
        Qt6
        6.8
        REQUIRED
        COMPONENTS Core Gui Widgets Svg Concurrent
    )
    find_package(Qt6CorePrivate 6.8 REQUIRED)
    find_package(Qt6GuiPrivate 6.8 REQUIRED)
    find_package(Qt6WidgetsPrivate 6.8 REQUIRED)

    add_subdirectory(
        "${PROJECT_SOURCE_DIR}/thirdParty/ZzPureTools"
        "${PROJECT_BINARY_DIR}/thirdParty/ZzPureTools"
        EXCLUDE_FROM_ALL
    )

    # 上游用顶层 CMAKE_SOURCE_DIR 定位模块内部头文件；嵌入构建时补齐框架路径。
    target_include_directories(
        ZzPureTools PRIVATE
        "${PROJECT_SOURCE_DIR}/thirdParty/ZzPureTools/ZzFluentUI/widgets/src"
    )

    zzrk_restore_puretools_scope_snapshot(_zzrk_puretools_saved)
    zzrk_clear_puretools_scope_snapshot(_zzrk_puretools_saved)
endfunction()
