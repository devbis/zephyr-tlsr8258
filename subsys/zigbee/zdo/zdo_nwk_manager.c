/*
 * Copyright 2026 libzigbee
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "zb_common.h"
#include "zdo_nwk_manager.h"
#include "zdo.h"
#include "zdp_services.h"
#include "ss_apsSecurityME.h"
#include "ss_zdoSecurityME.h"
#include "nwk_routing.h"
#include <stdint.h>
#include "ev_timer.h"

typedef enum {
	ZDO_NWK_MGR_STATE_IDLE = 0,
	ZDO_NWK_MGR_STATE_FORMATION = 1,
	ZDO_NWK_MGR_STATE_START_ROUTER = 2,
	ZDO_NWK_MGR_STATE_NWK_DISC = 3,
	ZDO_NWK_MGR_STATE_ASSOC_JOIN = 4,
	ZDO_NWK_MGR_STATE_REJOIN = 5,
	ZDO_NWK_MGR_STATE_DIRECT_JOIN = 6,
	ZDO_NWK_MGR_STATE_ED_SCAN = 7,
} zdo_nwk_mgr_state_t;

u8 zdo_mgmt_nwk_flag = 0;
zdo_nwk_manager_t g_zdo_nwk_manager = {0};

#if UINTPTR_MAX == UINT32_MAX
STATIC_ASSERT(OFFSETOF(zdo_nwk_manager_t, discEvt) == 0);
STATIC_ASSERT(OFFSETOF(zdo_nwk_manager_t, authEvt) == 4);
STATIC_ASSERT(OFFSETOF(zdo_nwk_manager_t, pollEvt) == 8);
STATIC_ASSERT(OFFSETOF(zdo_nwk_manager_t, backoffEvt) == 12);
STATIC_ASSERT(OFFSETOF(zdo_nwk_manager_t, nwkDiscCb) == 16);
STATIC_ASSERT(OFFSETOF(zdo_nwk_manager_t, savedBuf) == 20);
STATIC_ASSERT(OFFSETOF(zdo_nwk_manager_t, scanChannels) == 24);
STATIC_ASSERT(OFFSETOF(zdo_nwk_manager_t, backoffTime) == 28);
STATIC_ASSERT(OFFSETOF(zdo_nwk_manager_t, backoffIter) == 30);
STATIC_ASSERT(OFFSETOF(zdo_nwk_manager_t, rejoinCnt) == 32);
STATIC_ASSERT(OFFSETOF(zdo_nwk_manager_t, scanDuration) == 33);
STATIC_ASSERT(OFFSETOF(zdo_nwk_manager_t, nwkDiscAttempt) == 34);
STATIC_ASSERT(OFFSETOF(zdo_nwk_manager_t, scanCount) == 35);
STATIC_ASSERT(OFFSETOF(zdo_nwk_manager_t, linkRetryCnt) == 36);
STATIC_ASSERT(OFFSETOF(zdo_nwk_manager_t, state) == 37);
STATIC_ASSERT(sizeof(zdo_nwk_manager_t) == 0x26);
#endif
static void zdo_startup_complete(void *arg);
static void zdo_startDeviceCnf(void *arg, u8 status);

#if defined(ZB_ROUTER_ROLE)
_attribute_no_inline_ static void zdo_nlme_start_router_req(void *arg)
{
	nlme_startRouter_req_t *req = (nlme_startRouter_req_t *)arg;

	req->beaconOrder = 15;
	req->superframeOrder = 15;
	req->batteryLifeExt = 0;

	zdo_nwk_mngr()->state = ZDO_NWK_MGR_STATE_START_ROUTER;
	tl_zbPrimitivePost(TL_Q_HIGH2NWK, NWK_NLME_START_ROUTER_REQ, arg);
}
#endif

static zdo_status_t zdo_nlmeNwkDiscReq(void *arg)
{
	(void)arg;
	nlme_nwkDisc_req_t *req;

	zb_buf_t *buf = zb_buf_allocate();
	if (buf == NULL) {
		return ZDO_INSUFFICIENT_SPACE;
	}

	zdo_nwk_mngr()->state = ZDO_NWK_MGR_STATE_NWK_DISC;
	req = (nlme_nwkDisc_req_t *)buf;
	req->scanChannels = zdo_nwk_mngr()->scanChannels;
	req->scanDuration = zdo_nwk_mngr()->scanDuration;
	tl_zbPrimitivePost(TL_Q_HIGH2NWK, NWK_NLME_NWK_DISCOVERY_REQ, buf);
	return ZDO_SUCCESS;
}

static int zdo_nwkDiscReqTimerCb(void *arg)
{
	(void)arg;

	if (zdo_nlmeNwkDiscReq(NULL) == ZDO_SUCCESS) {
		zdo_nwk_mngr()->discEvt = NULL;
		return -1;
	}

	return 0;
}

static zdo_status_t zdo_nlme_edScan(void *arg)
{
	nlme_edScan_req_t *req = (nlme_edScan_req_t *)arg;

	req->scanChannels = zdo_nwk_mngr()->scanChannels;
	req->scanDuration = zdo_nwk_mngr()->scanDuration;
	tl_zbPrimitivePost(TL_Q_HIGH2NWK, NWK_NLME_ED_SCAN_REQ, arg);
	return ZDO_SUCCESS;
}

#if defined(ZB_ROUTER_ROLE)
zdo_status_t zdo_nlmePermitJoinReq(u8 permitDuration)
{
	zb_buf_t *buf = zb_buf_allocate();
	nlme_permitJoining_req_t *req;

	if (buf == NULL) {
		return ZDO_INSUFFICIENT_SPACE;
	}

	req = (nlme_permitJoining_req_t *)buf;
	req->permitDuration = permitDuration;
	tl_zbPrimitivePost(TL_Q_HIGH2NWK, NWK_NLME_PERMIT_JOINING_REQ, buf);
	return ZDO_SUCCESS;
}

void zdo_nlmePermitJoinCnf(void *arg)
{
	zb_buf_free((zb_buf_t *)arg);
}

zdo_status_t zdo_routeDiscReq(nlme_routeDisc_req_t *pRouteDiscReq)
{
	zb_buf_t *buf = zb_buf_allocate();

	if (buf == NULL) {
		return ZDO_INSUFFICIENT_SPACE;
	}

	memcpy(buf, pRouteDiscReq, sizeof(*pRouteDiscReq));
	tl_zbPrimitivePost(TL_Q_HIGH2NWK, NWK_NLME_ROUTE_DISCOVERY_REQ, buf);
	return ZDO_SUCCESS;
}

void zdo_routeDiscCnf(void *arg)
{
	zb_buf_free((zb_buf_t *)arg);
}

#if defined(ZB_COORDINATOR_ROLE)
void zdo_manyToOneRouteDisc(void *arg)
{
	nlme_routeDisc_req_t *req = (nlme_routeDisc_req_t *)arg;

	req->dstAddr = NWK_BROADCAST_ROUTER_COORDINATOR;
	req->dstAddrMode = 0;
	req->radius = g_zbNIB.concentratorRadius;
	req->noRouteCache = (NWK_ROUTE_RECORD_TABLE_SIZE == 0U);
	tl_zbPrimitivePost(TL_Q_HIGH2NWK, NWK_NLME_ROUTE_DISCOVERY_REQ, arg);
}
#endif

zdo_status_t zdo_nwkRouterStart(void)
{
	zb_buf_t *buf;

	if (zdo_nwk_mngr()->state != ZDO_NWK_MGR_STATE_IDLE) {
		return ZDO_INVALID_REQUEST;
	}

	buf = zb_buf_allocate();
	if (buf == NULL) {
		return ZDO_INSUFFICIENT_SPACE;
	}

	*(u8 *)&g_zbNIB.capabilityInfo = af_nodeMacCapabilityGet();
	zdo_nlme_start_router_req(buf);
	return ZDO_SUCCESS;
}

void zdo_nlme_start_router_confirm(void *arg)
{
	nlme_startRouter_cnf_t *cnf = (nlme_startRouter_cnf_t *)arg;
	u8 status = cnf->status;

	if (zdo_nwk_mngr()->state != ZDO_NWK_MGR_STATE_START_ROUTER) {
		zb_buf_free((zb_buf_t *)arg);
		return;
	}

	zdo_startDeviceCnf(arg, status);
#if defined(ZB_COORDINATOR_ROLE)
	if (status == ZDO_SUCCESS) {
		zb_buf_t *buf = zb_buf_allocate();

		if (buf != NULL) {
			tl_zbTaskPost(zdo_manyToOneRouteDisc, buf);
		}
	}
#endif
}

zdo_status_t zdo_nwkFormationStart(u32 scanChannels, u8 scanDuration)
{
	nlme_nwkFormation_req_t *req;
#if defined(ZB_ROUTER_ROLE) && !defined(ZB_COORDINATOR_ROLE)
	u16 nwkAddr = g_zbNIB.nwkAddr;
#endif

	if (zdo_nwk_mngr()->state != ZDO_NWK_MGR_STATE_IDLE) {
		return ZDO_INVALID_REQUEST;
	}

	req = (nlme_nwkFormation_req_t *)zb_buf_allocate();
	if (req == NULL) {
		return ZDO_INSUFFICIENT_SPACE;
	}

#if defined(ZB_COORDINATOR_ROLE)
	req->distributedNetwork = 0;
	req->distributedNwkAddr = 0;
#else
	req->distributedNetwork = 1;
	req->distributedNwkAddr = nwkAddr;

	if (nwkAddr == NWK_BROADCAST_RESERVED) {
		do {
			nwkAddr = (u16)drv_u32Rand();
		} while (nwkAddr == 0U || nwkAddr == NWK_BROADCAST_RESERVED ||
			 ZB_NWK_IS_ADDRESS_BROADCAST(nwkAddr));
		req->distributedNwkAddr = nwkAddr;
	} else if (nwkAddr == 0U || ZB_NWK_IS_ADDRESS_BROADCAST(nwkAddr)) {
		zb_buf_free((zb_buf_t *)req);
		return ZDO_NOT_SUPPORTED;
	}
#endif

	req->scanChannels = scanChannels;
	req->batteryLifeExt = 0;
	req->scanDuration = scanDuration;
	req->beaconOrder = 15;
	req->superframeOrder = 15;

	zdo_nwk_mngr()->state = ZDO_NWK_MGR_STATE_FORMATION;
	zdo_nwk_mngr()->scanChannels = scanChannels;
	zdo_nwk_mngr()->scanDuration = scanDuration;
	*(u8 *)&g_zbNIB.capabilityInfo = af_nodeMacCapabilityGet();

	if (!ZB_EXTPANID_IS_ZERO(aps_ib.aps_use_ext_panid)) {
		ZB_EXTPANID_COPY(g_zbNIB.extPANId, aps_ib.aps_use_ext_panid);
	}

	tl_zbAdditionNeighborReset();
	tl_zbPrimitivePost(TL_Q_HIGH2NWK, NWK_NLME_NWK_FORMATION_REQ, req);
	return ZDO_SUCCESS;
}

void zdo_network_formation_confirm(void *arg)
{
	nlme_nwkFormation_cnf_t *cnf = (nlme_nwkFormation_cnf_t *)arg;
	u8 status = cnf->status;

	if (zdo_nwk_mngr()->state != ZDO_NWK_MGR_STATE_FORMATION) {
		zb_buf_free((zb_buf_t *)arg);
		return;
	}

	zdo_startDeviceCnf(arg, status);
#if defined(ZB_COORDINATOR_ROLE)
	if (status == ZDO_SUCCESS) {
		zb_buf_t *buf = zb_buf_allocate();

		if (buf != NULL) {
			tl_zbTaskPost(zdo_manyToOneRouteDisc, buf);
		}
	}
#endif
}
#endif

static void zdo_startDeviceCnf(void *arg, u8 status)
{
	zdo_start_device_confirm_t *cnf = (zdo_start_device_confirm_t *)arg;

	cnf->channel_num = 0;
	cnf->pan_id = 0;
	cnf->short_addr = 0;
	cnf->status = status;

	if (status == ZDO_SUCCESS) {
		cnf->channel_num = g_zbInfo.macPib.phyChannelCur;
		cnf->pan_id = g_zbInfo.macPib.panId;
		cnf->short_addr = g_zbInfo.macPib.shortAddress;
	}

	zdo_nwk_mngr()->state = ZDO_NWK_MGR_STATE_IDLE;
	tl_zbTaskPost(zdo_startup_complete, arg);
}
static zdo_status_t zdo_nwkRejoinReqSend(void *arg)
{
	(void)arg;
	nlme_join_req_t *req;

	zb_buf_t *buf = zb_buf_allocate();
	if (buf == NULL) {
		return ZDO_INSUFFICIENT_SPACE;
	}

	zdo_nwk_mngr()->state = ZDO_NWK_MGR_STATE_REJOIN;
	tl_zbAdditionNeighborReset();

	req = (nlme_join_req_t *)buf;
	req->rejoinNwk = NLME_REJOIN_METHOD_REJOIN;
	req->scanChannels = zdo_nwk_mngr()->scanChannels;
	req->scanDuration = zdo_nwk_mngr()->scanDuration;
	*(u8 *)&req->capabilityInfo = af_nodeMacCapabilityGet();
	req->securityEnabled = (aps_ib.aps_use_insecure_join & 0x01U) == 0U;

	if (ZB_EXTPANID_IS_ZERO(aps_ib.aps_use_ext_panid)) {
		ZB_EXTPANID_COPY(req->extPANId, g_zbInfo.nwkNib.extPANId);
	} else {
		ZB_EXTPANID_COPY(req->extPANId, aps_ib.aps_use_ext_panid);
	}

	tl_zbPrimitivePost(TL_Q_HIGH2NWK, NWK_NLME_JOIN_REQ, buf);
	return ZDO_SUCCESS;
}
static int zdo_nwkRejoinBackOffCb(void *arg)
{
	(void)arg;

	if (zdo_af_get_rejoin_duration() != 0) {
		if (zdo_nwk_mngr()->rejoinCnt < zdo_af_get_rejoin_times()) {
			zdo_nwk_mngr()->rejoinCnt++;
			zdo_nwkRejoinReqSend(NULL);
			return (int)(zdo_af_get_rejoin_duration() * 1000U);
		}

		zdo_nwk_mngr()->rejoinCnt = 0;
	}

	if (zdo_af_get_rejoin_backoff_time() == 0) {
		zdo_nwk_mngr()->backoffEvt = NULL;
		return -1;
	}

	if (zdo_af_get_rejoin_backoff_iteration() == 0) {
		u16 iter = zdo_nwk_mngr()->backoffIter;
		if (iter != 0xffff) {
			iter++;
			zdo_nwk_mngr()->backoffIter = iter;
		}
	} else {
		u16 iter = zdo_nwk_mngr()->backoffIter;
		if (iter >= zdo_af_get_rejoin_backoff_iteration()) {
			zdo_nwk_mngr()->backoffTime = 0;
			iter = 0;
		}
		zdo_nwk_mngr()->backoffIter = iter + 1;
	}

	if (zdo_nwk_mngr()->backoffTime > zdo_af_get_max_rejoin_backoff_time()) {
		zdo_nwk_mngr()->backoffTime = zdo_af_get_max_rejoin_backoff_time();
	} else {
		zdo_nwk_mngr()->backoffTime =
			(u16)(zdo_af_get_rejoin_backoff_time() * zdo_nwk_mngr()->backoffIter);
	}

	zdo_nwkRejoinReqSend(NULL);
	return (int)(zdo_nwk_mngr()->backoffTime * 1000U);
}
static int zdo_auth_check_timer_cb(void *arg)
{
	tl_zbNwkAddrMapInit();
	tl_zbNeighborTableInit();
	zdo_startDeviceCnf(arg, ZDO_NOT_AUTHORIZED);
	zdo_nwk_mngr()->savedBuf = NULL;
	zdo_nwk_mngr()->authEvt = NULL;
	return -1;
}

bool zb_isUnderRejoinMode(void)
{
	return zdo_nwk_mngr()->state == ZDO_NWK_MGR_STATE_REJOIN;
}

zdo_status_t zdo_reset_req(u8 arg)
{
	zb_buf_t *buf = zb_buf_allocate();
	nlme_reset_req_t *req;
	if (buf == NULL) {
		return ZDO_INSUFFICIENT_SPACE;
	}

	req = (nlme_reset_req_t *)buf;
	req->warmStart = arg != 0U;
	tl_zbPrimitivePost(TL_Q_HIGH2NWK, NWK_NLME_RESET_REQ, buf);
	return ZDO_SUCCESS;
}

void zdo_reset_confirm_cb(void *arg)
{
	nlme_reset_cnf_t cnf;

	if (zdoAppIndCbLst != NULL && zdoAppIndCbLst->zdpResetCnfCb != NULL) {
		cnf.status = ((nlme_reset_cnf_t *)arg)->status;
		zdoAppIndCbLst->zdpResetCnfCb(&cnf);
	}

	zb_buf_free((zb_buf_t *)arg);
}
zdo_status_t zdo_nlmeEdScanReq(u32 scanChannels, u8 scanDuration, u8 scanCount)
{
	if (scanCount == 0 || zdo_nwk_mngr()->state != ZDO_NWK_MGR_STATE_IDLE) {
		return ZDO_INVALID_REQUEST;
	}

	zb_buf_t *buf = zb_buf_allocate();
	if (buf == NULL) {
		return ZDO_INSUFFICIENT_SPACE;
	}

	zdo_nwk_mngr()->state = ZDO_NWK_MGR_STATE_ED_SCAN;
	zdo_nwk_mngr()->scanChannels = scanChannels;
	zdo_nwk_mngr()->scanDuration = scanDuration;
	zdo_nwk_mngr()->scanCount = scanCount;
	zdo_nlme_edScan(buf);
	return ZDO_SUCCESS;
}

void zdo_nlme_ed_scan_confirm(void *arg)
{
	if (zdo_nwk_mngr()->state == ZDO_NWK_MGR_STATE_ED_SCAN) {
		u8 scanCount = (u8)(zdo_nwk_mngr()->scanCount - 1U);

		zdo_nwk_mngr()->scanCount = scanCount;
		if (scanCount != 0) {
			zdo_nlme_edScan(arg);
			return;
		}

		zdo_nwk_mngr()->state = ZDO_NWK_MGR_STATE_IDLE;
		if ((zdo_mgmt_nwk_flag & 0x01U) != 0U) {
			zdo_mgmt_nwk_flag &= (u8)~0x01U;
			tl_zbTaskPost(zdo_nwkUpdateNotifyRespSend, arg);
			return;
		}
	}

	zb_buf_free((zb_buf_t *)arg);
}
#if !defined(ZB_ROUTER_ROLE)
void zdo_nlme_sync_confirm(void *arg)
{
	nlme_sync_cnf_t cnf;
	u8 status = ((nlme_sync_cnf_t *)arg)->status;

	if (zdoAppIndCbLst != NULL && zdoAppIndCbLst->zdoNlmeSyncCnfCb != NULL) {
		cnf.status = status;
		zdoAppIndCbLst->zdoNlmeSyncCnfCb(&cnf);
	}

	if (status != MAC_STA_CHANNEL_ACCESS_FAILURE && status != MAC_STA_NO_ACK) {
		zdo_nwk_mngr()->linkRetryCnt = 0;
	}

	zb_buf_free((zb_buf_t *)arg);
}

u8 zdo_syncReq(void)
{
	zb_buf_t *buf = zb_buf_allocate();
	nlme_sync_req_t *req;
	if (buf == NULL) {
		return ZDO_INSUFFICIENT_SPACE;
	}

	req = (nlme_sync_req_t *)buf;
	req->track = false;
	tl_zbPrimitivePost(TL_Q_HIGH2NWK, NWK_NLME_SYNC_REQ, buf);
	return RET_OK;
}

static int pollRateCb(void *arg)
{
	(void)arg;

	zdo_syncReq();
	return 0;
}

u8 zdo_set_pollRate(u32 rate)
{
	if (g_zbInfo.macPib.rxOnWhenIdle != 0U) {
		return ZDO_NOT_SUPPORTED;
	}

	if (zdo_af_get_syn_rate() == rate) {
		if (rate != 0U) {
			return ZDO_NOT_SUPPORTED;
		}

		if (zdo_nwk_mngr()->pollEvt != NULL) {
			ev_timer_taskCancel(&zdo_nwk_mngr()->pollEvt);
		}

		return ZDO_SUCCESS;
	}

	zdo_af_set_syn_rate(rate);
	if (rate != 0U) {
		ev_timer_event_t *evt = zdo_nwk_mngr()->pollEvt;

		if (evt != NULL) {
			ev_on_timer(evt, rate);
		} else {
			evt = ev_timer_taskPost(pollRateCb, NULL, rate);
			zdo_nwk_mngr()->pollEvt = evt;
		}
		return ZDO_SUCCESS;
	}

	if (zdo_nwk_mngr()->pollEvt != NULL) {
		ev_timer_taskCancel(&zdo_nwk_mngr()->pollEvt);
	}

	return ZDO_SUCCESS;
}
#endif

static void zdo_startup_complete(void *arg)
{
	zdo_start_device_confirm_t *cnf = (zdo_start_device_confirm_t *)arg;

	/* The three vendor variants of this function differ well beyond the
	 * link-status calls, so each branch is spelled out per role:
	 *
	 *  failure  ed         : flags cleared unconditionally  ("bc: tj 14")
	 *           router/coor: flags cleared only when factory new
	 *                        ("5a: tjmi 9a" ... "ca: tj 5c")
	 *  success  ed         : is_tc=0, aps_ib auth, announce unconditionally
	 *           router     : same, but the announce is guarded
	 *           coordinator: is_tc=1 + ss_zdoTcInit(), no aps_ib, no announce
	 */
	if (cnf->status == ZDO_SUCCESS) {
		g_zbNwkCtx.joined = 1;
#if defined(ZB_COORDINATOR_ROLE)
		g_zbNwkCtx.is_tc = 1;
		ss_zdoTcInit();
#else
		g_zbNwkCtx.is_tc = 0;
		aps_ib.aps_authenticated = 1;
		aps_ib.aps_use_insecure_join = 0;
#if defined(ZB_ROUTER_ROLE)
		/* "32: tnand r1,r3" (is_factory_new) and, failing that,
		 * "3a: tshftls r1,r3,#26; 3c: tjpl 42" (hdr.rejoinStartAgain). */
		if (g_zbNwkCtx.is_factory_new || ((zb_buf_t *)arg)->hdr.rejoinStartAgain) {
			zdo_device_announce_send();
		}
#else
		zdo_device_announce_send();
#endif
#endif
		g_zbNwkCtx.is_factory_new = 0;
#if defined(ZB_ROUTER_ROLE) || defined(ZB_COORDINATOR_ROLE)
		tl_zbNwkLinkStatusStart();
#endif
	} else {
#if defined(ZB_ROUTER_ROLE) || defined(ZB_COORDINATOR_ROLE)
		if (g_zbNwkCtx.is_factory_new) {
			ZB_IEEE_ADDR_ZERO(ss_ib.trust_center_address);
			g_zbNwkCtx.joined = 0;
			g_zbNwkCtx.joined_pro = 0;
		}

		tl_zbNwkLinkStatusStop();
#else
		if (g_zbNwkCtx.is_factory_new) {
			ZB_IEEE_ADDR_ZERO(ss_ib.trust_center_address);
		}

		g_zbNwkCtx.joined = 0;
		g_zbNwkCtx.joined_pro = 0;
		zdo_set_pollRate(0);
		keepaliveMsgSendStop();
#endif
	}

	g_zbNwkCtx.user_state = NLME_IDLE;
	tl_zbAdditionNeighborReset();

	if (zdoAppIndCbLst != NULL && zdoAppIndCbLst->zdpStartDevCnfCb != NULL) {
		zdoAppIndCbLst->zdpStartDevCnfCb(cnf);
	}

	zb_buf_free((zb_buf_t *)arg);
}
void zdo_nlmeForgetDev(addrExt_t nodeIeeeAddr, bool rejoin)
{
	u16 shortAddr = (u16)-1;
	u16 addrMapIdx = 0;

#if defined(ZB_COORDINATOR_ROLE)
	if (!rejoin) {
		(void)ss_devKeyPairDelete(nodeIeeeAddr);
	}
#endif

	if (tl_zbShortAddrByExtAddr(&shortAddr, nodeIeeeAddr, &addrMapIdx) != RET_OK) {
		return;
	}

	if (rejoin) {
#if !defined(ZB_ED_ROLE)
		nwkRoutingTabEntryDstDel(shortAddr);
#endif
#if defined(ZB_COORDINATOR_ROLE)
		nwkRouteRecTabEntryDstDel(shortAddr);
#endif
	} else {
		aps_bindingTblEntryDelByDstExtAddr(nodeIeeeAddr);
	}

	{
		tl_zb_normal_neighbor_entry_t *entry = nwk_neTblGetByExtAddr(nodeIeeeAddr);

		if (entry != NULL) {
			zb_addrForNeighbor_t addrInfo;

			memset(&addrInfo, 0, sizeof(addrInfo));
			addrInfo.shortAddr = tl_zbshortAddrByIdx(entry->addrmapIdx);
			tl_zbExtAddrByIdx(entry->addrmapIdx, addrInfo.extAddr);
			addrInfo.relationship = entry->relationship;

			(void)nwk_nodeAddrInfoDelete(&addrInfo);
			tl_zbNeighborTableDelete(entry);
			return;
		}
	}

	tl_zbNwkAddrMapDelete(addrMapIdx);
}
void zdo_nlme_leave_indication_cb(void *arg)
{
	nlme_leave_ind_t ind;
#if defined(ZB_ROUTER_ROLE) || defined(ZB_COORDINATOR_ROLE)
	u16 shortAddr = 0;
	u16 addrMapIdx = 0;
	tl_zb_normal_neighbor_entry_t *entry;
#endif

	memcpy(&ind, arg, sizeof(ind));

#if defined(ZB_ROUTER_ROLE) || defined(ZB_COORDINATOR_ROLE)
	entry = tl_zbNeighborTableSearchFromExtAddr(&shortAddr, ind.deviceAddr, &addrMapIdx);
	if (!g_zbNwkCtx.is_tc && !ind.rejoin && entry != NULL &&
	    !ZB_IEEE_ADDR_IS_INVALID(ss_ib.trust_center_address) &&
	    entry->relationship == NEIGHBOR_IS_CHILD) {
		ss_apsmeUpdateDeviceReq_t *req = (ss_apsmeUpdateDeviceReq_t *)arg;

		ZB_IEEE_ADDR_COPY(req->dstAddr, ss_ib.trust_center_address);
		ZB_IEEE_ADDR_COPY(req->devAddr, ind.deviceAddr);
		req->devShortAddr = shortAddr;
		req->status = SS_DEV_LEFT;
		tl_zbTaskPost(ss_apsmeUpdateDevReq, req);
		return;
	}
#endif

	zb_buf_free((zb_buf_t *)arg);
	zdo_nlmeForgetDev(ind.deviceAddr, ind.rejoin);

	if (zdoAppIndCbLst != NULL && zdoAppIndCbLst->zdpLeaveIndCb != NULL) {
		zdoAppIndCbLst->zdpLeaveIndCb(&ind);
	}
}

