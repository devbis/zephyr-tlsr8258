/*
 * Copyright 2026 libzigbee
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "zb_common.h"
#include "bdb.h"
#include "mac_phy.h"
#include "nwk_ctx.h"
#include "zb_api.h"
#include "zcl_zll_commissioning.h"

void bdb_linkKeyCfg(bdb_commissionSetting_t *setting, u8 isFactoryNew)
{
	ss_ib.distributeLinkKey = setting->linkKey.distributeLinkKey.key;
	ss_ib.tcLinkKey = setting->linkKey.tcLinkKey.key;

	if (isFactoryNew) {
		ss_ib.tcLinkKeyType = setting->linkKey.tcLinkKey.keyType;
	}
}

void bdb_touchLinkPreCfg(u8 endpoint, bdb_commissionSetting_t *setting,
			 const zcl_touchlinkAppCallbacks_t *tlCb)
{
	if (!setting->touchlinkEnable) {
		return;
	}

	NODE_COMMISSIONING_CAPABILITY_SET(BDB_NODE_COMMISSION_CAP_TOUCHLINK);
	touchlink_keyModeSet(setting->linkKey.touchLinkKey.keyType,
			     setting->linkKey.touchLinkKey.key);
	g_bdbCtx.channel = setting->touchlinkChannel;
	touchlink_lqiThresholdSet(setting->touchlinkLqiThreshold);
	zcl_touchlink_register(endpoint, tlCb);
}

#if defined(ZB_ROUTER_ROLE) || defined(ZB_COORDINATOR_ROLE)
void bdb_routerStart(void)
{
	zb_routerStart();
}
#endif

#if defined(ZB_COORDINATOR_ROLE)
void bdb_coordinatorStart(void)
{
	aps_ib.aps_designated_coordinator = 1;
	ss_ib.tcPolicy.allowTCLKrequest = 1;
	zb_routerStart();
}
#endif

void bdb_endDeviceStart(u8 repower)
{
	if (!repower) {
		g_zbNwkCtx.joined = 1;
		aps_ib.aps_authenticated = 1;
		aps_ib.aps_use_insecure_join = 0;
		BDB_STATE_SET(BDB_STATE_IDLE);
		return;
	}

	zb_rejoinSecModeSet(REJOIN_SECURITY);
	zb_rejoinReq(1UL << g_zbInfo.macPib.phyChannelCur, g_bdbAttrs.scanDuration);
}

void bdb_outgoingFrameCountUpdate(u8 repower)
{
	if (!repower) {
		ss_ib.outgoingFrameCounter = drv_pm_deepSleep_frameCnt_get();
		return;
	}

	ss_ib.outgoingFrameCounter += SS_UPDATE_FRAMECOUNT_THRES;
	nv_nwkFrameCountSaveToFlash(ss_ib.outgoingFrameCounter);
}

void bdb_scanCfg(u32 chanMask, u8 duration)
{
	aps_ib.aps_channel_mask = chanMask;
	zdo_cfg_attributes.config_nwk_scan_duration = duration;
}

void bdb_factoryNewDevCfg(u8 touchLinkEn, u8 chan)
{
	ZB_EXTPANID_ZERO(aps_ib.aps_use_ext_panid);
	g_zbInfo.nwkNib.panId = g_zbInfo.macPib.panId;

#if defined(ZB_COORDINATOR_ROLE)
	ss_ib.tcPolicy.allowTCLKrequest = 1;
#endif

	if (!touchLinkEn) {
		return;
	}

	tl_zbMacChannelSet(chan);
	rf_setTrxState(RF_STATE_RX);
}
