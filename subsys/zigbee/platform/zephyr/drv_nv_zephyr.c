/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * NV storage backed by Zephyr NVS.
 *
 * Single items are stored under the key (id << 8) | itemId. Indexed items
 * (address map, key pairs, end device timeouts) are stored under
 * NV_INDEX_KEY_BASE | (id << 8) | index, with a header carrying the item id
 * and the payload length. Indexed records are appended after the highest
 * index in use, so a higher index is always a newer record, which is the
 * ordering the vendor NV layer gives its callers.
 */
#include <zephyr/drivers/flash.h>
#include <zephyr/kernel.h>
#include <zephyr/kvss/nvs.h>
#include <zephyr/logging/log.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/zigbee/zb_types.h>
#include <errno.h>
#include "drv_nv.h"

/*
 * Vendor factory-address globals remain part of the public driver ABI. The
 * Zephyr NV backend resolves storage through fixed partitions instead of
 * these absolute addresses, but the libzigbee headers still reference them.
 */
u32 g_u32MacFlashAddr;
u32 g_u32CfgFlashAddr;

/*
 * The Zigbee NVS volume owns its whole partition. storage_partition is only
 * a fallback for boards without a zigbee_nv_partition, and only when no
 * other storage backend claims it.
 */
#if FIXED_PARTITION_EXISTS(zigbee_nv_partition)
#define ZB_NV_PARTITION DT_NODELABEL(zigbee_nv_partition)
#elif FIXED_PARTITION_EXISTS(storage_partition)
#if defined(CONFIG_SETTINGS_NVS) || defined(CONFIG_SETTINGS_ZMS) || defined(CONFIG_SETTINGS_FCB)
#error "storage_partition is used by settings; add a zigbee_nv_partition"
#endif
#define ZB_NV_PARTITION DT_NODELABEL(storage_partition)
#else
#error "Zigbee requires a zigbee_nv_partition or storage_partition"
#endif

#define NV_ITEM_LEN_CHK_TABLE_NUM 16
#define NV_INDEX_KEY_BASE         0x8000U
#define NV_INDEX_MAGIC            0xa7U
#define NV_INDEX_MAX              UINT8_MAX
#define NV_INDEX_HEADER_LEN       4U
/*
 * Largest indexed record payload: ss_dev_pair_set_t and the address map
 * entries are well below this.
 */
#define NV_INDEX_VALUE_MAX        256U
/* nv_index_top[] value of a module without indexed records. */
#define NV_INDEX_TOP_EMPTY        (-1)

LOG_MODULE_DECLARE(zigbee, CONFIG_ZIGBEE_LOG_LEVEL);

static struct nvs_fs zb_nvs;
static bool zb_nvs_ready;
static bool zb_nvs_init_attempted;
static bool zb_nvs_geometry_ready;
static bool zb_nvs_degraded_logged;
static bool zb_nvs_mount_failed;
static u8 nv_item_len_chk_num;
/*
 * Highest index in use per module, valid when its bit in nv_index_top_known
 * is set.
 */
static int16_t nv_index_top[NV_MAX_MODULS];
static u8 nv_index_top_known;

BUILD_ASSERT(NV_MAX_MODULS <= 8, "nv_index_top_known holds one bit per module");

typedef struct {
	u8 item_id;
	u16 len;
} nv_item_len_chk_t;

static nv_item_len_chk_t nv_item_len_chk_tbl[NV_ITEM_LEN_CHK_TABLE_NUM];
/*
 * Indexed vendor records are small (neighbor/key-pair/timeout entries), but
 * are not singleton values. Keep one bounded scratch record so the adapter
 * can strip its metadata before copying into the caller's payload buffer.
 */
static u8 nv_index_scratch[NV_INDEX_HEADER_LEN + NV_INDEX_VALUE_MAX];

static void nv_index_top_invalidate_all(void)
{
	nv_index_top_known = 0U;
}

static void nv_index_top_invalidate(u8 id)
{
	nv_index_top_known &= (u8)~BIT(id);
}

static void nv_index_top_set(u8 id, int16_t top)
{
	nv_index_top[id] = top;
	nv_index_top_known |= (u8)BIT(id);
}

