# SPDX-License-Identifier: Apache-2.0
# Copyright (c) Zephyr Project Contributors

if(NOT CONFIG_USERSPACE)
  zephyr_sources(sem.c)
endif()

zephyr_sources_ifndef(CONFIG_TC32 thread_entry.c)
