/*
 * Copyright 2026 libzigbee
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "zb_common.h"
#include "zb_nwk_core.h"
#include "zb_nwk_addr_map.h"
#include "nwk_brc.h"
#include "nwk_data.h"
#include "nwk_discovery.h"
#include "nwk_formation.h"
#include "nwk_join.h"
#include "zb_nwk_neighbor.h"
#include "nwk_nlme.h"
#include "nwk_panid_conflict.h"
#include "nwk_pend.h"
#include "nwk_route_disc.h"
#include "nwk_routing.h"
#include "ev_timer.h"
#include "ss_security_flags.h"

nwk_ctx_t g_zbNwkCtx;

#if defined(ZB_ROUTER_ROLE)
uint8_t linkStExpiry = 0;
uint8_t T_DBG_linkStatus = 0;
ev_timer_event_t *linkStTimer = NULL;

#endif

/*
 * The vendor dispatches NWK tasks through two {primitive, handler} tables
 * rather than a switch (_router/nwk.s: the loops at "28:" and "5e:" index them
 * with a stride of 8 and compare field 0 against zb_buf_t.hdr.id).  Entry order
 * is the vendor's.
 */
typedef struct {
	uint32_t primitive;
	zb_callback_t handler;
} nwk_event_entry_t;

const nwk_event_entry_t g_zbNwkEventFromMacTbl[] = {
#if defined(ZB_ROUTER_ROLE)
	{MAC_MLME_ASSOCIATE_IND, zb_MacMlmeAssociateIndicationHandler},
	{MAC_MLME_COMM_STATUS_IND, zb_MacMlmeCommStatusIndicationHandler},
#else
	{MAC_MLME_POLL_CNF, zb_MacMlmePollConfirmHandler},
#endif
	{MAC_MCPS_DATA_IND, zb_MacMcpsDataIndicationHandler},
	{MAC_MCPS_DATA_CNF, zb_MacMcpsDataConfirmHandler},
	{MAC_MLME_START_CNF, zb_MacMlmeStartConfirmHandler},
	{MAC_MLME_BEACON_NOTIFY_IND, zb_MacMlmeBeaconNotifyIndicationHandler},
	{MAC_MLME_ORPHAN_IND, zb_MacMlmeOrphanIndicationHandler},
	{MAC_MLME_SCAN_CNF, zb_MacMlmeScanConfirmHandler},
	{MAC_MLME_ASSOCIATE_CNF, zb_MacMlmeAssociateConfirmHandler},
	{MAC_MLME_RESET_CNF, zb_MacMlmeResetConfirmHandler},
	{MAC_MLME_SYNC_LOSS_IND, zb_MacMlmeSyncLossIndicationHandler},
	{MAC_MLME_POLL_IND, zb_MacMlmePollIndicationHandler},
};

const nwk_event_entry_t g_zbNwkEventFromHighTbl[] = {
#if defined(ZB_ROUTER_ROLE)
	{NWK_NLME_NWK_FORMATION_REQ, zb_NwkNlmeNetworkFormationRequestHandler},
	{NWK_NLME_ROUTE_DISCOVERY_REQ, zb_NwkNlmeRouteDiscRequestHandler},
	{NWK_NLME_PERMIT_JOINING_REQ, zb_NwkNlmePermitJoiningRequestHandler},
	{NWK_NLME_START_ROUTER_REQ, zb_NwkNlmeStartRouterRequestHandler},
	{NWK_NLME_DIRECT_JOIN_REQ, zb_NwkNlmeDirectJoinRequestHandler},
#else
	{NWK_NLME_SYNC_REQ, zb_NwkNlmeSyncRequestHandler},
#endif
	{NWK_NLDE_DATA_REQ, zb_NwkNldeDataRequestHandler},
	{NWK_NLME_NWK_DISCOVERY_REQ, zb_NwkNlmeNwkDiscRequestHandler},
	{NWK_NLME_JOIN_REQ, zb_NwkNlmeJoinRequestHandler},
	{NWK_NLME_ED_SCAN_REQ, zb_NwkNlmeEDScanRequestHandler},
	{NWK_NLME_LEAVE_REQ, zb_NwkNlmeLeaveRequestHandler},
	{NWK_NLME_RESET_REQ, zb_NwkNlmeResetRequestHandler},
};

static inline void nwk_task_dispatch(const nwk_event_entry_t *tbl, uint8_t count, void *data)
{
	uint8_t id = ((zb_buf_t *)data)->hdr.id;

	for (uint8_t i = 0; i < count; i++) {
		if ((tbl[i].primitive == id) && (tbl[i].handler != NULL)) {
			tbl[i].handler(data);
			return;
		}
	}
}

void zb_MacMlmeScanConfirmHandler(void *arg)
{
	switch (g_zbNwkCtx.state) {
#if defined(ZB_ROUTER_ROLE)
	case NLME_STATE_FORMATION:
		nwk_formationScanCnfHandler(arg);
		break;
#endif
	case NLME_STATE_DISC:
		nwk_discoveryScanCnfHandler(arg);
		break;
	case NLME_STATE_REJOIN:
		nwk_rejoinScanCnfHandler(arg);
		break;
	case NLME_STATE_DIRECT_JOIN:
		nwk_directJoinScanCnfHandler(arg);
		break;
	case NLME_STATE_ED_SCAN:
		nwk_edScanCnfHandler(arg);
		break;
	default:
		zb_buf_free((zb_buf_t *)arg);
		break;
	}
}

