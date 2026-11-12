/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef LOADER_PAYLOAD_H_
#define LOADER_PAYLOAD_H_

#include <stdint.h>

#define LOADER_SECTOR_SIZE 4096U

struct loader_sector {
	/* In the flash */
	uint32_t offset;
	uint8_t data[LOADER_SECTOR_SIZE];
};

extern const struct loader_sector loader_sectors[];
extern const unsigned int loader_sector_count;

#endif /* LOADER_PAYLOAD_H_ */
