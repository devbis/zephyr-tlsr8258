/* SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors */
/* SPDX-License-Identifier: Apache-2.0 */

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/kernel.h>

#include "zb_common.h"

#include "ev_timer.h"

#include "aps/aps_api.h"
#include "zdo/zdo.h"
#include "zdo/zdo_nwk_manager.h"

#include "zb_ed_sleep.h"

/*
 * Deadline of the fast poll window as a k_uptime_get_32() value, 0 when no
 * window was ever opened. It is written from the receive path and read by the
 * Zigbee thread; a single aligned word needs no further locking.
 */
static volatile uint32_t zb_ed_poll_fast_until;
static bool zb_ed_poll_fast_applied;
static uint32_t zb_ed_poll_slow_rate;

static bool zb_ed_poll_window_open(void)
{
	uint32_t until = zb_ed_poll_fast_until;

	return (until != 0U) && ((int32_t)(until - k_uptime_get_32()) > 0);
}

void zb_ed_poll_note_activity(void)
{
	uint32_t until;

	if (CONFIG_ZIGBEE_ED_FAST_POLL_WINDOW_MS == 0) {
		return;
	}

	until = k_uptime_get_32() + CONFIG_ZIGBEE_ED_FAST_POLL_WINDOW_MS;
	zb_ed_poll_fast_until = (until == 0U) ? 1U : until;
}

bool zb_ed_poll_fast_active(void)
{
	return zb_ed_poll_window_open() || zb_ed_poll_fast_applied;
}

static void zb_ed_poll_rate_apply(uint32_t rate)
{
	if (zdo_af_get_syn_rate() != rate) {
		(void)zdo_set_pollRate(rate);
	}
}

void zb_ed_poll_service(void)
{
	bool fast;

	if ((g_zbNwkCtx.joined == 0U) || !aps_ib.aps_authenticated ||
	    (zdo_nwk_mngr()->pollEvt == NULL) || (g_zbMacPib.rxOnWhenIdle != 0U)) {
		return;
	}

	fast = zb_ed_poll_window_open();
	if (fast == zb_ed_poll_fast_applied) {
		return;
	}

	if (fast) {
		uint32_t current = zdo_af_get_syn_rate();

		zb_ed_poll_slow_rate = (current != 0U) ? current :
					(uint32_t)CONFIG_ZIGBEE_ED_POLL_INTERVAL_MS;
		zb_ed_poll_rate_apply(CONFIG_ZIGBEE_ED_FAST_POLL_INTERVAL_MS);
	} else {
		zb_ed_poll_rate_apply(zb_ed_poll_slow_rate);
	}

	zb_ed_poll_fast_applied = fast;
}

void zb_ed_poll_poll_now(void)
{
	/* The periodic poll keeps its interval; only this expiry is advanced. */
	if (zdo_nwk_mngr()->pollEvt != NULL) {
		(void)ev_timer_expire_in(zdo_nwk_mngr()->pollEvt, 1U);
	}
}
