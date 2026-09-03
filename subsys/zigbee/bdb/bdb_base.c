/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Adapted from libzigbee/src/bdb_base.c.
 *
 * This file is the vendor BDB role/configuration boundary. The Zephyr port
 * only changes the include set and the RF hook; role transitions and link-key
 * setup remain the vendor implementation.
 */

#include "zb_common_stub.h"
#include "../zbapi/zb_api.h"
#include "includes/bdb.h"
#include "../zcl/zll_commissioning/zcl_zll_commissioning.h"

#include <zephyr/zigbee/zb_radio_port.h>

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
#if TOUCHLINK_SUPPORT
	if (!setting->touchlinkEnable) {
		return;
	}

	NODE_COMMISSIONING_CAPABILITY_SET(BDB_NODE_COMMISSION_CAP_TOUCHLINK);
	touchlink_keyModeSet(setting->linkKey.touchLinkKey.keyType,
				     setting->linkKey.touchLinkKey.key);
	g_bdbCtx.channel = setting->touchlinkChannel;
	touchlink_lqiThresholdSet(setting->touchlinkLqiThreshold);
	zcl_touchlink_register(endpoint, tlCb);
#else
	ARG_UNUSED(endpoint);
	ARG_UNUSED(setting);
	ARG_UNUSED(tlCb);
#endif
}

#if ROUTER || COORDINATOR
void bdb_routerStart(void)
{
	(void)zb_routerStart();
}
#endif

#if COORDINATOR
void bdb_coordinatorStart(void)
{
	aps_ib.aps_designated_coordinator = 1;
	/* This is the vendor security-IB coordinator flag. */
	((u8 *)&ss_ib)[0x46] = 1;
	(void)zb_routerStart();
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
	(void)zb_rejoinReq(1UL << g_zbInfo.macPib.phyChannelCur,
			   g_bdbAttrs.scanDuration);
}

void bdb_outgoingFrameCountUpdate(u8 repower)
{
	if (!repower) {
		/* Zephyr has no separate vendor deep-sleep frame-counter register;
		 * the persistence adapter owns this value across power cycles. */
		return;
	}

	ss_ib.outgoingFrameCounter += SS_UPDATE_FRAMECOUNT_THRES;
	(void)nv_nwkFrameCountSaveToFlash(ss_ib.outgoingFrameCounter);
}

void bdb_scanCfg(u32 chanMask, u8 duration)
{
	aps_ib.aps_channel_mask = chanMask;
	zdo_cfg_attributes.config_nwk_scan_duration = duration;
}

void bdb_factoryNewDevCfg(u8 touchLinkEn, u8 chan)
{
	memset(aps_ib.aps_use_ext_panid, 0, EXT_ADDR_LEN);
	g_zbInfo.nwkNib.panId = g_zbInfo.macPib.panId;

	if (!touchLinkEn) {
		return;
	}

	tl_zbMacChannelSet(chan);
	(void)zb_radio_port_set_trx_state(ZB_RADIO_PORT_TRX_RX, chan);
}
