# SPDX-License-Identifier: Apache-2.0

# Configures binary tools as native Darwin tools.

find_program(CMAKE_OBJCOPY objcopy)
find_program(CMAKE_OBJDUMP objdump)
find_program(CMAKE_AR      ar)
find_program(CMAKE_RANLIB  ranlib)
find_program(CMAKE_READELF otool REQUIRED)
find_program(CMAKE_NM      nm)
find_program(CMAKE_STRIP   strip)

set(HOST_READELF_IS_OTOOL TRUE)

# Use the common properties for tools with GNU-compatible command semantics.
include(${ZEPHYR_BASE}/cmake/bintools/gnu/target_bintools.cmake)
