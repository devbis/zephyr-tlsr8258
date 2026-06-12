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

void nwkRouteRepair(u16 dstAddr)
{
	nwk_routingTabEntry_t *entry = nwkRoutingTabEntryFind(dstAddr);

	if (entry != NULL) {
		entry->status = NWK_ROUTE_STATE_DISCOVERY_FAILED;
	}

	g_zbNwkCtx.manyToOneRepair.nwkFwdDstAddr = dstAddr;
	g_zbNwkCtx.manyToOneRepair.nwkFwdSrcAddr = g_zbNIB.nwkAddr;

	if (g_zbNwkCtx.joined) {
		zb_buf_t *buf = (zb_buf_t *)ev_buf_allocate(LARGE_BUFFER);

		if (buf != NULL) {
			tl_zbNwkNlmeNwkStatusInd(buf, dstAddr,
						 NWK_COMMAND_STATUS_NO_ROUTE_AVAILABLE);
		}
	}
}

void nwkRouteMaintenance(nwk_routingTabEntry_t *entry)
{
	if (!nwk_routing_entry_active(entry)) {
		return;
	}

	if (entry->transFail >= NWKC_TRANSFAILURE_CNT_THRESHOLD) {
		entry->status = NWK_ROUTE_STATE_DISCOVERY_FAILED;
		return;
	}

	if (entry->status != NWK_ROUTE_STATE_ACTIVE) {
		if (entry->forgetCnt < 0xffU) {
			entry->forgetCnt++;
		}

		if (entry->forgetCnt >= 3U) {
			nwkRoutingTabEntryClear(entry);
		}
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
	ARG_UNUSED(arg);

	for (u16 i = 0; i < ROUTING_TABLE_SIZE; i++) {
		nwkRouteMaintenance(&g_routingTab[i]);
	}

	return 0;
}

#endif /* ZB_ROUTER_ROLE */
