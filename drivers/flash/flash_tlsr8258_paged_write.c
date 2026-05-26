/* SPDX-License-Identifier: Apache-2.0 */

#include "flash_tlsr8258_paged_write.h"

#define TLSR8258_FLASH_PAGE_SIZE 256u

int tlsr8258_flash_write_pages(void *ctx, uint32_t addr, const uint8_t *buf, size_t len,
			       tlsr8258_flash_page_writer_t writer,
			       tlsr8258_watchdog_feed_t watchdog_feed)
{
	int ret = 0;

	while (len > 0u) {
		size_t page_off = (size_t)addr & (TLSR8258_FLASH_PAGE_SIZE - 1u);
		size_t chunk = len;

		if (chunk > (TLSR8258_FLASH_PAGE_SIZE - page_off)) {
			chunk = TLSR8258_FLASH_PAGE_SIZE - page_off;
		}
		if (watchdog_feed != NULL) {
			watchdog_feed(ctx);
		}
		ret = writer(ctx, addr, buf, chunk);
		if (ret < 0) {
			break;
		}

		addr += (uint32_t)chunk;
		buf += chunk;
		len -= chunk;
	}

	return ret;
}
