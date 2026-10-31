# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(switch)

target_sources(app PRIVATE
  src/main.c
  ../common/app_common.c
  ../zigbee_shell/src/app_bdb.c
)
target_compile_definitions(app PRIVATE
  APP_MODEL_ID="TL-OnOffSwitch"
  APP_POWER_SOURCE=0x03
)
target_include_directories(app PRIVATE
  ../common
  ../zigbee_shell/src
  ${ZEPHYR_BASE}/subsys/zigbee
  ${ZEPHYR_BASE}/subsys/zigbee/include
  ${ZEPHYR_BASE}/subsys/zigbee/common/includes
  ${ZEPHYR_BASE}/subsys/zigbee/platform/zephyr
)
