/*
 * Copyright 2026 libzigbee
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "zb_common.h"
#include "nwk_data.h"
#include "zb_nwk_core.h"
#include "zb_nwk_neighbor.h"
#include "nwk_routing.h"
#include <stdint.h>
#include "ss_security_flags.h"

#if defined(ZB_ROUTER_ROLE) || defined(ZB_COORDINATOR_ROLE)

u8 NWKC_TRANSFAILURE_CNT_THRESHOLD = TRANSFAILURE_CNT_MAX;
u8 NWKC_INITIAL_RREQ_RETRIES = NWK_INITIAL_RREQ_RETRIES;
u8 NWKC_RREQ_RETRIES = NWK_RREQ_RETRIES;
u16 ROUTING_TABLE_SIZE = ROUTING_TABLE_NUM;
nwk_routingTabEntry_t g_routingTab[ROUTING_TABLE_NUM];
#if defined(ZB_COORDINATOR_ROLE)
u16 NWK_ROUTE_RECORD_TABLE_SIZE = NWK_ROUTE_RECORD_TABLE_NUM;
nwk_routeRecordTabEntry_t g_routeRecTab[NWK_ROUTE_RECORD_TABLE_NUM];

void nwkRouteRecTabEntryClear(nwk_routeRecordTabEntry_t *entry);
#endif

static bool nwk_routing_entry_active(const nwk_routingTabEntry_t *entry)
{
	return entry != NULL && entry->dstAddr != NWK_BROADCAST_RESERVED &&
	       (entry->status == NWK_ROUTE_STATE_ACTIVE ||
		entry->status == NWK_ROUTE_STATE_DISCOVERY_UNDERWAY ||
		entry->status == NWK_ROUTE_STATE_VALIDATION_UNDERWAY);
}

void nwkRoutingTabEntryClear(nwk_routingTabEntry_t *entry)
{
	if (entry == NULL) {
		return;
	}

	memset(entry, 0, sizeof(*entry));
	entry->dstAddr = NWK_BROADCAST_RESERVED;
	entry->nextHopAddr = NWK_BROADCAST_RESERVED;
	entry->status = NWK_ROUTE_STATE_DISCOVERY_INACTIVE;
}

void nwkRoutingTabInit(void)
{
	for (u16 i = 0; i < ROUTING_TABLE_SIZE; i++) {
		nwkRoutingTabEntryClear(&g_routingTab[i]);
	}

#if defined(ZB_COORDINATOR_ROLE)
	for (u16 i = 0; i < NWK_ROUTE_RECORD_TABLE_SIZE; i++) {
		nwkRouteRecTabEntryClear(&g_routeRecTab[i]);
	}
#endif
}

void nwkRoutingTabRst(void)
{
	nwkRoutingTabInit();
}

u8 nwkRoutingTabActiveNumGet(void)
{
	u8 count = 0;

	for (u16 i = 0; i < ROUTING_TABLE_SIZE; i++) {
		if (nwk_routing_entry_active(&g_routingTab[i])) {
			count++;
		}
	}

	return count;
}

nwk_routingTabEntry_t *nwkRoutingTabEntryDstActiveGet(u16 dstAddr)
{
	for (u16 i = 0; i < ROUTING_TABLE_SIZE; i++) {
		nwk_routingTabEntry_t *entry = &g_routingTab[i];

		if (entry->status == NWK_ROUTE_STATE_ACTIVE && entry->dstAddr == dstAddr) {
			return entry;
		}
	}

	return NULL;
}

nwk_routingTabEntry_t *nwkRoutingTabEntryNextHopActiveGet(u16 nextHop)
{
	for (u16 i = 0; i < ROUTING_TABLE_SIZE; i++) {
		nwk_routingTabEntry_t *entry = &g_routingTab[i];

		if (entry->status == NWK_ROUTE_STATE_ACTIVE && entry->nextHopAddr == nextHop) {
			return entry;
		}
	}

	return NULL;
}

nwk_routingTabEntry_t *nwkRoutingTabEntryFind(u16 dstAddr)
{
	for (u16 i = 0; i < ROUTING_TABLE_SIZE; i++) {
		nwk_routingTabEntry_t *entry = &g_routingTab[i];

		if (nwk_routing_entry_active(entry) && entry->dstAddr == dstAddr) {
			return entry;
		}
	}

	return NULL;
}

nwk_routingTabEntry_t *nwkRoutingTabEntryDstFind(u16 dstAddr)
{
	for (u16 i = 0; i < ROUTING_TABLE_SIZE; i++) {
		if (g_routingTab[i].dstAddr == dstAddr) {
			return &g_routingTab[i];
		}
	}

	return NULL;
}

nwk_routingTabEntry_t *nwkRoutingTabEntryCreate(u16 dstAddr)
{
	nwk_routingTabEntry_t *candidate = NULL;

	/* The vendor does not deduplicate destinations here; callers that need
	 * that behavior look the route up first.  A route is reusable only when
	 * it is failed or inactive (states 2 and 3). */
	if (ROUTING_TABLE_SIZE == 0U) {
		return NULL;
	}

	for (u16 i = 0; i < ROUTING_TABLE_SIZE; i++) {
		if ((g_routingTab[i].status == NWK_ROUTE_STATE_DISCOVERY_FAILED) ||
		    (g_routingTab[i].status == NWK_ROUTE_STATE_DISCOVERY_INACTIVE)) {
			candidate = &g_routingTab[i];
			break;
		}
	}

	if (candidate == NULL) {
		/* Prefer evicting a route whose destination is a good one-hop
		 * neighbor.  The strict comparison is the vendor's
		 * NWK_NEIGHBOR_SEND_OUTGOING_THRESHOLD > outgoingCost test. */
		for (u16 i = 0; i < ROUTING_TABLE_SIZE; i++) {
			tl_zb_normal_neighbor_entry_t *neighbor =
				nwk_neTblGetByShortAddr(g_routingTab[i].dstAddr);

			if ((neighbor != NULL) && (neighbor->outgoingCost != 0U) &&
			    (neighbor->outgoingCost < NWK_NEIGHBOR_SEND_OUTGOING_THRESHOLD)) {
				candidate = &g_routingTab[i];
				break;
			}
		}
	}

	if (candidate == NULL) {
		/* With no reusable route, the vendor chooses the entry with the
		 * greatest forget counter (keeping the first entry on ties). */
		candidate = &g_routingTab[0];

		for (u16 i = 1; i < ROUTING_TABLE_SIZE; i++) {
			if (g_routingTab[i].forgetCnt > candidate->forgetCnt) {
				candidate = &g_routingTab[i];
			}
		}
	}

	nwkRoutingTabEntryClear(candidate);
	candidate->dstAddr = dstAddr;
	candidate->status = NWK_ROUTE_STATE_DISCOVERY_UNDERWAY;
	return candidate;
}

