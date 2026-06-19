# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
project(zigbee_assoc_late_success_race C)

add_executable(zigbee_assoc_late_success_race_test main.c)
target_compile_features(zigbee_assoc_late_success_race_test PRIVATE c_std_99)
target_compile_definitions(zigbee_assoc_late_success_race_test PRIVATE
    WORKTREE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/../../.."
)

enable_testing()
add_test(
    NAME zigbee_assoc_late_success_race_test
    COMMAND zigbee_assoc_late_success_race_test
)
