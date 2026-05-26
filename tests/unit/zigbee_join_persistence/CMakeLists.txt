cmake_minimum_required(VERSION 3.20)
project(zigbee_join_persistence C)
include(CTest)

add_executable(zigbee_join_persistence
    main.c
)

target_compile_definitions(zigbee_join_persistence PRIVATE
    WORKTREE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/../../.."
    REPO_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/../../../../../.."
)

add_test(NAME zigbee_join_persistence COMMAND zigbee_join_persistence)
