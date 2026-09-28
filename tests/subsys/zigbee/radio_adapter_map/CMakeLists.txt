# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr COMPONENTS unittest REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(radio_adapter_map)

target_sources(testbinary PRIVATE
  main.c
  ${ZEPHYR_BASE}/subsys/zigbee/platform/zephyr/drv_radio_map.c
)

target_include_directories(testbinary PRIVATE
  ${ZEPHYR_BASE}/subsys/zigbee/platform/zephyr
)
