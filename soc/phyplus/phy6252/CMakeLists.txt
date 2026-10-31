# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

zephyr_include_directories(.)
zephyr_sources(soc.c)
set(SOC_LINKER_SCRIPT ${ZEPHYR_BASE}/include/zephyr/arch/arm/cortex_m/scripts/linker.ld CACHE INTERNAL "")

add_custom_target(phy6252_image ALL
  COMMAND ${PYTHON_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/make_image.py
          ${PROJECT_BINARY_DIR}/${KERNEL_ELF_NAME}
          ${PROJECT_BINARY_DIR}/zephyr.phy6252.bin
  DEPENDS ${PROJECT_BINARY_DIR}/${KERNEL_ELF_NAME}
  BYPRODUCTS ${PROJECT_BINARY_DIR}/zephyr.phy6252.bin
  VERBATIM
)
