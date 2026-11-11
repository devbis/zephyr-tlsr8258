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

static inline void neighbor_entry_copy_state(zb_normal_neighbor_entry_t *dst,
					     const zb_normal_neighbor_entry_t *src)
{
	enum {
		NEIGHBOR_COPY_OFFSET = OFFSETOF(zb_normal_neighbor_entry_t, authTimeout),
		NEIGHBOR_COPY_SIZE = sizeof(zb_normal_neighbor_entry_t) - NEIGHBOR_COPY_OFFSET,
	};

	memcpy((uint8_t *)dst + NEIGHBOR_COPY_OFFSET, (const uint8_t *)src + NEIGHBOR_COPY_OFFSET,
	       NEIGHBOR_COPY_SIZE);
}

static inline bool neighbor_entry_used(const zb_normal_neighbor_entry_t *entry)
{
	return (entry != NULL) && (entry->used != 0);
}

static inline bool neighbor_is_child_rel(uint8_t relationship)
{
	return (relationship == NEIGHBOR_IS_CHILD) || (relationship == NEIGHBOR_IS_UNAUTH_CHILD);
}

static inline uint8_t neighbor_active_count_update(void)
{
	uint8_t count = 0;
	zb_normal_neighbor_entry_t *entry = g_zb_neighborTbl.activeHead;

	while (neighbor_entry_used(entry)) {
		count++;
		entry = entry->activeNext;
	}

	g_zb_neighborTbl.normalNeighborNum = count;
	return count;
}