void zb_MacMlmeStartConfirmHandler(void *arg)
{
#if defined(ZB_ROUTER_ROLE)
	switch (g_zbNwkCtx.state) {
	case NLME_STATE_FORMATION:
		nwk_formationStartCnfHandler(arg);
		break;
	case NLME_STATE_ROUTER_START:
		nwk_startRouterCnfHandler(arg);
		break;
	case NLME_STATE_PANID_CONFLICT:
		nwk_panIdConflictCnfHandler(arg);
		break;
	default:
		zb_buf_free((zb_buf_t *)arg);
		break;
	}
#else
	zb_buf_free((zb_buf_t *)arg);
#endif
}

/*
 * Reconstructed from _router/nwk.s and _ed/nwk.s.  The reconstruction used to
 * rebuild the NIB field by field from macros; the vendor just copies
 * nwkNibDefault over it and clears the extended PAN ID.
 */
void zb_NwkNibInit(uint8_t coldReset)
{
	if (coldReset) {
		memcpy(&g_zbNIB, &nwkNibDefault, sizeof(g_zbNIB));
		ZB_EXTPANID_ZERO(g_zbNIB.extPANId);
		g_zbNwkCtx.is_factory_new = 1;
	} else {
		g_zbNwkCtx.is_factory_new = 0;
	}

	/* Stack profile is a compile-time network capability, not retained state. */
	g_zbNIB.stackProfile = nwkNibDefault.stackProfile;

	if (!coldReset && af_nodeDevTypeGet() == DEVICE_TYPE_COORDINATOR) {
		g_zbNwkCtx.is_tc = 1;
	}
}

void zb_NwkInit(uint8_t coldReset)
{
	/* g_zbNwkCtx is cleared *before* zb_NwkNibInit runs - the reconstruction
	 * cleared it afterwards and so threw away the is_factory_new/is_tc flags
	 * that call had just set. */
	memset(&g_zbNwkCtx, 0, sizeof(g_zbNwkCtx));
	g_zbNwkCtx.discoverRoute = 1;

	zb_NwkAddrMapInit();
	zb_NwkNibInit(coldReset);
	zb_NeighborTableInit();
#if defined(ZB_ROUTER_ROLE)
	nwkTxDataPendTabInit();
	nwkBrcTransTabInit();
	nwkRouteDiscTabInit();
	nwkRoutingTabInit();
#endif

	g_zbNIB.seqNum = (uint8_t)drv_u32Rand();
#if defined(ZB_ROUTER_ROLE)
	g_zbNIB.passiveAckTimeout = getPassiveAckTimeout();
#endif
}

uint8_t nwkHdrParse(nwk_hdr_t *pNwkHdr, uint8_t *msdu)
{
	uint8_t *ptr;

	/* The two on-wire frame-control bytes are copied as they are, including
	 * the reserved bits. */
	memcpy((uint8_t *)pNwkHdr + 4, msdu, 2);

	if (pNwkHdr->frameControl.frameType == FRAME_TYPE_INTERPAN) {
		return 2;
	}

	pNwkHdr->dstAddr = (uint16_t)msdu[2] | ((uint16_t)msdu[3] << 8);
	pNwkHdr->srcAddr = (uint16_t)msdu[4] | ((uint16_t)msdu[5] << 8);
	pNwkHdr->radius = msdu[6];
	pNwkHdr->seqNum = msdu[7];
	ptr = msdu + 8;

	if (pNwkHdr->frameControl.dstIEEEAddr) {
		ZB_IEEE_ADDR_COPY(pNwkHdr->dstIeeeAddr, ptr);
		ptr += EXT_ADDR_LEN;
	}
	if (pNwkHdr->frameControl.srcIEEEAddr) {
		ZB_IEEE_ADDR_COPY(pNwkHdr->srcIeeeAddr, ptr);
		ptr += EXT_ADDR_LEN;
	}

	if (pNwkHdr->frameControl.multicastFlg) {
		memcpy(&pNwkHdr->mcastControl, ptr, 1);
		ptr++;
	}

	if (pNwkHdr->frameControl.srcRoute) {
		pNwkHdr->srcRouteSubframe.relayCnt = ptr[0];
		pNwkHdr->srcRouteSubframe.relayIdx = ptr[1];
		ptr += 2;
		pNwkHdr->srcRouteSubframe.relayList = ptr;
		ptr += pNwkHdr->srcRouteSubframe.relayCnt * 2U;
	}

	if (pNwkHdr->frameControl.security) {
		ptr += 14;
	}

	return (uint8_t)(ptr - msdu);
}