zdo_status_t zdo_nlmeLeaveReq(void *arg)
{
	zb_buf_t *buf = zb_buf_allocate();
	if (buf == NULL) {
		return ZDO_INSUFFICIENT_SPACE;
	}

	memcpy(buf, arg, sizeof(nlme_leave_req_t));
	tl_zbPrimitivePost(TL_Q_HIGH2NWK, NWK_NLME_LEAVE_REQ, buf);
	return ZDO_SUCCESS;
}

void zdo_nwkAuthTimeoutStart(void *arg)
{
	if (zdo_nwk_mngr()->authEvt != NULL) {
		zb_buf_free((zb_buf_t *)arg);
		return;
	}

	zdo_nwk_mngr()->savedBuf = arg;
	/* "1a: tjl ev_timer_taskPost" is reached with r1 still holding the
	 * confirmation buffer: the callback completes the start-device request
	 * through its own argument and does not look at savedBuf. */
	zdo_nwk_mngr()->authEvt =
		ev_timer_taskPost(zdo_auth_check_timer_cb, arg, TRANSPORT_NETWORK_KEY_WAIT_TIME);
}

zdo_status_t zdo_nwkAssocJoinStart(void)
{
	zb_buf_t *buf;
	nlme_join_req_t *req;

	if (zdo_nwk_mngr()->state != ZDO_NWK_MGR_STATE_IDLE) {
		return ZDO_INVALID_REQUEST;
	}

	buf = zb_buf_allocate();
	if (buf == NULL) {
		return ZDO_INSUFFICIENT_SPACE;
	}

	g_zbNwkCtx.joined = 0;
	zdo_nwk_mngr()->state = ZDO_NWK_MGR_STATE_ASSOC_JOIN;

	req = (nlme_join_req_t *)buf;
	req->rejoinNwk = NLME_REJOIN_METHOD_ASSOCIATION;
	ZB_EXTPANID_COPY(req->extPANId, aps_ib.aps_use_ext_panid);
	*(u8 *)&req->capabilityInfo = af_nodeMacCapabilityGet();
	req->securityEnabled = FALSE;

	tl_zbPrimitivePost(TL_Q_HIGH2NWK, NWK_NLME_JOIN_REQ, buf);
	return ZDO_SUCCESS;
}
zdo_status_t zdo_nwkRejoinStart(u32 scanChannels, u8 scanDuration)
{
	if (zdo_nwk_mngr()->state != ZDO_NWK_MGR_STATE_IDLE) {
		return ZDO_INVALID_REQUEST;
	}

	if (ZB_EXTPANID_IS_ZERO(aps_ib.aps_use_ext_panid) &&
	    ZB_EXTPANID_IS_ZERO(g_zbInfo.nwkNib.extPANId)) {
		return ZDO_INVALID_REQUEST;
	}

	g_zbNwkCtx.joined = 0;
	zdo_nwk_mngr()->scanChannels = scanChannels;
	zdo_nwk_mngr()->scanDuration = scanDuration;
	return zdo_nwkRejoinReqSend(NULL);
}
static int zdo_selfLeaveProcessCb(void *arg)
{
	nlme_leave_cnf_t cnf = {0};

	g_zbNwkCtx.is_tc = 0;
	zdo_nwk_mngr()->linkRetryCnt = 0;

	if (arg != NULL) {
		zdo_nwkRejoinStart(1UL << g_zbInfo.macPib.phyChannelCur,
				   zdo_cfg_attributes.config_nwk_scan_duration);
		return -1;
	}

	nv_nwkFrameCountSaveToFlash(ss_ib.outgoingFrameCounter);
	nv_resetToFactoryNew();
	zb_reset();

	if (zdoTouchLinkCb != NULL) {
		if (zdoTouchLinkCb->attrClearCb != NULL) {
			zdoTouchLinkCb->attrClearCb();
		}

		g_zbNwkCtx.user_state = NLME_IDLE;

		if (zdoTouchLinkCb->leaveCnfCb != NULL && zdoTouchLinkCb->leaveCnfCb(&cnf)) {
			zdo_nwkRejoinStart(1UL << g_zbInfo.macPib.phyChannelCur,
					   zdo_cfg_attributes.config_nwk_scan_duration);
			return -1;
		}
	} else {
		g_zbNwkCtx.user_state = NLME_IDLE;
	}

	if (zdoAppIndCbLst != NULL && zdoAppIndCbLst->zdpLeaveCnfCb != NULL) {
		zdoAppIndCbLst->zdpLeaveCnfCb(&cnf);
	}

	return -1;
}
void zdo_nwkRejoinWithBackOffStop(void)
{
	void *evt = zdo_nwk_mngr()->backoffEvt;

	if (evt != NULL) {
		ev_timer_taskCancel(&zdo_nwk_mngr()->backoffEvt);
	}

	zdo_nwk_mngr()->rejoinCnt = 0;
	zdo_nwk_mngr()->backoffIter = 0;
	zdo_nwk_mngr()->backoffTime = 0;
}
void zdo_nlme_leave_confirm_cb(void *arg)
{
	nlme_leave_cnf_t cnf;
	bool rejoin;

	memcpy(&cnf, arg, sizeof(cnf));
	rejoin = ((zb_buf_t *)arg)->hdr.leaveRejoin != 0U;
	zb_buf_free((zb_buf_t *)arg);

	if (ZB_IEEE_ADDR_IS_ZERO(cnf.deviceAddr) ||
	    ZB_IEEE_ADDR_CMP(cnf.deviceAddr, g_zbInfo.macPib.extAddress)) {
		zdo_nwkRejoinWithBackOffStop();
		g_zbNwkCtx.user_state = NLME_LEAVING;
#if defined(ZB_ROUTER_ROLE)
		tl_zbNwkLinkStatusStop();
#else
		zdo_set_pollRate(0);
		keepaliveMsgSendStop();
#endif
		ev_timer_taskPost(zdo_selfLeaveProcessCb, (void *)(uintptr_t)rejoin, 200);
		return;
	}

	if (cnf.status != MAC_STA_FRAME_PENDING && cnf.status != NWK_STATUS_SUCCESS) {
		zdo_nlmeForgetDev(cnf.deviceAddr, rejoin);
	}
}
zdo_status_t zdo_nwkRejoinWithBackOff(u32 scanChannels, u8 scanDuration)
{
	if (zdo_nwk_mngr()->state != ZDO_NWK_MGR_STATE_IDLE) {
		return ZDO_INVALID_REQUEST;
	}

	if (ZB_EXTPANID_IS_ZERO(aps_ib.aps_use_ext_panid) &&
	    ZB_EXTPANID_IS_ZERO(g_zbInfo.nwkNib.extPANId)) {
		return ZDO_INVALID_REQUEST;
	}

	if (zdo_af_get_rejoin_times() == 0 || zdo_nwk_mngr()->backoffEvt != NULL) {
		return ZDO_INVALID_REQUEST;
	}

	g_zbNwkCtx.joined = 0;
	zdo_nwk_mngr()->scanChannels = scanChannels;
	zdo_nwk_mngr()->scanDuration = scanDuration;
	zdo_nwk_mngr()->rejoinCnt = 0;
	zdo_nwk_mngr()->backoffIter = 0;
	zdo_nwk_mngr()->backoffTime = 0;

	ev_timer_event_t *evt =
		ev_timer_taskPost(zdo_nwkRejoinBackOffCb, NULL, drv_u32Rand() % 1000U);
	zdo_nwk_mngr()->backoffEvt = evt;
	return ZDO_SUCCESS;
}
zdo_status_t zdo_nwkDirectJoinStart(u32 scanChannels, u8 scanDuration)
{
	zb_buf_t *buf;
	nlme_join_req_t *req;

	if (zdo_nwk_mngr()->state != ZDO_NWK_MGR_STATE_IDLE) {
		return ZDO_INVALID_REQUEST;
	}

	if (ZB_EXTPANID_IS_ZERO(aps_ib.aps_use_ext_panid) &&
	    ZB_EXTPANID_IS_ZERO(g_zbInfo.nwkNib.extPANId)) {
		return ZDO_INVALID_REQUEST;
	}

	buf = zb_buf_allocate();
	if (buf == NULL) {
		return ZDO_INSUFFICIENT_SPACE;
	}

	g_zbNwkCtx.joined = 0;
	zdo_nwk_mngr()->state = ZDO_NWK_MGR_STATE_DIRECT_JOIN;
	zdo_nwk_mngr()->scanChannels = scanChannels;
	zdo_nwk_mngr()->scanDuration = scanDuration;

	req = (nlme_join_req_t *)buf;
	req->rejoinNwk = NLME_REJOIN_METHOD_DIRECT;
	req->scanChannels = scanChannels;
	req->scanDuration = scanDuration;
	*(u8 *)&req->capabilityInfo = af_nodeMacCapabilityGet();
	req->securityEnabled = FALSE;

	if (ZB_EXTPANID_IS_ZERO(aps_ib.aps_use_ext_panid)) {
		ZB_EXTPANID_COPY(req->extPANId, g_zbInfo.nwkNib.extPANId);
	} else {
		ZB_EXTPANID_COPY(req->extPANId, aps_ib.aps_use_ext_panid);
	}

	tl_zbPrimitivePost(TL_Q_HIGH2NWK, NWK_NLME_JOIN_REQ, buf);
	return ZDO_SUCCESS;
}