void zb_nebListAdd(uint8_t freeList, zb_normal_neighbor_entry_t *entry)
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
void zb_nebListDelete(uint8_t freeList, zb_normal_neighbor_entry_t *entry)
{
	zb_normal_neighbor_entry_t *cur;
	zb_normal_neighbor_entry_t *next;

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

void zb_NeighborTableRst(void)
{
	memset(&g_zb_neighborTbl, 0, neighborTblSizeGet());
	g_zb_neighborTbl.activeHead = NULL;
	g_zb_neighborTbl.freeHead = &g_zb_neighborTbl.neighborTbl[0];

	{
		zb_normal_neighbor_entry_t *entry = &g_zb_neighborTbl.neighborTbl[0];

		/* The chaining loop is skipped for a
		 * one-entry table and only the last entry is terminated. */
		if (ZB_NEIGHBOR_TABLE_SIZE != 1U) {
			for (uint8_t i = 0; (uint8_t)(ZB_NEIGHBOR_TABLE_SIZE - 1U) > i; i++) {
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

	for (uint8_t i = 0; i < ZB_ADDITION_NEIGHBOR_TABLE_SIZE; i++) {
		g_zb_neighborTbl.additionNeighborTbl[i].shortAddr = MAC_SHORT_ADDR_NONE;
		ZB_IEEE_ADDR_INVALID(g_zb_neighborTbl.additionNeighborTbl[i].extAddr);
	}
}

zb_normal_neighbor_entry_t *zb_NeighborFreeEntryGet(void)
{
	zb_normal_neighbor_entry_t *entry = g_zb_neighborTbl.freeHead;

	if (entry == NULL) {
		return NULL;
	}

	if (entry->used) {
		return NULL;
	}

	entry->relationship = NEIGHBOR_IS_NONE_OF_ABOVE;
	return entry;
}

uint8_t zb_NeighborTableNumGet(void)
{
	return neighbor_active_count_update();
}

uint8_t zb_NeighborTableChildEDNumGet(void)
{
	uint8_t count = 0;
	zb_normal_neighbor_entry_t *entry = g_zb_neighborTbl.activeHead;

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

uint8_t zb_NeighborTableRouterValidNumGet(void)
{
	uint8_t count = 0;
	zb_normal_neighbor_entry_t *entry = g_zb_neighborTbl.activeHead;

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

zb_normal_neighbor_entry_t *zb_NeighborTableSearchForParent(void)
{
	zb_normal_neighbor_entry_t *entry = g_zb_neighborTbl.activeHead;

	while (neighbor_entry_used(entry)) {
		if (entry->relationship == NEIGHBOR_IS_PARENT) {
			return entry;
		}
		entry = entry->activeNext;
	}

	return NULL;
}

zb_normal_neighbor_entry_t *zb_NeighborTabSearchForChildEndDev(void *entry)
{
	zb_normal_neighbor_entry_t *cur =
		(entry == NULL) ? g_zb_neighborTbl.activeHead
				: ((zb_normal_neighbor_entry_t *)entry)->activeNext;

	while (neighbor_entry_used(cur)) {
		if ((cur->deviceType == NWK_DEVICE_TYPE_ED) &&
		    neighbor_is_child_rel(cur->relationship)) {
			return cur;
		}
		cur = cur->activeNext;
	}

	return NULL;
}

zb_normal_neighbor_entry_t *zb_NeighborTabSearchForRouter(void *entry)
{
	zb_normal_neighbor_entry_t *cur =
		(entry == NULL) ? g_zb_neighborTbl.activeHead
				: ((zb_normal_neighbor_entry_t *)entry)->activeNext;

	while (neighbor_entry_used(cur)) {
		if (cur->deviceType == NWK_DEVICE_TYPE_ROUTER) {
			return cur;
		}
		cur = cur->activeNext;
	}

	return NULL;
}

/* Every vendor lookup carries its own copy of this walk. */
static inline zb_normal_neighbor_entry_t *neighbor_by_addrmap_idx(uint16_t idx)
{
	zb_normal_neighbor_entry_t *entry = g_zb_neighborTbl.activeHead;

	while (neighbor_entry_used(entry)) {
		if (entry->addrmapIdx == idx) {
			return entry;
		}
		entry = entry->activeNext;
	}

	return NULL;
}

zb_normal_neighbor_entry_t *zb_NeighborTableSearchFromAddrmapIdx(uint16_t idx)
{
	return neighbor_by_addrmap_idx(idx);
}

zb_normal_neighbor_entry_t *zb_NeighborEntryGetFromIdx(uint8_t idx)
{
	zb_normal_neighbor_entry_t *entry = g_zb_neighborTbl.activeHead;
	uint8_t i = 0;

	while (neighbor_entry_used(entry)) {
		if (i == idx) {
			return entry;
		}
		i++;
		entry = entry->activeNext;
	}

	return NULL;
}

uint16_t zb_NeighborParentShortAddrGet(void)
{
	zb_normal_neighbor_entry_t *parent = zb_NeighborTableSearchForParent();

	return (parent != NULL) ? zb_shortAddrByIdx(parent->addrmapIdx) : MAC_SHORT_ADDR_NONE;
}

zb_normal_neighbor_entry_t *zb_NeighborTableSearchFromExtAddr(uint16_t *shortAddr,
								   addrExt_t extAddr, uint16_t *idx)
{
	if (zb_ShortAddrByExtAddr(shortAddr, extAddr, idx) != RET_OK) {
		return NULL;
	}

	return neighbor_by_addrmap_idx(*idx);
}

zb_normal_neighbor_entry_t *zb_NeighborTableSearchFromShortAddr(uint16_t shortAddr,
								     addrExt_t extAddr, uint16_t *idx)
{
	if (zb_ExtAddrByShortAddr(shortAddr, extAddr, idx) != RET_OK) {
		return NULL;
	}

	return neighbor_by_addrmap_idx(*idx);
}

zb_normal_neighbor_entry_t *nwk_neTblGetByShortAddr(uint16_t shortAddr)
{
	uint16_t idx = 0;

	if (zb_idxByShortAddr(&idx, shortAddr) != RET_OK) {
		return NULL;
	}

	return neighbor_by_addrmap_idx(idx);
}

zb_normal_neighbor_entry_t *nwk_neTblGetByExtAddr(addrExt_t extAddr)
{
	uint16_t idx = 0;

	if (zb_idxByExtAddr(&idx, extAddr) != RET_OK) {
		return NULL;
	}

	return neighbor_by_addrmap_idx(idx);
}

zb_normal_neighbor_entry_t *nwkValidNeighborToFwd(uint16_t shortAddr)
{
	zb_normal_neighbor_entry_t *entry = nwk_neTblGetByShortAddr(shortAddr);
	uint8_t outgoingCost;
	uint8_t cost;

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

void zb_NeighborTableDelete(zb_normal_neighbor_entry_t *entry)
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

	zb_ExtAddrByIdx(entry->addrmapIdx, extAddr);
	if (!aps_bindingTblExist(extAddr)) {
		zb_NwkAddrMapDelete(entry->addrmapIdx);
	}

	entry->relationship = NEIGHBOR_IS_NONE_OF_ABOVE;
	entry->used = 0;
	entry->transFailure = 0;

	zb_nebListDelete(0, entry);
	zb_nebListAdd(1, entry);

	neighbor_active_count_update();
	(void)zb_NeighborTableChildEDNumGet();
}

uint8_t zb_nwkNeighborDeleteByAddrmapIdx(uint16_t idx)
{
	zb_normal_neighbor_entry_t *entry = zb_NeighborTableSearchFromAddrmapIdx(idx);

	if (entry == NULL) {
		return 0;
	}

	zb_NeighborTableDelete(entry);
	return 1;
}

/*
 * Frees one neighbour entry and returns it, or NULL when nothing can be
 * evicted.  Reconstructed from _router/nwk_neighbor.s - the reconstruction
 * returned a bool, excluded NEIGHBOR_IS_PARENT (the vendor does not) and was
 * missing the routing-table check entirely.
 */
zb_normal_neighbor_entry_t *zb_NeighborTableDeleteAuto(uint8_t delete)
{
	zb_normal_neighbor_entry_t *bestNoCost = NULL;
	zb_normal_neighbor_entry_t *bestWithCost = NULL;
	zb_normal_neighbor_entry_t *entry = g_zb_neighborTbl.activeHead;
	zb_normal_neighbor_entry_t *victim = NULL;

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
		    nwkRoutingTabEntryFind(zb_shortAddrByIdx(entry->addrmapIdx)) != NULL) {
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
		zb_NeighborTableDelete(victim);
	}

	return victim;
}

zb_normal_neighbor_entry_t *zb_NeighborTableUpdate(zb_normal_neighbor_entry_t *entry,
							uint8_t delete)
{
	zb_normal_neighbor_entry_t *match;
	zb_normal_neighbor_entry_t *freeEntry;

	if (entry->lqi < NWK_NEIGHBORTBL_ADD_LQITHRESHOLD) {
		return NULL;
	}

	match = zb_NeighborTableSearchFromAddrmapIdx(entry->addrmapIdx);
	if (match != NULL) {
		neighbor_entry_copy_state(match, entry);
		match->used = 1;
		return match;
	}

	if ((entry->deviceType == NWK_DEVICE_TYPE_ED) &&
	    (zb_NeighborTableChildEDNumGet() >= ZB_CHILD_TABLE_SIZE)) {
		return NULL;
	}

	if (neighbor_active_count_update() >= ZB_NEIGHBOR_TABLE_SIZE) {
		if (zb_NeighborTableDeleteAuto(delete) == NULL) {
			return NULL;
		}
	}

	freeEntry = zb_NeighborFreeEntryGet();
	if (freeEntry == NULL) {
		return NULL;
	}

	neighbor_entry_copy_state(freeEntry, entry);
	freeEntry->used = 1;
	zb_nebListDelete(1, freeEntry);
	zb_nebListAdd(0, freeEntry);

	g_sysDiags.neighborAdded++;
	(void)zb_NeighborTableChildEDNumGet();
	neighbor_active_count_update();

	return freeEntry;
}

void zb_AdditionNeighborReset(void)
{
	for (uint8_t i = 0; i < ZB_ADDITION_NEIGHBOR_TABLE_SIZE; i++) {
		g_zb_neighborTbl.additionNeighborTbl[i].shortAddr = MAC_SHORT_ADDR_NONE;
		ZB_IEEE_ADDR_INVALID(g_zb_neighborTbl.additionNeighborTbl[i].extAddr);
	}

	g_zb_neighborTbl.additionNeighborNum = 0;
}

zb_addition_neighbor_entry_t *
AdditionNeighborEntryGetFromExtAddr(const zb_addition_neighbor_entry_t *key)
{
	for (uint8_t i = 0; i < g_zb_neighborTbl.additionNeighborNum; i++) {
		zb_addition_neighbor_entry_t *entry = &g_zb_neighborTbl.additionNeighborTbl[i];

		if (ZB_IEEE_ADDR_CMP(entry->extAddr, key->extAddr) &&
		    ZB_EXTPANID_CMP(entry->extPanId, key->extPanId)) {
			return entry;
		}
	}

	return NULL;
}

zb_addition_neighbor_entry_t *
AdditionNeighborEntryGetFromShortAddr(const zb_addition_neighbor_entry_t *key)
{
	for (uint8_t i = 0; i < g_zb_neighborTbl.additionNeighborNum; i++) {
		zb_addition_neighbor_entry_t *entry = &g_zb_neighborTbl.additionNeighborTbl[i];

		if ((entry->shortAddr == key->shortAddr) &&
		    ZB_EXTPANID_CMP(entry->extPanId, key->extPanId)) {
			return entry;
		}
	}

	return NULL;
}

uint8_t zb_AdditionNeighborTableUpdate(zb_addition_neighbor_entry_t *entry)
{
	zb_addition_neighbor_entry_t *dst = NULL;

	if (entry->addrMode == ADDR_MODE_SHORT) {
		dst = AdditionNeighborEntryGetFromShortAddr(entry);
	} else if (entry->addrMode == ADDR_MODE_EXT) {
		dst = AdditionNeighborEntryGetFromExtAddr(entry);
	}

	if (dst == NULL) {
		if (g_zb_neighborTbl.additionNeighborNum < ZB_ADDITION_NEIGHBOR_TABLE_SIZE) {
			dst = &g_zb_neighborTbl
				       .additionNeighborTbl[g_zb_neighborTbl.additionNeighborNum++];
		} else {
			uint8_t depth = entry->depth;

			for (uint8_t i = 0; i < ZB_ADDITION_NEIGHBOR_TABLE_SIZE; i++) {
				zb_addition_neighbor_entry_t *cur =
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

uint8_t zb_AdditionNeighborTableNumGet(void)
{
	return g_zb_neighborTbl.additionNeighborNum;
}

zb_addition_neighbor_entry_t *zb_AdditionNeighborEntryGetFromIdx(uint8_t idx)
{
	return &g_zb_neighborTbl.additionNeighborTbl[idx];
}

zb_addition_neighbor_entry_t *AdditionNeighborEntryGetFromExtPanId(extPANId_t extPanId)
{
	for (uint8_t i = 0; i < g_zb_neighborTbl.additionNeighborNum; i++) {
		zb_addition_neighbor_entry_t *entry = &g_zb_neighborTbl.additionNeighborTbl[i];

		/* The entry
		 * is returned when lqi is non-zero. */
		if (ZB_EXTPANID_CMP(entry->extPanId, extPanId) && entry->permitJoining &&
		    entry->potentialParent && (entry->lqi != 0U)) {
			return entry;
		}
	}

	return NULL;
}

uint8_t zb_neighborFrameCntReset(void)
{
	for (uint8_t i = 0; i < ZB_NEIGHBOR_TABLE_SIZE; i++) {
		g_zb_neighborTbl.neighborTbl[i].incomingFrameCnt = 0;
	}

	return 0;
}

void zb_NeighborTableInit(void)
{
	itemIfno_t itemInfo;
	zb_addrForNeighbor_t addrNv;
	zb_normal_neighbor_entry_t neighbor;

	memset(&itemInfo, 0, sizeof(itemInfo));
	zb_NeighborTableRst();

	memset(&neighbor, 0, sizeof(neighbor));

	/* Both passes read their index out of NV_MODULE_ADDRESS_TABLE, not
	 * NV_MODULE_ZB_INFO. */
	if (nv_flashReadNew(0, NV_MODULE_ADDRESS_TABLE, ITEM_FIELD_IDLE,
			    sizeof(zb_addrForNeighbor_t), (uint8_t *)&itemInfo) == NV_SUCC) {
		for (uint16_t i = 0; i <= itemInfo.opIndex; i++) {
			zb_normal_neighbor_entry_t *nbe;

			if (nv_flashReadByIndex(NV_MODULE_ADDRESS_TABLE,
						NV_ITEM_ADDRESS_FOR_NEIGHBOR, itemInfo.opSect, i,
						sizeof(zb_addrForNeighbor_t),
						(uint8_t *)&addrNv) != NV_SUCC) {
				continue;
			}

			(void)zb_NwkAddrMapAdd(addrNv.shortAddr, addrNv.extAddr,
						 &neighbor.addrmapIdx);
			/* The vendor stores the successful NV-read status here, not the
			 * return value from zb_NwkAddrMapAdd(). */
			neighbor.incomingFrameCnt = 0;
			neighbor.rxOnWhileIdle = addrNv.rxOnWhileIdle;
			neighbor.relationship = addrNv.relationship;
			neighbor.deviceType = addrNv.deviceType;
			neighbor.depth = addrNv.depth;

			if (neighbor.deviceType == NWK_DEVICE_TYPE_ED) {
				uint32_t timeout =
					g_zbInfo.nwkNib.endDevTimeoutDefault
						? (60UL << g_zbInfo.nwkNib.endDevTimeoutDefault)
						: 10UL;

				neighbor.timeoutCnt = timeout;
				neighbor.devTimeout = timeout;
			}

			neighbor.lqi = (uint8_t)(NWK_NEIGHBORTBL_ADD_LQITHRESHOLD + 1U);
			neighbor.outgoingCost = 0;
			neighbor.used = 1;

			nbe = zb_NeighborTableUpdate(&neighbor, 0);
			if (nbe != NULL) {
				nbe->lqi = 0;
			}
		}
	}

	/* Router roles also perform a second pass over stored end-device timeouts
	 * ("114:".."188:").  The end-device vendor object ends after the first
	 * neighbor pass. */
#if defined(ZB_ROUTER_ROLE)
	/* Note what the vendor object actually does with the value it reads: it
	 * writes timeoutCnt/devTimeout into the stack scratch left over from the
	 * first loop, not into the table entry nwk_neTblGetByExtAddr just returned.
	 * The stored timeouts are therefore discarded and every restored end device
	 * keeps the default computed above.  Reproduced as-is. */
	{
		nwk_endDevTimeout_nv_t timeoutInfo;

		memset(&timeoutInfo, 0, sizeof(timeoutInfo));
		memset(&itemInfo, 0, sizeof(itemInfo));

		if (nv_flashReadNew(0, NV_MODULE_ADDRESS_TABLE, ITEM_FIELD_IDLE,
				    sizeof(timeoutInfo), (uint8_t *)&itemInfo) != NV_SUCC) {
			return;
		}

		for (uint16_t i = 0; i <= itemInfo.opIndex; i++) {
			zb_normal_neighbor_entry_t *nbe;

			if (nv_flashReadByIndex(NV_MODULE_ADDRESS_TABLE, NV_ITEM_ED_TIMEOUT,
						itemInfo.opSect, i, sizeof(timeoutInfo),
						(uint8_t *)&timeoutInfo) != NV_SUCC) {
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
	for (uint8_t i = 0; i < ZB_NEIGHBOR_TABLE_SIZE; i++) {
		zb_normal_neighbor_entry_t *entry = &g_zb_neighborTbl.neighborTbl[i];

		/* The vendor tests the used bit before checking the parent record's
		 * type, relationship, and timeout fields. */
		if (!entry->used || (entry->deviceType != NWK_DEVICE_TYPE_ED) ||
		    (entry->relationship != NEIGHBOR_IS_CHILD) || (entry->devTimeout == 0U)) {
			continue;
		}

		if (entry->timeoutCnt != 0U) {
			entry->timeoutCnt--;
			if (entry->timeoutCnt != 0U) {
				continue;
			}
		}

		zb_TaskPost((zb_callback_t)nwkEndDevTimeoutRejoin, NULL);
		/* The vendor returns after posting the first rejoin request in a pass. */
		return;
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
	for (uint8_t i = 0; i < ZB_NEIGHBOR_TABLE_SIZE; i++) {
		zb_normal_neighbor_entry_t *entry = &g_zb_neighborTbl.neighborTbl[i];
		zb_addrForNeighbor_t rec;
		uint32_t *counter;

		if (!entry->used) {
			continue;
		}

		if (entry->relationship == NEIGHBOR_IS_UNAUTH_CHILD) {
			/* An unauthenticated child is on the
			 * authentication timer. */
			counter = &entry->authTimeout;
		} else if ((entry->deviceType == NWK_DEVICE_TYPE_ED) &&
			   (entry->relationship == NEIGHBOR_IS_CHILD)) {
			/* deviceType and relationship tested together
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
		rec.shortAddr = zb_shortAddrByIdx(entry->addrmapIdx);
		zb_ExtAddrByIdx(entry->addrmapIdx, rec.extAddr);
		(void)nwk_nodeAddrInfoDelete(&rec);
		zb_NeighborTableDelete(entry);
	}
}
#endif

void zb_allChildNodesRemove(void)
{
	zb_normal_neighbor_entry_t *entry = NULL;

	while ((entry = zb_NeighborTabSearchForChildEndDev(entry)) != NULL) {
		zb_addrForNeighbor_t addrInfo;

		/* Only these three fields are filled in; nwk_nodeAddrInfoDelete()
		 * matches on the extended address and the relationship. */
		addrInfo.shortAddr = zb_shortAddrByIdx(entry->addrmapIdx);
		zb_ExtAddrByIdx(entry->addrmapIdx, addrInfo.extAddr);
		addrInfo.relationship = entry->relationship;

		(void)nwk_nodeAddrInfoDelete(&addrInfo);
		zb_NeighborTableDelete(entry);
		/* The search resumes from the entry that was
		 * just removed rather than restarting at the head. */
	}
}

void zb_childNodesListGet(uint8_t startIdx, nwk_childTableInfo_t *t)
{
	zb_normal_neighbor_entry_t *entry = NULL;
	uint8_t skipped = 0;

	if (t == NULL) {
		return;
	}

	/* The vendor initializes the result header only; unused list slots retain
	 * their previous contents. */
	t->info.status = NWK_CHILD_NODES_STATUS_SUCCESS;
	t->info.totalChildNodesNum = zb_NeighborTableChildEDNumGet();
	t->info.startIdx = startIdx;
	t->info.childNodesNum = 0;

	while ((entry = zb_NeighborTabSearchForChildEndDev(entry)) != NULL) {
		uint8_t outIdx;

		if (skipped < startIdx) {
			skipped++;
			continue;
		}

		outIdx = t->info.childNodesNum;
		zb_ExtAddrByIdx(entry->addrmapIdx, t->list[outIdx].extAddr);
		t->list[outIdx].nwkAddr = zb_shortAddrByIdx(entry->addrmapIdx);
		t->info.childNodesNum++;

		if (t->info.childNodesNum >= ZBHCI_CHILD_LIST_NUM_MAX) {
			break;
		}
	}
}
