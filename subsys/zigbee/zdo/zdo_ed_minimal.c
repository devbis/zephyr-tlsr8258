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
u32 TRANSPORT_NETWORK_KEY_WAIT_TIME = ZDO_ED_MINIMAL_TRANSPORT_KEY_WAIT_TIME_MS;

static bool g_zdoUnderRejoinMode = FALSE;

extern void tl_zbNwkEdMinimalRuntimeReset(void);
extern bool tl_zbNwkEdMinimalDiscoveryStart(u32 scanChannels, u8 scanDuration);
extern void tl_zbNwkEdMinimalDiscoveryStop(void);
extern bool tl_zbNwkEdMinimalAssocJoinStart(void);
extern bool tl_zbNwkEdMinimalRejoinStart(u32 scanChannels, u8 scanDuration, bool withBackoff);
extern void tl_zbNwkEdMinimalOperationAbort(void);
extern void tl_zbNwkEdMinimalOperationComplete(u8 status);
extern bool tl_zbNwkEdMinimalManagerIdle(void);
extern u32 tl_zbNwkEdMinimalLastScanChannelsGet(void);
extern u32 tl_zbNwkEdMinimalLastRejoinScanChannelsGet(void);

typedef struct {
	nwkDiscoveryUserCb_t discoveryCb;
	bool discoveryPending;
	u8 discoveryToken;
	bool joinPending;
	u8 joinToken;
	bool rejoinPending;
	bool rejoinWithBackoff;
	u8 rejoinToken;
} zdo_ed_minimal_async_ctx_t;

static zdo_ed_minimal_async_ctx_t g_zdoEdAsync;

static u8 zdo_ed_minimal_next_token(u8 *token)
{
	*token = (u8)(*token + 1U);
	if (*token == 0U) {
		*token = 1U;
	}

	return *token;
}

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

static void zdo_ed_minimal_discovery_done(void *arg)
{
	u8 token = (u8)(uintptr_t)arg;
	nwkDiscoveryUserCb_t cb;

	if (!g_zdoEdAsync.discoveryPending || g_zdoEdAsync.discoveryToken != token) {
		return;
	}

	cb = g_zdoEdAsync.discoveryCb;
	g_zdoEdAsync.discoveryPending = FALSE;
	g_zdoEdAsync.discoveryCb = NULL;
	tl_zbNwkEdMinimalOperationComplete(ZDO_SUCCESS);

	if (cb != NULL) {
		cb();
	}
}

static void zdo_ed_minimal_assoc_join_done(void *arg)
{
	u8 token = (u8)(uintptr_t)arg;
	zdo_start_device_confirm_t cnf;

	if (!g_zdoEdAsync.joinPending || g_zdoEdAsync.joinToken != token) {
		return;
	}

	g_zdoEdAsync.joinPending = FALSE;
	cnf = zdo_ed_minimal_build_start_dev_cnf(ZDO_NOT_SUPPORTED, FALSE);
	tl_zbNwkEdMinimalOperationComplete(cnf.status);

	if (zdoAppIndCbLst != NULL && zdoAppIndCbLst->zdpStartDevCnfCb != NULL) {
		zdoAppIndCbLst->zdpStartDevCnfCb(&cnf);
	}
}

static void zdo_ed_minimal_rejoin_done(void *arg)
{
	u8 token = (u8)(uintptr_t)arg;
	zdo_start_device_confirm_t cnf;

	if (!g_zdoEdAsync.rejoinPending || g_zdoEdAsync.rejoinToken != token) {
		return;
	}

	g_zdoEdAsync.rejoinPending = FALSE;
	g_zdoEdAsync.rejoinWithBackoff = FALSE;
	g_zdoUnderRejoinMode = FALSE;
	cnf = zdo_ed_minimal_build_start_dev_cnf(ZDO_NOT_SUPPORTED, TRUE);
	tl_zbNwkEdMinimalOperationComplete(cnf.status);

	if (zdoAppIndCbLst != NULL && zdoAppIndCbLst->zdpStartDevCnfCb != NULL) {
		zdoAppIndCbLst->zdpStartDevCnfCb(&cnf);
	}
}

void zdo_zdpCbTblRegister(zdo_appIndCb_t *cbTbl)
{
	zdoAppIndCbLst = cbTbl;
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
	return ZDO_NOT_SUPPORTED;
}

