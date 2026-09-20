/* SPDX-License-Identifier: Apache-2.0 */
/*
 * ev_buffer backed by Zephyr k_mem_slab.
 *
 * Buffer groups mirror ev_buffer.h constants:
 *   Group 0: 24 bytes total, 8 blocks
 *   Group 1: 60 bytes total, 8 blocks
 *   Group 2: 152 bytes total, 8 blocks
 *   Group 3: 512 bytes total, 2 blocks
 *
 * Each slab block holds an entire ev_bufItem_t (header + data). The
 * ev_bufItem_t header is 8 bytes (next ptr + groupIndex), so user data
 * capacity = group_size - 8 bytes (matches SMALL_BUFFER / LARGE_BUFFER).
 */
#include <zephyr/kernel.h>
#include <zephyr/zigbee/zb_types.h>
#include "ev_buffer.h"

K_MEM_SLAB_DEFINE_STATIC(buf_slab_0, BUFFER_GROUP_0, BUFFER_NUM_IN_GROUP0, 4);
K_MEM_SLAB_DEFINE_STATIC(buf_slab_1, BUFFER_GROUP_1, BUFFER_NUM_IN_GROUP1, 4);
K_MEM_SLAB_DEFINE_STATIC(buf_slab_2, BUFFER_GROUP_2, BUFFER_NUM_IN_GROUP2, 4);
K_MEM_SLAB_DEFINE_STATIC(buf_slab_3, BUFFER_GROUP_3, BUFFER_NUM_IN_GROUP3, 4);

static struct k_mem_slab *const slabs[] = {
	&buf_slab_0, &buf_slab_1, &buf_slab_2, &buf_slab_3,
};
static const u16 slab_sizes[] = {
	BUFFER_GROUP_0, BUFFER_GROUP_1, BUFFER_GROUP_2, BUFFER_GROUP_3,
};
#define NUM_SLABS ARRAY_SIZE(slabs)

/* Size of ev_bufItem_t header before data[] field */
#define HDR_SIZE  OFFSETOF(ev_bufItem_t, data)

void ev_buf_init(void)
{
	/* k_mem_slab is statically allocated; nothing to do */
}

void ev_buf_reset(void)
{
	/* Not supported */
}

u8 *ev_buf_allocate(u16 size)
{
	u16 needed = size + (u16)HDR_SIZE;
	void *block = NULL;

	for (int i = 0; i < (int)NUM_SLABS; i++) {
		if (needed <= slab_sizes[i]) {
			if (k_mem_slab_alloc(slabs[i], &block, K_NO_WAIT) == 0) {
				ev_bufItem_t *item = (ev_bufItem_t *)block;

				item->next = NULL;
				item->groupIndex = (u32)i;
				return item->data;
			}
		}
	}
	return NULL;
}

buf_sts_t ev_buf_free(u8 *pBuf)
{
	if (pBuf == NULL) {
		return BUFFER_INVALID_PARAMETER;
	}
	ev_bufItem_t *item = ev_buf_getHead(pBuf);
	u32 idx = item->groupIndex;

	if (idx >= NUM_SLABS) {
		return BUFFER_INVALID_PARAMETER;
	}
	k_mem_slab_free(slabs[idx], item);
	return BUFFER_SUCC;
}

ev_bufItem_t *ev_buf_getHead(u8 *pd)
{
	return (ev_bufItem_t *)(pd - HDR_SIZE);
}

u8 is_ev_buf(void *arg)
{
	return arg != NULL ? 1 : 0;
}

u16 ev_buf_getFreeMaxSize(void)
{
	return (u16)(BUFFER_GROUP_3 - HDR_SIZE);
}

u8 *long_ev_buf_get(void)
{
	return ev_buf_allocate((u16)(BUFFER_GROUP_3 - HDR_SIZE));
}