#if defined(ZB_ROUTER_ROLE)
typedef struct _attribute_packed_ {
	addrExt_t srcAddr;
	addrExt_t devAddr;
	u16 devShortAddr;
	u8 useParent;
	u8 rejoinNwk;
	bool secureRejoin;
} zdo_child_auth_req_t;

STATIC_ASSERT(sizeof(zdo_child_auth_req_t) == 21);
STATIC_ASSERT(sizeof(nlme_directJoin_req_t) == 11);
zdo_status_t zdo_nwkDirectJoinAccept(nlme_directJoin_req_t *pReq)
{
	zb_buf_t *buf = zb_buf_allocate();

	if (buf == NULL) {
		return ZDO_INSUFFICIENT_SPACE;
	}

	memcpy(buf, pReq, sizeof(*pReq));
	tl_zbPrimitivePost(TL_Q_HIGH2NWK, NWK_NLME_DIRECT_JOIN_REQ, buf);
	return ZDO_SUCCESS;
}

void zdo_nlme_direct_join_confirm(void *arg)
{
	zb_buf_free((zb_buf_t *)arg);
}

void zdo_nlme_join_indication(void *arg)
{
	const nlme_join_ind_t *joinInd = (const nlme_join_ind_t *)arg;
	zdo_child_auth_req_t req;
	u16 addrMapIdx;

	memset(&req, 0, sizeof(req));
	ZB_IEEE_ADDR_COPY(req.devAddr, joinInd->extAddr);
	req.devShortAddr = joinInd->nwkAddr;
	if (tl_zbNwkAddrMapAdd(req.devShortAddr, req.devAddr, &addrMapIdx) != NWK_STATUS_SUCCESS) {
		zb_buf_free((zb_buf_t *)arg);
		return;
	}
#if defined(ZB_ROUTER_ROLE) && !defined(ZB_COORDINATOR_ROLE)
	req.useParent = 1U;
#endif
	req.rejoinNwk = joinInd->rejoinNwk;
	req.secureRejoin = joinInd->secureRejoin;

	memcpy(arg, &req, sizeof(req));
	ss_zdoChildAuthStart(arg);
}
#endif