void zb_platform_persistence_runtime_reset(void)
{
	zb_nvs_ready = false;
	zb_nvs_init_attempted = false;
	zb_nvs_geometry_ready = false;
	zb_nvs_degraded_logged = false;
	zb_nvs_mount_failed = false;
	nv_index_top_invalidate_all();
}

bool zb_platform_persistence_can_write(void)
{
	return zb_nvs_ready;
}

static u16 nv_item_expected_read_len(u8 itemId, u16 requested_len);

static void zb_nvs_log_degraded(const char *reason, int rc)
{
	if (zb_nvs_degraded_logged) {
		return;
	}

	zb_nvs_degraded_logged = true;
	LOG_ERR("NV unavailable: %s (%d)", reason, rc);
}

static const struct device *zb_nvs_flash_device_get(void)
{
	return PARTITION_NODE_DEVICE(ZB_NV_PARTITION);
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

	partition_offset = PARTITION_NODE_OFFSET(ZB_NV_PARTITION);
	partition_size = PARTITION_NODE_SIZE(ZB_NV_PARTITION);
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

static bool zb_nvs_ensure_ready(void)
{
	int rc;

	if (zb_nvs_ready) {
		return true;
	}

	if (zb_nvs_init_attempted || zb_nvs_mount_failed) {
		return false;
	}

	zb_nvs_init_attempted = true;

	rc = zb_nvs_geometry_init();
	if (rc < 0) {
		/*
		 * Keep the mount retryable: the stack touches NV from several
		 * init paths and an early failure must not poison later ones.
		 */
		zb_nvs_init_attempted = false;
		zb_nvs_log_degraded("flash geometry unavailable", rc);
		return false;
	}

	/*
	 * NVS mounts an erased partition, so a failure means the partition
	 * holds data NVS cannot parse. Never format it here: it may still hold
	 * the network key and frame counter. Only an explicit factory reset
	 * (nv_resetAll()) erases it.
	 */
	rc = nvs_mount(&zb_nvs);
	if (rc < 0) {
		zb_nvs_mount_failed = true;
		zb_nvs_log_degraded("mount failed, factory reset required", rc);
		return false;
	}

	zb_nvs_ready = true;
	zb_nvs_degraded_logged = false;
	nv_index_top_invalidate_all();
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

static bool nv_index_record_read(u8 id, u8 itemId, u8 opIdx, u16 len, u8 *buf)
{
	ssize_t stored_len;
	u16 payload_len;
	u16 expected_len;

	stored_len = nvs_read(&zb_nvs, nv_index_key(id, opIdx), nv_index_scratch,
			      sizeof(nv_index_scratch));
	if (stored_len < (ssize_t)NV_INDEX_HEADER_LEN ||
	    stored_len > (ssize_t)sizeof(nv_index_scratch) ||
	    nv_index_scratch[0] != NV_INDEX_MAGIC || nv_index_scratch[1] != itemId) {
		return false;
	}

	payload_len = (u16)nv_index_scratch[2] | ((u16)nv_index_scratch[3] << 8);
	expected_len = nv_item_expected_read_len(itemId, len);
	if (payload_len != (u16)(stored_len - NV_INDEX_HEADER_LEN) || expected_len > len ||
	    payload_len < len) {
		return false;
	}

	if (buf != NULL && len != 0U) {
		memcpy(buf, &nv_index_scratch[NV_INDEX_HEADER_LEN], len);
	}

	return true;
}

/* Read only the header of an indexed record. */
static bool nv_index_header_read(u8 id, u8 opIdx, u8 *item_id)
{
	u8 hdr[NV_INDEX_HEADER_LEN];
	ssize_t rc;

	rc = nvs_read(&zb_nvs, nv_index_key(id, opIdx), hdr, sizeof(hdr));
	if (rc < (ssize_t)NV_INDEX_HEADER_LEN || hdr[0] != NV_INDEX_MAGIC) {
		return false;
	}

	if (item_id != NULL) {
		*item_id = hdr[1];
	}

	return true;
}

/* Highest index in use in a module, NV_INDEX_TOP_EMPTY when none. */
static int16_t nv_index_top_get(u8 id)
{
	if ((nv_index_top_known & BIT(id)) == 0U) {
		int16_t top = NV_INDEX_TOP_EMPTY;

		for (int16_t i = NV_INDEX_MAX; i >= 0; i--) {
			if (nv_index_header_read(id, (u8)i, NULL)) {
				top = i;
				break;
			}
		}
		nv_index_top_set(id, top);
	}

	return nv_index_top[id];
}

/* Latest record of item_id, or of any item for ITEM_FIELD_IDLE. */
static bool nv_index_latest(u8 id, u8 item_id, u8 *op_idx)
{
	for (int16_t i = nv_index_top_get(id); i >= 0; i--) {
		u8 stored_item;

		if (!nv_index_header_read(id, (u8)i, &stored_item) ||
		    (item_id != ITEM_FIELD_IDLE && stored_item != item_id)) {
			continue;
		}

		*op_idx = (u8)i;
		return true;
	}

	return false;
}

/*
 * Move the records of a module down over deleted slots, keeping their
 * order, so that the next append finds room.
 */
static bool nv_index_compact(u8 id)
{
	int16_t top = nv_index_top_get(id);
	int16_t dst = 0;

	for (int16_t i = 0; i <= top; i++) {
		ssize_t len;

		len = nvs_read(&zb_nvs, nv_index_key(id, (u8)i), nv_index_scratch,
			       sizeof(nv_index_scratch));
		if (len == -ENOENT) {
			continue;
		}
		if (len < 0 || len > (ssize_t)sizeof(nv_index_scratch)) {
			return false;
		}
		if (i != dst) {
			if (nvs_write(&zb_nvs, nv_index_key(id, (u8)dst), nv_index_scratch,
				      (size_t)len) < 0 ||
			    nvs_delete(&zb_nvs, nv_index_key(id, (u8)i)) < 0) {
				nv_index_top_invalidate(id);
				return false;
			}
		}
		dst++;
	}

	nv_index_top_set(id, dst - 1);
	return true;
}

static bool nv_index_append_slot(u8 id, u8 *op_idx)
{
	int16_t top = nv_index_top_get(id);

	if (top >= (int16_t)NV_INDEX_MAX) {
		if (!nv_index_compact(id)) {
			return false;
		}
		top = nv_index_top[id];
		if (top >= (int16_t)NV_INDEX_MAX) {
			return false;
		}
	}

	*op_idx = (u8)(top + 1);
	return true;
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
			nv_index_top_invalidate(module_id);
			return NV_INVALID_MODULS;
		}
	}

	nv_index_top_set(module_id, NV_INDEX_TOP_EMPTY);
	return NV_SUCC;
}

