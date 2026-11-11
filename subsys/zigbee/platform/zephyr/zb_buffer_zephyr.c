/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * Zephyr-native replacement for libzigbee zb_buffer.c.
 *
 * The vendor implementation keeps a static array of zb_buf_t in
 * g_mPool, linked by a free-list head + usedNum counter. The
 * router build doesn't pull in g_mPool (it depends on
 * zb_buf_pool_t which the Zephyr port doesn't mirror). Instead,
 * back the same zb_buf_allocate / zb_buf_free / zb_buf_clear /
 * is_zb_buf / zb_bufInitalloc / zb_phyRxBufTozbBuf API with a
 * Zephyr k_mem_slab so callers stay binary-compatible with the
 * libzigbee NWK / MAC ports.
 */

#include <zephyr/kernel.h>

#include "zb_common.h"

/*
 * 127-byte PSDU + 5-byte RF DMA header is the complete 802.15.4 frame.
 * Keep a little alignment/headroom without spending 200 bytes in every one
 * of the 36 router buffers.
 */
#define ZB_BUF_RX_SNAPSHOT_SIZE 128U
/*
 * Room in front of a captured frame. A relayed frame is rebuilt in place over
 * the received MAC header, which is as long as the new one; only a coordinator
 * grows the network header of a relayed frame, by a source route subframe.
 */
#if defined(CONFIG_ZIGBEE_COORDINATOR)
#define ZB_BUF_RX_HEADROOM 16U
#else
#define ZB_BUF_RX_HEADROOM 0U
#endif

#define ZB_BUF_RX_SNAPSHOT_BYTES (ZB_BUF_RX_HEADROOM + ZB_BUF_RX_SNAPSHOT_SIZE)

/* A separate pool of received-frame copies instead of one per buffer. */
#define ZB_BUF_SNAPSHOT_POOL (CONFIG_ZIGBEE_RX_SNAPSHOTS != 0)

#if ZB_BUF_SNAPSHOT_POOL
typedef struct {
	zb_buf_t zb;
} zb_buf_block_t;

K_MEM_SLAB_DEFINE_STATIC(zb_snap_slab, ZB_BUF_RX_SNAPSHOT_BYTES, CONFIG_ZIGBEE_RX_SNAPSHOTS,
			 __alignof__(zb_buf_t));

/*
 * Snapshot block a buffer holds plus one (0: none), by buffer index, and the buffer
 * a block belongs to.
 */
static uint8_t zb_snap_of_buf[ZB_BUF_POOL_NUM];
static uint8_t zb_buf_of_snap[CONFIG_ZIGBEE_RX_SNAPSHOTS];
#else
typedef struct {
	zb_buf_t zb;
	uint8_t rx_snapshot[ZB_BUF_RX_SNAPSHOT_BYTES];
} zb_buf_block_t;
#endif

K_MEM_SLAB_DEFINE_STATIC(zb_buf_slab, sizeof(zb_buf_block_t), ZB_BUF_POOL_NUM,
			 __alignof__(zb_buf_t));

/*
 * Track slab ownership so an invalid or duplicate free cannot corrupt the
 * slab free list.
 */
static volatile uint32_t zb_buf_in_use[2];
static struct k_spinlock zb_buf_lock;

/* zb_buf_in_use has one bit per block; zb_buf_to_ref() reserves 0xff. */
BUILD_ASSERT(ZB_BUF_POOL_NUM <= ARRAY_SIZE(zb_buf_in_use) * 32U,
	     "zb_buf pool larger than its ownership bitmap");

static bool zb_buf_index(const zb_buf_t *buf, uint32_t *index)
{
	uintptr_t addr = (uintptr_t)buf;
	uintptr_t base = (uintptr_t)zb_buf_slab.buffer;
	size_t span = (size_t)zb_buf_slab.info.num_blocks * zb_buf_slab.info.block_size;

	if (buf == NULL || addr < base || addr >= base + span ||
	    ((addr - base) % zb_buf_slab.info.block_size) != 0U) {
		return false;
	}

	*index = (uint32_t)((addr - base) / zb_buf_slab.info.block_size);
	return *index < ZB_BUF_POOL_NUM;
}

