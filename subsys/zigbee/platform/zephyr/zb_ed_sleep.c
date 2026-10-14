/* SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors */
/* SPDX-License-Identifier: Apache-2.0 */

#include <stdint.h>
#include <stdbool.h>

#include <zephyr/device.h>
#include <zephyr/drivers/retained_mem.h>
#include <zephyr/init.h>
#include <zephyr/logging/log.h>
#include <zephyr/pm/pm.h>
#include <zephyr/pm/policy.h>
#include <zephyr/sys/util.h>
#include <zephyr/zigbee/zb_bootstrap.h>

/* zb_common.h defines the buffer, descriptor and confirmation types the
 * headers below declare their prototypes against, so it comes first.
 */
#include "zb_common.h"

#include "ev_timer.h"
#include "aps/aps_api.h"
#include "bdb/includes/bdb.h"
#include "mac/includes/mac_trx_api.h"
#include "mac/includes/tl_zb_mac.h"
#include "ss/ss_zdoSecurityME.h"
#include "zdo/zdo.h"
#include "zdo/zdo_nwk_manager.h"
#include "ss_security_flags.h"

#include "zb_ed_sleep.h"

LOG_MODULE_REGISTER(zigbee_ed_sleep, CONFIG_ZIGBEE_LOG_LEVEL);

/*
 * A deep-sleep interval shorter than this cannot repay the TLSR wake and RF
 * bootstrap latency and would create a reset loop instead of a useful poll.
 */
#define ZB_ED_SLEEP_MIN_INTERVAL_MS 20U

/*
 * The transmit queue head, NULL while the queue is empty.  The vendor SDK
 * header declares neither this nor any other emptiness test, so the port
 * declares it here rather than diverging from an imported header.  The count
 * that is exported, mac_data_pending(), reports the frames queued *behind* the
 * head and reads as zero with one frame still waiting, which is the one case
 * this check exists for.
 */
tx_data_queue *get_next_data(void);

enum zb_ed_sleep_reject_reason {
	ZB_ED_SLEEP_REJECT_NOT_JOINED,
	ZB_ED_SLEEP_REJECT_RX_ON,
	ZB_ED_SLEEP_REJECT_NETWORK_BUSY,
	ZB_ED_SLEEP_REJECT_MAC_BUSY,
	ZB_ED_SLEEP_REJECT_TASK_QUEUE,
	ZB_ED_SLEEP_REJECT_NO_DEADLINE,
	ZB_ED_SLEEP_REJECT_SHORT_INTERVAL,
	ZB_ED_SLEEP_REJECT_RADIO,
	ZB_ED_SLEEP_REJECT_FAST_POLL,
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
	       (get_next_data() != NULL);
}

static bool zb_ed_sleep_ready(void)
{
	ev_timer_event_t *poll_evt;
	ev_timer_event_t *nearest_evt;
	uint32_t sleep_ms;

	if (g_zbNwkCtx.joined == 0U) {
		zb_ed_sleep_rejected(ZB_ED_SLEEP_REJECT_NOT_JOINED);
		return false;
	}

	if (g_zbMacPib.rxOnWhenIdle != 0U) {
		zb_ed_sleep_rejected(ZB_ED_SLEEP_REJECT_RX_ON);
		return false;
	}

	if ((g_zbNwkCtx.state != NLME_STATE_IDLE) ||
	    (g_zbNwkCtx.user_state != NLME_IDLE) ||
	    (g_bdbCtx.state != BDB_STATE_IDLE) ||
	    (g_bdbAttrs.commissioningStatus == BDB_COMMISSION_STA_IN_PROGRESS) ||
	    (ss_ib_security_level_get() == 0U) ||
	    !zdo_ifZdoNwkManagerIdle() ||
	    /* Association is not secure-join completion: until the trust centre
	     * delivers the network key zdo_startup_complete() is still waiting
	     * on zdo_nwkAuthTimeoutStart(), and an end device that sleeps
	     * through that wait never receives the key.
	     */
	    !aps_ib.aps_authenticated ||
	    (zdo_nwk_mngr()->savedBuf != NULL) ||
	    (zdo_nwk_mngr()->discEvt != NULL) ||
	    (zdo_nwk_mngr()->authEvt != NULL) ||
	    (zdo_nwk_mngr()->backoffEvt != NULL)) {
		zb_ed_sleep_rejected(ZB_ED_SLEEP_REJECT_NETWORK_BUSY);
		return false;
	}

	if (zb_ed_sleep_has_pending_mac_work()) {
		zb_ed_sleep_rejected(ZB_ED_SLEEP_REJECT_MAC_BUSY);
		return false;
	}

	if (!zb_taskq_is_empty() || !zb_taskq_layer_queues_empty()) {
		zb_ed_sleep_rejected(ZB_ED_SLEEP_REJECT_TASK_QUEUE);
		return false;
	}

	if (zb_ed_poll_fast_active()) {
		zb_ed_sleep_rejected(ZB_ED_SLEEP_REJECT_FAST_POLL);
		return false;
	}

	poll_evt = zdo_nwk_mngr()->pollEvt;
	if ((poll_evt == NULL) || !ev_timer_exist(poll_evt)) {
		zb_ed_sleep_rejected(ZB_ED_SLEEP_REJECT_NO_DEADLINE);
		return false;
	}

	/*
	 * Other timers (the ZCL second clock, the commissioning info save, an
	 * application retry) are not state that has to survive the sleep: their
	 * owners arm them again after the restart. The device wakes for the
	 * nearest of them, which is never later than the poll.
	 */
	nearest_evt = ev_timer_nearestGet();
	if (nearest_evt == NULL) {
		zb_ed_sleep_rejected(ZB_ED_SLEEP_REJECT_NO_DEADLINE);
		return false;
	}

	sleep_ms = ev_timer_timeout_get(nearest_evt);
	if (sleep_ms < ZB_ED_SLEEP_MIN_INTERVAL_MS) {
		zb_ed_sleep_rejected(ZB_ED_SLEEP_REJECT_SHORT_INTERVAL);
		return false;
	}

	return true;
}

