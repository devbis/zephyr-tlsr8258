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

uint8_t NWKC_TRANSFAILURE_CNT_THRESHOLD = TRANSFAILURE_CNT_MAX;
uint8_t NWKC_INITIAL_RREQ_RETRIES = NWK_INITIAL_RREQ_RETRIES;
uint8_t NWKC_RREQ_RETRIES = NWK_RREQ_RETRIES;
uint16_t ROUTING_TABLE_SIZE = ROUTING_TABLE_NUM;
nwk_routingTabEntry_t g_routingTab[ROUTING_TABLE_NUM];
#if defined(ZB_COORDINATOR_ROLE)
uint16_t NWK_ROUTE_RECORD_TABLE_SIZE = NWK_ROUTE_RECORD_TABLE_NUM;
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
	for (uint16_t i = 0; i < ROUTING_TABLE_SIZE; i++) {
		nwkRoutingTabEntryClear(&g_routingTab[i]);
	}

#if defined(ZB_COORDINATOR_ROLE)
	for (uint16_t i = 0; i < NWK_ROUTE_RECORD_TABLE_SIZE; i++) {
		nwkRouteRecTabEntryClear(&g_routeRecTab[i]);
	}
#endif
}

void nwkRoutingTabRst(void)
{
	nwkRoutingTabInit();
}

uint8_t nwkRoutingTabActiveNumGet(void)
{
	uint8_t count = 0;

	for (uint16_t i = 0; i < ROUTING_TABLE_SIZE; i++) {
		if (nwk_routing_entry_active(&g_routingTab[i])) {
			count++;
		}
	}

	return count;
}

nwk_routingTabEntry_t *nwkRoutingTabEntryDstActiveGet(uint16_t dstAddr)
{
	for (uint16_t i = 0; i < ROUTING_TABLE_SIZE; i++) {
		nwk_routingTabEntry_t *entry = &g_routingTab[i];

		if (entry->status == NWK_ROUTE_STATE_ACTIVE && entry->dstAddr == dstAddr) {
			return entry;
		}
	}

	return NULL;
}

nwk_routingTabEntry_t *nwkRoutingTabEntryNextHopActiveGet(uint16_t nextHop)
{
	for (uint16_t i = 0; i < ROUTING_TABLE_SIZE; i++) {
		nwk_routingTabEntry_t *entry = &g_routingTab[i];

		if (entry->status == NWK_ROUTE_STATE_ACTIVE && entry->nextHopAddr == nextHop) {
			return entry;
		}
	}

	return NULL;
}

nwk_routingTabEntry_t *nwkRoutingTabEntryFind(uint16_t dstAddr)
{
	for (uint16_t i = 0; i < ROUTING_TABLE_SIZE; i++) {
		nwk_routingTabEntry_t *entry = &g_routingTab[i];

		if (nwk_routing_entry_active(entry) && entry->dstAddr == dstAddr) {
			return entry;
		}
	}

	return NULL;
}

nwk_routingTabEntry_t *nwkRoutingTabEntryDstFind(uint16_t dstAddr)
{
	for (uint16_t i = 0; i < ROUTING_TABLE_SIZE; i++) {
		if (g_routingTab[i].dstAddr == dstAddr) {
			return &g_routingTab[i];
		}
	}

	return NULL;
}

