# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20)
project(zigbee_restore_poll_gate C)
include(CTest)

add_executable(zigbee_restore_poll_gate
    main.c
)

target_compile_definitions(zigbee_restore_poll_gate PRIVATE
    WORKTREE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/../../.."
)

add_test(NAME zigbee_restore_poll_gate COMMAND zigbee_restore_poll_gate)