nv_sts_t nv_flashWriteNew(u8 single, u16 id, u8 itemId, u16 len, u8 *buf)
{
	u8 op_idx;
	ssize_t written;

	if (!zb_nvs_ensure_ready()) {
		return NV_NO_MEDIA;
	}
	if (id >= NV_MAX_MODULS || buf == NULL || len == 0U) {
		return NV_INVALID_ID;
	}

	if (single) {
		written = nvs_write(&zb_nvs, nv_key((u8)id, itemId), buf, len);
		return written >= 0 ? NV_SUCC : NV_NOT_ENOUGH_SAPCE;
	}

	if (len > NV_INDEX_VALUE_MAX || !nv_index_append_slot((u8)id, &op_idx)) {
		return NV_NOT_ENOUGH_SAPCE;
	}

	nv_index_scratch[0] = NV_INDEX_MAGIC;
	nv_index_scratch[1] = itemId;
	nv_index_scratch[2] = (u8)len;
	nv_index_scratch[3] = (u8)(len >> 8);
	memcpy(&nv_index_scratch[NV_INDEX_HEADER_LEN], buf, len);
	written = nvs_write(&zb_nvs, nv_index_key((u8)id, op_idx), nv_index_scratch,
			    len + NV_INDEX_HEADER_LEN);
	if (written < 0) {
		return NV_NOT_ENOUGH_SAPCE;
	}

	nv_index_top_set((u8)id, (int16_t)op_idx);
	return NV_SUCC;
}