/*
 * Layout of the record kept in the retained memory across a deep sleep: the
 * outgoing frame counter and an XOR checksum over it.
 */
#define ZB_ED_SLEEP_RECORD_SIZE 5U

static const struct device *const zb_ed_sleep_retained =
	DEVICE_DT_GET(DT_CHOSEN(zigbee_retained_mem));

/* True while the SoC may pick PM_STATE_SOFT_OFF; the SoC starts with it locked. */
static bool zb_ed_sleep_allowed;

static uint8_t zb_ed_sleep_record_checksum(const uint8_t *record)
{
	uint8_t sum = 0x5aU;

	for (size_t i = 0U; i < (ZB_ED_SLEEP_RECORD_SIZE - 1U); i++) {
		sum ^= record[i];
	}

	return sum;
}

static void zb_ed_sleep_record_store(uint32_t frame_counter)
{
	uint8_t record[ZB_ED_SLEEP_RECORD_SIZE];

	record[0] = (uint8_t)frame_counter;
	record[1] = (uint8_t)(frame_counter >> 8);
	record[2] = (uint8_t)(frame_counter >> 16);
	record[3] = (uint8_t)(frame_counter >> 24);
	record[4] = zb_ed_sleep_record_checksum(record);

	(void)retained_mem_write(zb_ed_sleep_retained, 0, record, sizeof(record));
}

static bool zb_ed_sleep_record_load(uint32_t *frame_counter)
{
	uint8_t record[ZB_ED_SLEEP_RECORD_SIZE];

	if (retained_mem_read(zb_ed_sleep_retained, 0, record, sizeof(record)) != 0) {
		return false;
	}

	if (record[4] != zb_ed_sleep_record_checksum(record)) {
		return false;
	}

	*frame_counter = (uint32_t)record[0] | ((uint32_t)record[1] << 8) |
			 ((uint32_t)record[2] << 16) | ((uint32_t)record[3] << 24);

	return true;
}

/*
 * The notifier runs in the idle thread just before the SoC enters the state,
 * so the counter saved here is the last one the device used.
 */
static void zb_ed_sleep_state_entry(enum pm_state state)
{
	if (state == PM_STATE_SOFT_OFF) {
		zb_ed_sleep_record_store(ss_ib.outgoingFrameCounter);
	}
}

/* Reached only when the SoC returned without restarting. */
static void zb_ed_sleep_state_exit(enum pm_state state)
{
	if (state == PM_STATE_SOFT_OFF) {
		(void)retained_mem_clear(zb_ed_sleep_retained);
	}
}

static struct pm_notifier zb_ed_sleep_notifier = {
	.state_entry = zb_ed_sleep_state_entry,
	.state_exit = zb_ed_sleep_state_exit,
};

static int zb_ed_sleep_init(void)
{
	if (!device_is_ready(zb_ed_sleep_retained)) {
		LOG_ERR("SED deep sleep: retained memory not ready");
		return -ENODEV;
	}

	pm_notifier_register(&zb_ed_sleep_notifier);
	pm_policy_state_lock_get(PM_STATE_SUSPEND_TO_IDLE, PM_ALL_SUBSTATES);

	return 0;
}

/* After the retained memory driver, which initializes at POST_KERNEL 40. */
BUILD_ASSERT(CONFIG_RETAINED_MEM_INIT_PRIORITY < 99);
SYS_INIT(zb_ed_sleep_init, POST_KERNEL, 99);

bool zb_ed_sleep_woke_from_deep(void)
{
	uint32_t frame_counter;

	return zb_ed_sleep_record_load(&frame_counter);
}

int zb_ed_sleep_retained_frame_counter(uint32_t *frame_counter)
{
	if (frame_counter == NULL) {
		return -EINVAL;
	}

	return zb_ed_sleep_record_load(frame_counter) ? 0 : -ENOENT;
}

void zb_ed_sleep_wake_clear(void)
{
	(void)retained_mem_clear(zb_ed_sleep_retained);
}

static void zb_ed_sleep_allow(bool allow)
{
	if (allow == zb_ed_sleep_allowed) {
		return;
	}

	if (allow) {
		/*
		 * The radio driver owns RF IRQ, DMA and the started state. Stop it
		 * only after every stack-busy check has passed, immediately before
		 * the SoC may enter the state.
		 */
		int rc = zb_platform_radio_stop();

		if (rc != 0) {
			LOG_WRN("SED deep sleep: radio stop failed rc=%d", rc);
			zb_ed_sleep_rejected(ZB_ED_SLEEP_REJECT_RADIO);
			return;
		}
		pm_policy_state_lock_put(PM_STATE_SOFT_OFF, PM_ALL_SUBSTATES);
	} else {
		pm_policy_state_lock_get(PM_STATE_SOFT_OFF, PM_ALL_SUBSTATES);
	}

	zb_ed_sleep_allowed = allow;
}

void zb_ed_sleep_maybe(void)
{
	zb_ed_sleep_allow(zb_ed_sleep_ready());
}
