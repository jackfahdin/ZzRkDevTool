# SPDX-License-Identifier: MIT

add_subdirectory(core)
add_subdirectory(protocol)
if(ZZRK_BUILD_TRANSPORT)
    add_subdirectory(transport)
endif()
add_subdirectory(platform)
