cmake_minimum_required(VERSION 3.20)
project(tlsr8258_rx_ack_decision C)
include(CTest)

add_executable(tlsr8258_rx_ack_decision main.c)

target_include_directories(tlsr8258_rx_ack_decision PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../drivers/ieee802154
)

add_test(NAME tlsr8258_rx_ack_decision COMMAND tlsr8258_rx_ack_decision)
