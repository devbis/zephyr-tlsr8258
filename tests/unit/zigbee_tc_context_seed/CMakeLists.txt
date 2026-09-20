# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20)
project(zigbee_tc_context_seed C)
include(CTest)

add_executable(zigbee_tc_context_seed
    main.c
)

target_compile_definitions(zigbee_tc_context_seed PRIVATE
    WORKTREE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/../../.."
)

add_test(NAME zigbee_tc_context_seed COMMAND zigbee_tc_context_seed)
