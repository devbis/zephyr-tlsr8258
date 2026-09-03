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
#include <zephyr/kvss/nvs.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/crc.h>
#include <zephyr/zigbee/zb_types.h>
#include <errno.h>
#include "drv_nv.h"

/* Vendor factory-address globals remain part of the public driver ABI. The
 * Zephyr NV backend resolves storage through fixed partitions instead of
 * these absolute addresses, but the libzigbee headers still reference them.
 */
u32 g_u32MacFlashAddr;
u32 g_u32CfgFlashAddr;

#if !FIXED_PARTITION_EXISTS(zigbee_nv_partition)
#error "Zigbee requires a fixed partition labeled zigbee_nv_partition"
#endif

#define NV_ITEM_LEN_CHK_TABLE_NUM 16
#define NV_INDEX_KEY_BASE         0x8000U
#define NV_INDEX_MAGIC            0xa7U
#define NV_INDEX_MAX              UINT8_MAX
#define NV_INDEX_HEADER_LEN       4U
#ifndef CONFIG_ZIGBEE_NV_INDEX_VALUE_MAX
#define CONFIG_ZIGBEE_NV_INDEX_VALUE_MAX 256U
#endif

static struct nvs_fs zb_nvs;
static bool zb_nvs_ready;
static bool zb_nvs_init_attempted;
static bool zb_nvs_geometry_ready;
static bool zb_nvs_degraded_logged;
static bool zb_nvs_blank_partition;
static u8 nv_item_len_chk_num;

typedef struct {
	u8 item_id;
	u16 len;
} nv_item_len_chk_t;

struct zb_nvs_ate {
	uint16_t id;
	uint16_t offset;
	uint16_t len;
	uint8_t part;
	uint8_t crc8;
} __packed;

static nv_item_len_chk_t nv_item_len_chk_tbl[NV_ITEM_LEN_CHK_TABLE_NUM];
/* Indexed vendor records are small (neighbor/key-pair/timeout entries), but
 * are not singleton values. Keep one bounded scratch record so the adapter
 * can strip its metadata before copying into the caller's payload buffer. */
static u8 nv_index_scratch[NV_INDEX_HEADER_LEN +
				  CONFIG_ZIGBEE_NV_INDEX_VALUE_MAX];

static u16 nv_item_expected_read_len(u8 itemId, u16 requested_len);

static void zb_nvs_log_degraded(const char *reason, int rc)
{
	if (zb_nvs_degraded_logged) {
		return;
	}

	zb_nvs_degraded_logged = true;
	printk("zigbee NV degraded: %s (%d)\n", reason, rc);
}

static const struct device *zb_nvs_flash_device_get(void)
{
	return FIXED_PARTITION_DEVICE(zigbee_nv_partition);
}

static int zb_nvs_geometry_init(void)
{
	const struct device *flash_device;
	struct flash_pages_info page_info;
	size_t partition_size;
	off_t partition_offset;
	int rc;

	if (zb_nvs_geometry_ready) {
		return 0;
	}

	flash_device = zb_nvs_flash_device_get();
	if (!device_is_ready(flash_device)) {
		return -ENODEV;
	}

	partition_offset = FIXED_PARTITION_OFFSET(zigbee_nv_partition);
	partition_size = FIXED_PARTITION_SIZE(zigbee_nv_partition);
	zb_nvs.flash_device = flash_device;
	zb_nvs.offset = partition_offset;

	rc = flash_get_page_info_by_offs(zb_nvs.flash_device, partition_offset, &page_info);
	if (rc < 0 || page_info.size == 0U) {
		return (rc < 0) ? rc : -EINVAL;
	}
	zb_nvs.sector_size = page_info.size;
	zb_nvs.sector_count = partition_size / page_info.size;

	if (zb_nvs.sector_count == 0U) {
		return -EINVAL;
	}

	zb_nvs_geometry_ready = true;
	return 0;
}

