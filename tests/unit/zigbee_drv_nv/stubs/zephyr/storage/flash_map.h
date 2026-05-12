/* SPDX-License-Identifier: Apache-2.0 */

#ifndef TEST_STUB_FLASH_MAP_H_
#define TEST_STUB_FLASH_MAP_H_

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

struct device;

struct flash_area {
uint8_t fa_id;
uint16_t pad16;
off_t fa_off;
size_t fa_size;
const struct device *fa_dev;
};

int flash_area_open(uint8_t id, const struct flash_area **fa);
void flash_area_close(const struct flash_area *fa);
const struct device *flash_area_get_device(const struct flash_area *fa);

#endif