void zdo_nlme_status_indication(void *arg)
{
	const zdo_nwk_status_ind_t *statusInd = (const zdo_nwk_status_ind_t *)arg;
	u16 shortAddr = statusInd->shortAddr;
	u8 status = statusInd->status;

#if defined(ZB_ROUTER_ROLE) || defined(ZB_COORDINATOR_ROLE)
	bool notifyApp = (status == NWK_COMMAND_STATUS_BAD_FRAME_COUNTER ||
			  status == NWK_COMMAND_STATUS_BAD_KEY_SEQUENCE_NUMBER);

#if defined(ZB_COORDINATOR_ROLE)
	if (status == NWK_COMMAND_STATUS_SOURCE_ROUTE_FAILURE ||
	    status == NWK_COMMAND_STATUS_MANY_TO_ONE_ROUTE_FAILURE) {
		if (g_zbNIB.isConcentrator != 0U) {
			if (status == NWK_COMMAND_STATUS_SOURCE_ROUTE_FAILURE) {
				nwkRouteRecTabEntryDstDel(shortAddr);
			} else {
				tl_zbTaskPost(zdo_manyToOneRouteDisc, arg);
				return;
			}
		} else {
			zb_buf_free((zb_buf_t *)arg);
			return;
		}
	}
#endif
	if (status == NWK_COMMAND_STATUS_ADDRESS_CONFLICT) {
		tl_zbNwkStatusAddrConflictInd(arg);
		zb_buf_free((zb_buf_t *)arg);
		return;
	}

	if (status == NWK_COMMAND_STATUS_PAN_IDENTIFIER_UPDATE) {
		zb_buf_free((zb_buf_t *)arg);
		zb_info_save(NULL);
		return;
	} else if (!notifyApp) {
		zb_buf_free((zb_buf_t *)arg);
		return;
	}

	if (zdoAppIndCbLst == NULL) {
		zb_buf_free((zb_buf_t *)arg);
		return;
	}

	if (zdoAppIndCbLst->nwkStatusIndCb != NULL) {
		zdo_nwk_status_ind_t ind;

		ind.shortAddr = shortAddr;
		ind.status = status;
		zdoAppIndCbLst->nwkStatusIndCb(&ind);
	}

	return;
#else
	if (status == NWK_COMMAND_STATUS_BAD_FRAME_COUNTER ||
	    status == NWK_COMMAND_STATUS_BAD_KEY_SEQUENCE_NUMBER) {
		if (zdoAppIndCbLst != NULL && zdoAppIndCbLst->nwkStatusIndCb != NULL) {
			zdo_nwk_status_ind_t ind;

			ind.shortAddr = shortAddr;
			ind.status = status;
			zdoAppIndCbLst->nwkStatusIndCb(&ind);
		}

		zb_buf_free((zb_buf_t *)arg);
		return;
	}

	if (status != NWK_COMMAND_STATUS_PARENT_LINK_FAILURE) {
		zb_buf_free((zb_buf_t *)arg);
		return;
	}

	zb_buf_free((zb_buf_t *)arg);
	if (zdo_af_get_link_retry_threshold() == 0U) {
		zdo_nwk_mngr()->linkRetryCnt = 0;
		return;
	}

	zdo_nwk_mngr()->linkRetryCnt++;

	if (zdo_nwk_mngr()->linkRetryCnt <= zdo_af_get_link_retry_threshold() ||
	    g_bdbCtx.state != BDB_STATE_IDLE) {
		return;
	}

	if (zdo_nwkDirectJoinStart(1UL << g_zbInfo.macPib.phyChannelCur,
				   zdo_cfg_attributes.config_nwk_scan_duration) == ZDO_SUCCESS) {
#if !defined(ZB_ROUTER_ROLE)
		zdo_set_pollRate(0);
#endif
		zdo_nwk_mngr()->linkRetryCnt = 0;
	}
#endif
}

