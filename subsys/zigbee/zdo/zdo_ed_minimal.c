/* SPDX-License-Identifier: Apache-2.0 */

#include "zb_common_stub.h"
#include <stdint.h>

#define ZDO_ED_MINIMAL_POLL_RATE_MS                    1000U
#define ZDO_ED_MINIMAL_TIME_BTWN_SCANS_MS              100U
#define ZDO_ED_MINIMAL_SCAN_ATTEMPTS                   5U
#define ZDO_ED_MINIMAL_PERMIT_JOIN_DURATION            0U
#define ZDO_ED_MINIMAL_PARENT_LINK_RETRY_THRESHOLD     3U
#define ZDO_ED_MINIMAL_REJOIN_TIMES                    0U
#define ZDO_ED_MINIMAL_REJOIN_DURATION_S               0U
#define ZDO_ED_MINIMAL_REJOIN_BACKOFF_S                0U
#define ZDO_ED_MINIMAL_MAX_REJOIN_BACKOFF_S            0U
#define ZDO_ED_MINIMAL_REJOIN_BACKOFF_ITERATION        0U
#define ZDO_ED_MINIMAL_ACCEPT_NWK_UPDATE_PAN_ID        0xFFFFU
#define ZDO_ED_MINIMAL_ACCEPT_NWK_UPDATE_CHANNEL       0xFFU
#define ZDO_ED_MINIMAL_USE_MGMT_LEAVE_APS_SEC          FALSE
#define ZDO_ED_MINIMAL_USE_TC_SEC_ON_KEY_ROTATION      FALSE
#define ZDO_ED_MINIMAL_SCAN_DURATION                   5U
#define ZDO_ED_MINIMAL_TRANSPORT_KEY_WAIT_TIME_MS      5000U

#define ZDO_ED_MINIMAL_CFG_INIT                        \
	{                                              \
		.config_nwk_indirectPollRate = ZDO_ED_MINIMAL_POLL_RATE_MS,      \
		.config_nwk_time_btwn_scans = ZDO_ED_MINIMAL_TIME_BTWN_SCANS_MS, \
		.config_nwk_scan_attempts = ZDO_ED_MINIMAL_SCAN_ATTEMPTS,         \
		.config_permit_join_duration = ZDO_ED_MINIMAL_PERMIT_JOIN_DURATION, \
		.config_parent_link_retry_threshold = ZDO_ED_MINIMAL_PARENT_LINK_RETRY_THRESHOLD, \
		.config_rejoin_times = ZDO_ED_MINIMAL_REJOIN_TIMES,               \
		.config_rejoin_duration = ZDO_ED_MINIMAL_REJOIN_DURATION_S,       \
		.config_rejoin_backoff_time = ZDO_ED_MINIMAL_REJOIN_BACKOFF_S,    \
		.config_max_rejoin_backoff_time = ZDO_ED_MINIMAL_MAX_REJOIN_BACKOFF_S, \
		.config_rejoin_backoff_iteration = ZDO_ED_MINIMAL_REJOIN_BACKOFF_ITERATION, \
		.config_accept_nwk_update_pan_id = ZDO_ED_MINIMAL_ACCEPT_NWK_UPDATE_PAN_ID, \
		.config_accept_nwk_update_channel = ZDO_ED_MINIMAL_ACCEPT_NWK_UPDATE_CHANNEL, \
		.config_mgmt_leave_use_aps_sec = ZDO_ED_MINIMAL_USE_MGMT_LEAVE_APS_SEC, \
		.config_use_tc_sec_on_nwk_key_rotation = ZDO_ED_MINIMAL_USE_TC_SEC_ON_KEY_ROTATION, \
		.config_nwk_scan_duration = ZDO_ED_MINIMAL_SCAN_DURATION,          \
	}

