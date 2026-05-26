cmake_minimum_required(VERSION 3.20)
project(tlsr8258_tx_irq C)
include(CTest)

add_executable(tlsr8258_tx_irq
    main.c
    ../../../drivers/ieee802154/ieee802154_tlsr8258_tx_irq.c
)

add_test(NAME tlsr8258_tx_irq COMMAND tlsr8258_tx_irq)