uint8_t getNwkHdrSize(nwk_hdr_t *pNwkHdr)
{
	uint8_t size = 8;
	if (pNwkHdr->frameControl.dstIEEEAddr) {
		size += EXT_ADDR_LEN;
	}
	if (pNwkHdr->frameControl.srcIEEEAddr) {
		size += EXT_ADDR_LEN;
	}
	if (pNwkHdr->frameControl.multicastFlg) {
		size += 1;
	}
	/* The source-route subframe and the auxiliary security header are part
	 * of the header too. */
	if (pNwkHdr->frameControl.srcRoute) {
		size += (uint8_t)((pNwkHdr->srcRouteSubframe.relayCnt * 2U) + 2U);
	}
	if (pNwkHdr->frameControl.security) {
		size += 14;
	}
	return size;
}

uint8_t *nwkHdrBuilder(uint8_t *buf, nwk_hdr_t *pNwkHdr)
{
	memcpy(buf, &pNwkHdr->frameControl, sizeof(pNwkHdr->frameControl));
	COPY_U16TOBUFFER(buf + 2, pNwkHdr->dstAddr);
	COPY_U16TOBUFFER(buf + 4, pNwkHdr->srcAddr);
	buf[6] = pNwkHdr->radius;
	buf[7] = pNwkHdr->seqNum;

	uint8_t *ptr = buf + 8;

	if (pNwkHdr->frameControl.dstIEEEAddr) {
		ZB_IEEE_ADDR_COPY(ptr, pNwkHdr->dstIeeeAddr);
		ptr += EXT_ADDR_LEN;
	}
	if (pNwkHdr->frameControl.srcIEEEAddr) {
		ZB_IEEE_ADDR_COPY(ptr, pNwkHdr->srcIeeeAddr);
		ptr += EXT_ADDR_LEN;
	}
	if (pNwkHdr->frameControl.multicastFlg) {
		*ptr++ = *(uint8_t *)&pNwkHdr->mcastControl;
	}
	if (pNwkHdr->frameControl.srcRoute) {
		*ptr++ = pNwkHdr->srcRouteSubframe.relayCnt;
		*ptr++ = pNwkHdr->srcRouteSubframe.relayIdx;
		memcpy(ptr, pNwkHdr->srcRouteSubframe.relayList,
		       (uint16_t)pNwkHdr->srcRouteSubframe.relayCnt * sizeof(uint16_t));
		ptr += (uint16_t)pNwkHdr->srcRouteSubframe.relayCnt * sizeof(uint16_t);
	}

	return ptr;
}

#if defined(ZB_ROUTER_ROLE)
uint32_t getPassiveAckTimeout(void)
{
	/* The NIB value is only the fallback; when a MAC backoff exponent is
	 * configured the timeout is derived from it ("a:".."1a:"). */
	if (g_zbMacPib.maxBe != 0U) {
		return (uint32_t)g_zbMacPib.maxCsmaBackoffs * ((320UL << g_zbMacPib.maxBe) - 320UL);
	}

	return g_zbNIB.passiveAckTimeout;
}

void zb_NwkLinkStatusStop(void)
{
	linkStExpiry = 0;
	if (linkStTimer != NULL) {
		ev_timer_taskCancel(&linkStTimer);
	}
}

void zb_NwkNeighborTabAging(void)
{
	uint8_t neighborNum = zb_NeighborTableNumGet();

	for (uint8_t i = 0; i < neighborNum; i++) {
		zb_normal_neighbor_entry_t *entry = zb_NeighborEntryGetFromIdx(i);

		if (entry == NULL || entry->deviceType == NWK_DEVICE_TYPE_ED ||
		    g_zbNIB.addrAlloc == 0U) {
			continue;
		}

		entry->age++;

		if (entry->age >= g_zbNIB.routerAgeLimit) {
			entry->outgoingCost = 0;
			entry->lqi = 0;
			entry->age = 0;
			entry->transFailure = 0;
			g_sysDiags.neighborStale++;
		}
	}
}

/* The vendor's builder includes coordinators and routers, while excluding
 * end-device entries; the router filter used to size the temporary buffer is
 * therefore broader than this device-type test. */
uint8_t nwk_linkStEntryBuild(linkStatus_entry_t *list, uint8_t maxEntries)
{
	uint8_t count = 0;

	for (uint8_t i = 0; i < zb_NeighborTableNumGet(); i++) {
		zb_normal_neighbor_entry_t *entry = zb_NeighborEntryGetFromIdx(i);

		if ((entry == NULL) || (entry->lqi == 0U) ||
		    (entry->deviceType == NWK_DEVICE_TYPE_ED)) {
			continue;
		}

		list[count].neighborNwkAddr = zb_shortAddrByIdx(entry->addrmapIdx);
		list[count].linkStatus.incomingCost = rf_lqi2cost(entry->lqi) & 0x07U;
		list[count].linkStatus.reservedL = 0;
		list[count].linkStatus.outingCost = entry->outgoingCost & 0x07U;
		list[count].linkStatus.reservedH = 0;

		count++;
		if (count >= maxEntries) {
			break;
		}
	}

	for (uint8_t j = 0; (uint8_t)(j + 1) < count; j++) {
		uint8_t min = j;

		for (uint8_t k = j + 1; k < count; k++) {
			if (list[k].neighborNwkAddr < list[min].neighborNwkAddr) {
				min = k;
			}
		}

		if (min != j) {
			linkStatus_entry_t tmp = list[j];

			list[j] = list[min];
			list[min] = tmp;
		}
	}

	return count;
}

