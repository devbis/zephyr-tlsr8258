# SPDX-License-Identifier: Apache-2.0

# Apple ld64 uses the common ld implementation with Darwin archive semantics.
set(TOOLCHAIN_LD_LINK_ELF_USE_FORCE_LOAD TRUE)

include(${ZEPHYR_BASE}/cmake/linker/ld/target.cmake)
