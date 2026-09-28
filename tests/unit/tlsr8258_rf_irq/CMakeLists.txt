# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr COMPONENTS unittest REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(tlsr8258_rf_irq)

target_sources(testbinary PRIVATE
  main.c
  ${ZEPHYR_BASE}/drivers/ieee802154/ieee802154_tlsr8258_rf_irq.c
)

target_include_directories(testbinary PRIVATE
  ${ZEPHYR_BASE}/drivers/ieee802154
)
