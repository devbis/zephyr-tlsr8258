/* SPDX-License-Identifier: Apache-2.0 */
/*
 * NV storage backed by Zephyr NVS.
 *
 * SDK function signatures (from drv_nv.h):
 *   nv_flashWriteNew(u8 single, u16 id, u8 itemId, u16 len, u8 *buf)
 *   nv_flashReadNew(u8 single, u8 id, u8 itemId, u16 len, u8 *buf)
 *
 * NVS key encoding: (id << 8) | itemId  (fits in uint16_t)
 */
#include <zephyr/kernel.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/kvss/nvs.h>
#include <zephyr/zigbee/zb_types.h>
#include <errno.h>
#include "drv_nv.h"

#define NVS_PARTITION_LABEL  nvs_storage
#define NVS_PARTITION_ID     FIXED_PARTITION_ID(NVS_PARTITION_LABEL)
#define NV_ITEM_LEN_CHK_TABLE_NUM 16

static struct nvs_fs zb_nvs;
static bool zb_nvs_ready;
static u8 nv_item_len_chk_num;

typedef struct {
	u8 item_id;
	u16 len;
} nv_item_len_chk_t;

static nv_item_len_chk_t nv_item_len_chk_tbl[NV_ITEM_LEN_CHK_TABLE_NUM];

static int zb_nvs_init(void)
{
	const struct flash_area *fa;
	int rc;

	rc = flash_area_open(NVS_PARTITION_ID, &fa);
	if (rc < 0) {
		return rc;
	}
	zb_nvs.flash_device = flash_area_get_device(fa);
	zb_nvs.offset       = fa->fa_off;
	zb_nvs.sector_size  = 4096;
	zb_nvs.sector_count = CONFIG_ZIGBEE_NV_SECTOR_COUNT;
	flash_area_close(fa);

	rc = nvs_mount(&zb_nvs);
	if (rc == 0) {
		zb_nvs_ready = true;
	}
	return rc;
}
SYS_INIT(zb_nvs_init, APPLICATION, 90);

static inline uint16_t nv_key(u8 id, u8 itemId)
{
	return (uint16_t)((id << 8) | itemId);
}

static u16 nv_item_expected_read_len(u8 itemId, u16 requested_len)
{
	u16 expected_len = requested_len;

	for (u8 i = 0; i < nv_item_len_chk_num; i++) {
		if (nv_item_len_chk_tbl[i].item_id == itemId &&
		    nv_item_len_chk_tbl[i].len > expected_len) {
			expected_len = nv_item_len_chk_tbl[i].len;
			break;
		}
	}

	return expected_len;
}

static nv_sts_t nv_clear_module_items(u8 module_id)
{
	int rc;

	for (u16 item_id = 0; item_id <= UINT8_MAX; item_id++) {
		rc = nvs_delete(&zb_nvs, nv_key(module_id, (u8)item_id));
		if (rc < 0 && rc != -ENOENT) {
			return NV_INVALID_MODULS;
		}
	}

	return NV_SUCC;
}

static bool nv_index_key_supported(u8 opSect, u16 opIdx)
{
	return (opSect == 0U) && (opIdx == 0U);
}

nv_sts_t nv_flashWriteNew(u8 single, u16 id, u8 itemId, u16 len, u8 *buf)
{
	ARG_UNUSED(single);
	if (!zb_nvs_ready) {
		return NV_NO_MEDIA;
	}
	int rc = nvs_write(&zb_nvs, nv_key((u8)id, itemId), buf, len);

	return rc >= 0 ? NV_SUCC : NV_NOT_ENOUGH_SAPCE;
}

nv_sts_t nv_flashReadNew(u8 single, u8 id, u8 itemId, u16 len, u8 *buf)
{
	ssize_t rc;
	u16 expected_len;

	ARG_UNUSED(single);
	if (!zb_nvs_ready) {
		return NV_NO_MEDIA;
	}
	expected_len = nv_item_expected_read_len(itemId, len);
	rc = nvs_read(&zb_nvs, nv_key(id, itemId), NULL, 0);

	if (rc == -ENOENT) {
		return NV_ITEM_NOT_FOUND;
	}
	if (rc < 0) {
		return NV_DATA_CHECK_ERROR;
	}
	if (expected_len > len) {
		return NV_DATA_CHECK_ERROR;
	}
	if (rc < expected_len) {
		return NV_DATA_CHECK_ERROR;
	}
	rc = nvs_read(&zb_nvs, nv_key(id, itemId), buf, len);

	if (rc == -ENOENT) {
		return NV_ITEM_NOT_FOUND;
	}
	return rc >= expected_len ? NV_SUCC : NV_DATA_CHECK_ERROR;
}

