/* SPDX-License-Identifier: Apache-2.0 */
/*
 * app_bdb.c — Commissioning / rejoin / poll-rate / leave policy for the
 * zigbee_shell sample.
 *
 * Contains all runtime BDB state that was previously inlined in main.c.
 * main.c delegates each zb_platform_app_* hook to the functions here.
 */

#include "app_bdb.h"
#include "app_profile.h"

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/zigbee/zb_config.h>

#if !defined(ZDO_ZCL_STUBS_H_)
/* Production build: pull in the real SDK headers for ZDO/ZCL types and
 * constants.  In the host test harness zdo_zcl_stubs.h is pre-included
 * before app_bdb.c, so this block is deliberately skipped there. */
#include "zbapi/zb_api.h"
#include "zcl/zcl_config.h"
#include "zcl/zcl.h"
#endif

LOG_MODULE_REGISTER(app_bdb);

typedef int (*app_bdb_timer_callback_t)(void *data);

struct ev_timer_event_t;

extern struct ev_timer_event_t *ev_timer_taskPost(app_bdb_timer_callback_t func, void *arg,
						   uint32_t t_ms);
extern uint8_t ev_timer_taskCancel(struct ev_timer_event_t **evt);

#define APP_BDB_COMMISSIONING_RETRY_MS 5000U

#if defined(CONFIG_ZIGBEE_BDB)
	static bool commissioning_start_requested;
	static bool bdb_runtime_ready;
	static bool leave_recommission_pending;
	static struct ev_timer_event_t *commissioning_retry_timer;
	__attribute__((weak)) volatile uint32_t zb_nwk_ed_trace[16];
#if defined(CONFIG_ZIGBEE_DEBUG_TRACES)
	volatile uint32_t zb_app_bdb_retry_trace[16] = {0xa4bd0000U};
	volatile uint32_t zb_rejoin_callback_trace[16] = {0xa5c10000U};
	volatile uint32_t zb_restore_diag_trace[16] = {0xa5d10000U};
	static uint8_t zb_app_bdb_retry_trace_pos;
	static uint8_t zb_rejoin_callback_trace_pos;
#endif
	static const uint8_t app_bdb_fixed_tc_addr[8] = {
		0x60, 0x2d, 0xce, 0xfe, 0xff, 0x89, 0xc0, 0x1c,
	};

extern bool zb_isDeviceJoinedNwk(void);
extern bool zdo_ifZdoNwkManagerIdle(void);
extern uint8_t zb_setPollRate(uint32_t newRate);
extern uint32_t zb_getPollRate(void);

#if defined(CONFIG_ZIGBEE_DEBUG_TRACES)
	static void app_bdb_retry_trace_put(uint32_t tag)
	{
		enum {
			trace_slots = sizeof(zb_app_bdb_retry_trace) / sizeof(zb_app_bdb_retry_trace[0]),
	};
	uint8_t slot = (uint8_t)(2U + (zb_app_bdb_retry_trace_pos %
				       (trace_slots - 2U)));

	zb_app_bdb_retry_trace[slot] = tag;
	zb_app_bdb_retry_trace_pos++;
	zb_app_bdb_retry_trace[1] = zb_app_bdb_retry_trace_pos;
}

	void app_bdb_rejoin_callback_trace_put(uint32_t tag)
	{
		enum {
			trace_slots = sizeof(zb_rejoin_callback_trace) /
				      sizeof(zb_rejoin_callback_trace[0]),
	};
	uint8_t slot = (uint8_t)(2U + (zb_rejoin_callback_trace_pos %
				       (trace_slots - 2U)));

	zb_rejoin_callback_trace[slot] = tag;
		zb_rejoin_callback_trace_pos++;
		zb_rejoin_callback_trace[1] = zb_rejoin_callback_trace_pos;
	}
#else
	static void app_bdb_retry_trace_put(uint32_t tag)
	{
		ARG_UNUSED(tag);
	}

	void app_bdb_rejoin_callback_trace_put(uint32_t tag)
	{
		ARG_UNUSED(tag);
	}
#endif

static void app_bdb_activate_poll_rate(void)
{
	uint32_t poll_rate = zb_getPollRate();

	app_bdb_rejoin_callback_trace_put((0x11U << 24) | (poll_rate & 0xffffU));
	if (poll_rate == 0U) {
		poll_rate = POLL_RATE;
	}

	(void)zb_setPollRate(poll_rate);
	app_bdb_rejoin_callback_trace_put((0x12U << 24) | (poll_rate & 0xffffU));
}

