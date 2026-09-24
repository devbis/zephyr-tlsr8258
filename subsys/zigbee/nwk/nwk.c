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
u8 linkStExpiry = 0;
u8 T_DBG_linkStatus = 0;
ev_timer_event_t *linkStTimer = NULL;

#endif

/*
 * The vendor dispatches NWK tasks through two {primitive, handler} tables
 * rather than a switch (_router/nwk.s: the loops at "28:" and "5e:" index them
 * with a stride of 8 and compare field 0 against zb_buf_t.hdr.id).  Entry order
 * is the vendor's.
 */
typedef struct {
	u32 primitive;
	tl_zb_callback_t handler;
} nwk_event_entry_t;

const nwk_event_entry_t g_zbNwkEventFromMacTbl[] = {
#if defined(ZB_ROUTER_ROLE)
	{MAC_MLME_ASSOCIATE_IND, tl_zbMacMlmeAssociateIndicationHandler},
	{MAC_MLME_COMM_STATUS_IND, tl_zbMacMlmeCommStatusIndicationHandler},
#else
	{MAC_MLME_POLL_CNF, tl_zbMacMlmePollConfirmHandler},
#endif
	{MAC_MCPS_DATA_IND, tl_zbMacMcpsDataIndicationHandler},
	{MAC_MCPS_DATA_CNF, tl_zbMacMcpsDataConfirmHandler},
	{MAC_MLME_START_CNF, tl_zbMacMlmeStartConfirmHandler},
	{MAC_MLME_BEACON_NOTIFY_IND, tl_zbMacMlmeBeaconNotifyIndicationHandler},
	{MAC_MLME_ORPHAN_IND, tl_zbMacMlmeOrphanIndicationHandler},
	{MAC_MLME_SCAN_CNF, tl_zbMacMlmeScanConfirmHandler},
	{MAC_MLME_ASSOCIATE_CNF, tl_zbMacMlmeAssociateConfirmHandler},
	{MAC_MLME_RESET_CNF, tl_zbMacMlmeResetConfirmHandler},
	{MAC_MLME_SYNC_LOSS_IND, tl_zbMacMlmeSyncLossIndicationHandler},
	{MAC_MLME_POLL_IND, tl_zbMacMlmePollIndicationHandler},
};

const nwk_event_entry_t g_zbNwkEventFromHighTbl[] = {
#if defined(ZB_ROUTER_ROLE)
	{NWK_NLME_NWK_FORMATION_REQ, tl_zbNwkNlmeNetworkFormationRequestHandler},
	{NWK_NLME_ROUTE_DISCOVERY_REQ, tl_zbNwkNlmeRouteDiscRequestHandler},
	{NWK_NLME_PERMIT_JOINING_REQ, tl_zbNwkNlmePermitJoiningRequestHandler},
	{NWK_NLME_START_ROUTER_REQ, tl_zbNwkNlmeStartRouterRequestHandler},
	{NWK_NLME_DIRECT_JOIN_REQ, tl_zbNwkNlmeDirectJoinRequestHandler},
#else
	{NWK_NLME_SYNC_REQ, tl_zbNwkNlmeSyncRequestHandler},
#endif
	{NWK_NLDE_DATA_REQ, tl_zbNwkNldeDataRequestHandler},
	{NWK_NLME_NWK_DISCOVERY_REQ, tl_zbNwkNlmeNwkDiscRequestHandler},
	{NWK_NLME_JOIN_REQ, tl_zbNwkNlmeJoinRequestHandler},
	{NWK_NLME_ED_SCAN_REQ, tl_zbNwkNlmeEDScanRequestHandler},
	{NWK_NLME_LEAVE_REQ, tl_zbNwkNlmeLeaveRequestHandler},
	{NWK_NLME_RESET_REQ, tl_zbNwkNlmeResetRequestHandler},
};

static inline void nwk_task_dispatch(const nwk_event_entry_t *tbl, u8 count, void *data)
{
	u8 id = ((zb_buf_t *)data)->hdr.id;

	for (u8 i = 0; i < count; i++) {
		if ((tbl[i].primitive == id) && (tbl[i].handler != NULL)) {
			tbl[i].handler(data);
			return;
		}
	}
}

void tl_zbMacMlmeScanConfirmHandler(void *arg)
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

