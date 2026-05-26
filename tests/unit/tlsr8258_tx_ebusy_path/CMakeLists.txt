# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
project(tlsr8258_tx_ebusy_path C)

add_executable(tlsr8258_tx_ebusy_path_test main.c)

target_compile_definitions(tlsr8258_tx_ebusy_path_test PRIVATE
    WORKTREE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/../../.."
)

target_compile_features(tlsr8258_tx_ebusy_path_test PRIVATE c_std_99)

enable_testing()
add_test(NAME tlsr8258_tx_ebusy_path_test COMMAND tlsr8258_tx_ebusy_path_test)