nwk_routingTabEntry_t *nwkRoutingTabGetNextHop(nwk_hdr_t *pNwkHdr)
{
	nwk_routingTabEntry_t *entry;

	entry = NULL;
	for (u16 i = 0; i < ROUTING_TABLE_SIZE; i++) {
		if (g_routingTab[i].dstAddr == pNwkHdr->dstAddr) {
			entry = &g_routingTab[i];
			break;
		}
	}

	if (entry == NULL || entry->nextHopAddr == NWK_BROADCAST_RESERVED) {
		return NULL;
	}

	/* The vendor invalidates a route in validation state before reporting that
	 * it cannot be used.  Other non-zero states may still carry a usable hop.
	 */
	if (entry->status == NWK_ROUTE_STATE_VALIDATION_UNDERWAY) {
		entry->status = NWK_ROUTE_STATE_ACTIVE;
		return entry;
	}

	if (entry->status == NWK_ROUTE_STATE_ACTIVE) {
		return entry;
	}

	return NULL;
}

void nwkRoutingTabEntryDstDel(u16 dstAddr)
{
	for (u16 i = 0; i < ROUTING_TABLE_SIZE; i++) {
		if (g_routingTab[i].dstAddr == dstAddr) {
			nwkRoutingTabEntryClear(&g_routingTab[i]);
		}
	}
}

