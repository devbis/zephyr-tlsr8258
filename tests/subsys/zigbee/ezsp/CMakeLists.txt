# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(ezsp)

set(EZSP_DIR ${ZEPHYR_BASE}/subsys/zigbee/ncp/ezsp)

target_sources(app PRIVATE
  src/main.c
  ${EZSP_DIR}/zb_ezsp_ash.c
  ${EZSP_DIR}/zb_ezsp_frame.c
)

target_include_directories(app PRIVATE ${EZSP_DIR})
