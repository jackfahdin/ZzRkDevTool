# SPDX-License-Identifier: MIT

function(zzrk_add_puretools)
    set(
        _zzrk_puretools_option_names
        BUILD_SHARED_LIBS
        ZZ_BUILD_TESTS
        ZZ_BUILD_EXAMPLES
        ZZ_BUILD_BENCHMARKS
        ZZ_BUILD_FLUENT_QUICK
        ZZ_RELEASE_BUILD
        ZZLOG_BUILD_TESTS
        ZZLOG_BUILD_EXAMPLES
        ZZLOG_INSTALL
    )

    foreach(_zzrk_puretools_option IN LISTS _zzrk_puretools_option_names)
        if(DEFINED "${_zzrk_puretools_option}")
            set("_zzrk_puretools_saved_${_zzrk_puretools_option}"
                "${${_zzrk_puretools_option}}")
            set("_zzrk_puretools_${_zzrk_puretools_option}_was_defined" TRUE)
        endif()
    endforeach()

    set(BUILD_SHARED_LIBS OFF)
    set(ZZ_BUILD_TESTS OFF)
    set(ZZ_BUILD_EXAMPLES OFF)
    set(ZZ_BUILD_BENCHMARKS OFF)
    set(ZZ_BUILD_FLUENT_QUICK OFF)
    set(ZZ_RELEASE_BUILD OFF)
    set(ZZLOG_BUILD_TESTS OFF)
    set(ZZLOG_BUILD_EXAMPLES OFF)
    set(ZZLOG_INSTALL OFF)

    find_package(
        Qt6
        6.11.1
        EXACT
        REQUIRED
        COMPONENTS Core Gui Widgets Svg Concurrent
    )
    find_package(Qt6CorePrivate 6.11.1 EXACT REQUIRED)
    find_package(Qt6GuiPrivate 6.11.1 EXACT REQUIRED)
    find_package(Qt6WidgetsPrivate 6.11.1 EXACT REQUIRED)

    add_subdirectory(
        "${PROJECT_SOURCE_DIR}/thirdParty/ZzPureTools"
        "${PROJECT_BINARY_DIR}/thirdParty/ZzPureTools"
        EXCLUDE_FROM_ALL
    )

    foreach(_zzrk_puretools_option IN LISTS _zzrk_puretools_option_names)
        if(DEFINED "_zzrk_puretools_${_zzrk_puretools_option}_was_defined")
            set(
                "${_zzrk_puretools_option}"
                "${_zzrk_puretools_saved_${_zzrk_puretools_option}}"
            )
        else()
            unset("${_zzrk_puretools_option}")
        endif()
        unset("_zzrk_puretools_saved_${_zzrk_puretools_option}")
        unset("_zzrk_puretools_${_zzrk_puretools_option}_was_defined")
    endforeach()
    unset(_zzrk_puretools_option)
    unset(_zzrk_puretools_option_names)
endfunction()