static inline bool zb_buf_owned(uint32_t index)
{
	return (zb_buf_in_use[index >> 5] & BIT(index & 31U)) != 0U;
}

static inline void zb_buf_mark_owned(uint32_t index)
{
	zb_buf_in_use[index >> 5] |= BIT(index & 31U);
}

static inline void zb_buf_mark_free(uint32_t index)
{
	zb_buf_in_use[index >> 5] &= ~BIT(index & 31U);
}

#if ZB_BUF_SNAPSHOT_POOL
static inline uint8_t *zb_snap_addr(uint32_t snap)
{
	return (uint8_t *)zb_snap_slab.buffer + (size_t)snap * ZB_BUF_RX_SNAPSHOT_BYTES;
}

/* Give the snapshot of a buffer back to its pool. Call with zb_buf_lock held. */
static void zb_snap_release(uint32_t buf_index)
{
	uint8_t held = zb_snap_of_buf[buf_index];

	if (held != 0U) {
		zb_snap_of_buf[buf_index] = 0U;
		k_mem_slab_free(&zb_snap_slab, zb_snap_addr(held - 1U));
	}
}

/* The snapshot of a buffer, taken from the pool when it has none. NULL when the pool is empty. */
static uint8_t *zb_snap_get(zb_buf_t *buf)
{
	k_spinlock_key_t key = k_spin_lock(&zb_buf_lock);
	uint32_t index;
	uint8_t *snap = NULL;

	if (zb_buf_index(buf, &index)) {
		if (zb_snap_of_buf[index] == 0U) {
			void *block;

			if (k_mem_slab_alloc(&zb_snap_slab, &block, K_NO_WAIT) == 0) {
				uint32_t snap_index = (uint32_t)((uint8_t *)block - (uint8_t *)zb_snap_slab.buffer) /
						 ZB_BUF_RX_SNAPSHOT_BYTES;

				zb_snap_of_buf[index] = (uint8_t)(snap_index + 1U);
				zb_buf_of_snap[snap_index] = (uint8_t)index;
			}
		}
		if (zb_snap_of_buf[index] != 0U) {
			snap = zb_snap_addr(zb_snap_of_buf[index] - 1U);
		}
	}
	k_spin_unlock(&zb_buf_lock, key);
	return snap;
}
#endif

zb_buf_t *zb_buf_allocate(void)
{
	void *block = NULL;

	if (k_mem_slab_alloc(&zb_buf_slab, &block, K_NO_WAIT) != 0) {
		g_sysDiags.packetBufferAllocateFailures++;
		return NULL;
	}

	memset(block, 0, sizeof(zb_buf_t));
	/*
	 * The vendor pool marks the buffer in use. Ported code tests that
	 * flag: nwk_fwdPacket() drops a frame whose buffer reads as free.
	 */
	((zb_buf_t *)block)->hdr.used = 1;
	{
		uint32_t index;
		k_spinlock_key_t key = k_spin_lock(&zb_buf_lock);

		if (zb_buf_index((zb_buf_t *)block, &index)) {
			zb_buf_mark_owned(index);
		}
		k_spin_unlock(&zb_buf_lock, key);
	}
	return (zb_buf_t *)block;
}

void zb_buf_free(zb_buf_t *buf)
{
	if (buf == NULL) {
		return;
	}
	{
		uint32_t index;
		k_spinlock_key_t key;

		if (!zb_buf_index(buf, &index)) {
			return;
		}
		key = k_spin_lock(&zb_buf_lock);
		if (!zb_buf_owned(index)) {
			k_spin_unlock(&zb_buf_lock, key);
			return;
		}
		zb_buf_mark_free(index);
#if ZB_BUF_SNAPSHOT_POOL
		zb_snap_release(index);
#endif
		k_spin_unlock(&zb_buf_lock, key);
	}
	buf->hdr.used = 0;
	k_mem_slab_free(&zb_buf_slab, buf);
}

void zb_buf_clear(zb_buf_t *p)
{
	if (p != NULL) {
		memset(p, 0, sizeof(*p));
	}
}

