# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20)
project(tlsr8258_rx_queue C)
include(CTest)

add_executable(tlsr8258_rx_queue
    main.c
    ../../../drivers/ieee802154/ieee802154_tlsr8258_rx_queue.c
)

add_test(NAME tlsr8258_rx_queue COMMAND tlsr8258_rx_queue)