nv_sts_t nv_flashReadNew(u8 single, u8 id, u8 itemId, u16 len, u8 *buf)
{
	ssize_t actual_len;
	ssize_t rc;
	u16 expected_len;

	if (!zb_nvs_ensure_ready()) {
		return NV_NO_MEDIA;
	}
	if (id >= NV_MAX_MODULS) {
		return NV_INVALID_ID;
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

		/*
		 * A single item never lives in the index. Probing it anyway
		 * costs an NVS lookup per slot, each a flash walk with
		 * interrupts masked.
		 */
		if (single) {
			return NV_ITEM_NOT_FOUND;
		}

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
	int rc;

	if (!zb_nvs_ensure_ready()) {
		if (!zb_nvs_mount_failed) {
			return NV_NO_MEDIA;
		}
		/* Factory reset of a partition NVS could not mount. */
		rc = flash_flatten(zb_nvs.flash_device, zb_nvs.offset,
				   zb_nvs.sector_size * zb_nvs.sector_count);
	} else {
		rc = nvs_clear(&zb_nvs);
	}

	zb_nvs_ready = false;
	nv_index_top_invalidate_all();
	if (rc < 0) {
		zb_nvs_log_degraded("erase failed", rc);
		return NV_INVALID_MODULS;
	}

	rc = nvs_mount(&zb_nvs);
	if (rc < 0) {
		zb_nvs_mount_failed = true;
		zb_nvs_log_degraded("mount after erase failed", rc);
		return NV_INVALID_MODULS;
	}

	zb_nvs_mount_failed = false;
	zb_nvs_ready = true;
	zb_nvs_degraded_logged = false;
	return NV_SUCC;
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
	int rc = nvs_write(&zb_nvs, nv_key(NV_MODULE_NWK_FRAME_COUNT, NV_ITEM_NWK_FRAME_COUNT),
			   &frameCount, sizeof(frameCount));

	return rc >= 0 ? NV_SUCC : NV_NOT_ENOUGH_SAPCE;
}

nv_sts_t nv_nwkFrameCountFromFlash(u32 *frameCount)
{
	if (frameCount == NULL || !zb_nvs_ensure_ready()) {
		return NV_NO_MEDIA;
	}
	int rc = nvs_read(&zb_nvs, nv_key(NV_MODULE_NWK_FRAME_COUNT, NV_ITEM_NWK_FRAME_COUNT),
			  frameCount, sizeof(*frameCount));

	if (rc == -ENOENT) {
		*frameCount = 0;
		return NV_ITEM_NOT_FOUND;
	}
	return rc >= 0 ? NV_SUCC : NV_DATA_CHECK_ERROR;
}

nv_sts_t nv_flashReadByIndex(u8 id, u8 itemId, u8 opSect, u16 opIdx, u16 len, u8 *buf)
{
	if (opSect != 0U || opIdx > NV_INDEX_MAX || id >= NV_MAX_MODULS) {
		return NV_ITEM_NOT_FOUND;
	}
	if (!zb_nvs_ensure_ready()) {
		return NV_NO_MEDIA;
	}

	return nv_index_record_read(id, itemId, (u8)opIdx, len, buf) ? NV_SUCC : NV_ITEM_NOT_FOUND;
}

nv_sts_t nv_itemDeleteByIndex(u8 id, u8 itemId, u8 opSect, u16 opIdx)
{
	int rc;

	u8 stored_item;

	if (opSect != 0U || opIdx > NV_INDEX_MAX || id >= NV_MAX_MODULS) {
		return NV_ITEM_NOT_FOUND;
	}
	if (!zb_nvs_ensure_ready()) {
		return NV_NO_MEDIA;
	}
	if (!nv_index_header_read(id, (u8)opIdx, &stored_item) || stored_item != itemId) {
		return NV_ITEM_NOT_FOUND;
	}
	rc = nvs_delete(&zb_nvs, nv_index_key(id, (u8)opIdx));
	if (rc != 0) {
		return NV_ITEM_NOT_FOUND;
	}

	if (nv_index_top[id] == (int16_t)opIdx) {
		/* Rescanned lazily on the next use. */
		nv_index_top_invalidate(id);
	}
	return NV_SUCC;
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
