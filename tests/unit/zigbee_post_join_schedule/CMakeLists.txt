# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
project(zigbee_post_join_schedule C)

add_executable(zigbee_post_join_schedule_test main.c)
target_include_directories(zigbee_post_join_schedule_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs
)

target_compile_features(zigbee_post_join_schedule_test PRIVATE c_std_99)

enable_testing()
add_test(NAME zigbee_post_join_schedule_test COMMAND zigbee_post_join_schedule_test)
