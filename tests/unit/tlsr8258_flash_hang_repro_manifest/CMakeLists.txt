# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20)
project(tlsr8258_flash_hang_repro_manifest C)
include(CTest)

add_executable(tlsr8258_flash_hang_repro_manifest
    main.c
)

target_compile_definitions(tlsr8258_flash_hang_repro_manifest PRIVATE
    WORKTREE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/../../.."
)

add_test(NAME tlsr8258_flash_hang_repro_manifest
         COMMAND tlsr8258_flash_hang_repro_manifest)
