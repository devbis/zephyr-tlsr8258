# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20)
project(tc32_irq_pending_dispatch C)
include(CTest)

add_executable(tc32_irq_pending_dispatch
    main.c
)

target_compile_definitions(tc32_irq_pending_dispatch PRIVATE
    WORKTREE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/../../.."
)

add_test(NAME tc32_irq_pending_dispatch COMMAND tc32_irq_pending_dispatch)