static bool zb_nvs_partition_appears_blank(void)
{
	const struct flash_parameters *params;
	uint8_t sample[16];
	uint8_t erase_value;
	off_t tail_off;
	int rc;

	if (!zb_nvs_geometry_ready || zb_nvs.flash_device == NULL ||
	    zb_nvs.sector_count == 0U || zb_nvs.sector_size < sizeof(sample)) {
		return false;
	}

	params = flash_get_parameters(zb_nvs.flash_device);
	erase_value = (params != NULL) ? params->erase_value : 0xffU;

	for (uint16_t sector = 0U; sector < zb_nvs.sector_count; sector++) {
		off_t head_off = zb_nvs.offset + ((off_t)sector * zb_nvs.sector_size);

		rc = flash_read(zb_nvs.flash_device, head_off, sample, sizeof(sample));
		if (rc < 0) {
			return false;
		}
		for (size_t i = 0; i < sizeof(sample); i++) {
			if (sample[i] != erase_value) {
				return false;
			}
		}

		tail_off = head_off + zb_nvs.sector_size - sizeof(sample);
		rc = flash_read(zb_nvs.flash_device, tail_off, sample, sizeof(sample));
		if (rc < 0) {
			return false;
		}
		for (size_t i = 0; i < sizeof(sample); i++) {
			if (sample[i] != erase_value) {
				return false;
			}
		}
	}

	return true;
}

static int zb_nvs_blank_partition_init(void)
{
	struct zb_nvs_ate gc_done_ate = {
		.id = 0xffffU,
		.offset = 0U,
		.len = 0U,
		.part = 0xffU,
	};
	off_t marker_off;

	gc_done_ate.crc8 = crc8_ccitt(0xff, &gc_done_ate,
				      offsetof(struct zb_nvs_ate, crc8));
	marker_off = zb_nvs.offset + zb_nvs.sector_size -
		     (2 * (off_t)sizeof(gc_done_ate));

	return flash_write(zb_nvs.flash_device, marker_off, &gc_done_ate,
			   sizeof(gc_done_ate));
}

static bool zb_nvs_ensure_ready(void)
{
	int rc;

	if (zb_nvs_ready) {
		return true;
	}

	if (zb_nvs_blank_partition) {
		return true;
	}

	if (zb_nvs_init_attempted) {
		return false;
	}

	/*
	 * Keep mount retryable across a single boot. The vendor stack touches NV
	 * from multiple init paths; a transient early failure must not permanently
	 * poison all later restore attempts.
	 */
	zb_nvs_init_attempted = true;

	rc = zb_nvs_geometry_init();
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

	if (zb_nvs_partition_appears_blank()) {
		zb_nvs_blank_partition = true;
		zb_nvs_init_attempted = false;
		return true;
	}
	rc = nvs_mount(&zb_nvs);
	if (rc == 0) {
		zb_nvs_ready = true;
		zb_nvs_degraded_logged = false;
		return true;
	}

	zb_nvs_init_attempted = false;
	zb_nvs_log_degraded("nvs mount failed", rc);
	return false;
}

static bool zb_nvs_ensure_writable(void)
{
	int rc;

	if (!zb_nvs_ensure_ready()) {
		return false;
	}

	if (!zb_nvs_blank_partition) {
		return true;
	}

	rc = zb_nvs_blank_partition_init();
	if (rc < 0) {
		zb_nvs_log_degraded("nvs deferred blank init failed", rc);
		return false;
	}

	rc = nvs_mount(&zb_nvs);
	if (rc < 0) {
		zb_nvs_log_degraded("nvs deferred mount failed", rc);
		return false;
	}

	zb_nvs_blank_partition = false;
	zb_nvs_ready = true;
	zb_nvs_init_attempted = false;
	zb_nvs_degraded_logged = false;
	return true;
}

static inline uint16_t nv_key(u8 id, u8 itemId)
{
	return (uint16_t)((id << 8) | itemId);
}

static inline uint16_t nv_index_key(u8 id, u8 opIdx)
{
	return (uint16_t)(NV_INDEX_KEY_BASE | ((u16)id << 8) | opIdx);
}

static bool nv_index_record_read(u8 id, u8 itemId, u8 opIdx, u16 len,
					 u8 *buf)
{
	ssize_t stored_len;
	ssize_t read_len;
	u16 expected_len;

	stored_len = nvs_read(&zb_nvs, nv_index_key(id, opIdx), NULL, 0);
	if (stored_len < (ssize_t)NV_INDEX_HEADER_LEN ||
	    stored_len > (ssize_t)sizeof(nv_index_scratch)) {
		return false;
	}

	read_len = nvs_read(&zb_nvs, nv_index_key(id, opIdx),
				    nv_index_scratch, (size_t)stored_len);
	if (read_len != stored_len ||
	    nv_index_scratch[0] != NV_INDEX_MAGIC ||
	    nv_index_scratch[1] != itemId) {
		return false;
	}

	stored_len = (ssize_t)nv_index_scratch[2] |
			     ((ssize_t)nv_index_scratch[3] << 8);
	expected_len = nv_item_expected_read_len(itemId, len);
	if (expected_len > len || stored_len < expected_len || stored_len < len ||
	    stored_len > (ssize_t)(sizeof(nv_index_scratch) -
				    NV_INDEX_HEADER_LEN)) {
		return false;
	}

	if (buf != NULL && len != 0U) {
		memcpy(buf, &nv_index_scratch[NV_INDEX_HEADER_LEN], len);
	}

	return true;
}

