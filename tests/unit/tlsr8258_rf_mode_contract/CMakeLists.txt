cmake_minimum_required(VERSION 3.20)
project(tlsr8258_rf_mode_contract C)
include(CTest)

add_executable(tlsr8258_rf_mode_contract
    main.c
)

target_compile_definitions(tlsr8258_rf_mode_contract PRIVATE
    WORKTREE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/../../.."
)

add_test(NAME tlsr8258_rf_mode_contract COMMAND tlsr8258_rf_mode_contract)
