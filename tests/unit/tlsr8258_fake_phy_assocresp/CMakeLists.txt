cmake_minimum_required(VERSION 3.20)
project(tlsr8258_fake_phy_assocresp C)
include(CTest)

add_executable(tlsr8258_fake_phy_assocresp
    main.c
    fake_phy_backend.c
    scenario_runner.c
    ../../../drivers/ieee802154/ieee802154_tlsr8258_tx_irq.c
)

target_include_directories(tlsr8258_fake_phy_assocresp PRIVATE
    ../../../drivers/ieee802154
)

add_test(NAME tlsr8258_fake_phy_assocresp COMMAND tlsr8258_fake_phy_assocresp)
