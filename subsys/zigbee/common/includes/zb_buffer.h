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
void *tl_bufInitalloc(zb_buf_t *p, u8 size);

#define TL_BUF_INITIAL_ALLOC(p, size, ptr, type)                                                   \
	do {                                                                                       \
		(ptr) = (type)tl_bufInitalloc((p), (size));                                        \
	} while (0)

#define TL_COPY_BUF(dst, src)                                                                      \
	do {                                                                                       \
		memcpy((dst), (src), ZB_BUF_SIZE + sizeof(zb_buf_hdr_t) - 1);                      \
	} while (0)

void tl_zbBufferInit(void);
u8 *tl_phyRxBufTozbBuf(u8 *p);
u8 *tl_zbBufToPhyRxBuf(u8 *p);
u8 *tl_getRxBuf(void);

#define TL_RXBUF_TO_INBUF(p) tl_phyRxBufTozbBuf(p)
#define TL_INBUF_TO_RXBUF(p) tl_zbBufToPhyRxBuf(p)

/*
 * Buffer references keep the vendor's compact pool-index ABI; the Zephyr
 * buffer adapter owns the index mapping, so no pool object is exposed.
 */
zb_buf_t *zb_buf_from_ref(u8 ref);
u8 zb_buf_to_ref(zb_buf_t *buf);

#define ZB_BUF_FROM_REF(ref) zb_buf_from_ref(ref)
#define ZB_REF_FROM_BUF(p)   zb_buf_to_ref(p)

u8 zb_buf_rx_free_count(void);
u8 *zb_buf_rx_payload_capture(zb_buf_t *buf, const u8 *data, u8 len);

#endif /* ZEPHYR_SUBSYS_ZIGBEE_COMMON_INCLUDES_ZB_BUFFER_H_ */
