# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(zigbee_nv)

target_sources(app PRIVATE
  src/main.c
  ${ZEPHYR_BASE}/subsys/zigbee/platform/zephyr/drv_nv_zephyr.c
)

target_include_directories(app PRIVATE
  ${ZEPHYR_BASE}/subsys/zigbee/include
  ${ZEPHYR_BASE}/subsys/zigbee/platform/zephyr
)

# drv_nv_zephyr.c logs through the Zigbee log module; register it here
# without building the stack.
target_compile_definitions(app PRIVATE CONFIG_ZIGBEE_LOG_LEVEL=LOG_LEVEL_NONE)
