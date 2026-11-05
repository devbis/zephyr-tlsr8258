# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

zephyr_include_directories(.)
zephyr_sources(soc.c)
set(SOC_LINKER_SCRIPT ${ZEPHYR_BASE}/include/zephyr/arch/arm/cortex_m/scripts/linker.ld CACHE INTERNAL "")

# The image for the boot ROM: its segment table, then the XIP code. The window is the chosen flash of the board
# (the loader's own when MCUboot is built; an application that MCUboot starts has no table: its signed
# zephyr.bin goes into the slot).
if(NOT CONFIG_BOOTLOADER_MCUBOOT)
  dt_chosen(phy6252_flash PROPERTY "zephyr,flash")
  dt_reg_addr(phy6252_xip_base PATH ${phy6252_flash})
  dt_reg_size(phy6252_xip_size PATH ${phy6252_flash})
  math(EXPR phy6252_xip_end "${phy6252_xip_base} + ${phy6252_xip_size}" OUTPUT_FORMAT HEXADECIMAL)
  add_custom_target(phy6252_image ALL
    COMMAND ${PYTHON_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/make_image.py
            ${PROJECT_BINARY_DIR}/${KERNEL_ELF_NAME}
            ${PROJECT_BINARY_DIR}/zephyr.phy6252.bin
            --xip-base ${phy6252_xip_base} --xip-end ${phy6252_xip_end}
    DEPENDS ${PROJECT_BINARY_DIR}/${KERNEL_ELF_NAME}
    BYPRODUCTS ${PROJECT_BINARY_DIR}/zephyr.phy6252.bin
    VERBATIM
  )
endif()
