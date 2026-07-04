/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Neighbor / addition-neighbor table maintenance for the router build.
 *
 * Adapted from libzigbee/src/nwk_neighbor.c. The vendor file is ~670
 * LOC and tightly coupled to the rest of the libzigbee runtime — most
 * notably to nwk_addr_map.c (tl_zbshortAddrByIdx, tl_idxByShortAddr,
 * …), aps_bindingTblExist, rf_lqi2cost, nwk_endDev_timeout, and the
 * vendor NV index/itemIfno_t persistence layout.
 *
 * This port intentionally keeps the SUBSET that other already-ported
 * TUs reference (nwk_brc.c, the upcoming nwk_formation.c):
 *
 *   * table reset / free-list management
 *   * row counters (normalNeighborNum, childrenNum, router-valid count)
 *   * forward iterators used by broadcast retransmit / formation
 *   * addition-neighbor table API consumed by formation channel/PAN
 *     selection
 *
 * Symbols that depend on the address map (tl_zbNeighborTableSearchFromExtAddr,
 * tl_zbNeighborTableUpdate full path, tl_zbNeighborTableDelete, the
 * NV-restore tl_zbNeighborTableInit, ZBHCI-shaped tl_childNodesListGet)
 * are deferred to the address-map port that lands separately. Their
 * absence is fine — the static-formation router does not yet touch
 * them, and --gc-sections drops the unused entry points from the
 * binary.
 */

#include "zb_common_stub.h"
#include "mac/includes/mac_phy.h"
#include "nwk/includes/nwk.h"
#include "nwk/includes/nwk_addr_map.h"
#include "nwk/includes/nwk_neighbor.h"

#include <stdbool.h>
#include <string.h>

#if defined(ZB_ROUTER_ROLE) && ZB_ROUTER_ROLE

static inline bool neighbor_entry_used(const tl_zb_normal_neighbor_entry_t *entry)
{
	return (entry != NULL) && (entry->used != 0);
}

static inline bool neighbor_is_child_rel(u8 relationship)
{
	return (relationship == NEIGHBOR_IS_CHILD) ||
	       (relationship == NEIGHBOR_IS_UNAUTH_CHILD);
}

static u8 neighbor_active_count_update(void)
{
	u8 count = 0;
	tl_zb_normal_neighbor_entry_t *entry = g_zb_neighborTbl.activeHead;

	while (neighbor_entry_used(entry)) {
		count++;
		entry = entry->activeNext;
	}

	g_zb_neighborTbl.normalNeighborNum = count;
	return count;
}

void tl_nebListAdd(u8 freeList, tl_zb_normal_neighbor_entry_t *entry)
{
	if (freeList) {
		entry->freeNext = g_zb_neighborTbl.freeHead;
		g_zb_neighborTbl.freeHead = entry;
	} else {
		entry->activeNext = g_zb_neighborTbl.activeHead;
		g_zb_neighborTbl.activeHead = entry;
	}
}

void tl_nebListDelete(u8 freeList, tl_zb_normal_neighbor_entry_t *entry)
{
	tl_zb_normal_neighbor_entry_t **head;
	tl_zb_normal_neighbor_entry_t *prev = NULL;
	tl_zb_normal_neighbor_entry_t *cur;

	if (entry == NULL) {
		return;
	}

	head = freeList ? &g_zb_neighborTbl.freeHead : &g_zb_neighborTbl.activeHead;
	cur = *head;

	while (cur != NULL) {
		tl_zb_normal_neighbor_entry_t *next =
			freeList ? cur->freeNext : cur->activeNext;

		if (cur == entry) {
			if (prev == NULL) {
				*head = next;
			} else if (freeList) {
				prev->freeNext = next;
			} else {
				prev->activeNext = next;
			}
			return;
		}

		prev = cur;
		cur = next;
	}
}

