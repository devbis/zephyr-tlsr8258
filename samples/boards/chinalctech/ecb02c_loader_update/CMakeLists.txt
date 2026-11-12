# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(ecb02c_loader_update)

if(NOT DEFINED LOADER_IMAGE)
  message(FATAL_ERROR
          "Give the image of the new loader: "
          "-DLOADER_IMAGE=<MCUboot build>/zephyr/zephyr.phy6252.bin")
endif()

# The table of the sectors of the loader that hold data
set(payload ${CMAKE_CURRENT_BINARY_DIR}/loader_payload.c)
add_custom_command(
  OUTPUT ${payload}
  COMMAND ${PYTHON_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/gen_loader_payload.py
          ${LOADER_IMAGE} ${payload}
  DEPENDS ${LOADER_IMAGE} ${CMAKE_CURRENT_SOURCE_DIR}/gen_loader_payload.py
  VERBATIM
)

target_sources(app PRIVATE src/main.c src/loader_update.c ${payload})
target_include_directories(app PRIVATE src)