void nwkLinkStatusCmdSend(void *arg, nwk_hdr_t *pNwkHdr, nwkCmd_t *cmd, uint8_t handle)
{
	zb_buf_t *buf = (zb_buf_t *)arg;
	uint8_t entryCnt = cmd->linkSt.options.entryCnt;
	uint8_t payloadLen = (uint8_t)(entryCnt * sizeof(linkStatus_entry_t) + 2U);
	uint8_t *payload;

	ZB_BUF_INITIAL_ALLOC(buf, payloadLen, payload, uint8_t *);

	payload[0] = cmd->cmdId;
	memcpy(&payload[1], &cmd->linkSt.options, 1);

	if (entryCnt != 0U) {
		memcpy(&payload[2], cmd->linkSt.linkStatusList,
		       entryCnt * sizeof(linkStatus_entry_t));
	}

	buf->hdr.handle = handle;

	nwk_fwdPacket(buf, pNwkHdr, payload, payloadLen);
}

void zb_NwkSendLinkStatus(void)
{
	linkStatus_entry_t *entries = NULL;
	uint8_t routerCnt = zb_NeighborTableRouterValidNumGet();
	uint8_t entryCnt = 0;
	uint8_t frameCnt = 1;

	if (routerCnt != 0U) {
		uint16_t freeSize = ev_buf_getFreeMaxSize();
		uint16_t allocSize = (uint16_t)routerCnt * sizeof(linkStatus_entry_t);

		if (allocSize > freeSize) {
			routerCnt = (uint8_t)(freeSize / sizeof(linkStatus_entry_t));
			allocSize = (uint16_t)routerCnt * sizeof(linkStatus_entry_t);
		}

		entries = (linkStatus_entry_t *)ev_buf_allocate(allocSize);
		if (entries == NULL) {
			ZB_EXCEPTION_POST(SYS_EXCEPTTION_ZB_NWK_LINK_STA_MEM_ALLOC_FAIL);
			return;
		}

		entryCnt = nwk_linkStEntryBuild(entries, routerCnt);
		if (entryCnt != 0U) {
			frameCnt = (uint8_t)(entryCnt / NWK_LINK_STATUS_ENTRY_MAX_PER_FRAME);
			if ((entryCnt % NWK_LINK_STATUS_ENTRY_MAX_PER_FRAME) != 0U) {
				frameCnt++;
			}
		}
	}

	{
		linkStatus_entry_t *cursor = entries;
		uint8_t remaining = entryCnt;

		for (uint8_t i = 0; i < frameCnt; i++) {
			zb_buf_t *buf = zb_buf_allocate();
			nwk_hdr_t hdr;
			nwkCmd_t cmd;
			uint8_t frameEntryCnt;

			if (buf == NULL) {
				break;
			}

			memset(&hdr, 0, sizeof(hdr));

			hdr.frameControl.frameType = FRAME_TYPE_COMMAND;
			hdr.frameControl.protocolVer = ZB_PROTOCOL_VERSION;
			hdr.frameControl.srcIEEEAddr = 1;
			hdr.frameControl.dstIEEEAddr = 0;
			hdr.frameControl.security =
				(ss_ib_secure_all_fresh() && ss_ib_security_level_get())
					? (ss_keyPreconfigured() ? 1 : 0)
					: 0;

			ZB_IEEE_ADDR_COPY(hdr.srcIeeeAddr, g_zbInfo.macPib.extAddress);

			hdr.dstAddr = NWK_BROADCAST_ROUTER_COORDINATOR;
			hdr.srcAddr = g_zbNIB.nwkAddr;
			hdr.radius = 1;
			hdr.seqNum = g_zbNIB.seqNum++;
			hdr.frameHdrLen = getNwkHdrSize(&hdr);

			frameEntryCnt = (remaining > NWK_LINK_STATUS_ENTRY_MAX_PER_FRAME)
						? NWK_LINK_STATUS_ENTRY_MAX_PER_FRAME
						: remaining;

			memset(&cmd, 0, sizeof(cmd));
			cmd.cmdId = NWK_CMD_LINK_STATUS;
			cmd.linkSt.options.entryCnt = frameEntryCnt & 0x1fU;
			cmd.linkSt.options.firstFrame = (i == 0U);
			cmd.linkSt.options.lastFrame = (i == (uint8_t)(frameCnt - 1U));
			cmd.linkSt.linkStatusList = cursor;

			if (cursor != NULL) {
				cursor += frameEntryCnt;
			}

			nwkLinkStatusCmdSend(buf, &hdr, &cmd, NWK_INTERNAL_LINK_STATUS_CMD_HANDLE);

			remaining = (uint8_t)(remaining - frameEntryCnt);
		}
	}

	if (entries != NULL) {
		ev_buf_free((uint8_t *)entries);
	}
}

