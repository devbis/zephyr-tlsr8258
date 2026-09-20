/* SPDX-License-Identifier: Apache-2.0 */

#include "flash_tlsr8258_paged_write.h"

#define TLSR8258_FLASH_PAGE_SIZE 256u

/* Forward declarations of the direct callees implemented in flash_tlsr8258.c.
 * Calling them by name eliminates the function-pointer indirect-call
 * sequence (`tjl trampoline; tjex r4`) whose XIP fetch hung the chip on
 * the deferred NV-save path; the linker now emits a single direct-call
 * long thunk.
 */
struct tlsr8258_flash_write_ctx;
extern int tlsr8258_flash_write_page_locked(void *ctx, uint32_t addr, const uint8_t *buf,
					    size_t len);
extern void tlsr8258_flash_watchdog_clear(void);

TLSR8258_FLASH_PAGED_EXEC int tlsr8258_flash_write_pages(void *ctx, uint32_t addr,
							 const uint8_t *buf, size_t len)
{
	int ret = 0;

	while (len > 0u) {
		size_t page_off = (size_t)addr & (TLSR8258_FLASH_PAGE_SIZE - 1u);
		size_t chunk = len;

		if (chunk > (TLSR8258_FLASH_PAGE_SIZE - page_off)) {
			chunk = TLSR8258_FLASH_PAGE_SIZE - page_off;
		}
		tlsr8258_flash_watchdog_clear();
		ret = tlsr8258_flash_write_page_locked(ctx, addr, buf, chunk);
		if (ret < 0) {
			break;
		}

		addr += (uint32_t)chunk;
		buf += chunk;
		len -= chunk;
	}

	return ret;
}
