/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CONFIG_ZIGBEE_NV_SECTOR_COUNT 2
#ifdef FIXED_PARTITION_ID
#undef FIXED_PARTITION_ID
#endif
#define FIXED_PARTITION_ID(label) 1

#include "../../../subsys/zigbee/platform/zephyr/drv_nv_zephyr.c"

#define MAX_FAKE_ENTRIES 64
#define MAX_FAKE_VALUE_LEN 256

struct fake_nvs_entry {
bool used;
uint16_t id;
size_t len;
uint8_t data[MAX_FAKE_VALUE_LEN];
};

static struct fake_nvs_entry fake_entries[MAX_FAKE_ENTRIES];
static struct flash_area fake_area = {
.fa_id = 1,
.fa_off = 0,
.fa_size = 0x10000,
.fa_dev = (const struct device *)0x1,
};

static struct fake_nvs_entry *find_entry(uint16_t id)
{
for (size_t i = 0; i < ARRAY_SIZE(fake_entries); i++) {
if (fake_entries[i].used && fake_entries[i].id == id) {
return &fake_entries[i];
}
}

return NULL;
}

static struct fake_nvs_entry *find_or_alloc_entry(uint16_t id)
{
struct fake_nvs_entry *entry = find_entry(id);

if (entry != NULL) {
return entry;
}

for (size_t i = 0; i < ARRAY_SIZE(fake_entries); i++) {
if (!fake_entries[i].used) {
fake_entries[i].used = true;
fake_entries[i].id = id;
return &fake_entries[i];
}
}

return NULL;
}

int flash_area_open(uint8_t id, const struct flash_area **fa)
{
ARG_UNUSED(id);
*fa = &fake_area;
return 0;
}

void flash_area_close(const struct flash_area *fa)
{
ARG_UNUSED(fa);
}

const struct device *flash_area_get_device(const struct flash_area *fa)
{
return fa->fa_dev;
}

int nvs_mount(struct nvs_fs *fs)
{
ARG_UNUSED(fs);
return 0;
}

int nvs_clear(struct nvs_fs *fs)
{
ARG_UNUSED(fs);
memset(fake_entries, 0, sizeof(fake_entries));
return 0;
}

ssize_t nvs_write(struct nvs_fs *fs, uint16_t id, const void *data, size_t len)
{
ARG_UNUSED(fs);
if (len > MAX_FAKE_VALUE_LEN) {
return -ENOMEM;
}

struct fake_nvs_entry *entry = find_or_alloc_entry(id);
if (entry == NULL) {
return -ENOMEM;
}

entry->len = len;
if (len > 0) {
memcpy(entry->data, data, len);
}

return (ssize_t)len;
}

int nvs_delete(struct nvs_fs *fs, uint16_t id)
{
ARG_UNUSED(fs);
struct fake_nvs_entry *entry = find_entry(id);

if (entry == NULL) {
return -ENOENT;
}

memset(entry, 0, sizeof(*entry));
return 0;
}

ssize_t nvs_read(struct nvs_fs *fs, uint16_t id, void *data, size_t len)
{
ARG_UNUSED(fs);
struct fake_nvs_entry *entry = find_entry(id);

if (entry == NULL) {
return -ENOENT;
}

if (data == NULL && len == 0U) {
return (ssize_t)entry->len;
}

size_t copied_len = MIN(len, entry->len);
if (copied_len > 0U) {
memcpy(data, entry->data, copied_len);
}

return (ssize_t)copied_len;
}

static void setup_test(void)
{
memset(fake_entries, 0, sizeof(fake_entries));
zb_nvs_ready = true;
}

#define EXPECT_TRUE(cond) do { \
if (!(cond)) { \
fprintf(stderr, "FAIL: %s:%d: %s\n", __FILE__, __LINE__, #cond); \
return false; \
} \
} while (0)

#define EXPECT_EQ(actual, expected) EXPECT_TRUE((actual) == (expected))

static bool test_module_reset_isolated(void)
{
uint8_t ota_data[4] = { 1, 2, 3, 4 };
uint8_t aps_data[4] = { 5, 6, 7, 8 };
uint8_t readback[4] = { 0 };

setup_test();
EXPECT_EQ(nv_flashWriteNew(1, NV_MODULE_OTA, NV_ITEM_OTA_CODE,
 sizeof(ota_data), ota_data), NV_SUCC);
EXPECT_EQ(nv_flashWriteNew(1, NV_MODULE_APS, NV_ITEM_APS_SSIB,
 sizeof(aps_data), aps_data), NV_SUCC);

EXPECT_EQ(nv_resetModule(NV_MODULE_OTA), NV_SUCC);
EXPECT_EQ(nv_flashReadNew(1, NV_MODULE_OTA, NV_ITEM_OTA_CODE,
sizeof(readback), readback), NV_ITEM_NOT_FOUND);
EXPECT_EQ(nv_flashReadNew(1, NV_MODULE_APS, NV_ITEM_APS_SSIB,
sizeof(readback), readback), NV_SUCC);
EXPECT_TRUE(memcmp(readback, aps_data, sizeof(readback)) == 0);

return true;
}

