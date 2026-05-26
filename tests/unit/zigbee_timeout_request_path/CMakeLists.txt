# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
project(zigbee_timeout_request_path C)

add_executable(zigbee_timeout_request_path_test main.c)

target_compile_definitions(zigbee_timeout_request_path_test PRIVATE
    WORKTREE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/../../.."
)

target_compile_features(zigbee_timeout_request_path_test PRIVATE c_std_99)

enable_testing()
add_test(NAME zigbee_timeout_request_path_test COMMAND zigbee_timeout_request_path_test)