void zb_NwkLinkStatusStart(void)
{
	zb_NwkSendLinkStatus();
	if (g_zbNIB.linkStatusPeriod != 0U) {
		linkStExpiry = g_zbNIB.linkStatusPeriod;
	}
}

int zb_NwkLinkStatusTimerEvtCb(void *arg)
{
	(void)arg;

	if (g_zbNwkCtx.joined != 0U) {
		T_DBG_linkStatus++;
		zb_NwkNeighborTabAging();
		zb_NwkLinkStatusStart();
	}

	linkStTimer = NULL;

	return -1;
}

int nwk_linkStPeriodic(void *arg)
{
	(void)arg;

	if (linkStExpiry == 0U) {
		return 0;
	}

	linkStExpiry--;
	if (linkStExpiry == 0U) {
		uint32_t delay = (drv_u32Rand() & 0x7fU) + 5U;
		linkStTimer = ev_timer_taskPost(zb_NwkLinkStatusTimerEvtCb, NULL, delay);
	}

	return 0;
}

uint8_t zb_nwkGetAverageLqi(uint8_t oldLqi, uint8_t newLqi)
{
	uint16_t avg = newLqi;

	/* A 15/16 exponential average, not a plain mean.  Both arguments are
	 * zero-extended, so an LQI above 127 stays positive. */
	if (oldLqi != 0U) {
		avg = (uint16_t)((oldLqi * 15 + newLqi) / 16);
	}

	return (uint8_t)avg;
}

void zb_NwkLinkStatusCmdHandler(void *arg, nwk_hdr_t *pNwkHdr, nwkCmd_t *cmd)
{
	zb_mscp_data_ind_t *ind = (zb_mscp_data_ind_t *)arg;
	linkStatus_entry_t *list = cmd->linkSt.linkStatusList;
	uint8_t entryCnt = cmd->linkSt.options.entryCnt;
	uint8_t payloadLen = (uint8_t)(ind->msduLength - pNwkHdr->frameHdrLen);
	zb_normal_neighbor_entry_t *neighbor;
	uint16_t srcAddr = pNwkHdr->srcAddr;
	uint16_t localAddr = g_zbNIB.nwkAddr;
	uint8_t newNeighbor = 0;

	if (payloadLen != (uint8_t)(entryCnt * sizeof(linkStatus_entry_t) + 2U)) {
		zb_buf_free((zb_buf_t *)arg);
		return;
	}

	neighbor = nwk_neTblGetByShortAddr(srcAddr);

	if (neighbor == NULL) {
		zb_normal_neighbor_entry_t candidate;
		uint16_t addrmapIdx;
		uint8_t deleteOld = 0;

		/* An end-device-initiator frame carries the authoritative source IEEE
		 * address when the MAC indication also reports the same short address. */
		if (pNwkHdr->frameControl.endDevInitiator != 0U &&
		    ind->srcAddr.addrMode == ADDR_MODE_SHORT &&
		    ind->srcAddr.addr.shortAddr == srcAddr) {
			(void)zb_NwkAddrMapAdd(srcAddr, pNwkHdr->srcIeeeAddr, &addrmapIdx);
		}

		if (zb_idxByShortAddr(&addrmapIdx, srcAddr) != RET_OK) {
			zb_buf_free((zb_buf_t *)arg);
			return;
		}

		memset(&candidate, 0, sizeof(candidate));
		candidate.addrmapIdx = addrmapIdx;
		candidate.deviceType =
			(srcAddr != 0U) ? NWK_DEVICE_TYPE_ROUTER : NWK_DEVICE_TYPE_COORDINATOR;
		candidate.relationship = NEIGHBOR_IS_SIBLING;
		candidate.lqi = ind->mpduLinkQuality;

		if (srcAddr == 0U && entryCnt != 0U) {
			for (uint8_t i = 0; i < entryCnt; i++) {
				if (list[i].neighborNwkAddr == localAddr) {
					deleteOld = 1;
					break;
				}
			}
		}

		neighbor = zb_NeighborTableUpdate(&candidate, deleteOld);
		if (neighbor == NULL) {
			zb_buf_free((zb_buf_t *)arg);
			return;
		}
		newNeighbor = 1;
	} else {
		neighbor->age = 0;
		neighbor->deviceType =
			(srcAddr != 0U) ? NWK_DEVICE_TYPE_ROUTER : NWK_DEVICE_TYPE_COORDINATOR;
	}

	/* A link-status record is relevant to this node only when it names our
	 * own network address.  The vendor stores the record's low three bits,
	 * i.e. incomingCost, in the neighbor outgoing-cost field. */
	for (uint8_t i = 0; i < entryCnt; i++) {
		if (list[i].neighborNwkAddr == localAddr) {
			neighbor->outgoingCost = list[i].linkStatus.incomingCost;
			zb_buf_free((zb_buf_t *)arg);
			return;
		}
	}

	if (newNeighbor != 0U && cmd->linkSt.options.firstFrame != 0U &&
	    cmd->linkSt.options.lastFrame != 0U) {
		neighbor->outgoingCost = 0U;
	}

	zb_buf_free((zb_buf_t *)arg);
}