void tl_zbNeighborTableRst(void)
{
	memset(&g_zb_neighborTbl, 0, sizeof(g_zb_neighborTbl));
	g_zb_neighborTbl.activeHead = NULL;

	if (TL_ZB_NEIGHBOR_TABLE_SIZE != 0U) {
		g_zb_neighborTbl.freeHead = &g_zb_neighborTbl.neighborTbl[0];

		for (u8 i = 0; i < TL_ZB_NEIGHBOR_TABLE_SIZE; i++) {
			tl_zb_normal_neighbor_entry_t *entry = &g_zb_neighborTbl.neighborTbl[i];

			entry->relationship = NEIGHBOR_IS_NONE_OF_ABOVE;
			entry->used = 0;
			entry->activeNext = NULL;
			entry->freeNext =
				(i + 1U < TL_ZB_NEIGHBOR_TABLE_SIZE)
					? &g_zb_neighborTbl.neighborTbl[i + 1U]
					: NULL;
		}
	}

	for (u8 i = 0; i < TL_ZB_ADDITION_NEIGHBOR_TABLE_SIZE; i++) {
		g_zb_neighborTbl.additionNeighborTbl[i].shortAddr = 0xffffU;
		memset(g_zb_neighborTbl.additionNeighborTbl[i].extAddr, 0xff, EXT_ADDR_LEN);
	}

	g_zb_neighborTbl.additionNeighborNum = 0;
}

tl_zb_normal_neighbor_entry_t *tl_zbNeighborFreeEntryGet(void)
{
	tl_zb_normal_neighbor_entry_t *entry = g_zb_neighborTbl.freeHead;

	if (entry == NULL) {
		return NULL;
	}

	if (entry->used) {
		return NULL;
	}

	entry->relationship = NEIGHBOR_IS_NONE_OF_ABOVE;
	return entry;
}

u8 tl_zbNeighborTableNumGet(void)
{
	return neighbor_active_count_update();
}

u8 tl_zbNeighborTableChildEDNumGet(void)
{
	u8 count = 0;
	tl_zb_normal_neighbor_entry_t *entry = g_zb_neighborTbl.activeHead;

	while (neighbor_entry_used(entry)) {
		if ((entry->deviceType == NWK_DEVICE_TYPE_ED) &&
		    neighbor_is_child_rel(entry->relationship)) {
			count++;
		}
		entry = entry->activeNext;
	}

	g_zb_neighborTbl.childrenNum = count;
	return count;
}

u8 tl_zbNeighborTableRouterValidNumGet(void)
{
	u8 count = 0;
	tl_zb_normal_neighbor_entry_t *entry = g_zb_neighborTbl.activeHead;

	while (neighbor_entry_used(entry)) {
		if (((entry->deviceType == NWK_DEVICE_TYPE_COORDINATOR) ||
		     (entry->deviceType == NWK_DEVICE_TYPE_ROUTER)) &&
		    (entry->outgoingCost != 0U)) {
			count++;
		}
		entry = entry->activeNext;
	}

	return count;
}

tl_zb_normal_neighbor_entry_t *tl_zbNeighborTableSearchForParent(void)
{
	tl_zb_normal_neighbor_entry_t *entry = g_zb_neighborTbl.activeHead;

	while (neighbor_entry_used(entry)) {
		if (entry->relationship == NEIGHBOR_IS_PARENT) {
			return entry;
		}
		entry = entry->activeNext;
	}

	return NULL;
}

tl_zb_normal_neighbor_entry_t *tl_zbNeighborTabSearchForChildEndDev(void *entry)
{
	tl_zb_normal_neighbor_entry_t *cur =
		(entry == NULL)
			? g_zb_neighborTbl.activeHead
			: ((tl_zb_normal_neighbor_entry_t *)entry)->activeNext;

	while (neighbor_entry_used(cur)) {
		if ((cur->deviceType == NWK_DEVICE_TYPE_ED) &&
		    neighbor_is_child_rel(cur->relationship)) {
			return cur;
		}
		cur = cur->activeNext;
	}

	return NULL;
}

tl_zb_normal_neighbor_entry_t *tl_zbNeighborTabSearchForRouter(void *entry)
{
	tl_zb_normal_neighbor_entry_t *cur =
		(entry == NULL)
			? g_zb_neighborTbl.activeHead
			: ((tl_zb_normal_neighbor_entry_t *)entry)->activeNext;

	while (neighbor_entry_used(cur)) {
		if (cur->deviceType == NWK_DEVICE_TYPE_ROUTER) {
			return cur;
		}
		cur = cur->activeNext;
	}

	return NULL;
}

tl_zb_normal_neighbor_entry_t *tl_zbNeighborTableSearchFromAddrmapIdx(u16 idx)
{
	tl_zb_normal_neighbor_entry_t *entry = g_zb_neighborTbl.activeHead;

	while (neighbor_entry_used(entry)) {
		if (entry->addrmapIdx == idx) {
			return entry;
		}
		entry = entry->activeNext;
	}

	return NULL;
}

