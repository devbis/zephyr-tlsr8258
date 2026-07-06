/* SPDX-License-Identifier: Apache-2.0 */
/*
 * rx-off End-Device poll driver for the experimental CONFIG_ZIGBEE_ED_LIBZIGBEE
 * build (ED on the full libzigbee stack).
 *
 * An rx-off ED must poll its parent (MAC DATA_REQ) to fetch frames the parent /
 * trust center holds for it — most importantly the Transport-Key the TC defers
 * right after association. The shared BDB post-join path calls
 * tl_zbNwkEdMinimalInterviewPollStart() to drive that TCLK poll, and
 * zb_setPollRate() calls tl_zbNwkEdMinimalPollRestart() for the steady-state
 * poll cycle. In the minimal-ED build those live in nwk_ed_minimal.c; here they
 * route to the libzigbee poll primitive endDevMacDataPoll() (nwk_nlme.c) through
 * a single ev_timer.
 *
 * These are STRONG definitions: they override the weak no-op
 * tl_zbNwkEdMinimalInterviewPollStart in zb_primitive_dispatch.c. The matching
 * tl_zbNwkEdMinimalPollRestart is removed from zb_ed_libzigbee_stubs.c so this
 * is the single definition. Compiled only for CONFIG_ZIGBEE_ED_LIBZIGBEE.
 */
#include "zb_common_stub.h"
#include "os/ev_timer.h"

/* libzigbee ED poll primitive (nwk_nlme.c): posts MAC_MLME_POLL_REQ to the
 * parent recorded in the neighbor table; a no-op if no parent is known yet. */
extern void endDevMacDataPoll(void);

#define ED_LZ_POLL_DEFAULT_INTERVAL_MS 200U
#define ED_LZ_POLL_DEFAULT_COUNT       20U
#define ED_LZ_POLL_INFINITE            0xFFFFU

static ev_timer_event_t *ed_lz_poll_timer_evt;
static u16 ed_lz_poll_remaining;
static u32 ed_lz_poll_interval_ms = ED_LZ_POLL_DEFAULT_INTERVAL_MS;

static int ed_lz_poll_timer_cb(void *arg)
{
	(void)arg;

	if (ed_lz_poll_remaining == 0U) {
		ed_lz_poll_timer_evt = NULL;
		return -1; /* stop */
	}

	endDevMacDataPoll();
	if (ed_lz_poll_remaining != ED_LZ_POLL_INFINITE) {
		ed_lz_poll_remaining--;
	}

	return (int)ed_lz_poll_interval_ms; /* reschedule */
}

static void ed_lz_poll_start(u16 count, u32 intervalMs)
{
	ed_lz_poll_remaining = count;
	ed_lz_poll_interval_ms = (intervalMs != 0U) ? intervalMs : ED_LZ_POLL_DEFAULT_INTERVAL_MS;

	if (ed_lz_poll_timer_evt != NULL) {
		ev_timer_taskCancel(&ed_lz_poll_timer_evt);
	}

	/* Fire the first poll immediately so a deferred Transport-Key is fetched
	 * promptly rather than one interval later. */
	endDevMacDataPoll();
	if (ed_lz_poll_remaining != ED_LZ_POLL_INFINITE && ed_lz_poll_remaining > 0U) {
		ed_lz_poll_remaining--;
	}

	if (ed_lz_poll_remaining > 0U) {
		ed_lz_poll_timer_evt = ev_timer_taskPost(ed_lz_poll_timer_cb, NULL,
							 ed_lz_poll_interval_ms);
	}
}

/* Post-join / interview poll burst (BDB TCLK window). count==0 -> default. */
void tl_zbNwkEdMinimalInterviewPollStart(u8 count, u32 intervalMs)
{
	ed_lz_poll_start((count != 0U) ? (u16)count : ED_LZ_POLL_DEFAULT_COUNT, intervalMs);
}

/* Steady-state poll cycle (zb_setPollRate). Polls indefinitely at the rate. */
void tl_zbNwkEdMinimalPollRestart(u32 timeoutMs)
{
	ed_lz_poll_start(ED_LZ_POLL_INFINITE, timeoutMs);
}
