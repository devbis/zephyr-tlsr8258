# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20)
project(zigbee_nwk_leave_handling C)
include(CTest)

add_executable(zigbee_nwk_leave_handling
    main.c
)

target_compile_definitions(zigbee_nwk_leave_handling PRIVATE
    WORKTREE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/../../.."
)

add_test(NAME zigbee_nwk_leave_handling COMMAND zigbee_nwk_leave_handling)