tl_zb_normal_neighbor_entry_t *tl_zbNeighborEntryGetFromIdx(u8 idx)
{
	tl_zb_normal_neighbor_entry_t *entry = g_zb_neighborTbl.activeHead;
	u8 i = 0;

	while (neighbor_entry_used(entry)) {
		if (i == idx) {
			return entry;
		}
		i++;
		entry = entry->activeNext;
	}

	return NULL;
}

tl_zb_normal_neighbor_entry_t *nwk_neTblGetByShortAddr(u16 shortAddr)
{
	u16 idx;

	if (tl_idxByShortAddr(&idx, shortAddr) != RET_OK) {
		return NULL;
	}

	return tl_zbNeighborTableSearchFromAddrmapIdx(idx);
}

tl_zb_normal_neighbor_entry_t *nwk_neTblGetByExtAddr(addrExt_t extAddr)
{
	u16 idx;

	if (tl_idxByExtAddr(&idx, extAddr) != RET_OK) {
		return NULL;
	}

	return tl_zbNeighborTableSearchFromAddrmapIdx(idx);
}

tl_zb_normal_neighbor_entry_t *nwkValidNeighborToFwd(u16 shortAddr)
{
	tl_zb_normal_neighbor_entry_t *entry = nwk_neTblGetByShortAddr(shortAddr);
	u8 outgoingCost;
	u8 cost;

	if (entry == NULL) {
		return NULL;
	}

	if ((entry->deviceType == NWK_DEVICE_TYPE_ED) &&
	    (entry->relationship == NEIGHBOR_IS_CHILD)) {
		return entry;
	}

	outgoingCost = entry->outgoingCost;
	if (outgoingCost == 0U) {
		return NULL;
	}

	cost = rf_lqi2cost(entry->lqi);
	if (cost < outgoingCost) {
		cost = outgoingCost;
	}

	return (cost < NWK_NEIGHBOR_SEND_OUTGOING_THRESHOLD) ? entry : NULL;
}

/* ------------------------------------------------------------------ */
/* Addition-neighbor table (beacon-scan accumulator) — used by formation. */
/* ------------------------------------------------------------------ */

void tl_zbAdditionNeighborReset(void)
{
	for (u8 i = 0; i < TL_ZB_ADDITION_NEIGHBOR_TABLE_SIZE; i++) {
		g_zb_neighborTbl.additionNeighborTbl[i].shortAddr = 0xffffU;
		memset(g_zb_neighborTbl.additionNeighborTbl[i].extAddr, 0xff, EXT_ADDR_LEN);
	}

	g_zb_neighborTbl.additionNeighborNum = 0;
}

tl_zb_addition_neighbor_entry_t *AdditionNeighborEntryGetFromExtAddr(
	const tl_zb_addition_neighbor_entry_t *key)
{
	for (u8 i = 0; i < g_zb_neighborTbl.additionNeighborNum; i++) {
		tl_zb_addition_neighbor_entry_t *entry =
			&g_zb_neighborTbl.additionNeighborTbl[i];

		if ((memcmp(entry->extAddr, key->extAddr, EXT_ADDR_LEN) == 0) &&
		    (memcmp(entry->extPanId, key->extPanId, EXT_ADDR_LEN) == 0)) {
			return entry;
		}
	}

	return NULL;
}

tl_zb_addition_neighbor_entry_t *AdditionNeighborEntryGetFromShortAddr(
	const tl_zb_addition_neighbor_entry_t *key)
{
	for (u8 i = 0; i < g_zb_neighborTbl.additionNeighborNum; i++) {
		tl_zb_addition_neighbor_entry_t *entry =
			&g_zb_neighborTbl.additionNeighborTbl[i];

		if ((entry->shortAddr == key->shortAddr) &&
		    (memcmp(entry->extPanId, key->extPanId, EXT_ADDR_LEN) == 0)) {
			return entry;
		}
	}

	return NULL;
}