void zb_NwkReportCmdHandler(void *arg, nwk_hdr_t *pNwkHdr, nwkCmd_t *cmd)
{
	/* Is this report addressed to this device? */
	if (pNwkHdr->dstAddr != g_zbNIB.nwkAddr) {
		if (pNwkHdr->radius == 0U) {
			zb_buf_free((zb_buf_t *)arg);
			return;
		}

		nwkReportCmdSend((zb_buf_t *)arg, pNwkHdr, cmd,
				 NWK_INTERNAL_NETWORK_REPORT_CMD_HANDLE);
		return;
	}

	nwkReportCmdHandler(arg, cmd);
}

void zb_NwkSendNwkStatusCmd(void *arg, nwk_hdr_t *pNwkHdr, nwkCmd_t *pNwkStatus, uint8_t handle)
{
	zb_buf_t *buf = (zb_buf_t *)arg;
	uint8_t *payload;
	uint16_t nextHop;

	/* The ordinary case: the status command is relayed along the route the
	 * incoming header already describes, and nwk_fwdPacket picks the next hop.
	 * Only a many-to-one route failure takes the repair path below, because
	 * that is the one status the concentrator's route can no longer carry. */
	if (pNwkStatus->nwkStatus.statusCode == NWK_COMMAND_STATUS_MANY_TO_ONE_ROUTE_FAILURE) {
		zb_normal_neighbor_entry_t *neighbor;

		/* Drop the routing entry that just failed, unless this device is the
		 * originator - a status we generated ourselves says nothing about the
		 * validity of our own routing table. */
		if (pNwkHdr->srcAddr != g_zbNIB.nwkAddr) {
			nwk_routingTabEntry_t *entry = nwkRoutingTabEntryDstFind(pNwkHdr->dstAddr);

			if (entry != NULL) {
				if ((entry->nextHopAddr != g_zbNwkCtx.manyToOneRepair.senderAddr) &&
				    (entry->nextHopAddr != pNwkHdr->srcAddr)) {
					goto forward;
				}

				nwkRoutingTabEntryClear(entry);
			}
		}

		neighbor = nwkValidNeighborToFwd(pNwkHdr->dstAddr);

		if ((neighbor != NULL) && (pNwkHdr->srcAddr != g_zbNIB.nwkAddr)) {
			nextHop = pNwkHdr->dstAddr;
		} else {
			/* No usable neighbor for the concentrator: try any router in the
			 * neighbor table, skipping the two addresses the repair context
			 * already knows are no good - the sender of the failing frame and
			 * the address the last repair attempt failed on. */
			for (;;) {
				neighbor = zb_NeighborTabSearchForRouter(neighbor);
				if (neighbor == NULL) {
					nextHop = pNwkHdr->dstAddr;
					break;
				}

				nextHop = zb_shortAddrByIdx(neighbor->addrmapIdx);

				if ((nextHop != g_zbNwkCtx.manyToOneRepair.senderAddr) &&
				    (nextHop != g_zbNwkCtx.manyToOneRepair.lastSendFailAddr)) {
					break;
				}
			}
		}

		ZB_BUF_INITIAL_ALLOC(buf, 1 + sizeof(nwkCmd_nwkStatus_t), payload, uint8_t *);
		payload[0] = pNwkStatus->cmdId;
		payload[1] = pNwkStatus->nwkStatus.statusCode;
		memcpy(payload + 2, &pNwkStatus->nwkStatus.dstAddr, 2);
		buf->hdr.handle = handle;

		nwk_tx(buf, pNwkHdr, nextHop, 0, payload, 1 + sizeof(nwkCmd_nwkStatus_t));
		return;
	}

forward:
	ZB_BUF_INITIAL_ALLOC(buf, 1 + sizeof(nwkCmd_nwkStatus_t), payload, uint8_t *);
	payload[0] = pNwkStatus->cmdId;
	payload[1] = pNwkStatus->nwkStatus.statusCode;
	memcpy(payload + 2, &pNwkStatus->nwkStatus.dstAddr, 2);
	buf->hdr.handle = handle;

	nwk_fwdPacket(buf, pNwkHdr, payload, 1 + sizeof(nwkCmd_nwkStatus_t));
}

/*
 * The application-facing network-status sender.  The vendor takes a single
 * pointer that is both the transmit buffer and - overlaid on its head - a
 * myNwkCmd_nwkStatus_t describing the frame to build, and it reads the
 * descriptor fields back after zb_bufInitalloc has carved the payload out of
 * the same buffer.  Reproduced as written: the payload is allocated at the
 * tail, so the descriptor at the head survives.
 */
