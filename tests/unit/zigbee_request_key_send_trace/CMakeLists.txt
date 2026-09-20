# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20)
project(zigbee_request_key_send_trace C)
include(CTest)

add_executable(zigbee_request_key_send_trace
    main.c
)

target_compile_definitions(zigbee_request_key_send_trace PRIVATE
    WORKTREE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/../../.."
)

add_test(NAME zigbee_request_key_send_trace COMMAND zigbee_request_key_send_trace)
