# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(zigbee_shell)
target_sources(app PRIVATE
  src/main.c
  src/app_bdb.c
  src/app_profile.c
)

if(CONFIG_BOARD_WEACT_STM32WB55_CORE)
  # USB CDC console and mcumgr, watchdog and the flash state fix of the board; the same
  # fix is built into its MCUboot, see mcuboot/
  include(${ZEPHYR_BASE}/samples/subsys/usb/common/common.cmake)
  target_sources(app PRIVATE
    src/board_weact_stm32wb55_core.c
    mcuboot/wbfix/wbfix.c
  )
endif()

# Expose ZCL SDK headers (zcl/, af/) that live under subsys/zigbee but are
# not part of the public zephyr_include_directories set.
target_include_directories(app PRIVATE
    ${ZEPHYR_BASE}/subsys/zigbee
    ${ZEPHYR_BASE}/subsys/zigbee/include
    ${ZEPHYR_BASE}/subsys/zigbee/common/includes
    ${ZEPHYR_BASE}/subsys/zigbee/platform/zephyr
)
