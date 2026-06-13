/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Zephyr-native replacement for libzigbee zb_buffer.c.
 *
 * The vendor implementation keeps a static array of zb_buf_t in
 * g_mPool, linked by a free-list head + usedNum counter. The
 * router build doesn't pull in g_mPool (it depends on
 * zb_buf_pool_t which the Zephyr port doesn't mirror). Instead,
 * back the same zb_buf_allocate / zb_buf_free / zb_buf_clear /
 * is_zb_buf / tl_bufInitalloc / tl_phyRxBufTozbBuf API with a
 * Zephyr k_mem_slab so callers stay binary-compatible with the
 * libzigbee NWK / MAC ports.
 */

#include <zephyr/kernel.h>

#include "zb_common_stub.h"

K_MEM_SLAB_DEFINE_STATIC(zb_buf_slab, sizeof(zb_buf_t), ZB_BUF_POOL_NUM,
			 __alignof__(zb_buf_t));

zb_buf_t *zb_buf_allocate(void)
{
	void *block = NULL;

	if (k_mem_slab_alloc(&zb_buf_slab, &block, K_NO_WAIT) != 0) {
		return NULL;
	}

	memset(block, 0, sizeof(zb_buf_t));
	return (zb_buf_t *)block;
}

void zb_buf_free(zb_buf_t *buf)
{
	if (buf == NULL) {
		return;
	}
	k_mem_slab_free(&zb_buf_slab, buf);
}

void zb_buf_clear(zb_buf_t *p)
{
	if (p != NULL) {
		memset(p, 0, sizeof(*p));
	}
}

bool is_zb_buf(void *p)
{
	uintptr_t addr = (uintptr_t)p;
	uintptr_t base = (uintptr_t)zb_buf_slab.buffer;
	size_t total = (size_t)zb_buf_slab.info.num_blocks *
		       zb_buf_slab.info.block_size;

	return (p != NULL) && (addr >= base) && (addr < base + total) &&
	       ((addr - base) % zb_buf_slab.info.block_size == 0U);
}

/*
 * The vendor TL_BUF_INITIAL_ALLOC pattern hands back the tail of the
 * buf->buf[] payload area minus `size` bytes. libzigbee callers
 * then write headers there and grow forward.
 */
void *tl_bufInitalloc(zb_buf_t *p, u8 size)
{
	if (p == NULL || size > ZB_BUF_SIZE) {
		return NULL;
	}
	return &p->buf[ZB_BUF_SIZE - size];
}

/*
 * Convert a raw radio rxBuf (the DMA region exposed by
 * drv_radio_zephyr.c::zb_radio_on_rx_sink) into a freshly allocated
 * zb_buf_t. libzigbee mac_trx.c::zb_macDataRecvHandler then writes
 * the PSDU pointer + meta (timestamp / rssi / len) into the first
 * few bytes of buf->buf[].
 */
void *tl_phyRxBufTozbBuf(u8 *rxBuf)
{
	ARG_UNUSED(rxBuf);
	return zb_buf_allocate();
}

/*
 * Vendor zb_buffer.c also exports g_mPool / ZB_BUF_POOL_SIZE etc.
 * Those are referenced from a couple of helper symbols
 * (zbBufferSizeGet, etc.); leave the SDK-copied stubs in
 * subsys/zigbee/common/zb_config.c gated behind
 * ZB_ZEPHYR_NO_VENDOR_BUFFER_POOL — no caller in the router path
 * actually dereferences g_mPool now that this TU owns the
 * allocator.
 */
