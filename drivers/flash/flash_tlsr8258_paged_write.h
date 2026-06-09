/* SPDX-License-Identifier: Apache-2.0 */

#ifndef ZEPHYR_DRIVERS_FLASH_TLSR8258_PAGED_WRITE_H_
#define ZEPHYR_DRIVERS_FLASH_TLSR8258_PAGED_WRITE_H_

#include <stddef.h>
#include <stdint.h>
#include <zephyr/toolchain.h>

#define TLSR8258_FLASH_PAGED_EXEC __attribute__((noinline, section(".ram_code")))

/*
 * Experimental full-.ram_code flash path: keep the paged-write loop in the
 * same low-flash window as the public entrypoints and low-level MSPI helpers
 * so the write path has no XIP -> __ramfunc transition at all.
 */
TLSR8258_FLASH_PAGED_EXEC int tlsr8258_flash_write_pages(void *ctx, uint32_t addr,
							 const uint8_t *buf, size_t len);

#endif
