/*
 * Copyright 2026 libzigbee
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "zb_common.h"
#include <stdint.h>

/* Both are .bss-local u32 in the vendor object and are written with 32-bit
 * stores ("b8: tstorer r3,[r1,#0]" / "bc: tstorer r4,[r3,#0]"). */
static u32 T_zbBufFreeDbg = 0;
static u32 T_zbBufDbg = 0;

_attribute_ram_code_ _attribute_no_inline_ zb_buf_t *zb_buf_get(void);

/* The vendor library bounds-checks against the runtime pool size rather than
 * ZB_BUF_POOL_NUM; see "150: ... tmuls r3,#208; tadds r3,#8". */
static bool zb_buf_inPool(const void *p)
{
	return (uintptr_t)p >= (uintptr_t)&g_mPool.pool[0];
}

void tl_zbBufferInit(void)
{
	u32 poolBytes = zbBufferSizeGet();
	u8 poolCount;

	memset(&g_mPool, 0, poolBytes);

	g_mPool.head = &g_mPool.pool[0];
	poolCount = ZB_BUF_POOL_SIZE;
	for (u8 i = 0; (u8)(i + 1U) < poolCount; i++) {
		g_mPool.pool[i].next = &g_mPool.pool[i + 1U];
	}
	g_mPool.pool[(poolCount > 0U) ? (poolCount - 1U) : 0U].next = NULL;
}

u8 *tl_phyRxBufTozbBuf(u8 *p)
{
	return p - RX_ZBBUF_OFFSET;
}

u8 *tl_zbBufToPhyRxBuf(u8 *p)
{
	return p + RX_ZBBUF_OFFSET;
}

/* Reconstructed from _router/zb_buffer.s:.text.zb_buf_free (0x20a bytes).  The
 * whole body runs with interrupts disabled, validates the pointer and the pool
 * head, keeps the per-buffer alloc/free counters, and never clears the payload.
 * It always returns BUFFER_SUCC ("180: tmovs r0, #0"). */
u8 zb_buf_free(zb_buf_t *buf)
{
	u32 irq = drv_disable_irq();

	/* 10: the raw MAC TX buffer is not part of the pool. */
	if (buf == (zb_buf_t *)g_zbMacCtx.txRawDataBuf) {
		if (buf == NULL) {
			ZB_EXCEPTION_POST(SYS_EXCEPTTION_ZB_BUFFER_EXCEPTION_FREE_OVERFLON);
		}

		drv_restore_irq(irq);
		return BUFFER_SUCC;
	}

	/* 2c/1b8: a pointer below the pool is reported, then freed anyway. */
	if ((uintptr_t)buf < (uintptr_t)&g_mPool.pool[0]) {
		ZB_EXCEPTION_POST(SYS_EXCEPTTION_ZB_BUFFER_EXCEPTION_FREE_OVERFLON);
	}

	buf->freeCnt++;

	/* 72..c2: a buffer still queued in the MAC TX FIFO, a free without a
	 * matching allocate, or a buffer that is not marked used. */
	if (buf->hdr.macTxFifo || (buf->freeCnt > buf->allocCnt) || (buf->hdr.used == 0U)) {
		T_zbBufFreeDbg = ((u32)buf->hdr.macTxFifo << 24) |
				 ((buf->freeCnt > buf->allocCnt) ? 0x10000U : 0U) |
				 (u32)buf->hdr.used;
		T_zbBufDbg = (u32)(unsigned long)buf;
		ZB_EXCEPTION_POST(SYS_EXCEPTTION_ZB_BUFFER_EXCEPTION_FREE_MULTI);
	}

	/* c6/1ce: the current pool head must itself be a pool member. */
	if ((g_mPool.head != NULL) && !zb_buf_inPool(g_mPool.head)) {
		ZB_EXCEPTION_POST(SYS_EXCEPTTION_ZB_BUFFER_EXCEPTION);
	}

	g_mPool.usedNum--; /* 118: unconditional */
	buf->next = g_mPool.head;
	g_mPool.head = buf;

	if (!zb_buf_inPool(buf)) {
		ZB_EXCEPTION_POST(SYS_EXCEPTTION_ZB_BUFFER_EXCEPTION);
	}

	buf->hdr.used = 0;
	buf->hdr.handle = TL_RETURN_INVALID; /* 174: tmovs r2,#255; tstorerb r2,[r4,#193] */

	drv_restore_irq(irq);

	return BUFFER_SUCC;
}

void *tl_bufInitalloc(zb_buf_t *p, u8 size)
{
	return &p->buf[((u8)(ZB_BUF_SIZE - (u8)(size + 8U))) & (u8)~3U];
}

void zb_buf_clear(zb_buf_t *p)
{
	u8 used = p->hdr.used;
	u32 irq = drv_disable_irq();

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

_attribute_ram_code_ u8 *tl_getRxBuf(void)
{
	zb_buf_t *buf = zb_buf_get();

	return (buf != NULL) ? tl_zbBufToPhyRxBuf((u8 *)buf) : NULL;
}

/* Reconstructed from _router/zb_buffer.s:.ram_code+0 (0x194 bytes). */
_attribute_ram_code_ _attribute_no_inline_ zb_buf_t *zb_buf_get(void)
{
	u32 irq = drv_disable_irq();

	if (g_mPool.usedNum < ZB_BUF_POOL_SIZE) {
		zb_buf_t *buf = g_mPool.head;

		/* 4c/176: a non-NULL head outside the pool is reported. */
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

		/* 12c: an empty free list while the pool is not fully handed out. */
		if (g_mPool.usedNum < ZB_BUF_POOL_SIZE) {
			ZB_EXCEPTION_POST(SYS_EXCEPTTION_ZB_BUFFER_EXCEPTION);
		}
	}

	drv_restore_irq(irq);
	g_sysDiags.packetBufferAllocateFailures++;

	return NULL;
}
