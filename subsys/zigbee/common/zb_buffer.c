/*
 * Copyright 2026 libzigbee
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "zb_common.h"
#include <stdint.h>

/* Both are .bss-local uint32_t debug variables in the vendor object. */
static uint32_t T_zbBufFreeDbg = 0;
static uint32_t T_zbBufDbg = 0;

_attribute_ram_code_ _attribute_no_inline_ zb_buf_t *zb_buf_get(void);

/* The vendor library bounds-checks against the runtime pool size rather than
 * ZB_BUF_POOL_NUM. */
static bool zb_buf_inPool(const void *p)
{
	return (uintptr_t)p >= (uintptr_t)&g_mPool.pool[0];
}

void zb_BufferInit(void)
{
	uint32_t poolBytes = zbBufferSizeGet();
	uint8_t poolCount;

	memset(&g_mPool, 0, poolBytes);

	g_mPool.head = &g_mPool.pool[0];
	poolCount = ZB_BUF_POOL_SIZE;
	for (uint8_t i = 0; (uint8_t)(i + 1U) < poolCount; i++) {
		g_mPool.pool[i].next = &g_mPool.pool[i + 1U];
	}
	g_mPool.pool[(poolCount > 0U) ? (poolCount - 1U) : 0U].next = NULL;
}

uint8_t *zb_phyRxBufTozbBuf(uint8_t *p)
{
	return p - RX_ZBBUF_OFFSET;
}

uint8_t *zb_BufToPhyRxBuf(uint8_t *p)
{
	return p + RX_ZBBUF_OFFSET;
}

/* Reconstructed from _router/zb_buffer.s:.text.zb_buf_free (0x20a bytes).  The
 * whole body runs with interrupts disabled, validates the pointer and the pool
 * head, keeps the per-buffer alloc/free counters, and never clears the payload.
 * It always returns BUFFER_SUCC. */
uint8_t zb_buf_free(zb_buf_t *buf)
{
	uint32_t irq = drv_disable_irq();

	/* The raw MAC TX buffer is not part of the pool. */
	if (buf == (zb_buf_t *)g_zbMacCtx.txRawDataBuf) {
		if (buf == NULL) {
			ZB_EXCEPTION_POST(SYS_EXCEPTTION_ZB_BUFFER_EXCEPTION_FREE_OVERFLON);
		}

		drv_restore_irq(irq);
		return BUFFER_SUCC;
	}

	/* A pointer below the pool is reported, then freed anyway. */
	if ((uintptr_t)buf < (uintptr_t)&g_mPool.pool[0]) {
		ZB_EXCEPTION_POST(SYS_EXCEPTTION_ZB_BUFFER_EXCEPTION_FREE_OVERFLON);
	}

	buf->freeCnt++;

	/* A buffer still queued in the MAC TX FIFO, a free without a
	 * matching allocate, or a buffer that is not marked used. */
	if (buf->hdr.macTxFifo || (buf->freeCnt > buf->allocCnt) || (buf->hdr.used == 0U)) {
		T_zbBufFreeDbg = ((uint32_t)buf->hdr.macTxFifo << 24) |
				 ((buf->freeCnt > buf->allocCnt) ? 0x10000U : 0U) |
				 (uint32_t)buf->hdr.used;
		T_zbBufDbg = (uint32_t)(unsigned long)buf;
		ZB_EXCEPTION_POST(SYS_EXCEPTTION_ZB_BUFFER_EXCEPTION_FREE_MULTI);
	}

	/* The current pool head must itself be a pool member. */
	if ((g_mPool.head != NULL) && !zb_buf_inPool(g_mPool.head)) {
		ZB_EXCEPTION_POST(SYS_EXCEPTTION_ZB_BUFFER_EXCEPTION);
	}

	g_mPool.usedNum--;
	buf->next = g_mPool.head;
	g_mPool.head = buf;

	if (!zb_buf_inPool(buf)) {
		ZB_EXCEPTION_POST(SYS_EXCEPTTION_ZB_BUFFER_EXCEPTION);
	}

	buf->hdr.used = 0;
	buf->hdr.handle = ZB_RETURN_INVALID;

	drv_restore_irq(irq);

	return BUFFER_SUCC;
}

void *zb_bufInitalloc(zb_buf_t *p, uint8_t size)
{
	return &p->buf[((uint8_t)(ZB_BUF_SIZE - (uint8_t)(size + 8U))) & (uint8_t)~3U];
}

void zb_buf_clear(zb_buf_t *p)
{
	uint8_t used = p->hdr.used;
	uint32_t irq = drv_disable_irq();

	memset(&p->hdr, 0, sizeof(p->hdr));
	p->hdr.used = used;

	drv_restore_irq(irq);
}

bool is_zb_buf(void *arg)
{
	uintptr_t address = (uintptr_t)arg;
	uintptr_t poolStart = (uintptr_t)&g_mPool.pool[0];
	uintptr_t poolEnd = (uintptr_t)&g_mPool.pool[ZB_BUF_POOL_SIZE];

	return address >= poolStart && address < poolEnd;
}

/* The vendor library keeps the allocation wrappers in .ram_code next to
 * zb_buf_get (_router/zb_buffer.s:.ram_code+0x194 and +0x19c). */
_attribute_ram_code_ zb_buf_t *zb_buf_allocate(void)
{
	return zb_buf_get();
}

_attribute_ram_code_ uint8_t *zb_getRxBuf(void)
{
	zb_buf_t *buf = zb_buf_get();

	return (buf != NULL) ? zb_BufToPhyRxBuf((uint8_t *)buf) : NULL;
}

/* Reconstructed from _router/zb_buffer.s:.ram_code+0 (0x194 bytes). */
_attribute_ram_code_ _attribute_no_inline_ zb_buf_t *zb_buf_get(void)
{
	uint32_t irq = drv_disable_irq();

	if (g_mPool.usedNum < ZB_BUF_POOL_SIZE) {
		zb_buf_t *buf = g_mPool.head;

		/* A non-NULL head outside the pool is reported. */
		if ((buf != NULL) && !zb_buf_inPool(buf)) {
			ZB_EXCEPTION_POST(SYS_EXCEPTTION_ZB_BUFFER_EXCEPTION);
		}

		buf = g_mPool.head;

		if (buf != NULL) {
			g_mPool.head = buf->next;
			buf->next = NULL;
			memset(&buf->hdr, 0, sizeof(buf->hdr));
			memset(buf->buf, 0, sizeof(buf->buf));
			g_mPool.usedNum++;
			buf->hdr.used = 1;
			buf->allocCnt++;

			drv_restore_irq(irq);
			return buf;
		}

		/* An empty free list while the pool is not fully handed out. */
		if (g_mPool.usedNum < ZB_BUF_POOL_SIZE) {
			ZB_EXCEPTION_POST(SYS_EXCEPTTION_ZB_BUFFER_EXCEPTION);
		}
	}

	drv_restore_irq(irq);
	g_sysDiags.packetBufferAllocateFailures++;

	return NULL;
}
