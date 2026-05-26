# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20)
project(zigbee_interview_poll_start C)
include(CTest)

add_executable(zigbee_interview_poll_start
    main.c
)

target_compile_definitions(zigbee_interview_poll_start PRIVATE
    WORKTREE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/../../.."
)

add_test(NAME zigbee_interview_poll_start COMMAND zigbee_interview_poll_start)
