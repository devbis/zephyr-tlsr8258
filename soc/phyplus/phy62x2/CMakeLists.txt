# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

zephyr_include_directories(.)
zephyr_sources(soc.c)
zephyr_sources_ifdef(CONFIG_POWEROFF poweroff.c)
zephyr_sources_ifdef(CONFIG_PHY62X2_DEEP_SLEEP sleep.c)
zephyr_sources(wakeup.c)
zephyr_sources_ifdef(CONFIG_PHY62X2_SUSPEND suspend.c suspend.S)
set(SOC_LINKER_SCRIPT ${ZEPHYR_BASE}/include/zephyr/arch/arm/cortex_m/scripts/linker.ld CACHE INTERNAL "")

# The image for the boot ROM: its segment table, then the XIP code. The window is the chosen flash of the board
# (the loader's own when MCUboot is built; an application that MCUboot starts has no table: its signed
# zephyr.bin goes into the slot).
if(NOT CONFIG_BOOTLOADER_MCUBOOT)
  dt_chosen(phy62x2_flash PROPERTY "zephyr,flash")
  dt_reg_addr(phy62x2_xip_base PATH ${phy62x2_flash})
  dt_reg_size(phy62x2_xip_size PATH ${phy62x2_flash})
  math(EXPR phy62x2_xip_end "${phy62x2_xip_base} + ${phy62x2_xip_size}" OUTPUT_FORMAT HEXADECIMAL)
  add_custom_target(phy62x2_image ALL
    COMMAND ${PYTHON_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/make_image.py
            ${PROJECT_BINARY_DIR}/${KERNEL_ELF_NAME}
            ${PROJECT_BINARY_DIR}/zephyr.phy62x2.bin
            --xip-base ${phy62x2_xip_base} --xip-end ${phy62x2_xip_end}
    DEPENDS ${PROJECT_BINARY_DIR}/${KERNEL_ELF_NAME}
    BYPRODUCTS ${PROJECT_BINARY_DIR}/zephyr.phy62x2.bin
    VERBATIM
  )
endif()