zdo_appIndCb_t *zdoAppIndCbLst = NULL;
zdo_touchLinkCb_t *zdoTouchLinkCb = NULL;
zdo_attrCfg_t zdo_cfg_attributes = ZDO_ED_MINIMAL_CFG_INIT;
/* TRANSPORT_NETWORK_KEY_WAIT_TIME is defined in zb_config.c (SDK copy).
 * zdo_init() below overrides it with ZDO_ED_MINIMAL_TRANSPORT_KEY_WAIT_TIME_MS.
 */
extern u32 TRANSPORT_NETWORK_KEY_WAIT_TIME;

static bool g_zdoUnderRejoinMode = FALSE;
#if defined(CONFIG_ZIGBEE_DEBUG_TRACES)
volatile u32 zb_zdo_ed_trace[16] = {0x5a444f45U};

static void zdo_ed_trace_put(u32 tag)
{
	u8 count = (u8)zb_zdo_ed_trace[1];
	u8 span = (u8)(ARRAY_SIZE(zb_zdo_ed_trace) - 2U);
	u8 slot = (u8)(2U + (count % span));

	zb_zdo_ed_trace[slot] = tag;
	zb_zdo_ed_trace[1] = count + 1U;
}
#else
static void zdo_ed_trace_put(u32 tag)
{
	ARG_UNUSED(tag);
}
#endif

extern void app_bdb_rejoin_callback_trace_put(uint32_t tag);

extern void tl_zbNwkEdMinimalRuntimeReset(void);
extern bool tl_zbNwkEdMinimalDiscoveryStart(u32 scanChannels, u8 scanDuration);
extern void tl_zbNwkEdMinimalDiscoveryStop(void);
extern bool tl_zbNwkEdMinimalAssocJoinStart(void);
extern bool tl_zbNwkEdMinimalRejoinStart(u32 scanChannels, u8 scanDuration, bool withBackoff);
extern void tl_zbNwkEdMinimalOperationAbort(void);
extern bool tl_zbNwkEdMinimalManagerIdle(void);
extern u32 tl_zbNwkEdMinimalLastScanChannelsGet(void);
extern u32 tl_zbNwkEdMinimalLastRejoinScanChannelsGet(void);

typedef struct {
	nwkDiscoveryUserCb_t discoveryCb;
	bool discoveryPending;
	bool joinPending;
	bool rejoinPending;
	bool rejoinWithBackoff;
	bool assocNotified;
} zdo_ed_minimal_async_ctx_t;

static zdo_ed_minimal_async_ctx_t g_zdoEdAsync;

static zdo_start_device_confirm_t zdo_ed_minimal_build_start_dev_cnf(u8 status, bool rejoinMode)
{
	zdo_start_device_confirm_t cnf = {
		.status = status,
		.channel_num = 0xFFU,
		.pan_id = MAC_INVALID_PANID,
		.short_addr = MAC_SHORT_ADDR_NONE,
	};
	u32 scanChannels = rejoinMode ? tl_zbNwkEdMinimalLastRejoinScanChannelsGet() :
			tl_zbNwkEdMinimalLastScanChannelsGet();

	cnf.channel_num = zdo_channel_page2num(scanChannels);
	cnf.pan_id = g_zbNIB.panId;
	cnf.short_addr = g_zbNIB.nwkAddr;

	return cnf;
}

static void zdo_ed_minimal_discovery_done(u8 status)
{
	nwkDiscoveryUserCb_t cb;

	if (!g_zdoEdAsync.discoveryPending) {
		return;
	}

	cb = g_zdoEdAsync.discoveryCb;
	g_zdoEdAsync.discoveryPending = FALSE;
	g_zdoEdAsync.discoveryCb = NULL;

	if (cb != NULL) {
		cb();
	}
}

static void zdo_ed_minimal_promote_discovery_to_join(void)
{
	if (!g_zdoEdAsync.discoveryPending || g_zdoEdAsync.joinPending) {
		return;
	}

	g_zdoEdAsync.discoveryPending = FALSE;
	g_zdoEdAsync.discoveryCb = NULL;
	g_zdoEdAsync.joinPending = TRUE;
	g_zdoEdAsync.assocNotified = FALSE;
	zdo_ed_trace_put(0x03000003U);
}

