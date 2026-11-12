/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include <zephyr/device.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "loader_payload.h"
#include "loader_update.h"

#define CHUNK 256U

static uint8_t chunk[CHUNK];

/* A chunk of a sector of the payload as it is in the flash? */
static bool chunk_same(const struct device *fl, const struct loader_sector *sector, unsigned int c)
{
	return (flash_read(fl, sector->offset + c * CHUNK, chunk, CHUNK) == 0) &&
	       (memcmp(chunk, &sector->data[c * CHUNK], CHUNK) == 0);
}

static bool sector_same(const struct device *fl, const struct loader_sector *sector)
{
	for (unsigned int c = 0U; c < LOADER_SECTOR_SIZE / CHUNK; c++) {
		if (!chunk_same(fl, sector, c)) {
			return false;
		}
	}

	return true;
}

int loader_update(void)
{
	const struct device *fl = DEVICE_DT_GET(DT_NODELABEL(flash_controller));
	unsigned int differ = 0U;

	if (!device_is_ready(fl)) {
		return -ENODEV;
	}

	for (unsigned int s = 0U; s < loader_sector_count; s++) {
		if (!sector_same(fl, &loader_sectors[s])) {
			differ++;
		}
	}
	if (differ == 0U) {
		printk("loader update: the loader is up to date (%u sectors)\n",
		       loader_sector_count);
		return 0;
	}
	printk("loader update: %u of %u sectors differ, writing\n", differ, loader_sector_count);

	for (unsigned int s = 0U; s < loader_sector_count; s++) {
		const struct loader_sector *sector = &loader_sectors[s];

		if (sector_same(fl, sector)) {
			continue;
		}
		if (flash_erase(fl, sector->offset, LOADER_SECTOR_SIZE) != 0) {
			printk("loader update: erase of 0x%x failed\n", sector->offset);
			return -EIO;
		}
		for (unsigned int c = 0U; c < LOADER_SECTOR_SIZE / CHUNK; c++) {
			if ((flash_write(fl, sector->offset + c * CHUNK, &sector->data[c * CHUNK],
					 CHUNK) != 0) ||
			    !chunk_same(fl, sector, c)) {
				printk("loader update: write of 0x%x failed\n",
				       sector->offset + c * CHUNK);
				return -EIO;
			}
		}
		printk("loader update: 0x%x done\n", sector->offset);
	}

	return 1;
}
