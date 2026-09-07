/* SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors */
/* SPDX-License-Identifier: Apache-2.0 */

#include <stdint.h>
#include <stdbool.h>

#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>
#include <zephyr/zigbee/zb_bootstrap.h>

#include "ev_timer.h"
#include "bdb/includes/bdb.h"
#include "mac/includes/mac_trx_api.h"
#include "mac/includes/tl_zb_mac.h"
#include "ss/ss_internal.h"
#include "zdo/zdo_internal.h"
#include "zb_common_stub.h"

#include <tlsr825x/power.h>

LOG_MODULE_REGISTER(zigbee_ed_sleep, CONFIG_ZIGBEE_LOG_LEVEL);

/*
 * A deep-sleep interval shorter than this cannot repay the TLSR wake and RF
 * bootstrap latency and would create a reset loop instead of a useful poll.
 */
#define ZB_ED_SLEEP_MIN_INTERVAL_MS 20U

enum zb_ed_sleep_reject_reason {
	ZB_ED_SLEEP_REJECT_NOT_JOINED,
	ZB_ED_SLEEP_REJECT_RX_ON,
	ZB_ED_SLEEP_REJECT_NETWORK_BUSY,
	ZB_ED_SLEEP_REJECT_MAC_BUSY,
	ZB_ED_SLEEP_REJECT_TASK_QUEUE,
	ZB_ED_SLEEP_REJECT_TRANSIENT_TIMER,
	ZB_ED_SLEEP_REJECT_NO_DEADLINE,
	ZB_ED_SLEEP_REJECT_SHORT_INTERVAL,
	ZB_ED_SLEEP_REJECT_RADIO,
	ZB_ED_SLEEP_REJECT_MAX,
};

static uint32_t zb_ed_sleep_reject_count[ZB_ED_SLEEP_REJECT_MAX];
static enum zb_ed_sleep_reject_reason zb_ed_sleep_last_reject =
	ZB_ED_SLEEP_REJECT_MAX;

static void zb_ed_sleep_rejected(enum zb_ed_sleep_reject_reason reason)
{
	uint32_t count;

	count = ++zb_ed_sleep_reject_count[reason];
	if ((reason != zb_ed_sleep_last_reject) || ((count & 0xffU) == 0U)) {
		LOG_DBG("SED deep sleep rejected: reason=%u count=%u",
			(unsigned int)reason, (unsigned int)count);
	}
	zb_ed_sleep_last_reject = reason;
}

static bool zb_ed_sleep_has_pending_mac_work(void)
{
	const u8 busy_flags = RX_BUSY | TX_BUSY | TX_ACKPACKET | RX_WAITINGACK |
				      RX_DATAPENDING | TX_UNDERWAY;

	return (g_zbMacCtx.status != ZB_MAC_STATE_NORMAL) ||
	       ((rf_busyFlag & busy_flags) != 0U) ||
	       (mac_getTrxState() != MAC_TX_IDLE) || tl_zbMacStateBusy() ||
	       !mac_tx_queue_empty();
}

void zb_ed_sleep_maybe(void)
{
	ev_timer_event_t *poll_evt;
	ev_timer_event_t *nearest_evt;
	uint32_t sleep_ms;
	int rc;

	if (g_zbNwkCtx.joined == 0U) {
		zb_ed_sleep_rejected(ZB_ED_SLEEP_REJECT_NOT_JOINED);
		return;
	}

	if (g_zbMacPib.rxOnWhenIdle != 0U) {
		zb_ed_sleep_rejected(ZB_ED_SLEEP_REJECT_RX_ON);
		return;
	}

	if ((g_zbNwkCtx.state != NLME_STATE_IDLE) ||
	    (g_zbNwkCtx.user_state != NLME_IDLE) ||
	    (g_bdbCtx.state != BDB_STATE_IDLE) ||
	    (g_bdbAttrs.commissioningStatus == BDB_COMMISSION_STA_IN_PROGRESS) ||
	    (ss_ib.securityLevel == 0U) ||
	    !zdo_ifZdoNwkManagerIdle() || zdo_secure_startup_pending ||
	    (zdo_nwk_mngr()->savedBuf != NULL) ||
	    (zdo_nwk_mngr()->discEvt != NULL) ||
	    (zdo_nwk_mngr()->authEvt != NULL) ||
	    (zdo_nwk_mngr()->backoffEvt != NULL)) {
		zb_ed_sleep_rejected(ZB_ED_SLEEP_REJECT_NETWORK_BUSY);
		return;
	}

	if (zb_ed_sleep_has_pending_mac_work()) {
		zb_ed_sleep_rejected(ZB_ED_SLEEP_REJECT_MAC_BUSY);
		return;
	}

	if (!zb_taskq_is_empty() || !zb_taskq_layer_queues_empty()) {
		zb_ed_sleep_rejected(ZB_ED_SLEEP_REJECT_TASK_QUEUE);
		return;
	}

	poll_evt = zdo_nwk_mngr()->pollEvt;
	if ((poll_evt == NULL) || !ev_timer_exist(poll_evt)) {
		zb_ed_sleep_rejected(ZB_ED_SLEEP_REJECT_NO_DEADLINE);
		return;
	}

	if (ev_timer_has_other_than(poll_evt)) {
		zb_ed_sleep_rejected(ZB_ED_SLEEP_REJECT_TRANSIENT_TIMER);
		return;
	}

	nearest_evt = ev_timer_nearestGet();
	if (nearest_evt != poll_evt) {
		zb_ed_sleep_rejected(ZB_ED_SLEEP_REJECT_TRANSIENT_TIMER);
		return;
	}

	if (!IS_ENABLED(CONFIG_TLSR8258_PM_TIMER_WAKEUP)) {
		zb_ed_sleep_rejected(ZB_ED_SLEEP_REJECT_NO_DEADLINE);
		return;
	}

	sleep_ms = ev_timer_timeout_get(poll_evt);
	if (sleep_ms < ZB_ED_SLEEP_MIN_INTERVAL_MS) {
		zb_ed_sleep_rejected(ZB_ED_SLEEP_REJECT_SHORT_INTERVAL);
		return;
	}

	/*
	 * The radio driver owns RF IRQ, DMA and the started state. Stop it only
	 * after every stack-busy check has passed, immediately before PM entry.
	 */
	rc = zb_platform_radio_stop();
	if (rc != 0) {
		LOG_WRN("SED deep sleep: radio stop failed rc=%d", rc);
		zb_ed_sleep_rejected(ZB_ED_SLEEP_REJECT_RADIO);
		return;
	}

	rc = tlsr8258_pm_save_frame_counter(ss_ib.outgoingFrameCounter);
	if (rc != 0) {
		LOG_ERR("SED deep sleep: frame counter retention failed rc=%d", rc);
		(void)zb_platform_radio_start_on_channel(g_zbMacPib.phyChannelCur);
		return;
	}

	rc = tlsr8258_pm_deep_sleep_for_ms(sleep_ms);
	if (rc != 0) {
		LOG_WRN("SED deep sleep entry failed rc=%d interval=%u ms", rc,
			(unsigned int)sleep_ms);
		if (zb_platform_radio_start_on_channel(g_zbMacPib.phyChannelCur) != 0) {
			LOG_ERR("SED deep sleep: radio recovery failed");
		}
	}
}
