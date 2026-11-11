/*
 * Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Zephyr replacement for the vendor zigbee/common/includes/zb_buffer.h.
 *
 * The vendor header describes a statically allocated pool (zb_buf_pool_t,
 * g_mPool) and includes tl_common.h to get at the chip_8258 platform. This
 * port backs the same API with Zephyr primitives instead, so it keeps the
 * declarations the imported stack uses and drops the pool layout. The buffer
 * types themselves live in zb_common.h.
 *
 * Implemented by platform/zephyr/zb_buffer_zephyr.c.
 */
#ifndef ZEPHYR_SUBSYS_ZIGBEE_COMMON_INCLUDES_ZB_BUFFER_H_
#define ZEPHYR_SUBSYS_ZIGBEE_COMMON_INCLUDES_ZB_BUFFER_H_

#include "zb_common.h"

zb_buf_t *zb_buf_allocate(void);
void zb_buf_free(zb_buf_t *buf);
void zb_buf_clear(zb_buf_t *p);
bool is_zb_buf(void *p);
void *zb_bufInitalloc(zb_buf_t *p, uint8_t size);

#define ZB_BUF_INITIAL_ALLOC(p, size, ptr, type)                                                   \
	do {                                                                                       \
		(ptr) = (type)zb_bufInitalloc((p), (size));                                        \
	} while (0)

#define ZB_COPY_BUF(dst, src)                                                                      \
	do {                                                                                       \
		memcpy((dst), (src), ZB_BUF_SIZE + sizeof(zb_buf_hdr_t) - 1);                      \
		zb_buf_rx_snapshot_copy((zb_buf_t *)(dst), (const zb_buf_t *)(src));               \
	} while (0)

void zb_BufferInit(void);
uint8_t *zb_phyRxBufTozbBuf(uint8_t *p);
uint8_t *zb_BufToPhyRxBuf(uint8_t *p);
uint8_t *zb_getRxBuf(void);

#define ZB_RXBUF_TO_INBUF(p) zb_phyRxBufTozbBuf(p)
#define ZB_INBUF_TO_RXBUF(p) zb_BufToPhyRxBuf(p)

/*
 * Buffer references keep the vendor's compact pool-index ABI; the Zephyr
 * buffer adapter owns the index mapping, so no pool object is exposed.
 */
zb_buf_t *zb_buf_owner_of(const void *payload);
zb_buf_t *zb_buf_from_ref(uint8_t ref);
uint8_t zb_buf_to_ref(zb_buf_t *buf);

#define ZB_BUF_FROM_REF(ref) zb_buf_from_ref(ref)
#define ZB_REF_FROM_BUF(p)   zb_buf_to_ref(p)

uint8_t zb_buf_rx_free_count(void);
uint8_t *zb_buf_rx_payload_capture(zb_buf_t *buf, const uint8_t *data, uint8_t len);
/* Copy the received frame captured behind src, see zb_buf_rx_payload_capture(). */
void zb_buf_rx_snapshot_copy(zb_buf_t *dst, const zb_buf_t *src);
/**
 * Pointer into dst that corresponds to p, a pointer into src, after
 * ZB_COPY_BUF(dst, src). p may point into the buffer or into the received
 * frame it holds.
 *
 * @return The pointer in dst, or NULL when dst has no copy of the frame.
 */
uint8_t *zb_buf_rebase(const zb_buf_t *src, zb_buf_t *dst, const uint8_t *p);

#endif /* ZEPHYR_SUBSYS_ZIGBEE_COMMON_INCLUDES_ZB_BUFFER_H_ */
