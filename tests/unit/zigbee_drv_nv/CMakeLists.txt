# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
project(zigbee_drv_nv C)

add_executable(zigbee_drv_nv_test main.c)
target_include_directories(zigbee_drv_nv_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs
)

target_compile_features(zigbee_drv_nv_test PRIVATE c_std_99)

enable_testing()
add_test(NAME zigbee_drv_nv_test COMMAND zigbee_drv_nv_test)
