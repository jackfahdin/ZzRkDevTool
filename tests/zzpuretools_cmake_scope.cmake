# SPDX-License-Identifier: MIT

get_directory_property(_zzrk_puretools_directory_variables VARIABLES)
foreach(_zzrk_puretools_scope_option IN LISTS ZZRK_PURETOOLS_CONTROLLED_VARIABLES)
    list(FIND _zzrk_puretools_directory_variables
         "${_zzrk_puretools_scope_option}"
         _zzrk_puretools_scope_option_index)
    list(FIND _zzrk_puretools_scope_NORMAL_VARIABLES
         "${_zzrk_puretools_scope_option}"
         _zzrk_puretools_scope_snapshot_index)
    if(NOT _zzrk_puretools_scope_snapshot_index EQUAL -1)
        if(_zzrk_puretools_scope_option_index EQUAL -1
           OR NOT "${${_zzrk_puretools_scope_option}}" STREQUAL
              "${_zzrk_puretools_scope_NORMAL_${_zzrk_puretools_scope_option}}")
            message(
                FATAL_ERROR
                "ZzPureTools changed parent ${_zzrk_puretools_scope_option}"
            )
        endif()
    elseif(NOT _zzrk_puretools_scope_option_index EQUAL -1)
        message(
            FATAL_ERROR
            "ZzPureTools defined parent ${_zzrk_puretools_scope_option}"
        )
    endif()
endforeach()
unset(_zzrk_puretools_scope_snapshot_index)
unset(_zzrk_puretools_scope_option_index)
unset(_zzrk_puretools_scope_option)
unset(_zzrk_puretools_directory_variables)

if(NOT TARGET Zz::PureTools OR NOT TARGET Zz::WindowKit OR NOT TARGET Zz::FluentUI)
    message(FATAL_ERROR "ZzPureTools did not provide all required framework targets")
endif()

get_cmake_property(_zzrk_puretools_cache_variables CACHE_VARIABLES)
foreach(_zzrk_puretools_cache_variable IN LISTS _zzrk_puretools_cache_variables)
    zzrk_puretools_is_framework_cache_variable(
        "${_zzrk_puretools_cache_variable}"
        _zzrk_puretools_is_framework_cache_variable
    )
    if(_zzrk_puretools_is_framework_cache_variable)
        list(FIND _zzrk_puretools_scope_CACHE_VARIABLES
             "${_zzrk_puretools_cache_variable}"
             _zzrk_puretools_cache_snapshot_index)
        if(_zzrk_puretools_cache_snapshot_index EQUAL -1)
            message(
                FATAL_ERROR
                "ZzPureTools leaked cache entry ${_zzrk_puretools_cache_variable}"
            )
        endif()
    endif()
endforeach()

foreach(_zzrk_puretools_cache_variable IN LISTS _zzrk_puretools_scope_CACHE_VARIABLES)
    get_property(
        _zzrk_puretools_cache_value
        CACHE "${_zzrk_puretools_cache_variable}"
        PROPERTY VALUE
    )
    get_property(
        _zzrk_puretools_cache_type
        CACHE "${_zzrk_puretools_cache_variable}"
        PROPERTY TYPE
    )
    get_property(
        _zzrk_puretools_cache_help
        CACHE "${_zzrk_puretools_cache_variable}"
        PROPERTY HELPSTRING
    )
    get_property(
        _zzrk_puretools_cache_advanced
        CACHE "${_zzrk_puretools_cache_variable}"
        PROPERTY ADVANCED
    )
    if(NOT "${_zzrk_puretools_cache_value}" STREQUAL
       "${_zzrk_puretools_scope_CACHE_${_zzrk_puretools_cache_variable}_VALUE}"
       OR NOT "${_zzrk_puretools_cache_type}" STREQUAL
              "${_zzrk_puretools_scope_CACHE_${_zzrk_puretools_cache_variable}_TYPE}"
       OR NOT "${_zzrk_puretools_cache_help}" STREQUAL
              "${_zzrk_puretools_scope_CACHE_${_zzrk_puretools_cache_variable}_HELPSTRING}"
       OR NOT "${_zzrk_puretools_cache_advanced}" STREQUAL
              "${_zzrk_puretools_scope_CACHE_${_zzrk_puretools_cache_variable}_ADVANCED}")
        message(
            FATAL_ERROR
            "ZzPureTools changed cache entry ${_zzrk_puretools_cache_variable}"
        )
    endif()
endforeach()
unset(_zzrk_puretools_cache_advanced)
unset(_zzrk_puretools_cache_help)
unset(_zzrk_puretools_cache_type)
unset(_zzrk_puretools_cache_value)
unset(_zzrk_puretools_cache_snapshot_index)
unset(_zzrk_puretools_is_framework_cache_variable)
unset(_zzrk_puretools_cache_variable)
unset(_zzrk_puretools_cache_variables)

if(EXISTS "${PROJECT_BINARY_DIR}/thirdParty/ZzPureTools/cmake_install.cmake")
    message(FATAL_ERROR "ZzPureTools registered upstream install rules")
endif()