/*
 * Sends a NWK status command back towards dstAddr.  Reconstructed from
 * _router/nwk_routing.s:.text.nwkRouteRepair - it builds its own NWK header
 * rather than reusing the one that failed.
 */
void nwkRouteRepair(zb_buf_t *buf, u16 dstAddr, u16 statusDstAddr, u8 statusCode)
{
	nwk_hdr_t hdr;
	nwkCmd_t cmd;
	u8 security = 0;

	memset(&hdr, 0, sizeof(hdr));
	memset(&cmd, 0, sizeof(cmd));

	cmd.cmdId = NWK_CMD_NETWORK_STATUS;
	cmd.nwkStatus.statusCode = statusCode;
	cmd.nwkStatus.dstAddr = statusDstAddr;

	/* "46: tshftls r2,r3,#28 / 4a: tshftls r2,r3,#29" - both fields live in
	 * the same ss_ib byte. */
	if (ss_ib_secure_all_fresh() && (ss_ib_security_level_get() != 0U)) {
		security = ss_keyPreconfigured() ? 1U : 0U;
	}

	hdr.frameControl.frameType = FRAME_TYPE_COMMAND;
	hdr.frameControl.protocolVer = ZB_PROTOCOL_VERSION;
	hdr.frameControl.srcIEEEAddr = 1;
	hdr.frameControl.dstIEEEAddr = 0;
	hdr.frameControl.security = security;
	ZB_IEEE_ADDR_COPY(hdr.srcIeeeAddr, g_zbInfo.macPib.extAddress);
	hdr.dstAddr = dstAddr;
	hdr.srcAddr = g_zbNIB.nwkAddr;
	hdr.radius = (u8)(g_zbNIB.maxDepth << 1);
	hdr.seqNum = g_zbNIB.seqNum++;
	hdr.frameHdrLen = getNwkHdrSize(&hdr);

	tl_zbNwkSendNwkStatusCmd(buf, &hdr, &cmd, NWK_INTERNAL_NETWORK_STATUS_CMD_HANDLE);
}

/*
 * Called from tl_zbMacMcpsDataConfirmHandler when a forwarded frame could not
 * be delivered.  Reconstructed from _router/nwk_routing.s:.text.nwkRouteMaintenance.
 */
void nwkRouteMaintenance(nwk_hdr_t *pNwkHdr, u16 macDstAddr)
{
	u16 repairDst;
	u16 statusDst;
	u8 statusCode;
	zb_buf_t *buf;

	if (pNwkHdr->frameControl.srcRoute) {
		repairDst = pNwkHdr->srcAddr;
		statusDst = pNwkHdr->dstAddr;
		statusCode = NWK_COMMAND_STATUS_SOURCE_ROUTE_FAILURE;
#if defined(ZB_COORDINATOR_ROLE)
		nwkRouteRecTabEntryDstDel(macDstAddr);
#endif
	} else {
		nwk_routingTabEntry_t *entry = NULL;

		/* Only ACTIVE entries for this destination; the scan walks the table
		 * directly rather than going through nwkRoutingTabEntryFind. */
		for (u16 i = 0; i < ROUTING_TABLE_SIZE; i++) {
			if (g_routingTab[i].status != NWK_ROUTE_STATE_ACTIVE) {
				continue;
			}

			if (g_routingTab[i].dstAddr != pNwkHdr->dstAddr) {
				continue;
			}

			entry = &g_routingTab[i];
			break;
		}

		if (entry == NULL) {
			return;
		}

		entry->status = NWK_ROUTE_STATE_DISCOVERY_INACTIVE;

		if (entry->manyToOne) {
			g_zbNwkCtx.manyToOneRepair.senderAddr = pNwkHdr->srcAddr;
			g_zbNwkCtx.manyToOneRepair.lastSendFailAddr = macDstAddr;
			repairDst = pNwkHdr->dstAddr;
			statusDst = pNwkHdr->srcAddr;
			statusCode = NWK_COMMAND_STATUS_MANY_TO_ONE_ROUTE_FAILURE;
		} else {
			repairDst = pNwkHdr->srcAddr;
			statusDst = pNwkHdr->dstAddr;
			statusCode = NWK_COMMAND_STATUS_NONE_TREE_LINK_FAILURE;
		}
	}

	buf = zb_buf_allocate();

	if (buf != NULL) {
		nwkRouteRepair(buf, repairDst, statusDst, statusCode);
	}
}