u8 tl_zbAdditionNeighborTableUpdate(tl_zb_addition_neighbor_entry_t *entry)
{
	tl_zb_addition_neighbor_entry_t *dst = NULL;

	if (entry->addrMode == ADDR_MODE_SHORT) {
		dst = AdditionNeighborEntryGetFromShortAddr(entry);
	} else if (entry->addrMode == ADDR_MODE_EXT) {
		dst = AdditionNeighborEntryGetFromExtAddr(entry);
	}

	if (dst == NULL) {
		if (g_zb_neighborTbl.additionNeighborNum <
		    TL_ZB_ADDITION_NEIGHBOR_TABLE_SIZE) {
			dst = &g_zb_neighborTbl.additionNeighborTbl
				       [g_zb_neighborTbl.additionNeighborNum++];
		} else {
			u8 depth = entry->depth;

			for (u8 i = 0; i < TL_ZB_ADDITION_NEIGHBOR_TABLE_SIZE; i++) {
				tl_zb_addition_neighbor_entry_t *cur =
					&g_zb_neighborTbl.additionNeighborTbl[i];

				if (cur->depth > depth) {
					dst = cur;
					break;
				}
			}

			if (dst == NULL) {
				return 0xc7;
			}
		}
	}

	memcpy(dst, entry, sizeof(*entry));
	return RET_OK;
}

u8 tl_zbAdditionNeighborTableNumGet(void)
{
	return g_zb_neighborTbl.additionNeighborNum;
}

tl_zb_addition_neighbor_entry_t *tl_zbAdditionNeighborEntryGetFromIdx(u8 idx)
{
	return &g_zb_neighborTbl.additionNeighborTbl[idx];
}

tl_zb_addition_neighbor_entry_t *AdditionNeighborEntryGetFromExtPanId(extPANId_t extPanId)
{
	for (u8 i = 0; i < g_zb_neighborTbl.additionNeighborNum; i++) {
		tl_zb_addition_neighbor_entry_t *entry =
			&g_zb_neighborTbl.additionNeighborTbl[i];

		if ((memcmp(entry->extPanId, extPanId, EXT_ADDR_LEN) == 0) &&
		    entry->permitJoining && entry->potentialParent &&
		    (entry->lqi == 0U)) {
			return entry;
		}
	}

	return NULL;
}

u8 tl_neighborFrameCntReset(void)
{
	for (u8 i = 0; i < TL_ZB_NEIGHBOR_TABLE_SIZE; i++) {
		g_zb_neighborTbl.neighborTbl[i].incomingFrameCnt = 0;
	}

	return 0;
}

/* Stub for the vendor NV-restore entrypoint: the Zephyr port does
 * its own NVS restore from zb_persistence_zephyr.c, so all this
 * stub needs to do is clear the in-RAM table. The full vendor
 * implementation will land when the address-map NV layout is ported.
 */
void tl_zbNeighborTableInit(void)
{
	tl_zbNeighborTableRst();
}

/*
 * Address-map-dependent neighbor-table operations, ported from libzigbee
 * src/nwk_neighbor.c. These used to be weak single-slot stubs in
 * zb_primitive_dispatch.c, which made tl_zbNeighborTableUpdate() "succeed"
 * without ever storing a retrievable entry — so a router that parented a child
 * could not later look the child up (nwk_neTblGetByExtAddr -> NULL), breaking
 * the APS Tunnel transport-key relay to a device joining THROUGH the router.
 * These strong definitions override the weak stubs.
 */
extern u8 aps_bindingTblExist(addrExt_t extAddr);

static void neighbor_entry_copy_state(tl_zb_normal_neighbor_entry_t *dst,
				      const tl_zb_normal_neighbor_entry_t *src)
{
	enum {
		NEIGHBOR_COPY_OFFSET = OFFSETOF(tl_zb_normal_neighbor_entry_t, authTimeout),
		NEIGHBOR_COPY_SIZE = sizeof(tl_zb_normal_neighbor_entry_t) - NEIGHBOR_COPY_OFFSET,
	};

	memcpy((u8 *)dst + NEIGHBOR_COPY_OFFSET, (const u8 *)src + NEIGHBOR_COPY_OFFSET,
	       NEIGHBOR_COPY_SIZE);
}

tl_zb_normal_neighbor_entry_t *tl_zbNeighborTableSearchFromExtAddr(u16 *shortAddr,
								   addrExt_t extAddr, u16 *idx)
{
	u16 addrmapIdx;
	u16 *idxOut = (idx != NULL) ? idx : &addrmapIdx;

	if (tl_zbShortAddrByExtAddr(shortAddr, extAddr, idxOut) != RET_OK) {
		return NULL;
	}

	return tl_zbNeighborTableSearchFromAddrmapIdx(*idxOut);
}