static bool nv_index_header_read(u8 id, u8 opIdx, u8 *item_id)
{
	ssize_t stored_len;
	ssize_t read_len;

	stored_len = nvs_read(&zb_nvs, nv_index_key(id, opIdx), NULL, 0);
	if (stored_len < (ssize_t)NV_INDEX_HEADER_LEN ||
	    stored_len > (ssize_t)sizeof(nv_index_scratch)) {
		return false;
	}

	read_len = nvs_read(&zb_nvs, nv_index_key(id, opIdx),
				    nv_index_scratch, (size_t)stored_len);
	if (read_len != stored_len ||
	    nv_index_scratch[0] != NV_INDEX_MAGIC) {
		return false;
	}

	if (item_id != NULL) {
		*item_id = nv_index_scratch[1];
	}

	return ((u16)nv_index_scratch[2] |
		((u16)nv_index_scratch[3] << 8)) ==
	       (u16)(stored_len - NV_INDEX_HEADER_LEN);
}

static bool nv_index_latest(u8 id, u8 item_id, u8 *op_idx)
{
	bool found = false;

	for (u16 i = 0U; i <= NV_INDEX_MAX; i++) {
		u8 stored_item;

		if (!nv_index_header_read(id, (u8)i, &stored_item) ||
		    (item_id != ITEM_FIELD_IDLE && stored_item != item_id)) {
			continue;
		}

		if (op_idx != NULL) {
			*op_idx = (u8)i;
		}
		found = true;
	}

	return found;
}

static bool nv_index_free_slot(u8 id, u8 *op_idx)
{
	for (u16 i = 0U; i <= NV_INDEX_MAX; i++) {
		ssize_t len = nvs_read(&zb_nvs, nv_index_key(id, (u8)i), NULL, 0);

		if (len == -ENOENT) {
			*op_idx = (u8)i;
			return true;
		}
	}

	return false;
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

		rc = nvs_delete(&zb_nvs, nv_index_key(module_id, (u8)item_id));
		if (rc < 0 && rc != -ENOENT) {
			return NV_INVALID_MODULS;
		}
	}

	return NV_SUCC;
}

nv_sts_t nv_flashWriteNew(u8 single, u16 id, u8 itemId, u16 len, u8 *buf)
{
	u8 op_idx;
	ssize_t written;

	if (!zb_nvs_ensure_writable()) {
		return NV_NO_MEDIA;
	}
	if (id >= NV_MAX_MODULS || buf == NULL || len == 0U) {
		return NV_INVALID_ID;
	}

	if (single) {
		written = nvs_write(&zb_nvs, nv_key((u8)id, itemId), buf, len);
		return written >= 0 ? NV_SUCC : NV_NOT_ENOUGH_SAPCE;
	}

	if (len > CONFIG_ZIGBEE_NV_INDEX_VALUE_MAX ||
	    !nv_index_free_slot((u8)id, &op_idx)) {
		return NV_NOT_ENOUGH_SAPCE;
	}

	nv_index_scratch[0] = NV_INDEX_MAGIC;
	nv_index_scratch[1] = itemId;
	nv_index_scratch[2] = (u8)len;
	nv_index_scratch[3] = (u8)(len >> 8);
	memcpy(&nv_index_scratch[NV_INDEX_HEADER_LEN], buf, len);
	written = nvs_write(&zb_nvs, nv_index_key((u8)id, op_idx),
				    nv_index_scratch, len + NV_INDEX_HEADER_LEN);

	return written >= 0 ? NV_SUCC : NV_NOT_ENOUGH_SAPCE;
}