static void app_bdb_commissioning_retry_schedule(void);
static void app_bdb_commissioning_retry_cancel(void);
/* forward declaration so retry handler can call it */
void app_bdb_start_commissioning(void);

static int app_bdb_commissioning_retry(void *data)
{
	bool joined = zb_isDeviceJoinedNwk();
	bool idle = zdo_ifZdoNwkManagerIdle();

	ARG_UNUSED(data);
	commissioning_retry_timer = NULL;
	app_bdb_retry_trace_put((0x01U << 24) |
				((uint32_t)(bdb_runtime_ready ? 1U : 0U) << 16) |
				((uint32_t)(leave_recommission_pending ? 1U : 0U) << 15) |
				((uint32_t)(commissioning_start_requested ? 1U : 0U) << 14) |
				((uint32_t)(joined ? 1U : 0U) << 13) |
				((uint32_t)(idle ? 1U : 0U) << 12));

	if (leave_recommission_pending) {
		if (!bdb_runtime_ready || joined || commissioning_start_requested) {
			app_bdb_retry_trace_put(0x02U << 24);
			app_bdb_commissioning_retry_schedule();
			return -1;
		}

		leave_recommission_pending = false;
		app_bdb_start_commissioning();
		if (!commissioning_start_requested) {
			leave_recommission_pending = true;
			app_bdb_commissioning_retry_schedule();
		}
		return -1;
	}

	if (joined) {
		app_bdb_retry_trace_put(0x03U << 24);
		app_bdb_activate_poll_rate();
		commissioning_start_requested = true;
		return -1;
	}
	if (!idle) {
		app_bdb_retry_trace_put(0x04U << 24);
		commissioning_start_requested = true;
		app_bdb_commissioning_retry_schedule();
		return -1;
	}

	commissioning_start_requested = false;
	app_bdb_retry_trace_put(0x05U << 24);
	app_bdb_start_commissioning();
	return -1;
}

static void app_bdb_commissioning_retry_cancel(void)
{
	if (commissioning_retry_timer != NULL) {
		(void)ev_timer_taskCancel(&commissioning_retry_timer);
		commissioning_retry_timer = NULL;
	}
}

static void app_bdb_commissioning_retry_schedule(void)
{
	app_bdb_commissioning_retry_cancel();
	app_bdb_retry_trace_put((0x06U << 24) |
				(APP_BDB_COMMISSIONING_RETRY_MS & 0xffffU));
	commissioning_retry_timer =
		ev_timer_taskPost(app_bdb_commissioning_retry, NULL,
				  APP_BDB_COMMISSIONING_RETRY_MS);
}
#endif /* CONFIG_ZIGBEE_BDB */

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

bool app_bdb_get_fixed_join_target(struct zb_platform_bdb_fixed_target *target)
{
	ARG_UNUSED(target);
	return false;
}

bool app_bdb_get_join_profile(struct zb_platform_bdb_join_profile *profile)
{
#if !defined(CONFIG_ZIGBEE_BDB)
	ARG_UNUSED(profile);
	return false;
#else
	if (profile == NULL) {
		return false;
	}

		memset(profile, 0, sizeof(*profile));
		profile->channel_mask = ((uint32_t)1U << CONFIG_ZIGBEE_CHANNEL);
		/*
		 * Real network joins must learn the NWK key during interview via the
		 * Trust Center Transport Key exchange.  Do not preload a test key
		 * here: it short-circuits interview and breaks live-network joins.
		 */
		profile->network_key_valid = false;
		memcpy(profile->tc_addr, app_bdb_fixed_tc_addr,
		       sizeof(app_bdb_fixed_tc_addr));
		profile->tc_addr_valid = true;

	return true;
#endif
}

