/* SPDX-License-Identifier: Apache-2.0 */

#ifndef TEST_STUB_FLASH_MAP_H_
#define TEST_STUB_FLASH_MAP_H_

#include <stdbool.h>
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

const struct device *test_fixed_partition_device(void);
off_t test_fixed_partition_offset(void);
size_t test_fixed_partition_size(void);
bool device_is_ready(const struct device *dev);

#define DT_NODELABEL(label) label
#define DT_FIXED_PARTITION_ID(node_id) 1
#define FIXED_PARTITION_EXISTS(label) 1
#define FIXED_PARTITION_DEVICE(label) test_fixed_partition_device()
#define FIXED_PARTITION_OFFSET(label) test_fixed_partition_offset()
#define FIXED_PARTITION_SIZE(label) test_fixed_partition_size()

int flash_area_open(uint8_t id, const struct flash_area **fa);
void flash_area_close(const struct flash_area *fa);
const struct device *flash_area_get_device(const struct flash_area *fa);

#endif
