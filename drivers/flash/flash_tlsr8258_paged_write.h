/* SPDX-License-Identifier: Apache-2.0 */

#ifndef ZEPHYR_DRIVERS_FLASH_TLSR8258_PAGED_WRITE_H_
#define ZEPHYR_DRIVERS_FLASH_TLSR8258_PAGED_WRITE_H_

#include <stddef.h>
#include <stdint.h>

typedef int (*tlsr8258_flash_page_writer_t)(void *ctx, uint32_t addr, const uint8_t *buf,
					    size_t len);
typedef void (*tlsr8258_watchdog_feed_t)(void *ctx);

int tlsr8258_flash_write_pages(void *ctx, uint32_t addr, const uint8_t *buf, size_t len,
			       tlsr8258_flash_page_writer_t writer,
			       tlsr8258_watchdog_feed_t watchdog_feed);

#endif
