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

/*
 * Assembly veneers in `flash_tlsr8258_entry.S` (.ram_code) that branch into
 * the high-RAM __ramfunc helpers via `tloadr + tjex r4`. Callers in flash
 * .text must route through these instead of calling the C entry points
 * directly, otherwise the linker emits a long-range thunk in .text that
 * wedges the bus on the first XIP fetch after arch_irq_lock disables
 * reg_irq_en.
 */
int tlsr8258_flash_call_write_pages(void *ctx, uint32_t addr,
				    const uint8_t *buf, size_t len);
int tlsr8258_flash_call_erase_sector_locked(uint32_t addr);

#endif
