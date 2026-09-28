# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(join_layout)

set(ZIGBEE_DIR ${ZEPHYR_BASE}/subsys/zigbee)

target_sources(app PRIVATE src/main.c)

target_include_directories(app PRIVATE
  ${ZIGBEE_DIR}
  ${ZIGBEE_DIR}/include
  ${ZIGBEE_DIR}/common
  ${ZIGBEE_DIR}/common/includes
  ${ZIGBEE_DIR}/os
  ${ZIGBEE_DIR}/mac/includes
  ${ZIGBEE_DIR}/nwk/includes
  ${ZIGBEE_DIR}/aps
  ${ZIGBEE_DIR}/ss
  ${ZIGBEE_DIR}/platform/zephyr
)
