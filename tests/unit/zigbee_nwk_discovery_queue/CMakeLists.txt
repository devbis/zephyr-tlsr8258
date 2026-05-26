# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
project(zigbee_nwk_discovery_queue C)

add_executable(zigbee_nwk_discovery_queue_test main.c)
target_include_directories(zigbee_nwk_discovery_queue_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs
)
target_compile_features(zigbee_nwk_discovery_queue_test PRIVATE c_std_99)
target_compile_options(zigbee_nwk_discovery_queue_test PRIVATE -ffunction-sections)
target_link_options(zigbee_nwk_discovery_queue_test PRIVATE -Wl,-dead_strip)

enable_testing()
add_test(NAME zigbee_nwk_discovery_queue_test COMMAND zigbee_nwk_discovery_queue_test)
