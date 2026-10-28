/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_DRIVERS_FLASH_FLASH_PHY6252_UTIL_H_
#define ZEPHYR_DRIVERS_FLASH_FLASH_PHY6252_UTIL_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#define PHY6252_FLASH_BASE 0x11000000U
#define PHY6252_FLASH_SIZE 0x40000U
#define PHY6252_FLASH_PAGE_SIZE 256U
#define PHY6252_FLASH_STAGING_SIZE 64U

static inline uint32_t phy6252_flash_address(size_t offset)
{
	return PHY6252_FLASH_BASE + (uint32_t)offset;
}

static inline bool phy6252_flash_range_valid(off_t offset, size_t len)
{
	return offset >= 0 && (uint64_t)offset <= PHY6252_FLASH_SIZE &&
	       len <= PHY6252_FLASH_SIZE - (size_t)offset;
}

static inline size_t phy6252_flash_write_chunk(size_t offset, size_t remaining)
{
	size_t page_remaining = PHY6252_FLASH_PAGE_SIZE - (offset % PHY6252_FLASH_PAGE_SIZE);
	size_t chunk = remaining;

	if (chunk > PHY6252_FLASH_STAGING_SIZE) {
		chunk = PHY6252_FLASH_STAGING_SIZE;
	}

	return chunk < page_remaining ? chunk : page_remaining;
}

#endif /* ZEPHYR_DRIVERS_FLASH_FLASH_PHY6252_UTIL_H_ */
