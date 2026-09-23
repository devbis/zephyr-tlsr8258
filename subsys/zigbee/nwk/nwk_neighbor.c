/*
 * Copyright 2026 libzigbee
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "zb_common.h"
#include "zb_nwk_neighbor.h"
#include "zb_nwk_addr_map.h"
#include "nwk_endDev_timeout.h"
#if defined(ZB_ROUTER_ROLE) || defined(ZB_COORDINATOR_ROLE)
#include "nwk_routing.h"
#endif
#include "aps.h"
#include "aps_me.h"

enum {
	NWK_CHILD_NODES_STATUS_SUCCESS = 0,
};

static inline void neighbor_entry_copy_state(tl_zb_normal_neighbor_entry_t *dst,
					     const tl_zb_normal_neighbor_entry_t *src)
{
	enum {
		NEIGHBOR_COPY_OFFSET = OFFSETOF(tl_zb_normal_neighbor_entry_t, authTimeout),
		NEIGHBOR_COPY_SIZE = sizeof(tl_zb_normal_neighbor_entry_t) - NEIGHBOR_COPY_OFFSET,
	};

	memcpy((u8 *)dst + NEIGHBOR_COPY_OFFSET, (const u8 *)src + NEIGHBOR_COPY_OFFSET,
	       NEIGHBOR_COPY_SIZE);
}

static inline bool neighbor_entry_used(const tl_zb_normal_neighbor_entry_t *entry)
{
	return (entry != NULL) && (entry->used != 0);
}

static inline bool neighbor_is_child_rel(u8 relationship)
{
	return (relationship == NEIGHBOR_IS_CHILD) || (relationship == NEIGHBOR_IS_UNAUTH_CHILD);
}

static inline u8 neighbor_active_count_update(void)
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

/* The vendor object carries a separate walk per list ("8:" free, "48:"
 * active) rather than selecting the link field inside the loop. */
void tl_nebListDelete(u8 freeList, tl_zb_normal_neighbor_entry_t *entry)
{
	tl_zb_normal_neighbor_entry_t *cur;
	tl_zb_normal_neighbor_entry_t *next;

	if (freeList) {
		cur = g_zb_neighborTbl.freeHead;

		if (cur == entry) {
			g_zb_neighborTbl.freeHead = entry->freeNext;
			return;
		}

		for (;;) {
			next = cur->freeNext;

			if ((next == NULL) || (next == entry)) {
				break;
			}

			cur = next;
		}

		if (next != NULL) {
			cur->freeNext = entry->freeNext;
		}
	} else {
		cur = g_zb_neighborTbl.activeHead;

		if (cur == entry) {
			g_zb_neighborTbl.activeHead = entry->activeNext;
			return;
		}

		for (;;) {
			next = cur->activeNext;

			if ((next == NULL) || (next == entry)) {
				break;
			}

			cur = next;
		}

		if (next != NULL) {
			cur->activeNext = entry->activeNext;
		}
	}
}

