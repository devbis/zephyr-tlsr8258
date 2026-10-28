/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT phyplus_phy6252_flash_controller

#include <errno.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/irq.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#include "flash_phy6252_util.h"

#define SECTOR_SIZE 4096U

/* Entry points in the PHY62XX SDK 3.1.1 ROM symbol table. */
#define ROM_SPIF_WRITE ((int (*)(uint32_t, uint8_t *, uint32_t))0x00017395U)
#define ROM_SPIF_ERASE ((int (*)(uint32_t))0x00016fa9U)

static K_MUTEX_DEFINE(flash_lock);

static const struct flash_parameters parameters = {
	.write_block_size = 1U,
	.erase_value = 0xff,
};

#ifdef CONFIG_FLASH_PAGE_LAYOUT
static const struct flash_pages_layout layout = {
	.pages_count = PHY6252_FLASH_SIZE / SECTOR_SIZE,
	.pages_size = SECTOR_SIZE,
};
#endif

static __ramfunc void phy6252_flash_wait(void)
{
	volatile uint32_t *const config = (volatile uint32_t *)0x4000c800U;
	volatile uint32_t *const command = (volatile uint32_t *)0x4000c890U;
	volatile uint32_t *const read_data = (volatile uint32_t *)0x4000c8a0U;

	do {
		while ((*command & 2U) != 0U) {
		}
		for (volatile uint32_t i = 0U; i < 32U; i++) {
			__asm__ volatile("nop");
		}
		while ((*config & BIT(31)) == 0U) {
		}
		*command = 0x05900001U;
		while ((*command & 2U) != 0U) {
		}
		for (volatile uint32_t i = 0U; i < 32U; i++) {
			__asm__ volatile("nop");
		}
		while ((*config & BIT(31)) == 0U) {
		}
	} while ((*read_data & 1U) != 0U);
}

/* The entire cache-bypass interval must run from SRAM or ROM. */
static __ramfunc int phy6252_flash_command(uint32_t offset, uint8_t *buf, uint32_t len,
					   bool erase)
{
	volatile uint32_t *const cache = (volatile uint32_t *)0x4000c000U;
	volatile uint32_t *const cache_reset = (volatile uint32_t *)0x40000024U;
	volatile uint32_t *const bypass = (volatile uint32_t *)0x40000028U;
	int result;

	*cache = 2U;
	*cache_reset = 2U;
	*bypass = 1U;
	phy6252_flash_wait();
	if (erase) {
		result = ROM_SPIF_ERASE(offset);
	} else {
		result = ROM_SPIF_WRITE(offset, buf, len);
	}
	phy6252_flash_wait();
	*cache = 0U;
	*cache_reset = 3U;
	*bypass = 0U;
	return result;
}

static int phy6252_read(const struct device *dev, off_t offset, void *buf, size_t len)
{
	int ret;

	ARG_UNUSED(dev);
	if (!phy6252_flash_range_valid(offset, len)) {
		return -EINVAL;
	}
	ret = k_mutex_lock(&flash_lock, K_FOREVER);
	if (ret != 0) {
		return ret;
	}
	memcpy(buf, (const void *)(PHY6252_FLASH_BASE + (uintptr_t)offset), len);
	k_mutex_unlock(&flash_lock);
	return 0;
}

static int phy6252_write(const struct device *dev, off_t offset, const void *buf, size_t len)
{
	const uint8_t *source = buf;
	uint8_t staging[PHY6252_FLASH_STAGING_SIZE];
	int ret = 0;

	ARG_UNUSED(dev);
	if (!phy6252_flash_range_valid(offset, len)) {
		return -EINVAL;
	}
	ret = k_mutex_lock(&flash_lock, K_FOREVER);
	if (ret != 0) {
		return ret;
	}
	while (len > 0U) {
		size_t chunk = phy6252_flash_write_chunk((size_t)offset, len);
		unsigned int key;

		memcpy(staging, source, chunk);
		key = irq_lock();
		ret = phy6252_flash_command(phy6252_flash_address((uint32_t)offset), staging,
					    (uint32_t)chunk, false);
		irq_unlock(key);
		if (ret != 0 || memcmp((const void *)(PHY6252_FLASH_BASE + (uintptr_t)offset), staging,
				       chunk) != 0) {
			ret = -EIO;
			break;
		}
		offset += (off_t)chunk;
		source += chunk;
		len -= chunk;
	}
	k_mutex_unlock(&flash_lock);
	return ret;
}

static int phy6252_erase(const struct device *dev, off_t offset, size_t len)
{
	int ret = 0;

	ARG_UNUSED(dev);
	if (!phy6252_flash_range_valid(offset, len) || ((size_t)offset % SECTOR_SIZE) != 0U ||
	    (len % SECTOR_SIZE) != 0U) {
		return -EINVAL;
	}
	ret = k_mutex_lock(&flash_lock, K_FOREVER);
	if (ret != 0) {
		return ret;
	}
	while (len > 0U) {
		unsigned int key = irq_lock();

		ret = phy6252_flash_command(phy6252_flash_address((uint32_t)offset), NULL, 0U, true);
		irq_unlock(key);
		if (ret != 0) {
			ret = -EIO;
			break;
		}
		const uint8_t *erased = (const uint8_t *)(PHY6252_FLASH_BASE + (uintptr_t)offset);

		for (size_t i = 0U; i < SECTOR_SIZE; i++) {
			if (erased[i] != 0xffU) {
				ret = -EIO;
				break;
			}
		}
		if (ret != 0) {
			break;
		}
		offset += SECTOR_SIZE;
		len -= SECTOR_SIZE;
	}
	k_mutex_unlock(&flash_lock);
	return ret;
}

static const struct flash_parameters *phy6252_parameters(const struct device *dev)
{
	ARG_UNUSED(dev);
	return &parameters;
}

#ifdef CONFIG_FLASH_PAGE_LAYOUT
static void phy6252_layout(const struct device *dev, const struct flash_pages_layout **pages,
			   size_t *count)
{
	ARG_UNUSED(dev);
	*pages = &layout;
	*count = 1U;
}
#endif

static DEVICE_API(flash, phy6252_flash_api) = {
	.read = phy6252_read,
	.write = phy6252_write,
	.erase = phy6252_erase,
	.get_parameters = phy6252_parameters,
#ifdef CONFIG_FLASH_PAGE_LAYOUT
	.page_layout = phy6252_layout,
#endif
};

DEVICE_DT_INST_DEFINE(0, NULL, NULL, NULL, NULL, POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEVICE,
		      &phy6252_flash_api);
