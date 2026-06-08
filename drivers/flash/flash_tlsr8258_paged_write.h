/* SPDX-License-Identifier: Apache-2.0 */

#ifndef ZEPHYR_DRIVERS_FLASH_TLSR8258_PAGED_WRITE_H_
#define ZEPHYR_DRIVERS_FLASH_TLSR8258_PAGED_WRITE_H_

#include <stddef.h>
#include <stdint.h>

/*
 * Direct-call API: the previous function-pointer signature emitted a
 * `tjl trampoline; tjex r4` indirect-call sequence whose XIP fetch of
 * the trampoline (located in flash .text) hung the chip when the
 * deferred NV-save chain crossed into the __ramfunc writer. Calling the
 * writer / watchdog feeder directly lets the linker emit a single
 * `__TC32ABSLongThunk_*` direct-call thunk, which fetches and dispatches
 * reliably in this code path.
 */
int tlsr8258_flash_write_pages(void *ctx, uint32_t addr, const uint8_t *buf, size_t len);

#endif
