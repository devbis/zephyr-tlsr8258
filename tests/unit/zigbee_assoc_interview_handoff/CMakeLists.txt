# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20)
project(zigbee_assoc_interview_handoff C)
include(CTest)

add_executable(zigbee_assoc_interview_handoff
    main.c
)

target_compile_definitions(zigbee_assoc_interview_handoff PRIVATE
    WORKTREE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/../../.."
)

add_test(NAME zigbee_assoc_interview_handoff COMMAND zigbee_assoc_interview_handoff)
