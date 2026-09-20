# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20)
project(zigbee_ed_parent_link_recovery C)
include(CTest)

add_executable(zigbee_ed_parent_link_recovery
    main.c
)

target_compile_definitions(zigbee_ed_parent_link_recovery PRIVATE
    WORKTREE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/../../.."
)

add_test(NAME zigbee_ed_parent_link_recovery COMMAND zigbee_ed_parent_link_recovery)
