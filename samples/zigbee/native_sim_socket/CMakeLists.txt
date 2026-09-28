# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(native_sim_socket)

target_sources(app PRIVATE
  src/main.c
  src/app_profile.c
  ../zigbee_shell/src/app_bdb.c
)
target_sources_ifdef(CONFIG_ZIGBEE_COORDINATOR app PRIVATE src/app_interview.c)

target_include_directories(app PRIVATE
  ../zigbee_shell/src
  ${ZEPHYR_BASE}/subsys/zigbee
  ${ZEPHYR_BASE}/subsys/zigbee/include
  ${ZEPHYR_BASE}/subsys/zigbee/common/includes
  ${ZEPHYR_BASE}/subsys/zigbee/platform/zephyr
)