void nwkSrcRouteRequiredClear(u16 dstAddr)
{
	for (u16 i = 0; i < ROUTING_TABLE_SIZE; i++) {
		nwk_routingTabEntry_t *entry = &g_routingTab[i];

		if (entry->dstAddr != dstAddr) {
			continue;
		}

		if ((entry->transFail & 0x08U) != 0U) {
			return;
		}

		entry->routeRecordRequired = 0;
		return;
	}
}

static u16 nwk_src_route_relay_addr(const nwk_hdr_t *pNwkHdr, u8 relayIdx)
{
	const u8 *relay = pNwkHdr->srcRouteSubframe.relayList + ((u16)relayIdx * 2U);

	return (u16)relay[0] | ((u16)relay[1] << 8);
}

u16 nwkSrcRouteReplayNextHop(nwk_hdr_t *pNwkHdr)
{
	tl_zb_normal_neighbor_entry_t *neighbor;

	if (pNwkHdr == NULL || pNwkHdr->srcAddr == g_zbInfo.nwkNib.nwkAddr ||
	    pNwkHdr->frameControl.srcRoute == 0U || pNwkHdr->srcRouteSubframe.relayList == NULL) {
		return MAC_ADDR_USE_EXT;
	}

	if (pNwkHdr->srcRouteSubframe.relayIdx == 0U) {
		nwkSrcRouteRequiredClear(pNwkHdr->dstAddr);
		return pNwkHdr->dstAddr;
	}

	neighbor = nwk_neTblGetByShortAddr(pNwkHdr->dstAddr);
	if (neighbor != NULL && neighbor->deviceType == NWK_DEVICE_TYPE_ED &&
	    neighbor->relationship == NEIGHBOR_IS_CHILD) {
		nwkSrcRouteRequiredClear(pNwkHdr->dstAddr);
		return pNwkHdr->dstAddr;
	}

	pNwkHdr->srcRouteSubframe.relayIdx--;
	return nwk_src_route_relay_addr(pNwkHdr, pNwkHdr->srcRouteSubframe.relayIdx);
}

u8 nwkSourceRoutePacketRelayFilter(nwk_hdr_t *pNwkHdr)
{
	u16 localAddr;
	u16 relayAddr;
	u16 distance;

	localAddr = g_zbInfo.nwkNib.nwkAddr;
	if (pNwkHdr->srcAddr == localAddr) {
		return 0xc1U;
	}

	if (pNwkHdr->dstAddr == localAddr || pNwkHdr->srcRouteSubframe.relayCnt == 0U ||
	    pNwkHdr->srcRouteSubframe.relayList == NULL) {
		return 0;
	}

	relayAddr = nwk_src_route_relay_addr(pNwkHdr, pNwkHdr->srcRouteSubframe.relayIdx);
	distance = (localAddr >= relayAddr) ? (localAddr - relayAddr) : (relayAddr - localAddr);
	return (u8)(0xc1U & distance);
}

#if defined(ZB_COORDINATOR_ROLE)

