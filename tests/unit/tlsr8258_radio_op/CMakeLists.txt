cmake_minimum_required(VERSION 3.20)
project(tlsr8258_radio_op C)
include(CTest)

add_executable(tlsr8258_radio_op
    main.c
    ../../../drivers/ieee802154/ieee802154_tlsr8258_radio_op.c
)

target_include_directories(tlsr8258_radio_op PRIVATE
    ../../../drivers/ieee802154
)

add_test(NAME tlsr8258_radio_op COMMAND tlsr8258_radio_op)
