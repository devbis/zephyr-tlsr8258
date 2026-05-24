cmake_minimum_required(VERSION 3.20)
project(tlsr8258_rf_irq C)
include(CTest)

add_executable(tlsr8258_rf_irq
    main.c
    ../../../drivers/ieee802154/ieee802154_tlsr8258_rf_irq.c
)

target_compile_definitions(tlsr8258_rf_irq PRIVATE
    WORKTREE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/../../.."
)

add_test(NAME tlsr8258_rf_irq COMMAND tlsr8258_rf_irq)