static inline bool nwk_route_rec_used(const nwk_routeRecordTabEntry_t *entry)
{
	return entry != NULL && entry->used != 0U;
}

void nwkRouteRecTabEntryClear(nwk_routeRecordTabEntry_t *entry)
{
	if (entry == NULL) {
		return;
	}

	entry->nwkAddr = NWK_BROADCAST_RESERVED;
	for (u8 i = 0; i < g_zbInfo.nwkNib.maxSourceRoute; i++) {
		entry->path[i] = NWK_BROADCAST_RESERVED;
	}
	entry->relayCnt = 0;
	entry->used = 0;
	entry->forgetCnt = 0;
}

u8 nwkRouteRecTabActiveNumGet(void)
{
	u8 count = 0;

	for (u16 i = 0; i < NWK_ROUTE_RECORD_TABLE_SIZE; i++) {
		if (nwk_route_rec_used(&g_routeRecTab[i])) {
			count++;
		}
	}

	return count;
}

nwk_routeRecordTabEntry_t *nwkRouteRecTabEntryFreeGet(void)
{
	for (u16 i = 0; i < NWK_ROUTE_RECORD_TABLE_SIZE; i++) {
		if (!nwk_route_rec_used(&g_routeRecTab[i])) {
			return &g_routeRecTab[i];
		}
	}

	return NULL;
}

nwk_routeRecordTabEntry_t *nwkRouteRecTabEntryFind(u16 nwkAddr)
{
	for (u16 i = 0; i < NWK_ROUTE_RECORD_TABLE_SIZE; i++) {
		nwk_routeRecordTabEntry_t *entry = &g_routeRecTab[i];

		if (nwk_route_rec_used(entry) && entry->nwkAddr == nwkAddr) {
			return entry;
		}
	}

	return NULL;
}

bool nwkRouteRecTabPathMatch(nwk_routeRecordTabEntry_t *entry, u16 nwkAddr)
{
	if (!nwk_route_rec_used(entry)) {
		return FALSE;
	}

	if (entry->nwkAddr == nwkAddr) {
		return TRUE;
	}

	for (u8 i = 0; i < entry->relayCnt && i < g_zbInfo.nwkNib.maxSourceRoute; i++) {
		if (entry->path[i] == nwkAddr) {
			return TRUE;
		}
	}

	return FALSE;
}

nwk_routeRecordTabEntry_t *nwkRouteRecTabEntryAddNew(u16 nwkAddr, nwkCmd_t *cmd)
{
	nwk_routeRecordTabEntry_t *entry = nwkRouteRecTabEntryFreeGet();
	u8 relayCnt;

	if (entry == NULL || cmd == NULL) {
		return NULL;
	}

	relayCnt = cmd->rrec.relayCnt;
	entry->nwkAddr = nwkAddr;
	entry->relayCnt = relayCnt;

	if (relayCnt != 0U && cmd->rrec.relayList != NULL) {
		memcpy(entry->path, cmd->rrec.relayList, (size_t)relayCnt * sizeof(u16));
	}

	entry->used = 1;
	entry->forgetCnt = 0;
	return entry;
}

nwk_routeRecordTabEntry_t *nwkRouteRecTabEntryCreat(u16 nwkAddr, nwkCmd_t *cmd)
{
	nwk_routeRecordTabEntry_t *entry;

	if (cmd == NULL || cmd->rrec.relayCnt > g_zbInfo.nwkNib.maxSourceRoute) {
		return NULL;
	}

	entry = nwkRouteRecTabEntryFind(nwkAddr);
	if (entry == NULL) {
		entry = nwkRouteRecTabEntryAddNew(nwkAddr, cmd);
		if (entry == NULL && NWK_ROUTE_RECORD_TABLE_SIZE != 0U) {
			entry = &g_routeRecTab[0];

			for (u16 i = 1; i < NWK_ROUTE_RECORD_TABLE_SIZE; i++) {
				if (g_routeRecTab[i].forgetCnt > entry->forgetCnt) {
					entry = &g_routeRecTab[i];
				}
			}

			entry->nwkAddr = nwkAddr;
			entry->relayCnt = cmd->rrec.relayCnt;
			entry->used = 1;
			entry->forgetCnt = 0;

			if (entry->relayCnt != 0U && cmd->rrec.relayList != NULL) {
				memcpy(entry->path, cmd->rrec.relayList,
				       (size_t)entry->relayCnt * sizeof(u16));
			}
		}

		return entry;
	}

	entry->relayCnt = cmd->rrec.relayCnt;
	entry->forgetCnt = 0;
	if (entry->relayCnt != 0U && cmd->rrec.relayList != NULL) {
		memcpy(entry->path, cmd->rrec.relayList, (size_t)entry->relayCnt * sizeof(u16));
	}

	return entry;
}