static void zdo_ed_minimal_assoc_handoff_done(u8 status, bool rejoinMode)
{
	zdo_start_device_confirm_t cnf;

	if (!rejoinMode) {
		zdo_ed_minimal_promote_discovery_to_join();
	}

	bool pending = rejoinMode ? g_zdoEdAsync.rejoinPending : g_zdoEdAsync.joinPending;

	zdo_ed_trace_put((0x13U << 24) |
			 ((u32)status << 16) |
			 ((u32)(rejoinMode ? 1U : 0U) << 8) |
			 (u32)(pending ? 1U : 0U));
	if ((status != ZDO_SUCCESS) || !pending || g_zdoEdAsync.assocNotified) {
		return;
	}

	g_zdoEdAsync.assocNotified = TRUE;
	cnf = zdo_ed_minimal_build_start_dev_cnf(status, rejoinMode);
	if (zdoAppIndCbLst != NULL && zdoAppIndCbLst->zdpAssocDoneCb != NULL) {
		zdoAppIndCbLst->zdpAssocDoneCb(&cnf);
	}
}

static void zdo_ed_minimal_assoc_join_done(u8 status)
{
	zdo_start_device_confirm_t cnf;

	zdo_ed_minimal_promote_discovery_to_join();

	zdo_ed_trace_put((0x10U << 24) |
			 ((u32)status << 16) |
			 ((u32)(g_zdoEdAsync.joinPending ? 1U : 0U) << 8) |
			 (u32)(zdoAppIndCbLst != NULL));
	if (!g_zdoEdAsync.joinPending) {
		zdo_ed_trace_put(0x10ff0000U);
		return;
	}

	g_zdoEdAsync.assocNotified = FALSE;
	g_zdoEdAsync.joinPending = FALSE;
	cnf = zdo_ed_minimal_build_start_dev_cnf(status, FALSE);
	zdo_ed_trace_put((0x11U << 24) |
			 ((u32)cnf.status << 16) |
			 ((u32)cnf.channel_num << 8) |
			 (u32)(zdoAppIndCbLst != NULL &&
			       zdoAppIndCbLst->zdpStartDevCnfCb != NULL));

	if (zdoAppIndCbLst != NULL && zdoAppIndCbLst->zdpStartDevCnfCb != NULL) {
		zdo_ed_trace_put(0x12000000U);
		zdoAppIndCbLst->zdpStartDevCnfCb(&cnf);
	} else {
		zdo_ed_trace_put(0x12ff0000U);
	}
}

static void zdo_ed_minimal_rejoin_done(u8 status)
{
	zdo_start_device_confirm_t cnf;

	app_bdb_rejoin_callback_trace_put((0x24U << 24) |
					  (uint32_t)status |
					  ((uint32_t)(g_zdoEdAsync.rejoinPending ? 1U : 0U) << 8));
	if (!g_zdoEdAsync.rejoinPending) {
		return;
	}

	g_zdoEdAsync.assocNotified = FALSE;
	g_zdoEdAsync.rejoinPending = FALSE;
	g_zdoEdAsync.rejoinWithBackoff = FALSE;
	g_zdoUnderRejoinMode = FALSE;
	cnf = zdo_ed_minimal_build_start_dev_cnf(status, TRUE);

	if (zdoAppIndCbLst != NULL && zdoAppIndCbLst->zdpStartDevCnfCb != NULL) {
		zdoAppIndCbLst->zdpStartDevCnfCb(&cnf);
	}
}

void tl_zdoEdMinimalDiscoveryDone(u8 status)
{
	zdo_ed_minimal_discovery_done(status);
}

void tl_zdoEdMinimalAssocDone(u8 status, bool rejoinMode)
{
	zdo_ed_minimal_assoc_handoff_done(status, rejoinMode);
}

