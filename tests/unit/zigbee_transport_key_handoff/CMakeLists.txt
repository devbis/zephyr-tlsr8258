# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20)
project(zigbee_transport_key_handoff C)
include(CTest)

add_executable(zigbee_transport_key_handoff
    main.c
)

target_compile_definitions(zigbee_transport_key_handoff PRIVATE
    WORKTREE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/../../.."
)

add_test(NAME zigbee_transport_key_handoff COMMAND zigbee_transport_key_handoff)