void tl_zbNeighborTableDelete(tl_zb_normal_neighbor_entry_t *entry)
{
	addrExt_t extAddr;

	if (entry == NULL) {
		return;
	}

	if (neighbor_is_child_rel(entry->relationship)) {
		g_sysDiags.neighborRemoved++;
	}

	if (g_zb_neighborTbl.normalNeighborNum == 0U) {
		ZB_EXCEPTION_POST(SYS_EXCEPTTION_ZB_NWK_NEIGHBOR_TABLE);
	}

	tl_zbExtAddrByIdx(entry->addrmapIdx, extAddr);
	if (!aps_bindingTblExist(extAddr)) {
		tl_zbNwkAddrMapDelete(entry->addrmapIdx);
	}

	entry->relationship = NEIGHBOR_IS_NONE_OF_ABOVE;
	entry->used = 0;
	entry->age = 0;

	tl_nebListDelete(0, entry);
	tl_nebListAdd(1, entry);

	neighbor_active_count_update();
	(void)tl_zbNeighborTableChildEDNumGet();
}

u8 tl_zbNeighborTableDeleteAuto(u8 delete_flag)
{
	tl_zb_normal_neighbor_entry_t *bestNoCost = NULL;
	tl_zb_normal_neighbor_entry_t *bestWithCost = NULL;
	tl_zb_normal_neighbor_entry_t *entry = g_zb_neighborTbl.activeHead;

	while (neighbor_entry_used(entry)) {
		if ((entry->relationship != NEIGHBOR_IS_PARENT) &&
		    (entry->relationship != NEIGHBOR_IS_CHILD) &&
		    (entry->relationship != NEIGHBOR_IS_UNAUTH_CHILD)) {
			if (entry->outgoingCost != 0U) {
				if (delete_flag &&
				    ((bestWithCost == NULL) || (bestWithCost->lqi > entry->lqi))) {
					bestWithCost = entry;
				}
			} else if (entry->lqi == 0U) {
				tl_zbNeighborTableDelete(entry);
				return 1;
			} else if ((bestNoCost == NULL) || (bestNoCost->lqi > entry->lqi)) {
				bestNoCost = entry;
			}
		}

		entry = entry->activeNext;
	}

	if (bestNoCost != NULL) {
		tl_zbNeighborTableDelete(bestNoCost);
		return 1;
	}

	if (bestWithCost != NULL) {
		tl_zbNeighborTableDelete(bestWithCost);
		return 1;
	}

	return 0;
}

tl_zb_normal_neighbor_entry_t *tl_zbNeighborTableUpdate(tl_zb_normal_neighbor_entry_t *entry,
							u8 delete_flag)
{
	tl_zb_normal_neighbor_entry_t *match;
	tl_zb_normal_neighbor_entry_t *freeEntry;

	if ((entry == NULL) || (entry->lqi < NWK_NEIGHBORTBL_ADD_LQITHRESHOLD)) {
		return NULL;
	}

	match = tl_zbNeighborTableSearchFromAddrmapIdx(entry->addrmapIdx);
	if (match != NULL) {
		neighbor_entry_copy_state(match, entry);
		match->used = 1;
		return match;
	}

	if ((entry->deviceType == NWK_DEVICE_TYPE_ED) &&
	    (tl_zbNeighborTableChildEDNumGet() >= TL_ZB_CHILD_TABLE_SIZE)) {
		return NULL;
	}

	if (neighbor_active_count_update() >= TL_ZB_NEIGHBOR_TABLE_SIZE) {
		if (!tl_zbNeighborTableDeleteAuto(delete_flag)) {
			return NULL;
		}
	}

	freeEntry = tl_zbNeighborFreeEntryGet();
	if (freeEntry == NULL) {
		return NULL;
	}

	neighbor_entry_copy_state(freeEntry, entry);
	freeEntry->used = 1;
	tl_nebListDelete(1, freeEntry);
	tl_nebListAdd(0, freeEntry);

	g_sysDiags.neighborAdded++;
	(void)tl_zbNeighborTableChildEDNumGet();
	neighbor_active_count_update();

	return freeEntry;
}

#endif /* ZB_ROUTER_ROLE */