void tl_zdoEdMinimalJoinDone(u8 status, bool rejoinMode)
{
	zdo_ed_trace_put((0x01U << 24) |
			 ((u32)status << 16) |
			 ((u32)(rejoinMode ? 1U : 0U) << 8) |
			 ((u32)(g_zdoEdAsync.joinPending ? 1U : 0U)));
	if (rejoinMode) {
		zdo_ed_minimal_rejoin_done(status);
	} else {
		zdo_ed_minimal_assoc_join_done(status);
	}
}

void zdo_zdpCbTblRegister(zdo_appIndCb_t *cbTbl)
{
	zdoAppIndCbLst = cbTbl;
	zdo_ed_trace_put((0x02U << 24) |
			 (u32)(cbTbl != NULL) |
			 ((u32)(cbTbl != NULL && cbTbl->zdpStartDevCnfCb != NULL) << 8) |
			 ((u32)(cbTbl != NULL && cbTbl->zdpAssocDoneCb != NULL) << 9));
}

void zdo_touchLinkCbRegister(zdo_touchLinkCb_t *cbTbl)
{
	zdoTouchLinkCb = cbTbl;
}

void zdo_init(void)
{
	zdo_cfg_attributes = (zdo_attrCfg_t)ZDO_ED_MINIMAL_CFG_INIT;
	TRANSPORT_NETWORK_KEY_WAIT_TIME = ZDO_ED_MINIMAL_TRANSPORT_KEY_WAIT_TIME_MS;
	g_zdoUnderRejoinMode = FALSE;
	memset(&g_zdoEdAsync, 0, sizeof(g_zdoEdAsync));
	tl_zbNwkEdMinimalRuntimeReset();
}

u8 zdo_af_get_link_retry_threshold(void)
{
	return zdo_cfg_attributes.config_parent_link_retry_threshold;
}

void zdo_af_set_link_retry_threshold(u8 threshold)
{
	zdo_cfg_attributes.config_parent_link_retry_threshold = threshold;
}

u32 zdo_af_get_syn_rate(void)
{
	return zdo_cfg_attributes.config_nwk_indirectPollRate;
}

void zdo_af_set_syn_rate(u32 newRate)
{
	zdo_cfg_attributes.config_nwk_indirectPollRate = newRate;
}

u8 zdo_af_get_rejoin_times(void)
{
	return zdo_cfg_attributes.config_rejoin_times;
}

void zdo_af_set_rejoin_times(u8 times)
{
	zdo_cfg_attributes.config_rejoin_times = times;
}

u16 zdo_af_get_rejoin_duration(void)
{
	return zdo_cfg_attributes.config_rejoin_duration;
}

void zdo_af_set_rejoin_duration(u16 duration)
{
	zdo_cfg_attributes.config_rejoin_duration = duration;
}

u16 zdo_af_get_rejoin_backoff_time(void)
{
	return zdo_cfg_attributes.config_rejoin_backoff_time;
}

void zdo_af_set_rejoin_backoff_time(u16 interval)
{
	zdo_cfg_attributes.config_rejoin_backoff_time = interval;
}

u16 zdo_af_get_max_rejoin_backoff_time(void)
{
	return zdo_cfg_attributes.config_max_rejoin_backoff_time;
}

void zdo_af_set_max_rejoin_backoff_time(u16 interval)
{
	zdo_cfg_attributes.config_max_rejoin_backoff_time = interval;
}

u16 zdo_af_get_rejoin_backoff_iteration(void)
{
	return zdo_cfg_attributes.config_rejoin_backoff_iteration;
}

void zdo_af_set_rejoin_backoff_iteration(u16 iteration)
{
	zdo_cfg_attributes.config_rejoin_backoff_iteration = iteration;
}

u8 zdo_af_get_scan_attempts(void)
{
	return zdo_cfg_attributes.config_nwk_scan_attempts;
}

void zdo_af_set_scan_attempts(u8 attempts)
{
	zdo_cfg_attributes.config_nwk_scan_attempts = (attempts == 0U) ? 1U : attempts;
}