nv_sts_t nv_flashReadNew(u8 single, u8 id, u8 itemId, u16 len, u8 *buf)
{
	ssize_t actual_len;
	ssize_t rc;
	u16 expected_len;

	if (!zb_nvs_ensure_ready()) {
		return NV_NO_MEDIA;
	}
	if (zb_nvs_blank_partition) {
		return NV_ITEM_NOT_FOUND;
	}

	if (!single && itemId == ITEM_FIELD_IDLE) {
		itemIfno_t *info = (itemIfno_t *)buf;
		u8 op_idx;

		if (info == NULL || len < sizeof(*info) ||
		    !nv_index_latest(id, ITEM_FIELD_IDLE, &op_idx)) {
			return NV_ITEM_NOT_FOUND;
		}
		info->opSect = 0U;
		info->opIndex = op_idx;
		return NV_SUCC;
	}

	expected_len = nv_item_expected_read_len(itemId, len);
	if (expected_len > len) {
		return NV_DATA_CHECK_ERROR;
	}
	actual_len = nvs_read(&zb_nvs, nv_key(id, itemId), NULL, 0);

	if (actual_len == -ENOENT) {
		u8 op_idx;

		if (!nv_index_latest(id, itemId, &op_idx) ||
		    !nv_index_record_read(id, itemId, op_idx, len, buf)) {
			return NV_ITEM_NOT_FOUND;
		}
		return NV_SUCC;
	}
	if (actual_len < 0) {
		return NV_DATA_CHECK_ERROR;
	}
	if (actual_len < expected_len) {
		return NV_DATA_CHECK_ERROR;
	}
	rc = nvs_read(&zb_nvs, nv_key(id, itemId), buf, len);

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
	u8 op_idx;

	if (!zb_nvs_ensure_ready()) {
		return NV_NO_MEDIA;
	}
	if (zb_nvs_blank_partition) {
		return NV_ITEM_NOT_FOUND;
	}
	int rc = nvs_delete(&zb_nvs, nv_key(id, itemId));
	if (rc == -ENOENT && nv_index_latest(id, itemId, &op_idx)) {
		rc = nvs_delete(&zb_nvs, nv_index_key(id, op_idx));
	}

	return rc == 0 ? NV_SUCC : NV_ITEM_NOT_FOUND;
}

nv_sts_t nv_flashSingleItemSizeGet(u8 id, u8 itemId, u16 *len)
{
	u8 op_idx;

	if (len == NULL || !zb_nvs_ensure_ready()) {
		return NV_NO_MEDIA;
	}
	if (zb_nvs_blank_partition) {
		return NV_ITEM_NOT_FOUND;
	}
	ssize_t rc = nvs_read(&zb_nvs, nv_key(id, itemId), NULL, 0);

	if (rc < 0) {
		if (!nv_index_latest(id, itemId, &op_idx) ||
		    !nv_index_header_read(id, op_idx, NULL)) {
			return NV_ITEM_NOT_FOUND;
		}
		rc = (ssize_t)nv_index_scratch[2] |
			 ((ssize_t)nv_index_scratch[3] << 8);
	}
	*len = (u16)rc;
	return NV_SUCC;
}

nv_sts_t nv_resetAll(void)
{
	if (!zb_nvs_ensure_ready()) {
		return NV_NO_MEDIA;
	}
	if (zb_nvs_blank_partition) {
		return NV_SUCC;
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
	if (zb_nvs_blank_partition) {
		return NV_SUCC;
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
	if (!zb_nvs_ensure_writable()) {
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
	if (zb_nvs_blank_partition) {
		*frameCount = 0;
		return NV_ITEM_NOT_FOUND;
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

nv_sts_t nv_flashReadByIndex(u8 id, u8 itemId, u8 opSect, u16 opIdx,
			      u16 len, u8 *buf)
{
	if (opSect != 0U || opIdx > NV_INDEX_MAX) {
		return NV_ITEM_NOT_FOUND;
	}
	if (!zb_nvs_ensure_ready()) {
		return NV_NO_MEDIA;
	}
	if (zb_nvs_blank_partition) {
		return NV_ITEM_NOT_FOUND;
	}

	return nv_index_record_read(id, itemId, (u8)opIdx, len, buf) ?
		NV_SUCC : NV_ITEM_NOT_FOUND;
}

nv_sts_t nv_itemDeleteByIndex(u8 id, u8 itemId, u8 opSect, u16 opIdx)
{
	int rc;

	if (opSect != 0U || opIdx > NV_INDEX_MAX) {
		return NV_ITEM_NOT_FOUND;
	}
	if (!zb_nvs_ensure_ready()) {
		return NV_NO_MEDIA;
	}
	if (zb_nvs_blank_partition) {
		return NV_ITEM_NOT_FOUND;
	}
	u8 stored_item;
	if (!nv_index_header_read(id, (u8)opIdx, &stored_item) ||
	    stored_item != itemId) {
		return NV_ITEM_NOT_FOUND;
	}
	rc = nvs_delete(&zb_nvs, nv_index_key(id, (u8)opIdx));

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