/*
 * Map a pointer into a buffer payload back to its zb_buf.
 *
 * The imported MAC records the sequence number it waits to see acknowledged as
 * txBuf->buf[2], while callers hand it a frame pointer that zb_bufInitalloc()
 * placed near the end of the payload. On this port those are different bytes,
 * so the radio side needs the owning buffer to read the same one the MAC did.
 */
zb_buf_t *zb_buf_owner_of(const void *payload)
{
	uintptr_t addr = (uintptr_t)payload;
	uintptr_t base = (uintptr_t)zb_buf_slab.buffer;
	size_t block = zb_buf_slab.info.block_size;
	size_t total = (size_t)zb_buf_slab.info.num_blocks * block;

#if ZB_BUF_SNAPSHOT_POOL
	uintptr_t snap_base = (uintptr_t)zb_snap_slab.buffer;

	if ((payload != NULL) && (addr >= snap_base) &&
	    (addr < snap_base + (size_t)CONFIG_ZIGBEE_RX_SNAPSHOTS * ZB_BUF_RX_SNAPSHOT_BYTES)) {
		uint8_t owner = zb_buf_of_snap[(addr - snap_base) / ZB_BUF_RX_SNAPSHOT_BYTES];

		return (zb_buf_t *)(base + (size_t)owner * block);
	}
#endif

	if ((payload == NULL) || (addr < base) || (addr >= base + total)) {
		return NULL;
	}

	return (zb_buf_t *)(base + ((addr - base) / block) * block);
}

bool is_zb_buf(void *p)
{
	uintptr_t addr = (uintptr_t)p;
	uintptr_t base = (uintptr_t)zb_buf_slab.buffer;
	size_t total = (size_t)zb_buf_slab.info.num_blocks * zb_buf_slab.info.block_size;

	return (p != NULL) && (addr >= base) && (addr < base + total) &&
	       ((addr - base) % zb_buf_slab.info.block_size == 0U);
}

zb_buf_t *zb_buf_from_ref(uint8_t ref)
{
	if (ref >= ZB_BUF_POOL_NUM) {
		return NULL;
	}

	return (zb_buf_t *)((uint8_t *)zb_buf_slab.buffer + ((size_t)ref * zb_buf_slab.info.block_size));
}

uint8_t zb_buf_to_ref(zb_buf_t *buf)
{
	uint32_t index;

	return zb_buf_index(buf, &index) ? (uint8_t)index : 0xffU;
}

/*
 * Bytes zb_bufInitalloc() keeps free after an allocation, as the vendor does:
 * ported MAC/NWK/security code appends security material after it.
 */
#define ZB_BUF_TAIL_RESERVE 8U

/*
 * The vendor ZB_BUF_INITIAL_ALLOC pattern hands back the tail of the
 * buf->buf[] payload area minus `size` bytes, 4-byte aligned. libzigbee
 * callers then write headers there and grow forward.
 */
void *zb_bufInitalloc(zb_buf_t *p, uint8_t size)
{
	if (p == NULL || (size_t)size + ZB_BUF_TAIL_RESERVE > ZB_BUF_SIZE) {
		return NULL;
	}

	return &p->buf[(ZB_BUF_SIZE - ZB_BUF_TAIL_RESERVE - size) & ~3U];
}

/*
 * Convert a raw radio rxBuf (the DMA region exposed by
 * drv_radio_zephyr.c::zb_radio_on_rx_sink) into a freshly allocated
 * zb_buf_t. libzigbee mac_trx.c::zb_macDataRecvHandler then writes
 * the PSDU pointer + meta (timestamp / rssi / len) into the first
 * few bytes of buf->buf[].
 */
uint8_t *zb_phyRxBufTozbBuf(uint8_t *rxBuf)
{
	zb_buf_t *owner;

	/*
	 * The radio captures each frame into a zb_buf of its own, see
	 * zb_buf_rx_payload_capture(); hand that buffer to the MAC.
	 */
	owner = zb_buf_owner_of(rxBuf);
	if (owner != NULL) {
		return (uint8_t *)owner;
	}

	return (uint8_t *)zb_buf_allocate();
}