u16 zdo_af_get_nwk_time_btwn_scans(void)
{
	return zdo_cfg_attributes.config_nwk_time_btwn_scans;
}

u8 zdo_af_get_permit_join_duration(void)
{
	return zdo_cfg_attributes.config_permit_join_duration;
}

void zdo_af_set_accept_nwk_update_pan_id(u16 panId)
{
	zdo_cfg_attributes.config_accept_nwk_update_pan_id = panId;
}

u16 zdo_af_get_accept_nwk_update_pan_id(void)
{
	return zdo_cfg_attributes.config_accept_nwk_update_pan_id;
}

void zdo_af_set_accept_nwk_update_channel(u8 channel)
{
	zdo_cfg_attributes.config_accept_nwk_update_channel = channel;
}

u8 zdo_af_get_accept_nwk_update_channel(void)
{
	return zdo_cfg_attributes.config_accept_nwk_update_channel;
}

void zdo_af_set_mgmtLeave_use_aps_sec(bool enable)
{
	zdo_cfg_attributes.config_mgmt_leave_use_aps_sec = enable;
}

bool zdo_af_get_mgmtLeave_use_aps_sec(void)
{
	return zdo_cfg_attributes.config_mgmt_leave_use_aps_sec;
}

void zdo_af_set_use_tc_sec_on_nwk_key_rotation(bool enable)
{
	zdo_cfg_attributes.config_use_tc_sec_on_nwk_key_rotation = enable;
}

bool zdo_af_get_use_tc_sec_on_nwk_key_rotation(void)
{
	return zdo_cfg_attributes.config_use_tc_sec_on_nwk_key_rotation;
}

bool zb_isUnderRejoinMode(void)
{
	return g_zdoUnderRejoinMode;
}

u8 zdo_channel_page2num(u32 chp)
{
	for (u8 i = 0; i < 32; i++) {
		if (chp & ((u32)1 << i)) {
			return i;
		}
	}

	return 0xFF;
}

zdo_status_t zdo_nwkFormationStart(u32 scanChannels, u8 scanDuration)
{
	ARG_UNUSED(scanChannels);
	ARG_UNUSED(scanDuration);

	return ZDO_NOT_SUPPORTED;
}

zdo_status_t zdo_nwkRouterStart(void)
{
#if ZB_ROUTER_ROLE
	g_zbNwkCtx.router_started = 1U;
	g_zbNwkCtx.state = NLME_STATE_IDLE;
	return ZDO_SUCCESS;
#else
	return ZDO_NOT_SUPPORTED;
#endif
}

zdo_status_t zdo_nwkDiscoveryStart(nlme_nwkDisc_req_t *pReq, nwkDiscoveryUserCb_t cb)
{
	if (pReq == NULL || cb == NULL) {
		return ZDO_INVALID_REQUEST;
	}
	if (g_zdoEdAsync.discoveryPending) {
		return ZDO_NOT_PERMITTED;
	}

	g_zdoEdAsync.discoveryCb = cb;
	g_zdoEdAsync.discoveryPending = TRUE;
	if (!tl_zbNwkEdMinimalDiscoveryStart(pReq->scanChannels, pReq->scanDuration)) {
		g_zdoEdAsync.discoveryPending = FALSE;
		g_zdoEdAsync.discoveryCb = NULL;
		return ZDO_INVALID_REQUEST;
	}

	return ZDO_SUCCESS;
}

void zdo_nwkDiscoveryStop(void)
{
	if (g_zdoEdAsync.discoveryPending) {
		g_zdoEdAsync.discoveryPending = FALSE;
		g_zdoEdAsync.discoveryCb = NULL;
		tl_zbNwkEdMinimalDiscoveryStop();
	}
}

