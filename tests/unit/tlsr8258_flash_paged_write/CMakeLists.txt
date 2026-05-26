# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
project(tlsr8258_flash_paged_write C)

add_executable(tlsr8258_flash_paged_write_test
    main.c
    ../../../drivers/flash/flash_tlsr8258_paged_write.c
)
target_compile_features(tlsr8258_flash_paged_write_test PRIVATE c_std_99)

enable_testing()
add_test(NAME tlsr8258_flash_paged_write_test COMMAND tlsr8258_flash_paged_write_test)