void zdo_nlme_join_confirm(void *arg)
{
	u8 state = zdo_nwk_mngr()->state;
	const nlme_join_cnf_t *joinCnf = (const nlme_join_cnf_t *)arg;
	u8 status = joinCnf->status;

	if (state != ZDO_NWK_MGR_STATE_ASSOC_JOIN && state != ZDO_NWK_MGR_STATE_REJOIN &&
	    state != ZDO_NWK_MGR_STATE_DIRECT_JOIN) {
		zb_buf_free((zb_buf_t *)arg);
		return;
	}

	if (state == ZDO_NWK_MGR_STATE_DIRECT_JOIN) {
		zdo_startDeviceCnf(arg,
				   (status == NWK_STATUS_SUCCESS) ? ZDO_SUCCESS : ZDO_NETWORK_LOST);
		return;
	}

	if (state == ZDO_NWK_MGR_STATE_REJOIN) {
		((zb_buf_t *)arg)->hdr.rejoinStartAgain = 1;
		if (status != NWK_STATUS_SUCCESS) {
			if (zdo_nwk_mngr()->backoffEvt != NULL) {
				zdo_nwk_mngr()->state = ZDO_NWK_MGR_STATE_IDLE;
				zb_buf_free((zb_buf_t *)arg);
				return;
			}

			zdo_startDeviceCnf(arg, status);
			return;
		}

		zdo_nwkRejoinWithBackOffStop();
	} else if (status != NWK_STATUS_SUCCESS) {
		zdo_startDeviceCnf(arg, status);
		return;
	}

#if !defined(ZB_ROUTER_ROLE)
	zdo_set_pollRate(500U);
	if (aps_ib.aps_authenticated || ss_ib_security_level_get() == 0U) {
		zdo_startDeviceCnf(arg, ZDO_SUCCESS);
		return;
	}

	zdo_nwkAuthTimeoutStart(arg);
#else
	if (!aps_ib.aps_authenticated && ss_ib_security_level_get() != 0U) {
		zdo_nwkAuthTimeoutStart(arg);
		return;
	}

	if (g_zbNIB.capabilityInfo.devType != 0U) {
		zdo_nlme_start_router_req(arg);
		return;
	}

	zdo_startDeviceCnf(arg, ZDO_SUCCESS);
#endif
}
zdo_status_t zdo_nwkDiscoveryStart(nlme_nwkDisc_req_t *pReq, nwkDiscoveryUserCb_t cb)
{
	if (zdo_af_get_scan_attempts() == 0U || zdo_af_get_nwk_time_btwn_scans() == 0U ||
	    cb == NULL) {
		return ZDO_NOT_PERMITTED;
	}

	if (zdo_nwk_mngr()->state != ZDO_NWK_MGR_STATE_IDLE || zdo_nwk_mngr()->discEvt != NULL) {
		return ZDO_INVALID_REQUEST;
	}

	tl_zbAdditionNeighborReset();
	zdo_nwk_mngr()->scanChannels = pReq->scanChannels;
	zdo_nwk_mngr()->scanDuration = pReq->scanDuration;
	zdo_nwk_mngr()->nwkDiscAttempt = 0;
	zdo_nwk_mngr()->nwkDiscCb = cb;

	return zdo_nlmeNwkDiscReq(NULL);
}

