# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
project(zigbee_mac_timer_event_contract C)

add_executable(zigbee_mac_timer_event_contract_test main.c)
target_compile_features(zigbee_mac_timer_event_contract_test PRIVATE c_std_99)
target_compile_definitions(zigbee_mac_timer_event_contract_test PRIVATE
    WORKTREE_ROOT=\"/Users/ivan.belokobylskiy/projects/telink/llvm-tc32-arm-based/zephyr\"
)

enable_testing()
add_test(
    NAME zigbee_mac_timer_event_contract_test
    COMMAND zigbee_mac_timer_event_contract_test
)