void tl_zbMacMlmeStartConfirmHandler(void *arg)
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
void tl_zbNwkNibInit(u8 coldReset)
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

void tl_zbNwkInit(u8 coldReset)
{
	/* g_zbNwkCtx is cleared *before* tl_zbNwkNibInit runs - the reconstruction
	 * cleared it afterwards and so threw away the is_factory_new/is_tc flags
	 * that call had just set. */
	memset(&g_zbNwkCtx, 0, sizeof(g_zbNwkCtx));
	g_zbNwkCtx.discoverRoute = 1;

	tl_zbNwkAddrMapInit();
	tl_zbNwkNibInit(coldReset);
	tl_zbNeighborTableInit();
#if defined(ZB_ROUTER_ROLE)
	nwkTxDataPendTabInit();
	nwkBrcTransTabInit();
	nwkRouteDiscTabInit();
	nwkRoutingTabInit();
#endif

	g_zbNIB.seqNum = (u8)drv_u32Rand();
#if defined(ZB_ROUTER_ROLE)
	g_zbNIB.passiveAckTimeout = getPassiveAckTimeout();
#endif
}

u8 nwkHdrParse(nwk_hdr_t *pNwkHdr, u8 *msdu)
{
	u8 *ptr;

	/* The vendor keeps the on-wire frame-control bytes in the packed
	 * structure, including the reserved bits. */
	memcpy((u8 *)pNwkHdr + 4, msdu, 2);

	if (pNwkHdr->frameControl.frameType == FRAME_TYPE_INTERPAN) {
		return 2;
	}

	pNwkHdr->dstAddr = (u16)msdu[2] | ((u16)msdu[3] << 8);
	pNwkHdr->srcAddr = (u16)msdu[4] | ((u16)msdu[5] << 8);
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

	return (u8)(ptr - msdu);
}

u8 getNwkHdrSize(nwk_hdr_t *pNwkHdr)
{
	u8 size = 8;
	if (pNwkHdr->frameControl.dstIEEEAddr) {
		size += EXT_ADDR_LEN;
	}
	if (pNwkHdr->frameControl.srcIEEEAddr) {
		size += EXT_ADDR_LEN;
	}
	if (pNwkHdr->frameControl.multicastFlg) {
		size += 1;
	}
	/* "20: tshftls r1,r3,#29" and "30: tshftls r2,r3,#30" - the source-route
	 * subframe and the auxiliary security header were both missing. */
	if (pNwkHdr->frameControl.srcRoute) {
		size += (u8)((pNwkHdr->srcRouteSubframe.relayCnt * 2U) + 2U);
	}
	if (pNwkHdr->frameControl.security) {
		size += 14;
	}
	return size;
}

u8 *nwkHdrBuilder(u8 *buf, nwk_hdr_t *pNwkHdr)
{
	memcpy(buf, &pNwkHdr->frameControl, sizeof(pNwkHdr->frameControl));
	COPY_U16TOBUFFER(buf + 2, pNwkHdr->dstAddr);
	COPY_U16TOBUFFER(buf + 4, pNwkHdr->srcAddr);
	buf[6] = pNwkHdr->radius;
	buf[7] = pNwkHdr->seqNum;

	u8 *ptr = buf + 8;

	if (pNwkHdr->frameControl.dstIEEEAddr) {
		ZB_IEEE_ADDR_COPY(ptr, pNwkHdr->dstIeeeAddr);
		ptr += EXT_ADDR_LEN;
	}
	if (pNwkHdr->frameControl.srcIEEEAddr) {
		ZB_IEEE_ADDR_COPY(ptr, pNwkHdr->srcIeeeAddr);
		ptr += EXT_ADDR_LEN;
	}
	if (pNwkHdr->frameControl.multicastFlg) {
		*ptr++ = *(u8 *)&pNwkHdr->mcastControl;
	}
	if (pNwkHdr->frameControl.srcRoute) {
		*ptr++ = pNwkHdr->srcRouteSubframe.relayCnt;
		*ptr++ = pNwkHdr->srcRouteSubframe.relayIdx;
		memcpy(ptr, pNwkHdr->srcRouteSubframe.relayList,
		       (u16)pNwkHdr->srcRouteSubframe.relayCnt * sizeof(u16));
		ptr += (u16)pNwkHdr->srcRouteSubframe.relayCnt * sizeof(u16);
	}

	return ptr;
}

