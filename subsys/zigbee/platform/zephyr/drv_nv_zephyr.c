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
#include <zephyr/drivers/flash.h>
#include <zephyr/kernel.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/kvss/nvs.h>
#include <zephyr/zigbee/zb_types.h>
#include <errno.h>
#include "drv_nv.h"

#define NV_ITEM_LEN_CHK_TABLE_NUM 16

static struct nvs_fs zb_nvs;
static bool zb_nvs_ready;
static bool zb_nvs_init_attempted;
static bool zb_nvs_geometry_ready;
static bool zb_nvs_degraded_logged;
static u8 nv_item_len_chk_num;

typedef struct {
	u8 item_id;
	u16 len;
} nv_item_len_chk_t;

static nv_item_len_chk_t nv_item_len_chk_tbl[NV_ITEM_LEN_CHK_TABLE_NUM];
extern volatile u32 zb_nwk_ed_trace[];

static void zb_nvs_log_degraded(const char *reason, int rc)
{
	if (zb_nvs_degraded_logged) {
		return;
	}

	zb_nvs_degraded_logged = true;
	printk("zigbee NV degraded: %s (%d)\n", reason, rc);
}

static int zb_nvs_flash_area_id_get(void)
{
	if (CONFIG_ZIGBEE_NV_FLASH_AREA_ID >= 0) {
		return CONFIG_ZIGBEE_NV_FLASH_AREA_ID;
	}

#if FIXED_PARTITION_EXISTS(nvs_storage)
	return DT_FIXED_PARTITION_ID(DT_NODELABEL(nvs_storage));
#else
	return -ENOENT;
#endif
}

static int zb_nvs_geometry_init(void)
{
	const struct flash_area *fa;
	struct flash_pages_info page_info;
	int area_id;
	int rc;

	if (zb_nvs_geometry_ready) {
		return 0;
	}

	area_id = zb_nvs_flash_area_id_get();
	if (area_id < 0) {
		return area_id;
	}

	rc = flash_area_open((uint8_t)area_id, &fa);
	if (rc < 0) {
		return rc;
	}
	zb_nvs.flash_device = flash_area_get_device(fa);
	zb_nvs.offset       = fa->fa_off;
	rc = flash_get_page_info_by_offs(zb_nvs.flash_device, fa->fa_off, &page_info);
	if (rc < 0 || page_info.size == 0U) {
		flash_area_close(fa);
		return (rc < 0) ? rc : -EINVAL;
	}
	zb_nvs.sector_size = page_info.size;
	zb_nvs.sector_count = fa->fa_size / page_info.size;
	flash_area_close(fa);

	if (zb_nvs.sector_count == 0U) {
		return -EINVAL;
	}

	zb_nvs_geometry_ready = true;
	return 0;
}

static bool zb_nvs_partition_appears_blank(void)
{
	uint8_t sample[16];
	off_t tail_off;
	int rc;

	if (!zb_nvs_geometry_ready || zb_nvs.flash_device == NULL ||
	    zb_nvs.sector_count == 0U || zb_nvs.sector_size < sizeof(sample)) {
		return false;
	}

	for (uint16_t sector = 0U; sector < zb_nvs.sector_count; sector++) {
		off_t head_off = zb_nvs.offset + ((off_t)sector * zb_nvs.sector_size);

		rc = flash_read(zb_nvs.flash_device, head_off, sample, sizeof(sample));
		if (rc < 0) {
			return false;
		}
		for (size_t i = 0; i < sizeof(sample); i++) {
			if (sample[i] != 0xffU) {
				return false;
			}
		}

		tail_off = head_off + zb_nvs.sector_size - sizeof(sample);
		rc = flash_read(zb_nvs.flash_device, tail_off, sample, sizeof(sample));
		if (rc < 0) {
			return false;
		}
		for (size_t i = 0; i < sizeof(sample); i++) {
			if (sample[i] != 0xffU) {
				return false;
			}
		}
	}

	return true;
}