void zdo_nwkDiscoveryStop(void)
{
	if (zdo_nwk_mngr()->discEvt != NULL) {
		ev_timer_taskCancel(&zdo_nwk_mngr()->discEvt);
	}

	zdo_nwk_mngr()->nwkDiscAttempt = 0;
	zdo_nwk_mngr()->nwkDiscCb = NULL;
}

void zdo_nlme_network_discovery_confirm_cb(void *arg)
{
	nwkDiscoveryUserCb_t cb;
	u8 attempt;

	zb_buf_free((zb_buf_t *)arg);

	if (zdo_nwk_mngr()->state != ZDO_NWK_MGR_STATE_NWK_DISC) {
		return;
	}

	zdo_nwk_mngr()->state = ZDO_NWK_MGR_STATE_IDLE;
	cb = zdo_nwk_mngr()->nwkDiscCb;
	if (cb == NULL) {
		zdo_nwk_mngr()->nwkDiscAttempt = 0;
		return;
	}

	attempt = (u8)(zdo_nwk_mngr()->nwkDiscAttempt + 1U);
	zdo_nwk_mngr()->nwkDiscAttempt = attempt;
	if (attempt < zdo_af_get_scan_attempts()) {
		zdo_nwk_mngr()->discEvt = ev_timer_taskPost(zdo_nwkDiscReqTimerCb, NULL,
							    zdo_af_get_nwk_time_btwn_scans());
		return;
	}

	zdo_nwk_mngr()->nwkDiscAttempt = 0;
	cb();
}

bool zdo_ifZdoNwkManagerIdle(void)
{
	return zdo_nwk_mngr()->state == ZDO_NWK_MGR_STATE_IDLE;
}
