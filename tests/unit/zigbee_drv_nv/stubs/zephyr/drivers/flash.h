/* SPDX-License-Identifier: Apache-2.0 */

#ifndef TEST_STUB_FLASH_H_
#define TEST_STUB_FLASH_H_

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

struct device;

struct flash_pages_info {
	off_t start_offset;
	size_t size;
	uint32_t index;
};

struct flash_parameters {
	uint8_t erase_value;
};

int flash_get_page_info_by_offs(const struct device *dev, off_t offs,
				 struct flash_pages_info *info);
const struct flash_parameters *flash_get_parameters(const struct device *dev);
int flash_read(const struct device *dev, off_t offs, void *dst, size_t len);
int flash_write(const struct device *dev, off_t offs, const void *src, size_t len);

#endif