uint8_t zb_buf_rx_free_count(void)
{
#if ZB_BUF_SNAPSHOT_POOL
	return (uint8_t)MIN(k_mem_slab_num_free_get(&zb_buf_slab),
		       k_mem_slab_num_free_get(&zb_snap_slab));
#else
	return (uint8_t)k_mem_slab_num_free_get(&zb_buf_slab);
#endif
}

uint8_t *zb_buf_rx_payload_capture(zb_buf_t *buf, const uint8_t *data, uint8_t len)
{
#if ZB_BUF_SNAPSHOT_POOL
	uint8_t *snapshot;

	if (buf == NULL || data == NULL || len > ZB_BUF_RX_SNAPSHOT_SIZE) {
		return NULL;
	}

	snapshot = zb_snap_get(buf);
	if (snapshot == NULL) {
		return NULL;
	}
#else
	zb_buf_block_t *block = (zb_buf_block_t *)buf;
	uint8_t *snapshot;

	if (block == NULL || data == NULL || len > ZB_BUF_RX_SNAPSHOT_SIZE) {
		return NULL;
	}

	snapshot = block->rx_snapshot;
#endif

	memset(snapshot, 0, ZB_BUF_RX_SNAPSHOT_BYTES);
	memcpy(&snapshot[ZB_BUF_RX_HEADROOM], data, len);

	return &snapshot[ZB_BUF_RX_HEADROOM];
}

/*
 * Vendor zb_buffer.c also exports g_mPool / ZB_BUF_POOL_SIZE etc.
 * Those pool symbols are intentionally omitted from the Zephyr
 * adapter; the corresponding size helpers use the Zephyr slab and
 * no full-stack caller dereferences the vendor free-list storage.
 */

void zb_buf_rx_snapshot_copy(zb_buf_t *dst, const zb_buf_t *src)
{
	if (!is_zb_buf(dst) || !is_zb_buf((void *)src)) {
		return;
	}

#if ZB_BUF_SNAPSHOT_POOL
	uint32_t src_index;
	uint8_t *to;

	if (!zb_buf_index(src, &src_index) || zb_snap_of_buf[src_index] == 0U) {
		return;
	}

	to = zb_snap_get(dst);
	if (to != NULL) {
		memcpy(to, zb_snap_addr(zb_snap_of_buf[src_index] - 1U), ZB_BUF_RX_SNAPSHOT_BYTES);
	}
#else
	memcpy(((zb_buf_block_t *)dst)->rx_snapshot, ((const zb_buf_block_t *)src)->rx_snapshot,
	       sizeof(((zb_buf_block_t *)dst)->rx_snapshot));
#endif
}

uint8_t *zb_buf_rebase(const zb_buf_t *src, zb_buf_t *dst, const uint8_t *p)
{
#if ZB_BUF_SNAPSHOT_POOL
	uintptr_t addr = (uintptr_t)p;
	uint8_t *result = NULL;
	uint32_t src_index;
	uint32_t dst_index;
	k_spinlock_key_t key;

	if (addr >= (uintptr_t)src && addr < (uintptr_t)src + sizeof(zb_buf_t)) {
		return (uint8_t *)dst + (addr - (uintptr_t)src);
	}

	key = k_spin_lock(&zb_buf_lock);
	if (zb_buf_index(src, &src_index) && zb_buf_index(dst, &dst_index) &&
	    zb_snap_of_buf[src_index] != 0U && zb_snap_of_buf[dst_index] != 0U) {
		uintptr_t from = (uintptr_t)zb_snap_addr(zb_snap_of_buf[src_index] - 1U);

		if (addr >= from && addr < from + ZB_BUF_RX_SNAPSHOT_BYTES) {
			result = zb_snap_addr(zb_snap_of_buf[dst_index] - 1U) + (addr - from);
		}
	}
	k_spin_unlock(&zb_buf_lock, key);

	return result;
#else
	/* The received frame follows the buffer in its block, so one offset serves both. */
	return (uint8_t *)dst->buf + (p - src->buf);
#endif
}
