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
#include "drv_nv.h"

#define NVS_PARTITION_LABEL  nvs_storage
#define NVS_PARTITION_ID     FIXED_PARTITION_ID(NVS_PARTITION_LABEL)

static struct nvs_fs zb_nvs;
static bool zb_nvs_ready;

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
	ARG_UNUSED(single);
	if (!zb_nvs_ready) {
		return NV_NO_MEDIA;
	}
	int rc = nvs_read(&zb_nvs, nv_key(id, itemId), buf, len);

	if (rc == -ENOENT) {
		return NV_ITEM_NOT_FOUND;
	}
	return rc >= 0 ? NV_SUCC : NV_DATA_CHECK_ERROR;
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
	ARG_UNUSED(modules);
	/* Module-granularity erase not supported; clear all */
	return nv_resetAll();
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
	ARG_UNUSED(opSect);
	/* Encode index into key high byte for now */
	uint16_t key = (uint16_t)(((u16)id << 8) | itemId) + opIdx;

	if (!zb_nvs_ready) {
		return NV_NO_MEDIA;
	}
	int rc = nvs_read(&zb_nvs, key, buf, len);

	return rc >= 0 ? NV_SUCC : NV_ITEM_NOT_FOUND;
}

nv_sts_t nv_itemDeleteByIndex(u8 id, u8 itemId, u8 opSect, u16 opIdx)
{
	ARG_UNUSED(opSect);
	uint16_t key = (uint16_t)(((u16)id << 8) | itemId) + opIdx;

	if (!zb_nvs_ready) {
		return NV_NO_MEDIA;
	}
	int rc = nvs_delete(&zb_nvs, key);

	return rc == 0 ? NV_SUCC : NV_ITEM_NOT_FOUND;
}

void nv_itemLengthCheckAdd(u8 itemId, u16 len)
{
	ARG_UNUSED(itemId);
	ARG_UNUSED(len);
}