void tl_zbNeighborTableRst(void)
{
	memset(&g_zb_neighborTbl, 0, neighborTblSizeGet());
	g_zb_neighborTbl.activeHead = NULL;
	g_zb_neighborTbl.freeHead = &g_zb_neighborTbl.neighborTbl[0];

	{
		tl_zb_normal_neighbor_entry_t *entry = &g_zb_neighborTbl.neighborTbl[0];

		/* "2c: tcmp r1,#1; 2e: tjeq 7e" - the chaining loop is skipped for a
		 * one-entry table and only the last entry is terminated. */
		if (TL_ZB_NEIGHBOR_TABLE_SIZE != 1U) {
			for (u8 i = 0; (u8)(TL_ZB_NEIGHBOR_TABLE_SIZE - 1U) > i; i++) {
				entry->relationship = NEIGHBOR_IS_NONE_OF_ABOVE;
				entry->used = 0;
				entry->activeNext = NULL;
				entry->freeNext = entry + 1;
				entry++;
			}
		}

		entry->freeNext = NULL;
		entry->used = 0;
		entry->activeNext = NULL;
	}

	for (u8 i = 0; i < TL_ZB_ADDITION_NEIGHBOR_TABLE_SIZE; i++) {
		g_zb_neighborTbl.additionNeighborTbl[i].shortAddr = MAC_SHORT_ADDR_NONE;
		ZB_IEEE_ADDR_INVALID(g_zb_neighborTbl.additionNeighborTbl[i].extAddr);
	}
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
		    (entry->lqi != 0U)) {
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
		(entry == NULL) ? g_zb_neighborTbl.activeHead
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
		(entry == NULL) ? g_zb_neighborTbl.activeHead
				: ((tl_zb_normal_neighbor_entry_t *)entry)->activeNext;

	while (neighbor_entry_used(cur)) {
		if (cur->deviceType == NWK_DEVICE_TYPE_ROUTER) {
			return cur;
		}
		cur = cur->activeNext;
	}

	return NULL;
}

/* Every vendor lookup carries its own copy of this walk. */
static inline tl_zb_normal_neighbor_entry_t *neighbor_by_addrmap_idx(u16 idx)
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