nv_sts_t nv_flashSingleItemRemove(u8 id, u8 itemId, u16 len)
{
	ARG_UNUSED(len);
	if (!zb_nvs_ready) {
		return NV_NO_MEDIA;
	}
	int rc = nvs_delete(&zb_nvs, nv_key(id, itemId));

	return rc == 0 ? NV_SUCC : NV_ITEM_NOT_FOUND;
}

nv_sts_t nv_flashSingleItemSizeGet(u8 id, u8 itemId, u16 *len)
{
	if (!zb_nvs_ready || len == NULL) {
		return NV_NO_MEDIA;
	}
	ssize_t rc = nvs_read(&zb_nvs, nv_key(id, itemId), NULL, 0);

	if (rc < 0) {
		return NV_ITEM_NOT_FOUND;
	}
	*len = (u16)rc;
	return NV_SUCC;
}

nv_sts_t nv_resetAll(void)
{
	if (!zb_nvs_ready) {
		return NV_NO_MEDIA;
	}
	int rc = nvs_clear(&zb_nvs);

	/* Re-mount after clear */
	if (rc == 0) {
		nvs_mount(&zb_nvs);
	}
	return rc == 0 ? NV_SUCC : NV_INVALID_MODULS;
}

nv_sts_t nv_resetModule(u8 modules)
{
	if (!zb_nvs_ready) {
		return NV_NO_MEDIA;
	}
	if (modules >= NV_MAX_MODULS) {
		return NV_INVALID_MODULS;
	}

	return nv_clear_module_items(modules);
}

nv_sts_t nv_resetToFactoryNew(void)
{
	return nv_resetAll();
}

/* Frame counter stored in dedicated NVS entry */
nv_sts_t nv_nwkFrameCountSaveToFlash(u32 frameCount)
{
	if (!zb_nvs_ready) {
		return NV_NO_MEDIA;
	}
	int rc = nvs_write(&zb_nvs,
			   nv_key(NV_MODULE_NWK_FRAME_COUNT, NV_ITEM_NWK_FRAME_COUNT),
			   &frameCount, sizeof(frameCount));

	return rc >= 0 ? NV_SUCC : NV_NOT_ENOUGH_SAPCE;
}

nv_sts_t nv_nwkFrameCountFromFlash(u32 *frameCount)
{
	if (!zb_nvs_ready || frameCount == NULL) {
		return NV_NO_MEDIA;
	}
	int rc = nvs_read(&zb_nvs,
			  nv_key(NV_MODULE_NWK_FRAME_COUNT, NV_ITEM_NWK_FRAME_COUNT),
			  frameCount, sizeof(*frameCount));

	if (rc == -ENOENT) {
		*frameCount = 0;
		return NV_ITEM_NOT_FOUND;
	}
	return rc >= 0 ? NV_SUCC : NV_DATA_CHECK_ERROR;
}

/* Stub for multi-item read-by-index (used by address table, binding) */
nv_sts_t nv_flashReadByIndex(u8 id, u8 itemId, u8 opSect, u16 opIdx,
			      u16 len, u8 *buf)
{
	if (!zb_nvs_ready) {
		return NV_NO_MEDIA;
	}
	if (!nv_index_key_supported(opSect, opIdx)) {
		return NV_ITEM_NOT_FOUND;
	}

	return nv_flashReadNew(1, id, itemId, len, buf);
}

nv_sts_t nv_itemDeleteByIndex(u8 id, u8 itemId, u8 opSect, u16 opIdx)
{
	int rc;

	if (!zb_nvs_ready) {
		return NV_NO_MEDIA;
	}
	if (!nv_index_key_supported(opSect, opIdx)) {
		return NV_ITEM_NOT_FOUND;
	}
	rc = nvs_delete(&zb_nvs, nv_key(id, itemId));

	return rc == 0 ? NV_SUCC : NV_ITEM_NOT_FOUND;
}

void nv_itemLengthCheckAdd(u8 itemId, u16 len)
{
	for (u8 i = 0; i < nv_item_len_chk_num; i++) {
		if (nv_item_len_chk_tbl[i].item_id == itemId) {
			nv_item_len_chk_tbl[i].len = len;
			return;
		}
	}
	if (nv_item_len_chk_num >= NV_ITEM_LEN_CHK_TABLE_NUM) {
		return;
	}

	nv_item_len_chk_tbl[nv_item_len_chk_num].item_id = itemId;
	nv_item_len_chk_tbl[nv_item_len_chk_num].len = len;
	nv_item_len_chk_num++;
}