nwk_routingTabEntry_t *nwkRoutingTabEntryCreate(uint16_t dstAddr)
{
	nwk_routingTabEntry_t *candidate = NULL;

	/* The vendor does not deduplicate destinations here; callers that need
	 * that behavior look the route up first.  A route is reusable only when
	 * it is failed or inactive (states 2 and 3). */
	if (ROUTING_TABLE_SIZE == 0U) {
		return NULL;
	}

	for (uint16_t i = 0; i < ROUTING_TABLE_SIZE; i++) {
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
		for (uint16_t i = 0; i < ROUTING_TABLE_SIZE; i++) {
			zb_normal_neighbor_entry_t *neighbor =
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

		for (uint16_t i = 1; i < ROUTING_TABLE_SIZE; i++) {
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
	for (uint16_t i = 0; i < ROUTING_TABLE_SIZE; i++) {
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

void nwkRoutingTabEntryDstDel(uint16_t dstAddr)
{
	for (uint16_t i = 0; i < ROUTING_TABLE_SIZE; i++) {
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
void nwkRouteRepair(zb_buf_t *buf, uint16_t dstAddr, uint16_t statusDstAddr, uint8_t statusCode)
{
	nwk_hdr_t hdr;
	nwkCmd_t cmd;
	uint8_t security = 0;

	memset(&hdr, 0, sizeof(hdr));
	memset(&cmd, 0, sizeof(cmd));

	cmd.cmdId = NWK_CMD_NETWORK_STATUS;
	cmd.nwkStatus.statusCode = statusCode;
	cmd.nwkStatus.dstAddr = statusDstAddr;

	/* Both fields live in the same ss_ib byte. */
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
	hdr.radius = (uint8_t)(g_zbNIB.maxDepth << 1);
	hdr.seqNum = g_zbNIB.seqNum++;
	hdr.frameHdrLen = getNwkHdrSize(&hdr);

	zb_NwkSendNwkStatusCmd(buf, &hdr, &cmd, NWK_INTERNAL_NETWORK_STATUS_CMD_HANDLE);
}

/*
 * Called from zb_MacMcpsDataConfirmHandler when a forwarded frame could not
 * be delivered.  Reconstructed from _router/nwk_routing.s:.text.nwkRouteMaintenance.
 */
void nwkRouteMaintenance(nwk_hdr_t *pNwkHdr, uint16_t macDstAddr)
{
	uint16_t repairDst;
	uint16_t statusDst;
	uint8_t statusCode;
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
		for (uint16_t i = 0; i < ROUTING_TABLE_SIZE; i++) {
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

void nwkSrcRouteRequiredClear(uint16_t dstAddr)
{
	for (uint16_t i = 0; i < ROUTING_TABLE_SIZE; i++) {
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

static uint16_t nwk_src_route_relay_addr(const nwk_hdr_t *pNwkHdr, uint8_t relayIdx)
{
	const uint8_t *relay = pNwkHdr->srcRouteSubframe.relayList + ((uint16_t)relayIdx * 2U);

	return (uint16_t)relay[0] | ((uint16_t)relay[1] << 8);
}

uint16_t nwkSrcRouteReplayNextHop(nwk_hdr_t *pNwkHdr)
{
	zb_normal_neighbor_entry_t *neighbor;

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

uint8_t nwkSourceRoutePacketRelayFilter(nwk_hdr_t *pNwkHdr)
{
	uint16_t localAddr;
	uint16_t relayAddr;
	uint16_t distance;

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
	return (uint8_t)(0xc1U & distance);
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
	for (uint8_t i = 0; i < g_zbInfo.nwkNib.maxSourceRoute; i++) {
		entry->path[i] = NWK_BROADCAST_RESERVED;
	}
	entry->relayCnt = 0;
	entry->used = 0;
	entry->forgetCnt = 0;
}

uint8_t nwkRouteRecTabActiveNumGet(void)
{
	uint8_t count = 0;

	for (uint16_t i = 0; i < NWK_ROUTE_RECORD_TABLE_SIZE; i++) {
		if (nwk_route_rec_used(&g_routeRecTab[i])) {
			count++;
		}
	}

	return count;
}

nwk_routeRecordTabEntry_t *nwkRouteRecTabEntryFreeGet(void)
{
	for (uint16_t i = 0; i < NWK_ROUTE_RECORD_TABLE_SIZE; i++) {
		if (!nwk_route_rec_used(&g_routeRecTab[i])) {
			return &g_routeRecTab[i];
		}
	}

	return NULL;
}

nwk_routeRecordTabEntry_t *nwkRouteRecTabEntryFind(uint16_t nwkAddr)
{
	for (uint16_t i = 0; i < NWK_ROUTE_RECORD_TABLE_SIZE; i++) {
		nwk_routeRecordTabEntry_t *entry = &g_routeRecTab[i];

		if (nwk_route_rec_used(entry) && entry->nwkAddr == nwkAddr) {
			return entry;
		}
	}

	return NULL;
}

bool nwkRouteRecTabPathMatch(nwk_routeRecordTabEntry_t *entry, uint16_t nwkAddr)
{
	if (!nwk_route_rec_used(entry)) {
		return FALSE;
	}

	if (entry->nwkAddr == nwkAddr) {
		return TRUE;
	}

	for (uint8_t i = 0; i < entry->relayCnt && i < g_zbInfo.nwkNib.maxSourceRoute; i++) {
		if (entry->path[i] == nwkAddr) {
			return TRUE;
		}
	}

	return FALSE;
}

nwk_routeRecordTabEntry_t *nwkRouteRecTabEntryAddNew(uint16_t nwkAddr, nwkCmd_t *cmd)
{
	nwk_routeRecordTabEntry_t *entry = nwkRouteRecTabEntryFreeGet();
	uint8_t relayCnt;

	if (entry == NULL || cmd == NULL) {
		return NULL;
	}

	relayCnt = cmd->rrec.relayCnt;
	entry->nwkAddr = nwkAddr;
	entry->relayCnt = relayCnt;

	if (relayCnt != 0U && cmd->rrec.relayList != NULL) {
		memcpy(entry->path, cmd->rrec.relayList, (size_t)relayCnt * sizeof(uint16_t));
	}

	entry->used = 1;
	entry->forgetCnt = 0;
	return entry;
}

nwk_routeRecordTabEntry_t *nwkRouteRecTabEntryCreat(uint16_t nwkAddr, nwkCmd_t *cmd)
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

			for (uint16_t i = 1; i < NWK_ROUTE_RECORD_TABLE_SIZE; i++) {
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
				       (size_t)entry->relayCnt * sizeof(uint16_t));
			}
		}

		return entry;
	}

	entry->relayCnt = cmd->rrec.relayCnt;
	entry->forgetCnt = 0;
	if (entry->relayCnt != 0U && cmd->rrec.relayList != NULL) {
		memcpy(entry->path, cmd->rrec.relayList, (size_t)entry->relayCnt * sizeof(uint16_t));
	}

	return entry;
}

void nwkRouteRecTabEntryDstDel(uint16_t dstAddr)
{
	for (uint16_t i = 0; i < NWK_ROUTE_RECORD_TABLE_SIZE; i++) {
		nwk_routeRecordTabEntry_t *entry = &g_routeRecTab[i];

		if (nwkRouteRecTabPathMatch(entry, dstAddr)) {
			nwkRouteRecTabEntryClear(entry);
		}
	}
}

#endif

int nwkRoutingTabPeriodic(void *arg)
{
	uint16_t activeNum = 0;
#if defined(ZB_COORDINATOR_ROLE)
	uint16_t routeRecordActiveNum = 0;
#endif

	(void)arg;

	/* The vendor library counts the ACTIVE entries first and returns without
	 * touching anything when there are none. */
	for (uint16_t i = 0; i < ROUTING_TABLE_SIZE; i++) {
		if (g_routingTab[i].status == NWK_ROUTE_STATE_ACTIVE) {
			activeNum++;
		}
	}

	/* The ageing body is inlined in the vendor object and only bumps the
	 * routing-entry forget counter; it does not call nwkRouteMaintenance
	 * (which is a two-argument route-repair helper there, invoked from
	 * zb_MacMcpsDataConfirmHandler). */
	if (activeNum != 0U) {
		for (uint16_t i = 0; i < ROUTING_TABLE_SIZE; i++) {
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
	for (uint16_t i = 0; i < NWK_ROUTE_RECORD_TABLE_SIZE; i++) {
		if (g_routeRecTab[i].used) {
			routeRecordActiveNum++;
		}
	}

	if (routeRecordActiveNum != 0U) {
		for (uint16_t i = 0; i < NWK_ROUTE_RECORD_TABLE_SIZE; i++) {
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
