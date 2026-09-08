# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20)
project(zigbee_ack_filter_match C)
include(CTest)

add_executable(zigbee_ack_filter_match
    main.c
)

target_compile_definitions(zigbee_ack_filter_match PRIVATE
    WORKTREE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/../../.."
)

target_include_directories(zigbee_ack_filter_match PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../drivers/ieee802154
)

add_test(NAME zigbee_ack_filter_match COMMAND zigbee_ack_filter_match)