zdo_status_t zdo_nwkDiscoveryStart(nlme_nwkDisc_req_t *pReq, nwkDiscoveryUserCb_t cb)
{
	u8 token;
	u8 rc;

	if (pReq == NULL || cb == NULL) {
		return ZDO_INVALID_REQUEST;
	}
	if (!tl_zbNwkEdMinimalDiscoveryStart(pReq->scanChannels, pReq->scanDuration)) {
		return ZDO_INVALID_REQUEST;
	}

	token = zdo_ed_minimal_next_token(&g_zdoEdAsync.discoveryToken);
	g_zdoEdAsync.discoveryCb = cb;
	g_zdoEdAsync.discoveryPending = TRUE;
	rc = TL_SCHEDULE_TASK(zdo_ed_minimal_discovery_done, (void *)(uintptr_t)token);
	if (rc != RET_OK) {
		g_zdoEdAsync.discoveryPending = FALSE;
		g_zdoEdAsync.discoveryCb = NULL;
		tl_zbNwkEdMinimalOperationAbort();
		return ZDO_NOT_SUPPORTED;
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
	u8 token;
	u8 rc;

	if (!tl_zbNwkEdMinimalAssocJoinStart()) {
		return ZDO_INVALID_REQUEST;
	}

	token = zdo_ed_minimal_next_token(&g_zdoEdAsync.joinToken);
	g_zdoEdAsync.joinPending = TRUE;
	rc = TL_SCHEDULE_TASK(zdo_ed_minimal_assoc_join_done, (void *)(uintptr_t)token);
	if (rc != RET_OK) {
		g_zdoEdAsync.joinPending = FALSE;
		tl_zbNwkEdMinimalOperationAbort();
		return ZDO_NOT_SUPPORTED;
	}

	return ZDO_SUCCESS;
}

zdo_status_t zdo_nwkRejoinStart(u32 scanChannels, u8 scanDuration)
{
	u8 token;
	u8 rc;

	if (!tl_zbNwkEdMinimalRejoinStart(scanChannels, scanDuration, FALSE)) {
		return ZDO_INVALID_REQUEST;
	}

	g_zdoUnderRejoinMode = TRUE;
	token = zdo_ed_minimal_next_token(&g_zdoEdAsync.rejoinToken);
	g_zdoEdAsync.rejoinPending = TRUE;
	g_zdoEdAsync.rejoinWithBackoff = FALSE;
	rc = TL_SCHEDULE_TASK(zdo_ed_minimal_rejoin_done, (void *)(uintptr_t)token);
	if (rc != RET_OK) {
		g_zdoEdAsync.rejoinPending = FALSE;
		g_zdoEdAsync.rejoinWithBackoff = FALSE;
		g_zdoUnderRejoinMode = FALSE;
		tl_zbNwkEdMinimalOperationAbort();
		return ZDO_NOT_SUPPORTED;
	}

	return ZDO_SUCCESS;
}

zdo_status_t zdo_nwkRejoinWithBackOff(u32 scanChannels, u8 scanDuration)
{
	u8 token;
	u8 rc;

	if (!tl_zbNwkEdMinimalRejoinStart(scanChannels, scanDuration, TRUE)) {
		return ZDO_INVALID_REQUEST;
	}

	g_zdoUnderRejoinMode = TRUE;
	token = zdo_ed_minimal_next_token(&g_zdoEdAsync.rejoinToken);
	g_zdoEdAsync.rejoinPending = TRUE;
	g_zdoEdAsync.rejoinWithBackoff = TRUE;
	rc = TL_SCHEDULE_TASK(zdo_ed_minimal_rejoin_done, (void *)(uintptr_t)token);
	if (rc != RET_OK) {
		g_zdoEdAsync.rejoinPending = FALSE;
		g_zdoEdAsync.rejoinWithBackoff = FALSE;
		g_zdoUnderRejoinMode = FALSE;
		tl_zbNwkEdMinimalOperationAbort();
		return ZDO_NOT_SUPPORTED;
	}

	return ZDO_SUCCESS;
}

void zdo_nwkRejoinWithBackOffStop(void)
{
	if (g_zdoEdAsync.rejoinPending && g_zdoEdAsync.rejoinWithBackoff) {
		g_zdoEdAsync.rejoinPending = FALSE;
		g_zdoEdAsync.rejoinWithBackoff = FALSE;
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
