# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

zephyr_include_directories(${ZEPHYR_BASE}/drivers)
zephyr_sources(
  soc.c
  )

zephyr_linker_sources_ifdef(CONFIG_BT_STM32_IPM
  SECTIONS
  ipm.ld
  )

if(CONFIG_PM)
  zephyr_sources(power.c)
  # z_get_next_timeout_expiry() times the Standby wakeup
  zephyr_library_include_directories(${ZEPHYR_BASE}/kernel/include)
endif()

zephyr_sources_ifdef(CONFIG_POWEROFF poweroff.c)

zephyr_include_directories(.)

set(SOC_LINKER_SCRIPT ${ZEPHYR_BASE}/include/zephyr/arch/arm/cortex_m/scripts/linker.ld CACHE INTERNAL "")