#if defined(ZB_ROUTER_ROLE)
u32 getPassiveAckTimeout(void)
{
	/* The NIB value is only the fallback; when a MAC backoff exponent is
	 * configured the timeout is derived from it ("a:".."1a:"). */
	if (g_zbMacPib.maxBe != 0U) {
		return (u32)g_zbMacPib.maxCsmaBackoffs * ((320UL << g_zbMacPib.maxBe) - 320UL);
	}

	return g_zbNIB.passiveAckTimeout;
}

void tl_zbNwkLinkStatusStop(void)
{
	linkStExpiry = 0;
	if (linkStTimer != NULL) {
		ev_timer_taskCancel(&linkStTimer);
	}
}

void tl_zbNwkNeighborTabAging(void)
{
	u8 neighborNum = tl_zbNeighborTableNumGet();

	for (u8 i = 0; i < neighborNum; i++) {
		tl_zb_normal_neighbor_entry_t *entry = tl_zbNeighborEntryGetFromIdx(i);

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
u8 nwk_linkStEntryBuild(linkStatus_entry_t *list, u8 maxEntries)
{
	u8 count = 0;

	for (u8 i = 0; i < tl_zbNeighborTableNumGet(); i++) {
		tl_zb_normal_neighbor_entry_t *entry = tl_zbNeighborEntryGetFromIdx(i);

		if ((entry == NULL) || (entry->lqi == 0U) ||
		    (entry->deviceType == NWK_DEVICE_TYPE_ED)) {
			continue;
		}

		list[count].neighborNwkAddr = tl_zbshortAddrByIdx(entry->addrmapIdx);
		list[count].linkStatus.incomingCost = rf_lqi2cost(entry->lqi) & 0x07U;
		list[count].linkStatus.reservedL = 0;
		list[count].linkStatus.outingCost = entry->outgoingCost & 0x07U;
		list[count].linkStatus.reservedH = 0;

		count++;
		if (count >= maxEntries) {
			break;
		}
	}

	for (u8 j = 0; (u8)(j + 1) < count; j++) {
		u8 min = j;

		for (u8 k = j + 1; k < count; k++) {
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

void nwkLinkStatusCmdSend(void *arg, nwk_hdr_t *pNwkHdr, nwkCmd_t *cmd, u8 handle)
{
	zb_buf_t *buf = (zb_buf_t *)arg;
	u8 entryCnt = cmd->linkSt.options.entryCnt;
	u8 payloadLen = (u8)(entryCnt * sizeof(linkStatus_entry_t) + 2U);
	u8 *payload;

	TL_BUF_INITIAL_ALLOC(buf, payloadLen, payload, u8 *);

	payload[0] = cmd->cmdId;
	memcpy(&payload[1], &cmd->linkSt.options, 1);

	if (entryCnt != 0U) {
		memcpy(&payload[2], cmd->linkSt.linkStatusList,
		       entryCnt * sizeof(linkStatus_entry_t));
	}

	buf->hdr.handle = handle;

	nwk_fwdPacket(buf, pNwkHdr, payload, payloadLen);
}

void tl_zbNwkSendLinkStatus(void)
{
	linkStatus_entry_t *entries = NULL;
	u8 routerCnt = tl_zbNeighborTableRouterValidNumGet();
	u8 entryCnt = 0;
	u8 frameCnt = 1;

	if (routerCnt != 0U) {
		u16 freeSize = ev_buf_getFreeMaxSize();
		u16 allocSize = (u16)routerCnt * sizeof(linkStatus_entry_t);

		if (allocSize > freeSize) {
			routerCnt = (u8)(freeSize / sizeof(linkStatus_entry_t));
			allocSize = (u16)routerCnt * sizeof(linkStatus_entry_t);
		}

		entries = (linkStatus_entry_t *)ev_buf_allocate(allocSize);
		if (entries == NULL) {
			ZB_EXCEPTION_POST(SYS_EXCEPTTION_ZB_NWK_LINK_STA_MEM_ALLOC_FAIL);
			return;
		}

		entryCnt = nwk_linkStEntryBuild(entries, routerCnt);
		if (entryCnt != 0U) {
			frameCnt = (u8)(entryCnt / NWK_LINK_STATUS_ENTRY_MAX_PER_FRAME);
			if ((entryCnt % NWK_LINK_STATUS_ENTRY_MAX_PER_FRAME) != 0U) {
				frameCnt++;
			}
		}
	}

	{
		linkStatus_entry_t *cursor = entries;
		u8 remaining = entryCnt;

		for (u8 i = 0; i < frameCnt; i++) {
			zb_buf_t *buf = zb_buf_allocate();
			nwk_hdr_t hdr;
			nwkCmd_t cmd;
			u8 frameEntryCnt;

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
			cmd.linkSt.options.lastFrame = (i == (u8)(frameCnt - 1U));
			cmd.linkSt.linkStatusList = cursor;

			if (cursor != NULL) {
				cursor += frameEntryCnt;
			}

			nwkLinkStatusCmdSend(buf, &hdr, &cmd, NWK_INTERNAL_LINK_STATUS_CMD_HANDLE);

			remaining = (u8)(remaining - frameEntryCnt);
		}
	}

	if (entries != NULL) {
		ev_buf_free((u8 *)entries);
	}
}

void tl_zbNwkLinkStatusStart(void)
{
	tl_zbNwkSendLinkStatus();
	if (g_zbNIB.linkStatusPeriod != 0U) {
		linkStExpiry = g_zbNIB.linkStatusPeriod;
	}
}

int tl_zbNwkLinkStatusTimerEvtCb(void *arg)
{
	(void)arg;

	if (g_zbNwkCtx.joined != 0U) {
		T_DBG_linkStatus++;
		tl_zbNwkNeighborTabAging();
		tl_zbNwkLinkStatusStart();
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
		u32 delay = (drv_u32Rand() & 0x7fU) + 5U;
		linkStTimer = ev_timer_taskPost(tl_zbNwkLinkStatusTimerEvtCb, NULL, delay);
	}

	return 0;
}

u8 tl_nwkGetAverageLqi(u8 oldLqi, u8 newLqi)
{
	if (oldLqi == 0U) {
		return newLqi;
	}

	/* "c: tshftls r3,r0,#4; e: tsubs r0,r3,r0" is oldLqi*15, and the shifts at
	 * "12:".."1a:" are a signed divide by 16 - a 15/16 exponential average, not
	 * a plain mean.  Both arguments are handled as s8. */
	return (u8)(((s8)oldLqi * 15 + (s8)newLqi) / 16);
}

void tl_zbNwkLinkStatusCmdHandler(void *arg, nwk_hdr_t *pNwkHdr, nwkCmd_t *cmd)
{
	zb_mscp_data_ind_t *ind = (zb_mscp_data_ind_t *)arg;
	linkStatus_entry_t *list = cmd->linkSt.linkStatusList;
	u8 entryCnt = cmd->linkSt.options.entryCnt;
	u8 payloadLen = (u8)(ind->msduLength - pNwkHdr->frameHdrLen);
	tl_zb_normal_neighbor_entry_t *neighbor;
	u16 srcAddr = pNwkHdr->srcAddr;
	u16 localAddr = g_zbNIB.nwkAddr;
	u8 newNeighbor = 0;

	if (payloadLen != (u8)(entryCnt * sizeof(linkStatus_entry_t) + 2U)) {
		zb_buf_free((zb_buf_t *)arg);
		return;
	}

	neighbor = nwk_neTblGetByShortAddr(srcAddr);

	if (neighbor == NULL) {
		tl_zb_normal_neighbor_entry_t candidate;
		u16 addrmapIdx;
		u8 deleteOld = 0;

		/* An end-device-initiator frame carries the authoritative source IEEE
		 * address when the MAC indication also reports the same short address. */
		if (pNwkHdr->frameControl.endDevInitiator != 0U &&
		    ind->srcAddr.addrMode == ADDR_MODE_SHORT &&
		    ind->srcAddr.addr.shortAddr == srcAddr) {
			(void)tl_zbNwkAddrMapAdd(srcAddr, pNwkHdr->srcIeeeAddr, &addrmapIdx);
		}

		if (tl_idxByShortAddr(&addrmapIdx, srcAddr) != RET_OK) {
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
			for (u8 i = 0; i < entryCnt; i++) {
				if (list[i].neighborNwkAddr == localAddr) {
					deleteOld = 1;
					break;
				}
			}
		}

		neighbor = tl_zbNeighborTableUpdate(&candidate, deleteOld);
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
	for (u8 i = 0; i < entryCnt; i++) {
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

void tl_zbNwkReportCmdHandler(void *arg, nwk_hdr_t *pNwkHdr, nwkCmd_t *cmd)
{
	/* The vendor library asks "is this report addressed to me": it loads the
	 * 16-bit value at nwk_hdr_t+0 (dstAddr) and compares it against
	 * g_zbInfo+102/103, which is g_zbNIB.nwkAddr. */
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

void tl_zbNwkSendNwkStatusCmd(void *arg, nwk_hdr_t *pNwkHdr, nwkCmd_t *pNwkStatus, u8 handle)
{
	zb_buf_t *buf = (zb_buf_t *)arg;
	u8 *payload;
	u16 nextHop;

	/* The ordinary case: the status command is relayed along the route the
	 * incoming header already describes, and nwk_fwdPacket picks the next hop.
	 * Only a many-to-one route failure takes the repair path below, because
	 * that is the one status the concentrator's route can no longer carry. */
	if (pNwkStatus->nwkStatus.statusCode == NWK_COMMAND_STATUS_MANY_TO_ONE_ROUTE_FAILURE) {
		tl_zb_normal_neighbor_entry_t *neighbor;

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
				neighbor = tl_zbNeighborTabSearchForRouter(neighbor);
				if (neighbor == NULL) {
					nextHop = pNwkHdr->dstAddr;
					break;
				}

				nextHop = tl_zbshortAddrByIdx(neighbor->addrmapIdx);

				if ((nextHop != g_zbNwkCtx.manyToOneRepair.senderAddr) &&
				    (nextHop != g_zbNwkCtx.manyToOneRepair.lastSendFailAddr)) {
					break;
				}
			}
		}

		TL_BUF_INITIAL_ALLOC(buf, 1 + sizeof(nwkCmd_nwkStatus_t), payload, u8 *);
		payload[0] = pNwkStatus->cmdId;
		payload[1] = pNwkStatus->nwkStatus.statusCode;
		memcpy(payload + 2, &pNwkStatus->nwkStatus.dstAddr, 2);
		buf->hdr.handle = handle;

		nwk_tx(buf, pNwkHdr, nextHop, 0, payload, 1 + sizeof(nwkCmd_nwkStatus_t));
		return;
	}

forward:
	TL_BUF_INITIAL_ALLOC(buf, 1 + sizeof(nwkCmd_nwkStatus_t), payload, u8 *);
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
 * descriptor fields back after tl_bufInitalloc has carved the payload out of
 * the same buffer.  Reproduced as written: the payload is allocated at the
 * tail, so the descriptor at the head survives.
 */
void my_tl_zbNwkSendNwkStatusCmd(void *arg)
{
	zb_buf_t *buf = (zb_buf_t *)arg;
	myNwkCmd_nwkStatus_t *req = (myNwkCmd_nwkStatus_t *)arg;
	nwk_hdr_t hdr;
	u8 len = req->payloadLen + 2;
	u8 *payload;

	TL_BUF_INITIAL_ALLOC(buf, len, payload, u8 *);

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
static inline bool nwk_statusAddrIsLocal(u16 shortAddr)
{
	tl_zb_normal_neighbor_entry_t *neighbor = nwk_neTblGetByShortAddr(shortAddr);

	if (shortAddr == g_zbNIB.nwkAddr) {
		return true;
	}

	return (neighbor != NULL) && (neighbor->deviceType == NWK_DEVICE_TYPE_ED) &&
	       (neighbor->relationship == NEIGHBOR_IS_CHILD);
}

void tl_zbNwkStatusCmdHandler(void *arg, nwk_hdr_t *pNwkHdr, nwkCmd_t *cmd)
{
	zb_buf_t *buf = (zb_buf_t *)arg;
	zb_mscp_data_ind_t *macInd = (zb_mscp_data_ind_t *)arg;

	if ((cmd->nwkStatus.statusCode == NWK_COMMAND_STATUS_TREE_LINK_FAILURE) ||
	    (cmd->nwkStatus.statusCode == NWK_COMMAND_STATUS_NONE_TREE_LINK_FAILURE)) {
		/* A route through us has broken.  When either end of the failing link
		 * is local the routing entry is marked inactive, so the next frame for
		 * that destination starts a fresh route discovery instead of being
		 * sent down the dead route.  Both tests run; the second is not an
		 * else - the vendor checks the status destination and the header
		 * destination independently. */
		if (nwk_statusAddrIsLocal(cmd->nwkStatus.dstAddr)) {
			nwk_routingTabEntry_t *entry =
				nwkRoutingTabEntryDstActiveGet(cmd->nwkStatus.dstAddr);

			if (entry != NULL) {
				entry->status = NWK_ROUTE_STATE_DISCOVERY_INACTIVE;
			}
		}

		if (nwk_statusAddrIsLocal(pNwkHdr->dstAddr)) {
			nwk_routingTabEntry_t *entry =
				nwkRoutingTabEntryDstActiveGet(cmd->nwkStatus.dstAddr);

			if (entry != NULL) {
				entry->status = NWK_ROUTE_STATE_DISCOVERY_INACTIVE;
			}
		}
	} else if (cmd->nwkStatus.statusCode == NWK_COMMAND_STATUS_MANY_TO_ONE_ROUTE_FAILURE) {
		/* Remember who handed us the failing many-to-one frame, so that the
		 * repair path in tl_zbNwkSendNwkStatusCmd does not hand it straight
		 * back.  Only for a frame that is still travelling onwards. */
		if ((pNwkHdr->dstAddr != g_zbNIB.nwkAddr) && (pNwkHdr->radius != 0U)) {
			g_zbNwkCtx.manyToOneRepair.senderAddr = macInd->srcAddr.addr.shortAddr;
		}
	}

	if (ZB_NWK_IS_ADDRESS_BROADCAST(pNwkHdr->dstAddr)) {
		if (pNwkHdr->radius != 0U) {
			if (nwkBrcTimerStart(buf, pNwkHdr, macInd->msdu + pNwkHdr->frameHdrLen,
					     (u8)(macInd->msduLength - pNwkHdr->frameHdrLen)) !=
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

		tl_zbNwkSendNwkStatusCmd(buf, pNwkHdr, cmd, NWK_INTERNAL_NETWORK_STATUS_CMD_HANDLE);
		return;
	}

	tl_zbNwkNlmeNwkStatusInd(buf, cmd->nwkStatus.dstAddr, cmd->nwkStatus.statusCode);
}
#else
/*
 * An end device has neither a routing table nor a broadcast transaction table,
 * so its variant only reports the status upwards.  _ed/nwk.s builds the whole
 * handler out of that one tail call: "2: tloadrb r3,[r2,#4]" and
 * "4: tloadrb r1,[r2,#5]" assemble nwkStatus.dstAddr, "a: tloadrb r2,[r2,#6]"
 * is nwkStatus.statusCode, and the buffer is passed on untouched.
 */
void tl_zbNwkStatusCmdHandler(void *arg, nwk_hdr_t *pNwkHdr, nwkCmd_t *cmd)
{
	(void)pNwkHdr;

	tl_zbNwkNlmeNwkStatusInd(arg, cmd->nwkStatus.dstAddr, cmd->nwkStatus.statusCode);
}
#endif

void tl_zbNwkTaskProc(void)
{
	tl_zb_task_t taskInfo;

	if ((tl_zbTaskQPop(TL_Q_HIGH2NWK, &taskInfo) != NULL) && (taskInfo.data != NULL)) {
		nwk_task_dispatch(
			g_zbNwkEventFromHighTbl,
			(u8)(sizeof(g_zbNwkEventFromHighTbl) / sizeof(g_zbNwkEventFromHighTbl[0])),
			taskInfo.data);
	}

	if ((tl_zbTaskQPop(TL_Q_MAC2NWK, &taskInfo) != NULL) && (taskInfo.data != NULL)) {
		nwk_task_dispatch(
			g_zbNwkEventFromMacTbl,
			(u8)(sizeof(g_zbNwkEventFromMacTbl) / sizeof(g_zbNwkEventFromMacTbl[0])),
			taskInfo.data);
	}
}

void tl_zbNwkNibSet(void *arg)
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

u8 is_device_factory_new(void)
{
	return g_zbNwkCtx.is_factory_new != 0U;
}