zdo_status_t zdo_nwkAssocJoinStart(void)
{
	if (g_zdoEdAsync.joinPending) {
		zdo_ed_trace_put(0x03ff0001U);
		return ZDO_NOT_PERMITTED;
	}

	g_zdoEdAsync.joinPending = TRUE;
	g_zdoEdAsync.assocNotified = FALSE;
	zdo_ed_trace_put(0x03000001U);
	if (!tl_zbNwkEdMinimalAssocJoinStart()) {
		g_zdoEdAsync.joinPending = FALSE;
		zdo_ed_trace_put(0x03ff0002U);
		return ZDO_INVALID_REQUEST;
	}
	zdo_ed_trace_put(0x03000002U);

	return ZDO_SUCCESS;
}

void zdo_ed_minimal_rejoin_restart_prepare(void)
{
	app_bdb_rejoin_callback_trace_put((0x25U << 24) |
					  ((uint32_t)(g_zdoEdAsync.rejoinPending ? 1U : 0U) << 8) |
					  ((uint32_t)(g_zdoEdAsync.rejoinWithBackoff ? 1U : 0U) << 9));
	g_zdoEdAsync.rejoinPending = FALSE;
	g_zdoEdAsync.rejoinWithBackoff = FALSE;
	g_zdoEdAsync.assocNotified = FALSE;
	g_zdoUnderRejoinMode = FALSE;
	tl_zbNwkEdMinimalOperationAbort();
}

zdo_status_t zdo_nwkRejoinStart(u32 scanChannels, u8 scanDuration)
{
	if (g_zdoEdAsync.rejoinPending) {
		return ZDO_NOT_PERMITTED;
	}

	if (!tl_zbNwkEdMinimalRejoinStart(scanChannels, scanDuration, FALSE)) {
		return ZDO_INVALID_REQUEST;
	}

	g_zdoUnderRejoinMode = TRUE;
	g_zdoEdAsync.rejoinPending = TRUE;
	g_zdoEdAsync.rejoinWithBackoff = FALSE;
	g_zdoEdAsync.assocNotified = FALSE;

	return ZDO_SUCCESS;
}

zdo_status_t zdo_nwkRejoinWithBackOff(u32 scanChannels, u8 scanDuration)
{
	if (g_zdoEdAsync.rejoinPending) {
		return ZDO_NOT_PERMITTED;
	}

	if (!tl_zbNwkEdMinimalRejoinStart(scanChannels, scanDuration, TRUE)) {
		return ZDO_INVALID_REQUEST;
	}

	g_zdoUnderRejoinMode = TRUE;
	g_zdoEdAsync.rejoinPending = TRUE;
	g_zdoEdAsync.rejoinWithBackoff = TRUE;
	g_zdoEdAsync.assocNotified = FALSE;

	return ZDO_SUCCESS;
}

void zdo_nwkRejoinWithBackOffStop(void)
{
	if (g_zdoEdAsync.rejoinPending && g_zdoEdAsync.rejoinWithBackoff) {
		g_zdoEdAsync.rejoinPending = FALSE;
		g_zdoEdAsync.rejoinWithBackoff = FALSE;
		g_zdoEdAsync.assocNotified = FALSE;
		tl_zbNwkEdMinimalOperationAbort();
	}

	g_zdoUnderRejoinMode = FALSE;
}

zdo_status_t zdo_nwkDirectJoinStart(u32 scanChannels, u8 scanDuration)
{
	ARG_UNUSED(scanChannels);
	ARG_UNUSED(scanDuration);

	return ZDO_NOT_SUPPORTED;
}

zdo_status_t zdo_nwkDirectJoinAccept(nlme_directJoin_req_t *pReq)
{
	ARG_UNUSED(pReq);

	return ZDO_NOT_SUPPORTED;
}

void zdo_nlmeForgetDev(addrExt_t nodeIeeeAddr, bool rejoin)
{
	ARG_UNUSED(nodeIeeeAddr);
	ARG_UNUSED(rejoin);
}

bool zdo_ifZdoNwkManagerIdle(void)
{
	return tl_zbNwkEdMinimalManagerIdle();
}