void app_bdb_bootstrap_ready(void)
{
#if defined(CONFIG_ZIGBEE_BDB)
	if (!bdb_runtime_ready) {
		int err = zb_platform_bdb_init_default();
		bool joined = zb_isDeviceJoinedNwk();
		bool idle = zdo_ifZdoNwkManagerIdle();

		app_bdb_rejoin_callback_trace_put((0x13U << 24) |
						  ((uint32_t)(joined ? 1U : 0U) << 8) |
						  ((uint32_t)(idle ? 1U : 0U) << 9) |
						  ((uint32_t)(uint8_t)err << 16));
		zb_nwk_ed_trace[11] = 0xA1B00000U |
				       ((uint32_t)(joined ? 1U : 0U) << 8) |
				       ((uint32_t)(idle ? 1U : 0U) << 9);
		if (err == 0) {
			bdb_runtime_ready = true;
			if (joined && idle) {
				app_bdb_activate_poll_rate();
			}
			zb_nwk_ed_trace[11] = 0xA1B00001U |
					       ((uint32_t)(joined ? 1U : 0U) << 8) |
					       ((uint32_t)(idle ? 1U : 0U) << 9);
		} else {
			zb_nwk_ed_trace[11] = 0xA1B00000U |
					       ((uint32_t)(uint8_t)err << 16);
			LOG_ERR("zigbee_shell BDB runtime init failed (%d)", err);
		}
	}
#endif
}

bool app_bdb_should_start_commissioning(void)
{
#if defined(CONFIG_ZIGBEE_BDB)
	bool should_start;

	if (!bdb_runtime_ready) {
		zb_nwk_ed_trace[12] = 0xA2B00000U;
		return false;
	}
	if (commissioning_start_requested) {
		zb_nwk_ed_trace[12] = 0xA2B00010U;
		return false;
	}

	should_start = !zb_isDeviceJoinedNwk();
	zb_nwk_ed_trace[12] = 0xA2B00020U |
			       ((uint32_t)(zb_isDeviceJoinedNwk() ? 1U : 0U) << 8) |
			       (uint32_t)(should_start ? 1U : 0U);
	return should_start;
#else
	return false;
#endif
}

void app_bdb_start_commissioning(void)
{
#if defined(CONFIG_ZIGBEE_BDB)
	uint8_t status;

	zb_nwk_ed_trace[13] = 0xA3B00000U |
			       ((uint32_t)(bdb_runtime_ready ? 1U : 0U) << 8) |
			       ((uint32_t)(commissioning_start_requested ? 1U : 0U) << 9) |
			       ((uint32_t)(zb_isDeviceJoinedNwk() ? 1U : 0U) << 10);
	if (!bdb_runtime_ready) {
		zb_nwk_ed_trace[13] = 0xA3B00001U;
		LOG_WRN("zigbee_shell commissioning start: BDB runtime not initialized");
		return;
	}

	if (commissioning_start_requested) {
		zb_nwk_ed_trace[13] = 0xA3B00002U;
		LOG_WRN("zigbee_shell commissioning start: already requested");
		return;
	}

	if (zb_isDeviceJoinedNwk()) {
		zb_nwk_ed_trace[13] = 0xA3B00003U;
		commissioning_start_requested = true;
		app_bdb_activate_poll_rate();
		app_bdb_commissioning_retry_cancel();
		return;
	}

	status = zb_platform_bdb_network_steer_start();
	app_bdb_retry_trace_put((0x07U << 24) | status);
	zb_nwk_ed_trace[14] = 0xA4B00000U | status;
	if (status == 0U) {
		commissioning_start_requested = true;
		if (zb_isDeviceJoinedNwk()) {
			app_bdb_commissioning_retry_cancel();
		}
		if (!zb_isDeviceJoinedNwk()) {
			app_bdb_commissioning_retry_schedule();
			zb_nwk_ed_trace[14] = 0xA4B00010U | status;
		}
		zb_nwk_ed_trace[14] = 0xA4B00020U | status;
		LOG_INF("zigbee_shell commissioning start requested (bdb status: 0x%02x)", status);
	} else {
		commissioning_start_requested = false;
		app_bdb_commissioning_retry_schedule();
		zb_nwk_ed_trace[14] = 0xA4B00030U | status;
		LOG_WRN("zigbee_shell commissioning start rejected (bdb status: 0x%02x)", status);
	}
#endif
}

void app_bdb_network_left(void)
{
#if defined(CONFIG_ZIGBEE_BDB)
	commissioning_start_requested = false;
	leave_recommission_pending = true;
	app_bdb_commissioning_retry_cancel();
	app_bdb_commissioning_retry_schedule();
#endif
}

/*
 * Callback fired by the ZDO layer when the coordinator's Active_EP_rsp
 * arrives.  Drives the interview: issues SimpleDesc + Basic-cluster read
 * for each endpoint reported by the coordinator.
 *
 * `p` is a `zdo_zdpDataInd_t *` allocated by the ZDO layer with `zpdu`
 * pointing at the raw Active_EP_rsp payload (seq, status, nwk_addr,
 * ep_count, ep_list[]).  This is the same contract used by bdb.c callbacks.
 */
