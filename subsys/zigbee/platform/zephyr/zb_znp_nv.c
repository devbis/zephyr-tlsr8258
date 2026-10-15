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

#define ZNP_NV_MAX_ITEMS 48U
#define ZNP_NV_POOL_SIZE 3072U

struct znp_nv_item {
	uint16_t id;
	uint16_t len;
	uint16_t pool_off;
};

static struct znp_nv_item items[ZNP_NV_MAX_ITEMS];
static uint8_t pool[ZNP_NV_POOL_SIZE];
static uint16_t pool_used;
static uint8_t item_count;

static struct znp_nv_item *nv_find(uint16_t id)
{
	for (uint8_t i = 0; i < item_count; i++) {
		if (items[i].id == id) {
			return &items[i];
		}
	}

	return NULL;
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

	memcpy(&pool[item->pool_off + offset], data, len);

	return ZNP_STATUS_SUCCESS;
}

uint8_t znp_nv_delete(uint16_t id)
{
	struct znp_nv_item *item = nv_find(id);

	if (item == NULL) {
		return ZNP_STATUS_INVALID_PARAMETER;
	}

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
}