void my_tl_zbNwkSendNwkStatusCmd(void *arg)
{
	zb_buf_t *buf = (zb_buf_t *)arg;
	myNwkCmd_nwkStatus_t *req = (myNwkCmd_nwkStatus_t *)arg;
	nwk_hdr_t hdr;
	uint8_t len = req->payloadLen + 2;
	uint8_t *payload;

	ZB_BUF_INITIAL_ALLOC(buf, len, payload, uint8_t *);

	payload[0] = req->cmdId;
	payload[1] = req->statusCode;
	memcpy(payload + 2, req->payload, req->payloadLen);

	memset(&hdr, 0, sizeof(hdr));

	hdr.frameControl.security = (ss_ib_secure_all_fresh() && ss_ib_security_level_get())
					    ? (ss_keyPreconfigured() ? 1 : 0)
					    : 0;
	hdr.frameControl.frameType = FRAME_TYPE_COMMAND;
	hdr.frameControl.protocolVer = ZB_PROTOCOL_VERSION;
	hdr.frameControl.srcIEEEAddr = req->srcIEEEAddrPresent & 1;
	hdr.frameControl.dstIEEEAddr = req->dstIEEEAddrPresent & 1;

	if (hdr.frameControl.srcIEEEAddr) {
		ZB_IEEE_ADDR_COPY(hdr.srcIeeeAddr, g_zbInfo.macPib.extAddress);
	}

	if (hdr.frameControl.dstIEEEAddr) {
		ZB_IEEE_ADDR_COPY(hdr.dstIeeeAddr, req->dstIeeeAddr);
	}

	hdr.dstAddr = req->dstAddr;
	/* The vendor takes the source from the MAC PIB short address, not from
	 * the NIB - the two agree once the device has joined. */
	hdr.srcAddr = g_zbInfo.macPib.shortAddress;
	hdr.radius = g_zbNIB.maxDepth * 2;
	hdr.seqNum = g_zbNIB.seqNum++;
	hdr.frameHdrLen = getNwkHdrSize(&hdr);

	buf->hdr.handle = req->handle;

	nwk_fwdPacket(buf, &hdr, payload, len);
}

#endif /* ZB_ROUTER_ROLE */

/* The network status names a device: true when it is this device, or one of
 * this device's end-device children. */
#if defined(ZB_ROUTER_ROLE) || defined(ZB_COORDINATOR_ROLE)
static inline bool nwk_statusAddrIsLocal(uint16_t shortAddr)
{
	zb_normal_neighbor_entry_t *neighbor = nwk_neTblGetByShortAddr(shortAddr);

	if (shortAddr == g_zbNIB.nwkAddr) {
		return true;
	}

	return (neighbor != NULL) && (neighbor->deviceType == NWK_DEVICE_TYPE_ED) &&
	       (neighbor->relationship == NEIGHBOR_IS_CHILD);
}

void zb_NwkStatusCmdHandler(void *arg, nwk_hdr_t *pNwkHdr, nwkCmd_t *cmd)
{
	zb_buf_t *buf = (zb_buf_t *)arg;
	zb_mscp_data_ind_t *macInd = (zb_mscp_data_ind_t *)arg;

	if ((cmd->nwkStatus.statusCode == NWK_COMMAND_STATUS_TREE_LINK_FAILURE) ||
	    (cmd->nwkStatus.statusCode == NWK_COMMAND_STATUS_NONE_TREE_LINK_FAILURE)) {
		/* The vendor deactivates the route once when either end of the
		 * failing link is local or is one of our children. */
		if (nwk_statusAddrIsLocal(cmd->nwkStatus.dstAddr) ||
		    nwk_statusAddrIsLocal(pNwkHdr->dstAddr)) {
			nwk_routingTabEntry_t *entry =
				nwkRoutingTabEntryDstActiveGet(cmd->nwkStatus.dstAddr);

			if (entry != NULL) {
				entry->status = NWK_ROUTE_STATE_DISCOVERY_INACTIVE;
			}
		}
	} else if (cmd->nwkStatus.statusCode == NWK_COMMAND_STATUS_MANY_TO_ONE_ROUTE_FAILURE) {
		/* Remember who handed us the failing many-to-one frame, so that the
		 * repair path in zb_NwkSendNwkStatusCmd does not hand it straight
		 * back.  Only for a frame that is still travelling onwards. */
		if ((pNwkHdr->dstAddr != g_zbNIB.nwkAddr) && (pNwkHdr->radius != 0U)) {
			g_zbNwkCtx.manyToOneRepair.senderAddr = macInd->srcAddr.addr.shortAddr;
		}
	}

	if (ZB_NWK_IS_ADDRESS_BROADCAST(pNwkHdr->dstAddr)) {
		if (pNwkHdr->radius != 0U) {
			if (nwkBrcTimerStart(buf, pNwkHdr, macInd->msdu + pNwkHdr->frameHdrLen,
					     (uint8_t)(macInd->msduLength - pNwkHdr->frameHdrLen)) !=
			    NWK_STATUS_SUCCESS) {
				/* Could not schedule the rebroadcast: undo the transaction
				 * record so a later copy of the same frame is not dropped as a
				 * duplicate. */
				nwk_brcTransRecordEntry_t *record =
					nwkBrcTransEntryFind(pNwkHdr->srcAddr, pNwkHdr->seqNum);

				if (record != NULL) {
					nwkBrcTransTabEntryClear(record);
				}

				zb_buf_free(buf);
				return;
			}
		}
	} else if (pNwkHdr->dstAddr != g_zbNIB.nwkAddr) {
		if (pNwkHdr->radius == 0U) {
			zb_buf_free(buf);
			return;
		}

		zb_NwkSendNwkStatusCmd(buf, pNwkHdr, cmd, NWK_INTERNAL_NETWORK_STATUS_CMD_HANDLE);
		return;
	}

	zb_NwkNlmeNwkStatusInd(buf, cmd->nwkStatus.dstAddr, cmd->nwkStatus.statusCode);
}
#else
/*
 * An end device has neither a routing table nor a broadcast transaction table,
 * so its variant only reports the status upwards.  _ed/nwk.s builds the whole
 * handler out of that one tail call: it reads nwkStatus.dstAddr and
 * nwkStatus.statusCode from the command and passes the buffer on untouched.
 */