static bool zb_nvs_ensure_ready(void)
{
	int rc;

	zb_nwk_ed_trace[9] = 0xA7B00001U;
	if (zb_nvs_ready) {
		zb_nwk_ed_trace[9] = 0xA7B00007U;
		return true;
	}

	if (zb_nvs_init_attempted) {
		zb_nwk_ed_trace[9] = 0xA7B00008U;
		return false;
	}

	/*
	 * Keep mount retryable across a single boot. The vendor stack touches NV
	 * from multiple init paths; a transient early failure must not permanently
	 * poison all later restore attempts.
	 */
	zb_nvs_init_attempted = true;
	zb_nwk_ed_trace[9] = 0xA7B00002U;

	rc = zb_nvs_geometry_init();
	zb_nwk_ed_trace[8] = (u32)rc;
	zb_nwk_ed_trace[9] = 0xA7B00003U;
	if (rc < 0) {
		zb_nvs_init_attempted = false;
		if (rc == -ENOENT) {
			zb_nvs_log_degraded("flash area missing", rc);
		} else if (rc == -EINVAL) {
			zb_nvs_log_degraded("flash area too small", rc);
		} else {
			zb_nvs_log_degraded("flash geometry unavailable", rc);
		}
		return false;
	}

	zb_nwk_ed_trace[9] = 0xA7B00004U;
	if (zb_nvs_partition_appears_blank()) {
		zb_nwk_ed_trace[9] = 0xA7B00009U;
		zb_nvs_log_degraded("nvs blank, skip mount", 0);
		return false;
	}

	rc = nvs_mount(&zb_nvs);
	zb_nwk_ed_trace[7] = (u32)rc;
	zb_nwk_ed_trace[9] = 0xA7B00005U;
	if (rc == 0) {
		zb_nvs_ready = true;
		zb_nvs_degraded_logged = false;
		zb_nwk_ed_trace[9] = 0xA7B00006U;
		return true;
	}

	zb_nvs_init_attempted = false;
	zb_nvs_log_degraded("nvs mount failed", rc);
	return false;
}

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
	if (!zb_nvs_ensure_ready()) {
		return NV_NO_MEDIA;
	}
	int rc = nvs_write(&zb_nvs, nv_key((u8)id, itemId), buf, len);

	return rc >= 0 ? NV_SUCC : NV_NOT_ENOUGH_SAPCE;
}

nv_sts_t nv_flashReadNew(u8 single, u8 id, u8 itemId, u16 len, u8 *buf)
{
	ssize_t actual_len;
	ssize_t rc;
	u16 expected_len;
	bool trace_aps_group = (id == NV_MODULE_APS) && (itemId == NV_ITEM_APS_GROUP_TABLE);

	ARG_UNUSED(single);
	if (trace_aps_group) {
		zb_nwk_ed_trace[13] = 0xA7A00001U;
	}
	if (!zb_nvs_ensure_ready()) {
		if (trace_aps_group) {
			zb_nwk_ed_trace[13] = 0xA7A00002U;
		}
		return NV_NO_MEDIA;
	}
	expected_len = nv_item_expected_read_len(itemId, len);
	if (trace_aps_group) {
		zb_nwk_ed_trace[12] = expected_len;
		zb_nwk_ed_trace[13] = 0xA7A00003U;
	}
	actual_len = nvs_read(&zb_nvs, nv_key(id, itemId), NULL, 0);
	if (trace_aps_group) {
		zb_nwk_ed_trace[11] = (u32)actual_len;
		zb_nwk_ed_trace[13] = 0xA7A00004U;
	}

	if (actual_len == -ENOENT) {
		return NV_ITEM_NOT_FOUND;
	}
	if (actual_len < 0) {
		return NV_DATA_CHECK_ERROR;
	}
	if (actual_len < expected_len) {
		return NV_DATA_CHECK_ERROR;
	}
	rc = nvs_read(&zb_nvs, nv_key(id, itemId), buf, len);
	if (trace_aps_group) {
		zb_nwk_ed_trace[10] = (u32)rc;
		zb_nwk_ed_trace[13] = 0xA7A00005U;
	}

	if (rc == -ENOENT) {
		return NV_ITEM_NOT_FOUND;
	}
	if (rc < 0 || rc < len) {
		return NV_DATA_CHECK_ERROR;
	}

	return NV_SUCC;
}

nv_sts_t nv_flashSingleItemRemove(u8 id, u8 itemId, u16 len)
{
	ARG_UNUSED(len);
	if (!zb_nvs_ensure_ready()) {
		return NV_NO_MEDIA;
	}
	int rc = nvs_delete(&zb_nvs, nv_key(id, itemId));

	return rc == 0 ? NV_SUCC : NV_ITEM_NOT_FOUND;
}

nv_sts_t nv_flashSingleItemSizeGet(u8 id, u8 itemId, u16 *len)
{
	if (len == NULL || !zb_nvs_ensure_ready()) {
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
	if (!zb_nvs_ensure_ready()) {
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
	if (!zb_nvs_ensure_ready()) {
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
	if (!zb_nvs_ensure_ready()) {
		return NV_NO_MEDIA;
	}
	int rc = nvs_write(&zb_nvs,
			   nv_key(NV_MODULE_NWK_FRAME_COUNT, NV_ITEM_NWK_FRAME_COUNT),
			   &frameCount, sizeof(frameCount));

	return rc >= 0 ? NV_SUCC : NV_NOT_ENOUGH_SAPCE;
}

nv_sts_t nv_nwkFrameCountFromFlash(u32 *frameCount)
{
	if (frameCount == NULL || !zb_nvs_ensure_ready()) {
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
	if (!zb_nvs_ensure_ready()) {
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

	if (!zb_nvs_ensure_ready()) {
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
