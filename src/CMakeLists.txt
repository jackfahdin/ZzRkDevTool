# SPDX-License-Identifier: MIT

add_subdirectory(core)
add_subdirectory(image)
add_subdirectory(protocol)
add_subdirectory(session)
if(TARGET spdlog::spdlog)
    add_subdirectory(logging)
endif()
if(ZZRK_BUILD_TRANSPORT)
    add_subdirectory(transport)
endif()
add_subdirectory(platform)
