/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * OSAL NV items as the host sees them. Hosts store their network settings
 * and bookkeeping here, and look items up by ID without knowing which exist,
 * so this is a plain table of byte blobs carved out of a static pool.
 */
#include <string.h>

#include "zb_znp_internal.h"

struct znp_nv_item {
	uint16_t id;
	uint16_t len;
	uint16_t pool_off;
};

static struct znp_nv_item items[ZNP_NV_MAX_ITEMS];
static uint8_t pool[ZNP_NV_POOL_SIZE];
static uint16_t pool_used;
static uint8_t item_count;
static uint32_t generation;

static struct znp_nv_item *nv_find(uint16_t id)
{
	for (uint8_t i = 0; i < item_count; i++) {
		if (items[i].id == id) {
			return &items[i];
		}
	}

	return NULL;
}

/*
 * Items whose value is the stack's own state. They are rebuilt from the stack
 * whenever the host looks, so they are neither saved nor counted as changes.
 */
static bool nv_is_derived(uint16_t id)
{
	switch (id) {
	case ZNP_NV_EXTADDR:
	case ZNP_NV_ADDRMGR:
	case ZNP_NV_NIB:
	case ZNP_NV_NWK_ACTIVE_KEY_INFO:
	case ZNP_NV_NWK_ALTERN_KEY_INFO:
	case ZNP_NV_BDBNODEISONANETWORK:
	case ZNP_NV_NWKKEY:
		return true;
	default:
		return (id >= ZNP_NV_NWK_SEC_MATERIAL_FIRST && id <= ZNP_NV_NWK_SEC_MATERIAL_LAST);
	}
}

static void nv_changed(uint16_t id)
{
	if (!nv_is_derived(id)) {
		generation++;
	}
}

uint32_t znp_nv_generation(void)
{
	return generation;
}

uint16_t znp_nv_length(uint16_t id)
{
	const struct znp_nv_item *item = nv_find(id);

	return (item != NULL) ? item->len : 0U;
}

uint8_t znp_nv_item_init(uint16_t id, uint16_t len, const uint8_t *init, uint8_t init_len)
{
	struct znp_nv_item *item = nv_find(id);

	if (item != NULL) {
		return (item->len == len) ? ZNP_STATUS_SUCCESS : ZNP_STATUS_NV_BAD_ITEM_LEN;
	}

	if (len == 0U || item_count == ZNP_NV_MAX_ITEMS || len > ZNP_NV_POOL_SIZE - pool_used) {
		return ZNP_STATUS_NV_OPER_FAILED;
	}

	item = &items[item_count++];
	item->id = id;
	item->len = len;
	item->pool_off = pool_used;
	pool_used += len;

	memset(&pool[item->pool_off], 0xff, len);
	memcpy(&pool[item->pool_off], init, (init_len < len) ? init_len : len);
	nv_changed(id);

	return ZNP_STATUS_NV_ITEM_UNINIT;
}

uint8_t znp_nv_read(uint16_t id, uint16_t offset, uint8_t *out, size_t max, size_t *out_len)
{
	const struct znp_nv_item *item = nv_find(id);
	size_t n;

	*out_len = 0U;
	if (item == NULL || offset > item->len) {
		return ZNP_STATUS_INVALID_PARAMETER;
	}

	n = item->len - offset;
	if (n > max) {
		n = max;
	}
	memcpy(out, &pool[item->pool_off + offset], n);
	*out_len = n;

	return ZNP_STATUS_SUCCESS;
}

uint8_t znp_nv_write(uint16_t id, uint16_t offset, const uint8_t *data, size_t len)
{
	const struct znp_nv_item *item = nv_find(id);

	if (item == NULL || offset > item->len || len > (size_t)item->len - offset) {
		return ZNP_STATUS_INVALID_PARAMETER;
	}

	if (memcmp(&pool[item->pool_off + offset], data, len) != 0) {
		memcpy(&pool[item->pool_off + offset], data, len);
		nv_changed(id);
	}

	return ZNP_STATUS_SUCCESS;
}

uint8_t znp_nv_delete(uint16_t id)
{
	struct znp_nv_item *item = nv_find(id);

	if (item == NULL) {
		return ZNP_STATUS_INVALID_PARAMETER;
	}

	nv_changed(id);

	/* Close the gap in the pool so recreating an item never exhausts it. */
	memmove(&pool[item->pool_off], &pool[item->pool_off + item->len],
		pool_used - (item->pool_off + item->len));
	pool_used -= item->len;
	for (uint8_t i = 0; i < item_count; i++) {
		if (items[i].pool_off > item->pool_off) {
			items[i].pool_off -= item->len;
		}
	}
	*item = items[--item_count];

	return ZNP_STATUS_SUCCESS;
}

void znp_nv_reset(void)
{
	item_count = 0U;
	pool_used = 0U;
	generation++;
}

size_t znp_nv_persist_count(void)
{
	size_t count = 0U;

	for (uint8_t i = 0U; i < item_count; i++) {
		if (!nv_is_derived(items[i].id)) {
			count++;
		}
	}

	return count;
}

bool znp_nv_persist_get(size_t index, uint16_t *id, uint16_t *len, const uint8_t **data)
{
	for (uint8_t i = 0U; i < item_count; i++) {
		if (nv_is_derived(items[i].id)) {
			continue;
		}
		if (index == 0U) {
			*id = items[i].id;
			*len = items[i].len;
			*data = &pool[items[i].pool_off];
			return true;
		}
		index--;
	}

	return false;
}

uint8_t *znp_nv_restore_item(uint16_t id, uint16_t len)
{
	static const uint8_t erased = 0xffU;
	struct znp_nv_item *item;

	if (nv_find(id) != NULL || len == 0U ||
	    znp_nv_item_init(id, len, &erased, 0U) != ZNP_STATUS_NV_ITEM_UNINIT) {
		return NULL;
	}

	item = nv_find(id);
	return (item != NULL) ? &pool[item->pool_off] : NULL;
}

uint8_t znp_nv_set_derived(uint16_t id, const uint8_t *data, size_t len)
{
	const struct znp_nv_item *item = nv_find(id);

	if (item == NULL || len != item->len) {
		if (item != NULL) {
			(void)znp_nv_delete(id);
		}
		(void)znp_nv_item_init(id, (uint16_t)len, data, (uint8_t)MIN(len, 255U));
		item = nv_find(id);
		if (item == NULL) {
			return ZNP_STATUS_NV_OPER_FAILED;
		}
	}

	memcpy(&pool[item->pool_off], data, len);

	return ZNP_STATUS_SUCCESS;
}