tl_zb_normal_neighbor_entry_t *tl_zbNeighborTableSearchFromAddrmapIdx(u16 idx)
{
	return neighbor_by_addrmap_idx(idx);
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

u16 tl_zbNeighborParentShortAddrGet(void)
{
	tl_zb_normal_neighbor_entry_t *parent = tl_zbNeighborTableSearchForParent();

	return (parent != NULL) ? tl_zbshortAddrByIdx(parent->addrmapIdx) : MAC_SHORT_ADDR_NONE;
}

tl_zb_normal_neighbor_entry_t *tl_zbNeighborTableSearchFromExtAddr(u16 *shortAddr,
								   addrExt_t extAddr, u16 *idx)
{
	if (tl_zbShortAddrByExtAddr(shortAddr, extAddr, idx) != RET_OK) {
		return NULL;
	}

	return neighbor_by_addrmap_idx(*idx);
}

tl_zb_normal_neighbor_entry_t *tl_zbNeighborTableSearchFromShortAddr(u16 shortAddr,
								     addrExt_t extAddr, u16 *idx)
{
	if (tl_zbExtAddrByShortAddr(shortAddr, extAddr, idx) != RET_OK) {
		return NULL;
	}

	return neighbor_by_addrmap_idx(*idx);
}

tl_zb_normal_neighbor_entry_t *nwk_neTblGetByShortAddr(u16 shortAddr)
{
	u16 idx = 0;

	if (tl_idxByShortAddr(&idx, shortAddr) != RET_OK) {
		return NULL;
	}

	return neighbor_by_addrmap_idx(idx);
}

tl_zb_normal_neighbor_entry_t *nwk_neTblGetByExtAddr(addrExt_t extAddr)
{
	u16 idx = 0;

	if (tl_idxByExtAddr(&idx, extAddr) != RET_OK) {
		return NULL;
	}

	return neighbor_by_addrmap_idx(idx);
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

void tl_zbNeighborTableDelete(tl_zb_normal_neighbor_entry_t *entry)
{
	addrExt_t extAddr;

	/* "16:" bumps g_sysDiags+58 (childMoved) only for a child, then both paths
	 * converge on "36:" which bumps g_sysDiags+52 (neighborRemoved). */
	if (neighbor_is_child_rel(entry->relationship)) {
		g_sysDiags.childMoved++;
	}

	if (g_zb_neighborTbl.normalNeighborNum == 0U) {
		ZB_EXCEPTION_POST(SYS_EXCEPTTION_ZB_NWK_NEIGHBOR_TABLE);
	}

	g_sysDiags.neighborRemoved++;

	tl_zbExtAddrByIdx(entry->addrmapIdx, extAddr);
	if (!aps_bindingTblExist(extAddr)) {
		tl_zbNwkAddrMapDelete(entry->addrmapIdx);
	}

	entry->relationship = NEIGHBOR_IS_NONE_OF_ABOVE;
	entry->used = 0;
	entry->transFailure = 0; /* "7a: tmovs r3,#41" */

	tl_nebListDelete(0, entry);
	tl_nebListAdd(1, entry);

	neighbor_active_count_update();
	(void)tl_zbNeighborTableChildEDNumGet();
}

u8 tl_nwkNeighborDeleteByAddrmapIdx(u16 idx)
{
	tl_zb_normal_neighbor_entry_t *entry = tl_zbNeighborTableSearchFromAddrmapIdx(idx);

	if (entry == NULL) {
		return 0;
	}

	tl_zbNeighborTableDelete(entry);
	return 1;
}

/*
 * Frees one neighbour entry and returns it, or NULL when nothing can be
 * evicted.  Reconstructed from _router/nwk_neighbor.s - the reconstruction
 * returned a bool, excluded NEIGHBOR_IS_PARENT (the vendor does not) and was
 * missing the routing-table check entirely.
 */
tl_zb_normal_neighbor_entry_t *tl_zbNeighborTableDeleteAuto(u8 delete)
{
	tl_zb_normal_neighbor_entry_t *bestNoCost = NULL;
	tl_zb_normal_neighbor_entry_t *bestWithCost = NULL;
	tl_zb_normal_neighbor_entry_t *entry = g_zb_neighborTbl.activeHead;
	tl_zb_normal_neighbor_entry_t *victim = NULL;

	while (neighbor_entry_used(entry)) {
		/* The end-device build never evicts a parent, child, or unauthenticated
		 * child.  The vendor router/coordinator builds instead skip a specific
		 * child entry while it is represented in the active routing table. */
#if defined(ZB_ED_ROLE)
		if (entry->relationship == NEIGHBOR_IS_PARENT ||
		    entry->relationship == NEIGHBOR_IS_CHILD ||
		    entry->relationship == NEIGHBOR_IS_UNAUTH_CHILD) {
			entry = entry->activeNext;
			continue;
		}
#else
		if (entry->rxOnWhileIdle == 0U &&
		    entry->deviceType == NWK_DEVICE_TYPE_COORDINATOR &&
		    entry->relationship == NEIGHBOR_IS_CHILD &&
		    nwkRoutingTabEntryFind(tl_zbshortAddrByIdx(entry->addrmapIdx)) != NULL) {
			entry = entry->activeNext;
			continue;
		}
#endif

		if (entry->outgoingCost != 0U) {
			if (delete &&
			    ((bestWithCost == NULL) || (entry->lqi < bestWithCost->lqi))) {
				bestWithCost = entry;
			}
		} else if (entry->lqi == 0U) {
			victim = entry;
			break;
		} else if ((bestNoCost == NULL) || (bestNoCost->lqi > entry->lqi)) {
			bestNoCost = entry;
		}

		entry = entry->activeNext;
	}

	if (victim == NULL) {
		victim = (bestNoCost != NULL) ? bestNoCost : bestWithCost;
	}

	if (victim != NULL) {
		tl_zbNeighborTableDelete(victim);
	}

	return victim;
}

tl_zb_normal_neighbor_entry_t *tl_zbNeighborTableUpdate(tl_zb_normal_neighbor_entry_t *entry,
							u8 delete)
{
	tl_zb_normal_neighbor_entry_t *match;
	tl_zb_normal_neighbor_entry_t *freeEntry;

	if (entry->lqi < NWK_NEIGHBORTBL_ADD_LQITHRESHOLD) {
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
		if (tl_zbNeighborTableDeleteAuto(delete) == NULL) {
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

void tl_zbAdditionNeighborReset(void)
{
	for (u8 i = 0; i < TL_ZB_ADDITION_NEIGHBOR_TABLE_SIZE; i++) {
		g_zb_neighborTbl.additionNeighborTbl[i].shortAddr = MAC_SHORT_ADDR_NONE;
		ZB_IEEE_ADDR_INVALID(g_zb_neighborTbl.additionNeighborTbl[i].extAddr);
	}

	g_zb_neighborTbl.additionNeighborNum = 0;
}

tl_zb_addition_neighbor_entry_t *
AdditionNeighborEntryGetFromExtAddr(const tl_zb_addition_neighbor_entry_t *key)
{
	for (u8 i = 0; i < g_zb_neighborTbl.additionNeighborNum; i++) {
		tl_zb_addition_neighbor_entry_t *entry = &g_zb_neighborTbl.additionNeighborTbl[i];

		if (ZB_IEEE_ADDR_CMP(entry->extAddr, key->extAddr) &&
		    ZB_EXTPANID_CMP(entry->extPanId, key->extPanId)) {
			return entry;
		}
	}

	return NULL;
}

tl_zb_addition_neighbor_entry_t *
AdditionNeighborEntryGetFromShortAddr(const tl_zb_addition_neighbor_entry_t *key)
{
	for (u8 i = 0; i < g_zb_neighborTbl.additionNeighborNum; i++) {
		tl_zb_addition_neighbor_entry_t *entry = &g_zb_neighborTbl.additionNeighborTbl[i];

		if ((entry->shortAddr == key->shortAddr) &&
		    ZB_EXTPANID_CMP(entry->extPanId, key->extPanId)) {
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
		if (g_zb_neighborTbl.additionNeighborNum < TL_ZB_ADDITION_NEIGHBOR_TABLE_SIZE) {
			dst = &g_zb_neighborTbl
				       .additionNeighborTbl[g_zb_neighborTbl.additionNeighborNum++];
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
		tl_zb_addition_neighbor_entry_t *entry = &g_zb_neighborTbl.additionNeighborTbl[i];

		/* "40: tloadrb r3,[r5,#25]; 42: tcmp r3,#0; 44: tjne 4e" - the entry
		 * is returned when lqi is non-zero. */
		if (ZB_EXTPANID_CMP(entry->extPanId, extPanId) && entry->permitJoining &&
		    entry->potentialParent && (entry->lqi != 0U)) {
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

void tl_zbNeighborTableInit(void)
{
	itemIfno_t itemInfo;
	zb_addrForNeighbor_t addrNv;
	tl_zb_normal_neighbor_entry_t neighbor;

	memset(&itemInfo, 0, sizeof(itemInfo));
	tl_zbNeighborTableRst();

	memset(&neighbor, 0, sizeof(neighbor));

	/* Both passes read their index out of NV_MODULE_ADDRESS_TABLE, not
	 * NV_MODULE_ZB_INFO ("34:"/"12c: tmovs r1,#1"). */
	if (nv_flashReadNew(0, NV_MODULE_ADDRESS_TABLE, ITEM_FIELD_IDLE,
			    sizeof(zb_addrForNeighbor_t), (u8 *)&itemInfo) == NV_SUCC) {
		for (u16 i = 0; i <= itemInfo.opIndex; i++) {
			tl_zb_normal_neighbor_entry_t *nbe;

			if (nv_flashReadByIndex(NV_MODULE_ADDRESS_TABLE,
						NV_ITEM_ADDRESS_FOR_NEIGHBOR, itemInfo.opSect, i,
						sizeof(zb_addrForNeighbor_t),
						(u8 *)&addrNv) != NV_SUCC) {
				continue;
			}

			(void)tl_zbNwkAddrMapAdd(addrNv.shortAddr, addrNv.extAddr,
						 &neighbor.addrmapIdx);
			/* The vendor stores the successful NV-read status here, not the
			 * return value from tl_zbNwkAddrMapAdd(). */
			neighbor.incomingFrameCnt = 0;
			neighbor.rxOnWhileIdle = addrNv.rxOnWhileIdle;
			neighbor.relationship = addrNv.relationship;
			neighbor.deviceType = addrNv.deviceType;
			neighbor.depth = addrNv.depth;

			if (neighbor.deviceType == NWK_DEVICE_TYPE_ED) {
				u32 timeout =
					g_zbInfo.nwkNib.endDevTimeoutDefault
						? (60UL << g_zbInfo.nwkNib.endDevTimeoutDefault)
						: 10UL;

				neighbor.timeoutCnt = timeout;
				neighbor.devTimeout = timeout;
			}

			neighbor.lqi = (u8)(NWK_NEIGHBORTBL_ADD_LQITHRESHOLD + 1U);
			neighbor.outgoingCost = 0;
			neighbor.used = 1;

			nbe = tl_zbNeighborTableUpdate(&neighbor, 0);
			if (nbe != NULL) {
				nbe->lqi = 0;
			}
		}
	}

	/* Router roles also perform a second pass over stored end-device timeouts
	 * ("114:".."188:").  The end-device vendor object ends after the first
	 * neighbor pass. */
#if defined(ZB_ROUTER_ROLE)
	/* Note what the vendor object actually does with the value it reads: at
	 * "17c: tadd r3,sp,#8 / 17e: tstorer r2,[r3,#16] / 180: tstorer r2,[r3,#12]"
	 * it writes timeoutCnt/devTimeout into the stack scratch left over from the
	 * first loop, not into the table entry nwk_neTblGetByExtAddr just returned.
	 * The stored timeouts are therefore discarded and every restored end device
	 * keeps the default computed above.  Reproduced as-is. */
	{
		nwk_endDevTimeout_nv_t timeoutInfo;

		memset(&timeoutInfo, 0, sizeof(timeoutInfo));
		memset(&itemInfo, 0, sizeof(itemInfo));

		if (nv_flashReadNew(0, NV_MODULE_ADDRESS_TABLE, ITEM_FIELD_IDLE,
				    sizeof(timeoutInfo), (u8 *)&itemInfo) != NV_SUCC) {
			return;
		}

		for (u16 i = 0; i <= itemInfo.opIndex; i++) {
			tl_zb_normal_neighbor_entry_t *nbe;

			if (nv_flashReadByIndex(NV_MODULE_ADDRESS_TABLE, NV_ITEM_ED_TIMEOUT,
						itemInfo.opSect, i, sizeof(timeoutInfo),
						(u8 *)&timeoutInfo) != NV_SUCC) {
				continue;
			}

			nbe = nwk_neTblGetByExtAddr(timeoutInfo.extAddr);

			if ((nbe == NULL) || (nbe->deviceType != NWK_DEVICE_TYPE_ED)) {
				continue;
			}

			neighbor.devTimeout = timeoutInfo.timeout;
			neighbor.timeoutCnt = timeoutInfo.timeout;
		}
	}
#endif
}

/* The router/coordinator object ages child timers and removes expired
 * neighbors.  The end-device object has a separate implementation: it keeps
 * the neighbor and queues a rejoin when its parent timeout reaches zero. */
#if !defined(ZB_ROUTER_ROLE)
void nwkNebManagePeriodic(void)
{
	for (u8 i = 0; i < TL_ZB_NEIGHBOR_TABLE_SIZE; i++) {
		tl_zb_normal_neighbor_entry_t *entry = &g_zb_neighborTbl.neighborTbl[i];

		if ((entry->deviceType != NWK_DEVICE_TYPE_ED) ||
		    (entry->relationship != NEIGHBOR_IS_CHILD) || (entry->devTimeout == 0U)) {
			continue;
		}

		if (entry->timeoutCnt != 0U) {
			entry->timeoutCnt--;
			if (entry->timeoutCnt != 0U) {
				continue;
			}
		}

		tl_zbTaskPost((tl_zb_callback_t)nwkEndDevTimeoutRejoin, NULL);
	}
}
#else
/*
 * Ages the two per-child timers and forgets the child when one expires.
 * Reconstructed from _router/nwk_neighbor.s:.text.nwkNebManagePeriodic - the
 * reconstruction used to decrement authTimeout for parents and post
 * nwkEndDevTimeoutRejoin, neither of which the vendor object does.
 */
void nwkNebManagePeriodic(void)
{
	for (u8 i = 0; i < TL_ZB_NEIGHBOR_TABLE_SIZE; i++) {
		tl_zb_normal_neighbor_entry_t *entry = &g_zb_neighborTbl.neighborTbl[i];
		zb_addrForNeighbor_t rec;
		u32 *counter;

		if (!entry->used) {
			continue;
		}

		if (entry->relationship == NEIGHBOR_IS_UNAUTH_CHILD) {
			/* "5c: tcmp r1,#0x50" - an unauthenticated child is on the
			 * authentication timer. */
			counter = &entry->authTimeout;
		} else if ((entry->deviceType == NWK_DEVICE_TYPE_ED) &&
			   (entry->relationship == NEIGHBOR_IS_CHILD)) {
			/* "32: tcmp r3,#20" - deviceType and relationship tested together
			 * out of the same byte. */
			if (entry->devTimeout == 0U) {
				continue;
			}

			counter = &entry->timeoutCnt;
		} else {
			continue;
		}

		if (*counter != 0U) {
			(*counter)--;

			if (*counter != 0U) {
				continue;
			}
		}

		memset(&rec, 0, sizeof(rec));
		rec.relationship = entry->relationship;
		rec.shortAddr = tl_zbshortAddrByIdx(entry->addrmapIdx);
		tl_zbExtAddrByIdx(entry->addrmapIdx, rec.extAddr);
		(void)nwk_nodeAddrInfoDelete(&rec);
		tl_zbNeighborTableDelete(entry);
	}
}
#endif

void tl_allChildNodesRemove(void)
{
	tl_zb_normal_neighbor_entry_t *entry = NULL;

	while ((entry = tl_zbNeighborTabSearchForChildEndDev(entry)) != NULL) {
		zb_addrForNeighbor_t addrInfo;

		/* Only these three fields are filled in; nwk_nodeAddrInfoDelete()
		 * matches on the extended address and the relationship. */
		addrInfo.shortAddr = tl_zbshortAddrByIdx(entry->addrmapIdx);
		tl_zbExtAddrByIdx(entry->addrmapIdx, addrInfo.extAddr);
		addrInfo.relationship = entry->relationship;

		(void)nwk_nodeAddrInfoDelete(&addrInfo);
		tl_zbNeighborTableDelete(entry);
		/* "5a: tadds r0,r4,#0" - the search resumes from the entry that was
		 * just removed rather than restarting at the head. */
	}
}

void tl_childNodesListGet(u8 startIdx, nwk_childTableInfo_t *t)
{
	tl_zb_normal_neighbor_entry_t *entry = NULL;
	u8 skipped = 0;

	if (t == NULL) {
		return;
	}

	memset(t, 0, sizeof(*t));
	t->info.status = NWK_CHILD_NODES_STATUS_SUCCESS;
	t->info.totalChildNodesNum = tl_zbNeighborTableChildEDNumGet();
	t->info.startIdx = startIdx;

	while ((entry = tl_zbNeighborTabSearchForChildEndDev(entry)) != NULL) {
		u8 outIdx;

		if (skipped < startIdx) {
			skipped++;
			continue;
		}

		outIdx = t->info.childNodesNum;
		tl_zbExtAddrByIdx(entry->addrmapIdx, t->list[outIdx].extAddr);
		t->list[outIdx].nwkAddr = tl_zbshortAddrByIdx(entry->addrmapIdx);
		t->info.childNodesNum++;

		if (t->info.childNodesNum >= ZBHCI_CHILD_LIST_NUM_MAX) {
			break;
		}
	}
}
