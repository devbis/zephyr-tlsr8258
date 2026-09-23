/*
 * Copyright 2026 libzigbee
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "zb_common.h"
#include "zb_af.h"
#include "zb_api.h"
#include "zdo_api.h"
#include "bdb.h"
#include "ss_apsSecurityME.h"
#include "ss_zdoSecurityME.h"
#include "aps.h"
#include "zdo_nwk_manager.h"
#include "zdp_services.h"
#include <stdint.h>

STATIC_ASSERT(sizeof(ss_apsmeRemoveDeviceReq_t) == 16);
STATIC_ASSERT(sizeof(ss_apsmeRequestKeyReq_t) == 18);
STATIC_ASSERT(sizeof(ss_apsmeTransportKeyReq_t) == 37);
STATIC_ASSERT(sizeof(ss_apsmeSwitchKeyReq_t) == 9);
STATIC_ASSERT(sizeof(nlme_routeDisc_req_t) == 5);

static inline zdo_status_t zb_zdo_send_short_req(u16 dstNwkAddr, u16 clusterId, void *payload,
						 u8 payloadLen, u8 *seqNo, zdo_callback indCb)
{
	zdo_zdp_req_t req;
	zdo_status_t status;

	memset(&req, 0, sizeof(req));
	req.dst_addr_mode = SHORT_ADDR_MODE;
	req.dst_nwk_addr = dstNwkAddr;
	req.cluster_id = clusterId;
	req.zdoRspReceivedIndCb = indCb;

	status = zdp_data_send((u8 *)payload, (u8)(payloadLen + 1U), &req);
	*seqNo = req.zdpSeqNum;

	return status;
}

bool zb_isDeviceFactoryNew(void)
{
	return g_zbNwkCtx.is_factory_new ? TRUE : FALSE;
}

void zb_deviceFactoryNewSet(bool new)
{
	g_zbNwkCtx.is_factory_new = new ? 1U : 0U;
}

bool zb_isDeviceJoinedNwk(void)
{
	return g_zbNwkCtx.joined ? TRUE : FALSE;
}

bool zb_getMacAssocPermit(void)
{
	return g_zbMacPib.associationPermit ? TRUE : FALSE;
}

void zb_nldeDataDiscoverRouteSet(bool enable)
{
	g_zbNwkCtx.discoverRoute = enable ? 1U : 0U;
}

bool zb_nldeDataDiscoverRouteGet(void)
{
	return g_zbNwkCtx.discoverRoute ? TRUE : FALSE;
}

void zb_apsExtPanidSet(extPANId_t panId)
{
	ZB_EXTPANID_COPY(aps_ib.aps_use_ext_panid, panId);
}

aps_status_t zb_apsChannelMaskSet(u32 mask)
{
	return apsSetChnMsk(mask);
}

u32 zb_apsChannelMaskGet(void)
{
	return aps_ib.aps_channel_mask;
}

void zdo_nlmeChannelShift(u8 ch)
{
	if ((u8)(ch - 11U) <= 15U) {
		tl_zbMacChannelSet(ch);
		tl_zbTaskPost(zb_info_save, NULL);
	}
}

device_type_t zb_getDeviceType(void)
{
	return af_nodeDevTypeGet();
}

void zb_getLocalExtAddr(addrExt_t extAddr)
{
	ZB_IEEE_ADDR_COPY(extAddr, g_zbMacPib.extAddress);
}

u16 zb_getLocalShortAddr(void)
{
	return g_zbMacPib.shortAddress;
}

u16 zb_getParentShortAddr(void)
{
	return tl_zbNeighborParentShortAddrGet();
}

u8 zb_getNwkAddrByExtAddr(addrExt_t extAddr, u16 *nwkAddr)
{
	u16 idx;
	return tl_zbShortAddrByExtAddr(nwkAddr, extAddr, &idx);
}

u8 zb_getExtAddrByNwkAddr(u16 nwkAddr, addrExt_t extAddr)
{
	u16 idx;
	return tl_zbExtAddrByShortAddr(nwkAddr, extAddr, &idx);
}

void zb_nlmeSetLinkStsPeriod(u8 periodInSec)
{
#if defined(ZB_ROUTER_ROLE) || defined(ZB_COORDINATOR_ROLE)
	g_zbInfo.nwkNib.linkStatusPeriod = periodInSec;
#else
	(void)periodInSec;
#endif
}

u8 zb_nwkDiscovery(u32 scanChannels, u8 scanDuration, nwkDiscoveryUserCb_t cb)
{
#if defined(ZB_COORDINATOR_ROLE)
	(void)scanChannels;
	(void)scanDuration;
	(void)cb;
	return 0x32;
#else
	nlme_nwkDisc_req_t req = {
		.scanChannels = scanChannels,
		.scanDuration = scanDuration,
	};

	return zdo_nwkDiscoveryStart(&req, cb);
#endif
}

void zb_nwkDiscoveryStop(void)
{
	zdo_nwkDiscoveryStop();
}

u8 zb_assocJoinReq(void)
{
#if defined(ZB_COORDINATOR_ROLE)
	return 0x32;
#else
	return zdo_nwkAssocJoinStart();
#endif
}

u8 zb_rejoinReq(u32 scanChannels, u8 scanDuration)
{
#if defined(ZB_COORDINATOR_ROLE)
	(void)scanChannels;
	(void)scanDuration;
	return 0x32;
#else
	return zdo_nwkRejoinStart(scanChannels, scanDuration);
#endif
}

u8 zb_rejoinReqWithBackOff(u32 scanChannels, u8 scanDuration)
{
#if defined(ZB_COORDINATOR_ROLE)
	(void)scanChannels;
	(void)scanDuration;
	return 0x32;
#else
	return zdo_nwkRejoinWithBackOff(scanChannels, scanDuration);
#endif
}

void zb_rejoinSecModeSet(u8 mode)
{
	if (mode == REJOIN_SECURITY) {
		aps_ib.aps_use_insecure_join = FALSE;
		aps_ib.aps_authenticated = ss_ib_security_level_get() ? TRUE : FALSE;
		return;
	}

	aps_ib.aps_use_insecure_join = TRUE;
	aps_ib.aps_authenticated = FALSE;
}

u8 zb_directJoinReq(u32 scanChannels, u8 scanDuration)
{
#if defined(ZB_COORDINATOR_ROLE)
	(void)scanChannels;
	(void)scanDuration;
	return 0x32;
#else
	return zdo_nwkDirectJoinStart(scanChannels, scanDuration);
#endif
}

static void zb_factoryResetNotify(void *arg)
{
	nlme_leave_cnf_t cnf;
	u8 status = (u8)(uintptr_t)arg;

	memset(&cnf, 0, sizeof(cnf));
	cnf.status = (status <= 1U) ? NWK_STATUS_SUCCESS : NWK_STATUS_INVALID_REQUEST;

	if (zdoAppIndCbLst != NULL && zdoAppIndCbLst->zdpLeaveCnfCb != NULL) {
		zdoAppIndCbLst->zdpLeaveCnfCb(&cnf);
	}
}

void zb_factoryReset(void)
{
	if (!g_zbNwkCtx.is_factory_new) {
		nlme_leave_req_t req;
		zdo_status_t status;

		memset(&req, 0, sizeof(req));
		status = zdo_nlmeLeaveReq(&req);
		if (status == ZDO_SUCCESS) {
			return;
		}

		tl_zbTaskPost((tl_zb_callback_t)zb_factoryResetNotify, (void *)(uintptr_t)status);
		return;
	}

	nv_resetToFactoryNew();
	tl_zbTaskPost((tl_zb_callback_t)zb_factoryResetNotify, (void *)1);
}

void zb_resetDevice2FN(void)
{
#if defined(ZB_COORDINATOR_ROLE)
	tl_bdbReset2FN();
#else
	if (g_zbNwkCtx.joined) {
		zb_factoryReset();
		return;
	}

	tl_bdbReset2FN();
#endif
}

u8 zb_endDeviceSyncReq(void)
{
#if defined(ZB_ED_ROLE)
	if (!g_zbNwkCtx.joined) {
		return ZDO_NOT_PERMITTED;
	}

	return zdo_syncReq();
#else
	return 0x32;
#endif
}

u8 zb_setPollRate(u32 newRate)
{
#if defined(ZB_ED_ROLE)
	if (!g_zbNwkCtx.joined) {
		return ZDO_NOT_PERMITTED;
	}

	return zdo_set_pollRate(newRate);
#elif defined(ZB_ROUTER_ROLE)
	return 0x32;
#else
	zdo_set_pollRate(newRate);
	zdo_af_set_syn_rate(newRate);
	return RET_OK;
#endif
}

u32 zb_getPollRate(void)
{
#if defined(ZB_ED_ROLE)
	if (!g_zbNwkCtx.joined) {
		return 0;
	}

	return zdo_af_get_syn_rate();
#else
	return 0;
#endif
}

u8 zb_nwkFormation(u32 scanChannels, u8 scanDuration)
{
#if defined(ZB_ROUTER_ROLE)
	return zdo_nwkFormationStart(scanChannels, scanDuration);
#else
	(void)scanChannels;
	(void)scanDuration;
	return 0x32;
#endif
}

u8 zb_routerStart(void)
{
#if defined(ZB_ROUTER_ROLE)
	return zdo_nwkRouterStart();
#else
	return 0x32;
#endif
}

u8 zb_nwkDirectJoinAccept(nlme_directJoin_req_t *pReq)
{
#if defined(ZB_ROUTER_ROLE)
	return zdo_nwkDirectJoinAccept(pReq);
#else
	(void)pReq;
	return 0x32;
#endif
}

u8 zb_nlmePermitJoiningRequest(u8 permitDuration)
{
#if defined(ZB_ROUTER_ROLE)
	return zdo_nlmePermitJoinReq(permitDuration);
#else
	(void)permitDuration;
	return 0x32;
#endif
}

u8 zb_routeDiscReq(nlme_routeDisc_req_t *pRouteDiscReq)
{
#if defined(ZB_ROUTER_ROLE)
	return zdo_routeDiscReq(pRouteDiscReq);
#else
	(void)pRouteDiscReq;
	return 0x32;
#endif
}

u8 zb_nlmeLeaveReq(nlme_leave_req_t *pLeaveReq)
{
	return zdo_nlmeLeaveReq(pLeaveReq);
}

zdo_status_t zb_zdoNwkAddrReq(u16 dstNwkAddr, zdo_nwk_addr_req_t *pReq, u8 *seqNo,
			      zdo_callback indCb)
{
	return zb_zdo_send_short_req(dstNwkAddr, NWK_ADDR_REQ_CLID, pReq, sizeof(*pReq), seqNo,
				     indCb);
}

zdo_status_t zb_zdoIeeeAddrReq(u16 dstNwkAddr, zdo_ieee_addr_req_t *pReq, u8 *seqNo,
			       zdo_callback indCb)
{
	return zb_zdo_send_short_req(dstNwkAddr, IEEE_ADDR_REQ_CLID, pReq, sizeof(*pReq), seqNo,
				     indCb);
}

zdo_status_t zb_zdoSimpleDescReq(u16 dstNwkAddr, zdo_simple_descriptor_req_t *pReq, u8 *seqNo,
				 zdo_callback indCb)
{
	return zb_zdo_send_short_req(dstNwkAddr, SIMPLE_DESC_REQ_CLID, pReq, sizeof(*pReq), seqNo,
				     indCb);
}

zdo_status_t zb_zdoNodeDescReq(u16 dstNwkAddr, zdo_node_descriptor_req_t *pReq, u8 *seqNo,
			       zdo_callback indCb)
{
	return zb_zdo_send_short_req(dstNwkAddr, NODE_DESC_REQ_CLID, pReq, sizeof(*pReq), seqNo,
				     indCb);
}

zdo_status_t zb_zdoPowerDescReq(u16 dstNwkAddr, zdo_power_descriptor_req_t *pReq, u8 *seqNo,
				zdo_callback indCb)
{
	return zb_zdo_send_short_req(dstNwkAddr, POWER_DESC_REQ_CLID, pReq, sizeof(*pReq), seqNo,
				     indCb);
}

zdo_status_t zb_zdoActiveEpReq(u16 dstNwkAddr, zdo_active_ep_req_t *pReq, u8 *seqNo,
			       zdo_callback indCb)
{
	return zb_zdo_send_short_req(dstNwkAddr, ACTIVE_EP_REQ_CLID, pReq, sizeof(*pReq), seqNo,
				     indCb);
}

zdo_status_t zb_zdoMatchDescReq(u16 dstNwkAddr, zdo_match_descriptor_req_t *pReq, u8 *seqNo,
				zdo_callback indCb)
{
	return zb_zdo_send_short_req(dstNwkAddr, MATCH_DESC_REQ_CLID, pReq, sizeof(*pReq), seqNo,
				     indCb);
}

zdo_status_t zb_zdoComplexDescReq(u16 dstNwkAddr, zdo_complex_descriptor_req_t *pReq, u8 *seqNo,
				  zdo_callback indCb)
{
	return zb_zdo_send_short_req(dstNwkAddr, COMPLEX_DESC_REQ_CLID, pReq, sizeof(*pReq), seqNo,
				     indCb);
}

zdo_status_t zb_zdoUserDescReq(u16 dstNwkAddr, zdo_user_descriptor_req_t *pReq, u8 *seqNo,
			       zdo_callback indCb)
{
	return zb_zdo_send_short_req(dstNwkAddr, USER_DESC_REQ_CLID, pReq, sizeof(*pReq), seqNo,
				     indCb);
}

zdo_status_t zb_zdoSystemServerDiscoveryReq(u16 serverMask, u8 *seqNo, zdo_callback indCb)
{
	u16 req = serverMask;

	return zb_zdo_send_short_req(NWK_BROADCAST_RX_ON_WHEN_IDLE,
				     SYSTEM_SERVER_DISCOVERY_REQ_CLID, &req, sizeof(req), seqNo,
				     indCb);
}

zdo_status_t zb_zdoBindUnbindReq(bool isBinding, zdo_bind_req_t *pReq, u8 *seqNo,
				 zdo_callback indCb)
{
	zdo_zdp_req_t req;
	u16 dstNwkAddr;
	u16 addrMapIdx;
	u8 payloadLen;
	zdo_status_t status;

	memset(&req, 0, sizeof(req));

	if (ZB_IEEE_ADDR_CMP(pReq->src_addr, g_zbMacPib.extAddress)) {
		return zdo_apsmeBindUnBind(isBinding, (aps_me_bind_req_t *)pReq);
	}

	if (tl_zbShortAddrByExtAddr(&dstNwkAddr, pReq->src_addr, &addrMapIdx) == 0xffU) {
		return ZDO_DEVICE_NOT_FOUND;
	}

	req.dst_nwk_addr = dstNwkAddr;
	req.cluster_id = isBinding ? BIND_REQ_CLID : UNBIND_REQ_CLID;
	req.zdoRspReceivedIndCb = indCb;
	payloadLen = (pReq->dst_addr_mode == LONG_EXADDR_DSTENDPOINT) ? 22U : 15U;

	status = zdp_data_send((u8 *)pReq, payloadLen, &req);
	*seqNo = req.zdpSeqNum;
	return status;
}

zdo_status_t zb_zdoEndDeviceBindReq(zdo_edBindReq_t *pReq, u8 *seqNo, zdo_callback indCb)
{
	zdo_zdp_req_t req;
	zdo_status_t status;

	memset(&req, 0, sizeof(req));
	req.zdoRspReceivedIndCb = indCb;

	status = zdo_end_device_bind_req(pReq, &req);
	*seqNo = req.zdpSeqNum;
	return status;
}

zdo_status_t zb_mgmtPermitJoinReq(u16 dstNwkAddr, u8 permitJoinDuration, u8 tcSignificance,
				  u8 *seqNo, zdo_callback indCb)
{
	zdo_mgmt_permit_join_req_t req;
	zdo_zdp_req_t zdpReq;
	zdo_status_t status;

	req.permit_duration = permitJoinDuration;
	req.trust_center_significance = tcSignificance;

	if (dstNwkAddr == g_zbNIB.nwkAddr) {
#if defined(ZB_ROUTER_ROLE)
		return zdo_nlmePermitJoinReq(permitJoinDuration);
#else
		return ZDO_INVALID_REQUEST;
#endif
	}

	memset(&zdpReq, 0, sizeof(zdpReq));
	zdpReq.dst_addr_mode = SHORT_ADDR_MODE;
	zdpReq.dst_nwk_addr = dstNwkAddr;
	zdpReq.cluster_id = MGMT_PERMIT_JOINING_REQ_CLID;
	if (!ZB_NWK_IS_ADDRESS_BROADCAST(dstNwkAddr)) {
		zdpReq.zdoRspReceivedIndCb = indCb;
	}

	status = zdp_data_send((u8 *)&req, (u8)(sizeof(req) + 1U), &zdpReq);
	*seqNo = zdpReq.zdpSeqNum;
	return status;
}

zdo_status_t zb_mgmtLeaveReq(u16 dstNwkAddr, zdo_mgmt_leave_req_t *pReq, u8 *seqNo,
			     zdo_callback indCb)
{
	return zb_zdo_send_short_req(dstNwkAddr, MGMT_LEAVE_REQ_CLID, pReq, sizeof(*pReq), seqNo,
				     indCb);
}

zdo_status_t zb_mgmtNwkUpdateReq(u16 dstNwkAddr, zdo_mgmt_nwk_update_req_t *pReq, u8 *seqNo,
				 zdo_callback indCb)
{
	return zb_zdo_send_short_req(dstNwkAddr, MGMT_NWK_UPDATE_REQ_CLID, pReq, sizeof(*pReq),
				     seqNo, indCb);
}

zdo_status_t zb_mgmtLqiReq(u16 dstNwkAddr, zdo_mgmt_lqi_req_t *pReq, u8 *seqNo, zdo_callback indCb)
{
	return zb_zdo_send_short_req(dstNwkAddr, MGMT_LQI_REQ_CLID, pReq, sizeof(*pReq), seqNo,
				     indCb);
}

zdo_status_t zb_mgmtBindReq(u16 dstNwkAddr, zdo_mgmt_bind_req_t *pReq, u8 *seqNo,
			    zdo_callback indCb)
{
	return zb_zdo_send_short_req(dstNwkAddr, MGMT_BIND_REQ_CLID, pReq, sizeof(*pReq), seqNo,
				     indCb);
}

u8 zb_apsmeRemoveDevReq(ss_apsmeRemoveDeviceReq_t *pRemoveDevReq)
{
#if defined(ZB_ROUTER_ROLE)
	zb_buf_t *buf = zb_buf_allocate();

	if (buf == NULL) {
		return RET_NO_MEMORY;
	}

	memcpy(buf, pRemoveDevReq, sizeof(*pRemoveDevReq));
	tl_zbTaskPost(ss_apsmeRemoveDeviceReq, buf);
	return RET_OK;
#else
	(void)pRemoveDevReq;
	return 0x32;
#endif
}

u8 zb_apsmeRequestKeyReq(ss_apsmeRequestKeyReq_t *pRequestKeyReq)
{
	zb_buf_t *buf = zb_buf_allocate();

	if (buf == NULL) {
		return RET_NO_MEMORY;
	}

	memcpy(buf, pRequestKeyReq, sizeof(*pRequestKeyReq));
	tl_zbTaskPost(ss_apsmeRequestKeyReq, buf);
	return RET_OK;
}

u8 zb_apsmeTransportKeyReq(ss_apsmeTransportKeyReq_t *pTransportKeyReq)
{
	zb_buf_t *buf = zb_buf_allocate();

	if (buf == NULL) {
		return RET_NO_MEMORY;
	}

	memcpy(buf, pTransportKeyReq, sizeof(*pTransportKeyReq));
	tl_zbTaskPost(ss_apsmeTransportKeyReq, buf);
	return RET_OK;
}

u8 zb_apsmeSwitchKeyReq(ss_apsmeSwitchKeyReq_t *pSwitchKeyReq)
{
	zb_buf_t *buf = zb_buf_allocate();

	if (buf == NULL) {
		return RET_NO_MEMORY;
	}

	memcpy(buf, pSwitchKeyReq, sizeof(*pSwitchKeyReq));
	tl_zbTaskPost(ss_apsmeSwitchKeyReq, buf);
	return RET_OK;
}

u8 zb_tcUpdateNwkKey(ss_tcUpdateNwkKey_t *pTcUpdateNwkKey)
{
#if defined(ZB_COORDINATOR_ROLE)
	ss_apsmeTransportKeyReq_t *req = (ss_apsmeTransportKeyReq_t *)zb_buf_allocate();

	if (req == NULL) {
		return RET_NO_MEMORY;
	}

	memset(req, 0, sizeof(*req));
	ZB_IEEE_ADDR_COPY(req->dstAddr, pTcUpdateNwkKey->dstAddr);
	memcpy(req->key, pTcUpdateNwkKey->key, SEC_KEY_LEN);
	req->keyType = SS_STANDARD_NETWORK_KEY;
	req->keySeqNum = (u8)(ss_ib.activeKeySeqNum + 1U);
	req->nwkSecurity = 1;
	ss_tcTransportKeyTimerStart(req);
	return RET_OK;
#else
	(void)pTcUpdateNwkKey;
	return 0x32;
#endif
}

bool zb_bindingTblSearched(u16 clusterID, u8 srcEp)
{
	return aps_bindingTblMatched(clusterID, srcEp) ? TRUE : FALSE;
}

u8 zb_zdoSendDevAnnance(void)
{
	zdo_device_announce_send();
	return RET_OK;
}

void zb_zdoSendParentAnnce(void)
{
	zdo_apsParentAnnceTimerStart();
}

void zb_macCbRegister(mac_appIndCb_t *cb)
{
	mac_appIndCbRegister(cb);
}

void zb_zdoCbRegister(zdo_appIndCb_t *cb)
{
	zdo_zdpCbTblRegister(cb);
}

void zb_joinAFixedNetwork(u8 channel, u16 panId, u16 shortAddr, u8 *extPanId, u8 *nwkKey,
			  u8 *tcAddr)
{
	tl_zbMacChannelSet(channel);
	g_zbMacPib.panId = panId;
	g_zbMacPib.shortAddress = shortAddr;
	g_zbNIB.panId = panId;
	g_zbNIB.nwkAddr = shortAddr;

	ZB_EXTPANID_COPY(g_zbNIB.extPANId, extPanId);
	ZB_IEEE_ADDR_COPY(g_zbNIB.ieeeAddr, g_zbMacPib.extAddress);
	memcpy(ss_ib.nwkSecurMaterialSet[0].key, nwkKey, SEC_KEY_LEN);
	ss_ib_active_secure_material_index_set(0);

	if (tcAddr != NULL && !ZB_IEEE_ADDR_IS_ZERO(tcAddr) && !ZB_IEEE_ADDR_IS_INVALID(tcAddr)) {
		ZB_IEEE_ADDR_COPY(ss_ib.trust_center_address, tcAddr);
	}

	g_zbNwkCtx.is_factory_new = 0;
}

void zb_extPanIdRejoin(extPANId_t extPanId)
{
	ZB_EXTPANID_COPY(aps_ib.aps_use_ext_panid, extPanId);
	g_zbNwkCtx.is_factory_new = 0;
	aps_ib.aps_use_insecure_join = TRUE;
	aps_ib.aps_authenticated = FALSE;
	zdo_nwkRejoinStart(aps_ib.aps_channel_mask, zdo_cfg_attributes.config_nwk_scan_duration);
}

void zb_preConfigNwkKey(u8 *nwkKey, bool enTransKey)
{
	if (!g_zbNwkCtx.is_factory_new) {
		return;
	}

	ss_zdoNwkKeyConfigure(nwkKey, 0, TRUE);
	ss_zdoUseKey(0);

	if (!enTransKey) {
		/* Vendor ASM sets bit 1 of aps_ib+18 here, i.e. the authenticated
		 * flag, rather than the adjacent hold-security bit (bit 2). */
		aps_ib.aps_authenticated = TRUE;
	}

	ss_ib.preConfiguredKeyType |= SS_PRECONFIGURED_NWKKEY;
}