void zb_NwkStatusCmdHandler(void *arg, nwk_hdr_t *pNwkHdr, nwkCmd_t *cmd)
{
	(void)pNwkHdr;

	zb_NwkNlmeNwkStatusInd(arg, cmd->nwkStatus.dstAddr, cmd->nwkStatus.statusCode);
}
#endif

void zb_NwkTaskProc(void)
{
	zb_task_t taskInfo;

	if ((zb_TaskQPop(ZB_Q_HIGH2NWK, &taskInfo) != NULL) && (taskInfo.data != NULL)) {
		nwk_task_dispatch(
			g_zbNwkEventFromHighTbl,
			(uint8_t)(sizeof(g_zbNwkEventFromHighTbl) / sizeof(g_zbNwkEventFromHighTbl[0])),
			taskInfo.data);
	}

	if ((zb_TaskQPop(ZB_Q_MAC2NWK, &taskInfo) != NULL) && (taskInfo.data != NULL)) {
		nwk_task_dispatch(
			g_zbNwkEventFromMacTbl,
			(uint8_t)(sizeof(g_zbNwkEventFromMacTbl) / sizeof(g_zbNwkEventFromMacTbl[0])),
			taskInfo.data);
	}
}

void zb_NwkNibSet(void *arg)
{
	nlme_set_req_t *req = (nlme_set_req_t *)arg;

	if (req == NULL || req->nibAttrVal == NULL) {
		return;
	}

	if (req->nibAttr == NIB_ATTRIBUTE_SEQUENCE_NUMBER) {
		g_zbNIB.seqNum = req->nibAttrVal[0];
	} else if (req->nibAttr == NIB_ATTRIBUTE_NETWORK_ADDRESS) {
		memcpy(&g_zbNIB.nwkAddr, req->nibAttrVal, sizeof(g_zbNIB.nwkAddr));
	} else if (req->nibAttr == NIB_ATTRIBUTE_PAN_ID) {
		memcpy(&g_zbNIB.panId, req->nibAttrVal, sizeof(g_zbNIB.panId));
	} else if (req->nibAttr == NIB_ATTRIBUTE_EXTENDED_PANID) {
		ZB_EXTPANID_COPY(g_zbNIB.extPANId, req->nibAttrVal);
	} else if (req->nibAttr == NIB_ATTRIBUTE_UPDATE_ID) {
		g_zbNIB.updateId = req->nibAttrVal[0];
	} else if (req->nibAttr == NIB_ATTRIBUTE_PARENT_INFORMATION) {
		g_zbNIB.parentInfo = req->nibAttrVal[0];
#if defined(ZB_ROUTER_ROLE)
	} else if (req->nibAttr == NIB_ATTRIBUTE_PASSIVE_ASK_TIMEOUT) {
		memcpy(&g_zbNIB.passiveAckTimeout, req->nibAttrVal,
		       sizeof(g_zbNIB.passiveAckTimeout));
	} else if (req->nibAttr == NIB_ATTRIBUTE_MAX_BROADCAST_RETRIES) {
		g_zbNIB.maxBroadcastRetries = req->nibAttrVal[0];
	} else if (req->nibAttr == NIB_ATTRIBUTE_BROADCAST_DELIVERY_TIME) {
		memcpy(&g_zbNIB.nwkBroadcastDeliveryTime, req->nibAttrVal,
		       sizeof(g_zbNIB.nwkBroadcastDeliveryTime));
	} else if (req->nibAttr == NIB_ATTRIBUTE_ROUTE_DISCOVERY_RETRIES_PERMITTED) {
		NWKC_RREQ_RETRIES = req->nibAttrVal[0];
	} else if (req->nibAttr == NIB_ATTRIBUTE_LINK_STATUS_PERIOD) {
		g_zbNIB.linkStatusPeriod = req->nibAttrVal[0];
	} else if (req->nibAttr == NIB_ATTRIBUTE_ROUTER_AGE_LIMIT) {
		g_zbNIB.routerAgeLimit = req->nibAttrVal[0];
#endif
	}
}

uint8_t is_device_factory_new(void)
{
	return g_zbNwkCtx.is_factory_new != 0U;
}
