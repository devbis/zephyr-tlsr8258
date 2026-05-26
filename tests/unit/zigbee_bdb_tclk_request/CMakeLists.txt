# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20)
project(zigbee_bdb_tclk_request C)
include(CTest)

add_executable(zigbee_bdb_tclk_request
    main.c
)

target_compile_definitions(zigbee_bdb_tclk_request PRIVATE
    WORKTREE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/../../.."
)

add_test(NAME zigbee_bdb_tclk_request COMMAND zigbee_bdb_tclk_request)
