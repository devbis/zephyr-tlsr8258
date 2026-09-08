/* SPDX-License-Identifier: Apache-2.0 */
/*
 * NWK routing table maintenance for the router build.
 *
 * Adapted from libzigbee/src/nwk_routing.c (~230 LOC). The vendor file
 * is kept structurally bit-for-bit. Adaptations:
 *
 *   * vendor "zb_local.h" → local zb_common_stub.h + nwk header set
 *   * vendor RAM-code attributes dropped (Zephyr-linker handles
 *     section placement)
 *   * NWKC_TRANSFAILURE_CNT_THRESHOLD / NWKC_INITIAL_RREQ_RETRIES /
 *     NWKC_RREQ_RETRIES / ROUTING_TABLE_SIZE / g_routingTab[] are
 *     defined in subsys/zigbee/common/zb_config.c (SDK copy); this
 *     file just consumes them via the externs in nwk.h.
 *
 * The table is now in the binary but no caller invokes the helpers
 * yet (the static-formation router doesn't forward frames). The data
 * structure is ready for the NWK forwarding path port that follows.
 */

#include "zb_common_stub.h"
#include "nwk/includes/nwk.h"
#include "nwk/includes/nwk_internal.h"

#include <stdbool.h>
#include <string.h>

#if defined(ZB_ROUTER_ROLE) && ZB_ROUTER_ROLE

static bool nwk_routing_entry_active(const nwk_routingTabEntry_t *entry)
{
	return entry != NULL &&
	       entry->status != NWK_ROUTE_STATE_DISCOVERY_INACTIVE &&
	       entry->dstAddr != NWK_BROADCAST_RESERVED;
}

void nwkRoutingTabEntryClear(nwk_routingTabEntry_t *entry)
{
	if (entry == NULL) {
		return;
	}

	memset(entry, 0, sizeof(*entry));
	entry->dstAddr = NWK_BROADCAST_RESERVED;
	entry->nextHopAddr = MAC_SHORT_ADDR_BROADCAST;
	entry->status = NWK_ROUTE_STATE_DISCOVERY_INACTIVE;
}

void nwkRoutingTabInit(void)
{
	for (u16 i = 0; i < ROUTING_TABLE_SIZE; i++) {
		nwkRoutingTabEntryClear(&g_routingTab[i]);
	}
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
	return nwkRoutingTabEntryFind(dstAddr);
}

nwk_routingTabEntry_t *nwkRoutingTabEntryCreate(u16 dstAddr)
{
	nwk_routingTabEntry_t *candidate = NULL;

	if (dstAddr == g_zbNIB.nwkAddr || ZB_NWK_IS_ADDRESS_BROADCAST(dstAddr)) {
		return NULL;
	}

	candidate = nwkRoutingTabEntryFind(dstAddr);
	if (candidate != NULL) {
		return candidate;
	}

	for (u16 i = 0; i < ROUTING_TABLE_SIZE; i++) {
		if (!nwk_routing_entry_active(&g_routingTab[i])) {
			candidate = &g_routingTab[i];
			break;
		}
	}

	if (candidate == NULL) {
		candidate = &g_routingTab[0];
	}

	nwkRoutingTabEntryClear(candidate);
	candidate->dstAddr = dstAddr;
	candidate->status = NWK_ROUTE_STATE_DISCOVERY_INACTIVE;
	return candidate;
}

u16 nwkRoutingTabGetNextHop(nwk_routingTabEntry_t *entry)
{
	if (!nwk_routing_entry_active(entry)) {
		return MAC_ADDR_USE_EXT;
	}

	if (entry->nextHopAddr == NWK_BROADCAST_RESERVED ||
	    entry->nextHopAddr == MAC_SHORT_ADDR_BROADCAST) {
		return MAC_ADDR_USE_EXT;
	}

	return entry->nextHopAddr;
}

void nwkRoutingTabEntryDstDel(u16 dstAddr)
{
	nwk_routingTabEntry_t *entry = nwkRoutingTabEntryFind(dstAddr);

	if (entry != NULL) {
		nwkRoutingTabEntryClear(entry);
	}
}

