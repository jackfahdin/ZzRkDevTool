# SPDX-License-Identifier: MIT

foreach(
    _zzrk_puretools_scope_option
    BUILD_SHARED_LIBS
    ZZ_BUILD_TESTS
    ZZ_BUILD_EXAMPLES
)
    if(DEFINED "_zzrk_puretools_scope_${_zzrk_puretools_scope_option}_was_defined")
        if(NOT "${${_zzrk_puretools_scope_option}}" STREQUAL
           "${_zzrk_puretools_scope_${_zzrk_puretools_scope_option}}")
            message(
                FATAL_ERROR
                "ZzPureTools changed parent ${_zzrk_puretools_scope_option}"
            )
        endif()
    elseif(DEFINED "${_zzrk_puretools_scope_option}")
        message(
            FATAL_ERROR
            "ZzPureTools defined parent ${_zzrk_puretools_scope_option}"
        )
    endif()
endforeach()
unset(_zzrk_puretools_scope_option)

if(NOT TARGET Zz::PureTools OR NOT TARGET Zz::WindowKit OR NOT TARGET Zz::FluentUI)
    message(FATAL_ERROR "ZzPureTools did not provide all required framework targets")
endif()
