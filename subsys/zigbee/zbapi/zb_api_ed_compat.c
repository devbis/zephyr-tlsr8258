/* SPDX-License-Identifier: Apache-2.0 */

#include "zb_common_stub.h"

extern void tl_zbNwkEdMinimalSetFixedJoinTarget(u8 channel, u16 panId, u16 shortAddr,
						 const u8 *extPanId, const u8 *nwkKey,
						 const u8 *tcAddr);
extern void tl_zbNwkEdMinimalPollRestart(u32 timeoutMs);

nwk_ctx_t g_zbNwkCtx __attribute__((weak)) = {
	.is_factory_new = 1,
};

__attribute__((weak)) u8 zb_nwkFormation(u32 scanChannels, u8 scanDuration)
{
	return (u8)zdo_nwkFormationStart(scanChannels, scanDuration);
}

__attribute__((weak)) u8 zb_routerStart(void)
{
	return (u8)zdo_nwkRouterStart();
}

__attribute__((weak)) u8 zb_nwkDiscovery(u32 scanChannels, u8 scanDuration, nwkDiscoveryUserCb_t cb)
{
	nlme_nwkDisc_req_t req = {
		.scanChannels = scanChannels,
		.scanDuration = scanDuration,
	};

	return (u8)zdo_nwkDiscoveryStart(&req, cb);
}

__attribute__((weak)) void zb_nwkDiscoveryStop(void)
{
	zdo_nwkDiscoveryStop();
}

__attribute__((weak)) u8 zb_assocJoinReq(void)
{
	return (u8)zdo_nwkAssocJoinStart();
}

__attribute__((weak)) u8 zb_rejoinReq(u32 scanChannels, u8 scanDuration)
{
	return (u8)zdo_nwkRejoinStart(scanChannels, scanDuration);
}

__attribute__((weak)) u8 zb_rejoinReqWithBackOff(u32 scanChannels, u8 scanDuration)
{
	return (u8)zdo_nwkRejoinWithBackOff(scanChannels, scanDuration);
}

__attribute__((weak)) u8 zb_setPollRate(u32 newRate)
{
	zdo_af_set_syn_rate(newRate);
	tl_zbNwkEdMinimalPollRestart(newRate);
	return RET_OK;
}

__attribute__((weak)) u32 zb_getPollRate(void)
{
	return zdo_af_get_syn_rate();
}

__attribute__((weak)) void zb_rejoinSecModeSet(u8 mode)
{
	ARG_UNUSED(mode);
}

__attribute__((weak)) u8 zb_directJoinReq(u32 scanChannels, u8 scanDuration)
{
	return (u8)zdo_nwkDirectJoinStart(scanChannels, scanDuration);
}

__attribute__((weak)) u8 zb_nwkDirectJoinAccept(nlme_directJoin_req_t *pReq)
{
	return (u8)zdo_nwkDirectJoinAccept(pReq);
}

__attribute__((weak)) void zb_joinAFixedNetwork(u8 channel, u16 panId, u16 shortAddr,
						 u8 *extPanId, u8 *nwkKey, u8 *tcAddr)
{
	u32 scanChannels = (channel < 32U) ? ((u32)1U << channel) : 0U;

	tl_zbNwkEdMinimalSetFixedJoinTarget(channel, panId, shortAddr, extPanId, nwkKey, tcAddr);
	if (nwkKey != NULL) {
		zb_preConfigNwkKey(nwkKey, FALSE);
	}
	if (scanChannels != 0U) {
		(void)zdo_nwkRejoinStart(scanChannels, zdo_cfg_attributes.config_nwk_scan_duration);
	} else {
		(void)zdo_nwkAssocJoinStart();
	}
}

__attribute__((weak)) s32 nwk_parentNodeInfoStore(void)
{
	return RET_OK;
}

/*
 * ED minimal binding table stub.
 * An ED has no binding table; zcl_reporting.c uses this to check whether
 * a cluster has a binding before scheduling attribute reports.  Return
 * false so reports are suppressed on a device with no bindings.
 */
__attribute__((weak)) bool zb_bindingTblSearched(u16 clusterID, u8 srcEp)
{
ARG_UNUSED(clusterID);
ARG_UNUSED(srcEp);
return false;
}