static bool test_length_contract_rejects_short_registered_item(void)
{
uint8_t stored_data[8] = { 0 };
uint8_t readback[16] = { 0 };

setup_test();
nv_itemLengthCheckAdd(NV_ITEM_ZCL_SCENE_TABLE, sizeof(readback));
EXPECT_EQ(nv_flashWriteNew(1, NV_MODULE_ZCL, NV_ITEM_ZCL_SCENE_TABLE,
 sizeof(stored_data), stored_data), NV_SUCC);
EXPECT_EQ(nv_flashReadNew(1, NV_MODULE_ZCL, NV_ITEM_ZCL_SCENE_TABLE,
sizeof(stored_data), readback), NV_DATA_CHECK_ERROR);

return true;
}

static bool test_length_contract_rejects_short_requested_item(void)
{
uint8_t stored_data[8] = { 0 };
uint8_t readback[16] = { 0 };

setup_test();
EXPECT_EQ(nv_flashWriteNew(1, NV_MODULE_ZCL, NV_ITEM_ZCL_ON_OFF,
 sizeof(stored_data), stored_data), NV_SUCC);
EXPECT_EQ(nv_flashReadNew(1, NV_MODULE_ZCL, NV_ITEM_ZCL_ON_OFF,
sizeof(readback), readback), NV_DATA_CHECK_ERROR);

return true;
}

static bool test_read_by_index_does_not_alias_other_item(void)
{
uint8_t value[4] = { 9, 9, 9, 9 };
uint8_t readback[4] = { 0 };

setup_test();
EXPECT_EQ(nv_flashWriteNew(1, NV_MODULE_APS, NV_ITEM_APS_BINDING_TABLE,
 sizeof(value), value), NV_SUCC);
EXPECT_EQ(nv_flashReadByIndex(NV_MODULE_APS, NV_ITEM_APS_GROUP_TABLE,
 0, 1, sizeof(readback), readback), NV_ITEM_NOT_FOUND);

return true;
}

static bool test_delete_by_index_does_not_alias_other_item(void)
{
uint8_t value[4] = { 1, 3, 5, 7 };
uint8_t readback[4] = { 0 };

setup_test();
EXPECT_EQ(nv_flashWriteNew(1, NV_MODULE_APS, NV_ITEM_APS_BINDING_TABLE,
 sizeof(value), value), NV_SUCC);
EXPECT_EQ(nv_itemDeleteByIndex(NV_MODULE_APS, NV_ITEM_APS_GROUP_TABLE,
 0, 1), NV_ITEM_NOT_FOUND);
EXPECT_EQ(nv_flashReadNew(1, NV_MODULE_APS, NV_ITEM_APS_BINDING_TABLE,
sizeof(readback), readback), NV_SUCC);
EXPECT_TRUE(memcmp(readback, value, sizeof(readback)) == 0);

return true;
}

static bool test_read_delete_by_index_zero_behaves_like_item(void)
{
uint8_t value[4] = { 10, 11, 12, 13 };
uint8_t readback[4] = { 0 };

setup_test();
EXPECT_EQ(nv_flashWriteNew(1, NV_MODULE_APS, NV_ITEM_APS_BINDING_TABLE,
 sizeof(value), value), NV_SUCC);
EXPECT_EQ(nv_flashReadByIndex(NV_MODULE_APS, NV_ITEM_APS_BINDING_TABLE,
 0, 0, sizeof(readback), readback), NV_SUCC);
EXPECT_TRUE(memcmp(readback, value, sizeof(readback)) == 0);
EXPECT_EQ(nv_itemDeleteByIndex(NV_MODULE_APS, NV_ITEM_APS_BINDING_TABLE,
 0, 0), NV_SUCC);
EXPECT_EQ(nv_flashReadNew(1, NV_MODULE_APS, NV_ITEM_APS_BINDING_TABLE,
sizeof(readback), readback), NV_ITEM_NOT_FOUND);

return true;
}

int main(void)
{
struct {
const char *name;
bool (*fn)(void);
} tests[] = {
{ "module_reset_isolated", test_module_reset_isolated },
{ "length_contract_registered", test_length_contract_rejects_short_registered_item },
{ "length_contract_requested", test_length_contract_rejects_short_requested_item },
{ "read_by_index_no_alias", test_read_by_index_does_not_alias_other_item },
{ "delete_by_index_no_alias", test_delete_by_index_does_not_alias_other_item },
{ "index_zero_behavior", test_read_delete_by_index_zero_behaves_like_item },
};

int failed = 0;

for (size_t i = 0; i < ARRAY_SIZE(tests); i++) {
if (!tests[i].fn()) {
failed++;
fprintf(stderr, "Test failed: %s\n", tests[i].name);
}
}

if (failed > 0) {
fprintf(stderr, "%d test(s) failed\n", failed);
return EXIT_FAILURE;
}

printf("All %zu tests passed\n", ARRAY_SIZE(tests));
return EXIT_SUCCESS;
}
