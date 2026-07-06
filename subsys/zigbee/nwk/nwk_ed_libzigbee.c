/* SPDX-License-Identifier: Apache-2.0 */
/*
 * rx-off End-Device poll glue for the experimental CONFIG_ZIGBEE_ED_LIBZIGBEE
 * build (ED on the full libzigbee stack).
 *
 * An rx-off ED must poll its parent (MAC DATA_REQ) to fetch frames the parent /
 * trust center holds for it (most importantly the Transport-Key the TC defers
 * right after association, then the interview requests). In the libzigbee stack
 * that polling is ALREADY driven natively:
 *   - zdo_set_pollRate(500) is armed right after association
 *     (zdo_nwk_manager.c), so pollRateCb -> zdo_syncReq polls the parent every
 *     500 ms while rx-off; and
 *   - nwk_data.c issues an immediate quick-poll whenever a MAC ACK reports
 *     frame-pending, so queued frames are drained back-to-back.
 * That native machinery is self-paced (one poll per timer tick / per pending
 * frame). A second, independent poll driver here would ISSUE polls faster than
 * native_sim confirms them; each in-flight poll holds a zb_buf, so the pool
 * (ZB_BUF_POOL_NUM=18) saturates within seconds and aps_ack_send() then fails
 * to allocate an APS-ACK buffer for the interview requests — stalling the
 * interview. So these platform hooks are intentionally inert for the
 * libzigbee-ED: the native libzigbee poll owns polling.
 *
 * These are STRONG definitions overriding the weak no-op
 * tl_zbNwkEdMinimalInterviewPollStart in zb_primitive_dispatch.c;
 * tl_zbNwkEdMinimalPollRestart was moved out of zb_ed_libzigbee_stubs.c so this
 * is its single definition. Compiled only for CONFIG_ZIGBEE_ED_LIBZIGBEE.
 */
#include "zb_common_stub.h"

/* Post-join / interview poll burst hook (BDB). Native pollRate + frame-pending
 * quick-poll already drive this; nothing to do. */
void tl_zbNwkEdMinimalInterviewPollStart(u8 count, u32 intervalMs)
{
	(void)count;
	(void)intervalMs;
}

/* Steady-state poll-rate hook (zb_setPollRate). Native zdo_set_pollRate owns the
 * poll timer for the libzigbee ED; nothing to do. */
void tl_zbNwkEdMinimalPollRestart(u32 timeoutMs)
{
	(void)timeoutMs;
}
