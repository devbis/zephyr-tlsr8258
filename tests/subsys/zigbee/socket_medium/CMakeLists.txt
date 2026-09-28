# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr COMPONENTS unittest REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(socket_medium)

target_sources(testbinary PRIVATE
  main.c
  ${ZEPHYR_BASE}/tests/subsys/zigbee/common/zb_native_sim_socket_medium.c
)

target_include_directories(testbinary PRIVATE
  ${ZEPHYR_BASE}/tests/subsys/zigbee/common
  ${ZEPHYR_BASE}/subsys/zigbee/include
)