static void app_bdb_active_ep_rsp_cb(void *p)
{
	const zdo_zdpDataInd_t *ind = (const zdo_zdpDataInd_t *)p;
	const zdo_active_ep_resp_t *rsp;
	u8 i;

	if ((ind == NULL) || (ind->zpdu == NULL)) {
		return;
	}

	rsp = (const zdo_active_ep_resp_t *)ind->zpdu;
	if (ind->status != (u8)ZDO_SUCCESS || rsp->active_ep_count == 0U) {
		return;
	}

	for (i = 0U; i < rsp->active_ep_count && i < MAX_REQUESTED_CLUSTER_NUMBER; i++) {
		zdo_simple_descriptor_req_t sd_req;
		epInfo_t ep_info;
		struct {
			u8  numAttr;
			u16 attrID[2];
		} basic_read = { 2U, { 0x0004U, 0x0005U } };
		u8 seq = 0U;
		u8 ep = rsp->active_ep_lst[i];

		sd_req.nwk_addr_interest = ind->src_addr;
		sd_req.endpoint = ep;
		(void)zb_zdoSimpleDescReq(ind->src_addr, &sd_req, &seq, NULL);

		memset(&ep_info, 0, sizeof(ep_info));
		ep_info.dstAddr.shortAddr = ind->src_addr;
		ep_info.dstEp = ep;
		ep_info.profileId = APP_PROFILE_HA_PROFILE_ID;
		ep_info.dstAddrMode = APS_SHORT_DSTADDR_WITHEP;
		ep_info.txOptions = APS_TX_OPT_ACK_TX;
		ep_info.radius = 30U;
		seq = 0U;
		(void)zcl_read(APP_PROFILE_ENDPOINT, &ep_info,
			       0x0000U, 0U, 1U, 0U, seq,
			       (zclReadCmd_t *)&basic_read);
	}
}

void app_bdb_commissioning_status(uint8_t status, bool joinedNetwork)
{
#if defined(CONFIG_ZIGBEE_BDB)
	app_bdb_rejoin_callback_trace_put((0x14U << 24) |
					  ((uint32_t)status) |
					  ((uint32_t)(joinedNetwork ? 1U : 0U) << 8) |
					  ((uint32_t)(zb_isDeviceJoinedNwk() ? 1U : 0U) << 9));
	if (joinedNetwork) {
		app_bdb_activate_poll_rate();
		leave_recommission_pending = false;
		commissioning_start_requested = true;
		app_bdb_commissioning_retry_cancel();
		if (status == 0x00U) {
			/* Post-join ZDO/ZCL interview: discover coordinator endpoints.
			 * The response callback app_bdb_active_ep_rsp_cb will issue
			 * SimpleDesc + Basic-cluster read for each discovered endpoint,
			 * making the interview flow response-driven rather than hardcoded. */
			zdo_active_ep_req_t ep_req = { .nwk_addr_interest = 0x0000U };
			u8 seq = 0U;

			(void)zb_zdoActiveEpReq(0x0000U, &ep_req, &seq,
						app_bdb_active_ep_rsp_cb);
		}
		app_bdb_rejoin_callback_trace_put((0x15U << 24) |
						  (zb_getPollRate() & 0xffffU));
		LOG_INF("zigbee_shell joined network (bdb status: 0x%02x)", status);
		return;
	}

	if (zb_isDeviceJoinedNwk()) {
		app_bdb_activate_poll_rate();
		leave_recommission_pending = false;
		commissioning_start_requested = true;
		app_bdb_commissioning_retry_cancel();
		app_bdb_rejoin_callback_trace_put((0x16U << 24) |
						  (zb_getPollRate() & 0xffffU));
		LOG_INF("zigbee_shell joined network after status check (bdb status: 0x%02x)",
			status);
		return;
	}

	commissioning_start_requested = false;
	app_bdb_rejoin_callback_trace_put((0x17U << 24) | status);
	LOG_INF("zigbee_shell not joined yet (bdb status: 0x%02x), retry scheduled", status);
	app_bdb_commissioning_retry_schedule();
#else
	ARG_UNUSED(status);
	ARG_UNUSED(joinedNetwork);
#endif
}
