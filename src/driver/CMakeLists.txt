# SPDX-License-Identifier: MIT
add_library(zzrk_driver_policy STATIC DriverPolicy.cpp)
add_library(zzrk::driver_policy ALIAS zzrk_driver_policy)
target_include_directories(zzrk_driver_policy PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(zzrk_driver_policy PUBLIC zzrk_project_options PRIVATE zzrk_project_warnings)
