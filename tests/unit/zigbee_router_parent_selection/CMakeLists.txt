# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20)
project(zigbee_router_parent_selection C)
include(CTest)

add_executable(zigbee_router_parent_selection
    main.c
)

target_compile_definitions(zigbee_router_parent_selection PRIVATE
    WORKTREE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/../../.."
)

add_test(NAME zigbee_router_parent_selection COMMAND zigbee_router_parent_selection)