void nwkRouteRecTabEntryDstDel(u16 dstAddr)
{
	for (u16 i = 0; i < NWK_ROUTE_RECORD_TABLE_SIZE; i++) {
		nwk_routeRecordTabEntry_t *entry = &g_routeRecTab[i];

		if (nwkRouteRecTabPathMatch(entry, dstAddr)) {
			nwkRouteRecTabEntryClear(entry);
		}
	}
}

#endif

int nwkRoutingTabPeriodic(void *arg)
{
	u16 activeNum = 0;
#if defined(ZB_COORDINATOR_ROLE)
	u16 routeRecordActiveNum = 0;
#endif

	(void)arg;

	/* The vendor library counts the ACTIVE entries first and returns without
	 * touching anything when there are none:
	 *   1c: tloadrb r2,[r3,#0]; 1e: tcmp r2,#0; 20: tjne 28; 22: tadds r4,#1
	 *   2e: tcmp r4,#0; 30: tjeq 60
	 */
	for (u16 i = 0; i < ROUTING_TABLE_SIZE; i++) {
		if (g_routingTab[i].status == NWK_ROUTE_STATE_ACTIVE) {
			activeNum++;
		}
	}

	/* The ageing body is inlined in the vendor object and only bumps the
	 * routing-entry forget counter; it does not call nwkRouteMaintenance
	 * (which is a two-argument route-repair helper there, invoked from
	 * tl_zbMacMcpsDataConfirmHandler):
	 *   3a: tloadrb r2,[r0,#0]; 3c: tjne 54          ; status != ACTIVE
	 *   44: tloadrb r2,[r2,#5]; 46/48: tjmi 54       ; manyToOne
	 *   4a: tloadrb r2,[r0,#3]; 4c: tcmp r2,#255     ; forgetCnt saturated
	 *   50: tadds r2,#1; 52: tstorerb r2,[r0,#3]
	 */
	if (activeNum != 0U) {
		for (u16 i = 0; i < ROUTING_TABLE_SIZE; i++) {
			nwk_routingTabEntry_t *entry = &g_routingTab[i];

			if (entry->status != NWK_ROUTE_STATE_ACTIVE) {
				continue;
			}

			if (entry->manyToOne) {
				continue;
			}

			if (entry->forgetCnt == 0xffU) {
				continue;
			}

			entry->forgetCnt++;
		}
	}

#if defined(ZB_COORDINATOR_ROLE)
	for (u16 i = 0; i < NWK_ROUTE_RECORD_TABLE_SIZE; i++) {
		if (g_routeRecTab[i].used) {
			routeRecordActiveNum++;
		}
	}

	if (routeRecordActiveNum != 0U) {
		for (u16 i = 0; i < NWK_ROUTE_RECORD_TABLE_SIZE; i++) {
			nwk_routeRecordTabEntry_t *entry = &g_routeRecTab[i];

			if (entry->used && entry->forgetCnt != UINT8_MAX) {
				entry->forgetCnt++;
			}
		}
	}
#else
	if (activeNum == 0U) {
		return 0;
	}
#endif

	return 0;
}

#else

#include "zb_common.h"

#endif