void nwkRouteRepair(zb_buf_t *buf, u16 dstAddr, u16 statusDstAddr, u8 statusCode)
{
	nwk_hdr_t hdr;
	nwkCmd_t cmd;
	u8 security = 0U;

	memset(&hdr, 0, sizeof(hdr));
	memset(&cmd, 0, sizeof(cmd));

	cmd.cmdId = NWK_CMD_NETWORK_STATUS;
	cmd.nwkStatus.statusCode = statusCode;
	cmd.nwkStatus.dstAddr = statusDstAddr;

	if ((ss_ib.secureAllFresh != 0U) && (ss_ib.securityLevel != 0U) &&
	    ss_keyPreconfigured()) {
		security = 1U;
	}

	hdr.frameControl.frameType = FRAME_TYPE_COMMAND;
	hdr.frameControl.protocolVer = ZB_PROTOCOL_VERSION;
	hdr.frameControl.srcIEEEAddr = 1U;
	hdr.frameControl.security = security;
	memcpy(hdr.srcIeeeAddr, g_zbMacPib.extAddress, EXT_ADDR_LEN);
	hdr.dstAddr = dstAddr;
	hdr.srcAddr = g_zbNIB.nwkAddr;
	hdr.radius = (u8)(g_zbNIB.maxDepth << 1);
	hdr.seqNum = g_zbNIB.seqNum++;
	hdr.frameHdrLen = getNwkHdrSize(&hdr);

	tl_zbNwkSendNwkStatusCmd(buf, &hdr, &cmd,
					NWK_INTERNAL_NETWORK_STATUS_CMD_HANDLE);
}

void nwkRouteMaintenance(nwk_hdr_t *pNwkHdr, u16 macDstAddr)
{
	nwk_routingTabEntry_t *entry = NULL;
	u16 repairDst;
	u16 statusDst;
	u8 statusCode;
	zb_buf_t *buf;

	if (pNwkHdr == NULL) {
		return;
	}

	if (pNwkHdr->frameControl.srcRoute) {
		repairDst = pNwkHdr->srcAddr;
		statusDst = pNwkHdr->dstAddr;
		statusCode = NWK_COMMAND_STATUS_SOURCE_ROUTE_FAILURE;
	} else {
		for (u16 i = 0U; i < ROUTING_TABLE_SIZE; i++) {
			if ((g_routingTab[i].status == NWK_ROUTE_STATE_ACTIVE) &&
			    (g_routingTab[i].dstAddr == pNwkHdr->dstAddr)) {
				entry = &g_routingTab[i];
				break;
			}
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
	nwk_routingTabEntry_t *entry = nwkRoutingTabEntryFind(dstAddr);

	if (entry != NULL) {
		entry->routeRecordRequired = 0;
	}
}

u16 nwkSrcRouteReplayNextHop(srcRouteSubframe_t *subframe)
{
	if (subframe == NULL || subframe->relayList == NULL ||
	    subframe->relayIdx >= subframe->relayCnt) {
		return MAC_ADDR_USE_EXT;
	}

	return (u16)subframe->relayList[subframe->relayIdx];
}

u8 nwkSourceRoutePacketRelayFilter(nwk_hdr_t *pNwkHdr)
{
	if (pNwkHdr == NULL) {
		return 0;
	}

	return pNwkHdr->frameControl.srcRoute ? 1U : 0U;
}

int nwkRoutingTabPeriodic(void *arg)
{
	u16 active_num = 0U;

	ARG_UNUSED(arg);

	for (u16 i = 0; i < ROUTING_TABLE_SIZE; i++) {
		if (g_routingTab[i].status == NWK_ROUTE_STATE_ACTIVE) {
			active_num++;
		}
	}

	if (active_num == 0U) {
		return 0;
	}

	for (u16 i = 0; i < ROUTING_TABLE_SIZE; i++) {
		nwk_routingTabEntry_t *entry = &g_routingTab[i];

		if ((entry->status == NWK_ROUTE_STATE_ACTIVE) && (entry->manyToOne == 0U) &&
		    (entry->forgetCnt != 0xffU)) {
			entry->forgetCnt++;
		}
	}

	return 0;
}

#endif /* ZB_ROUTER_ROLE */
