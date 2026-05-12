/* SPDX-License-Identifier: Apache-2.0 */

#ifndef TEST_STUB_NVS_H_
#define TEST_STUB_NVS_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

struct device;

struct nvs_fs {
off_t offset;
uint32_t sector_size;
uint16_t sector_count;
const struct device *flash_device;
};

int nvs_mount(struct nvs_fs *fs);
int nvs_clear(struct nvs_fs *fs);
ssize_t nvs_write(struct nvs_fs *fs, uint16_t id, const void *data, size_t len);
int nvs_delete(struct nvs_fs *fs, uint16_t id);
ssize_t nvs_read(struct nvs_fs *fs, uint16_t id, void *data, size_t len);

#endif
