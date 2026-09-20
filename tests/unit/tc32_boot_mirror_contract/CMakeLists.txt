# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20)
project(tc32_boot_mirror_contract C)
include(CTest)

add_executable(tc32_boot_mirror_contract
    main.c
)

target_compile_definitions(tc32_boot_mirror_contract PRIVATE
    WORKTREE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/../../.."
)

add_test(NAME tc32_boot_mirror_contract COMMAND tc32_boot_mirror_contract)
