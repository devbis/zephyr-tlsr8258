/*
 * Copyright 2026 libzigbee
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "zb_common.h"
#include "zb_initialize.h"
#include "nwk_endDev_timeout.h"
#include "zb_nwk_addr_map.h"

/*
 * Reconstructed from _router/nwk_addr_map.s.
 *
 * g_nwkAddrMap is two intrusive singly-linked lists over one array:
 *
 *   freeHead   -> entry->freeNext   -> ...   (entries available for reuse)
 *   activeHead -> entry->activeNext -> ...   (entries currently mapped)
 *
 * The lookups walk the *active* list, not the array.  The vendor's
 * zb_ShortAddrByExtAddr is the one exception to the used-bit check: it
 * compares every node until the active-list link is NULL.  The other lookup
 * variants stop at the first entry whose used bit is clear.  The table index
 * handed back to callers is recomputed from the entry address, which is why
 * the vendor object calls __udivsi3 with a divisor of 20
 * (sizeof(zb_addr_map_entry_t)).
 *
 * The list selector of zb_addrMapListAdd/Delete is 0 for the active list and
 * non-zero for the free list.
 */

#define ADDR_MAP_LIST_ACTIVE 0
#define ADDR_MAP_LIST_FREE   1

static uint16_t addr_map_idx_of(const zb_addr_map_entry_t *entry)
{
	return (uint16_t)(entry - &g_nwkAddrMap.addrMap[0]);
}

void zb_addrMapListAdd(uint8_t freeList, zb_addr_map_entry_t *entry)
{
	if (freeList) {
		entry->freeNext = g_nwkAddrMap.freeHead;
		g_nwkAddrMap.freeHead = entry;
	} else {
		entry->activeNext = g_nwkAddrMap.activeHead;
		g_nwkAddrMap.activeHead = entry;
	}
}

void zb_addrMapListDelete(uint8_t freeList, zb_addr_map_entry_t *entry)
{
	zb_addr_map_entry_t *cur;
	zb_addr_map_entry_t *next;

	if (freeList) {
		cur = g_nwkAddrMap.freeHead;

		if (cur == entry) {
			g_nwkAddrMap.freeHead = entry->freeNext;
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
		cur = g_nwkAddrMap.activeHead;

		if (cur == entry) {
			g_nwkAddrMap.activeHead = entry->activeNext;
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

void zb_NwkAddrMapRst(void)
{
	zb_addr_map_entry_t *entry;

	memset(&g_nwkAddrMap, 0, addrMapTblSizeGet());

	g_nwkAddrMap.activeHead = NULL;
	g_nwkAddrMap.freeHead = &g_nwkAddrMap.addrMap[0];

	entry = &g_nwkAddrMap.addrMap[0];

	/* The comparison is signed, so a table size of 0 or 1 skips the chaining
	 * loop entirely and only the first entry is terminated. */
	if ((int16_t)ZB_NWK_ADDR_MAP_SIZE > 1) {
		for (uint16_t i = 0; i < (uint16_t)(ZB_NWK_ADDR_MAP_SIZE - 1U); i++) {
			entry->used = 0;
			entry->activeNext = NULL;
			entry->freeNext = entry + 1;
			entry++;
		}
	}

	entry->used = 0;
	entry->freeNext = NULL;
	entry->activeNext = NULL;

	g_nwkAddrMap.validNum = 0;
}

void zb_NwkAddrMapInit(void)
{
	zb_NwkAddrMapRst();
}

void zb_NwkAddrMapDelete(uint16_t idx)
{
	zb_addr_map_entry_t *entry = &g_nwkAddrMap.addrMap[idx];

	if (!entry->used) {
		return;
	}

	/* The entry keeps its addresses; only the flags and the list membership
	 * change: used and bind are cleared, then the entry is unlinked. */
	entry->used = 0;
	entry->bind = 0;
	zb_addrMapListDelete(ADDR_MAP_LIST_ACTIVE, entry);

	entry->freeNext = g_nwkAddrMap.freeHead;
	g_nwkAddrMap.freeHead = entry;
	g_nwkAddrMap.validNum--;
}

uint8_t zb_ShortAddrByExtAddr(uint16_t *shortAddr, addrExt_t extAddr, uint16_t *idx)
{
	for (zb_addr_map_entry_t *e = g_nwkAddrMap.activeHead; e != NULL; e = e->activeNext) {
		if (ZB_IEEE_ADDR_CMP(e->extAddr, extAddr)) {
			*shortAddr = e->shortAddr;
			*idx = addr_map_idx_of(e);
			return RET_OK;
		}
	}

	return 0xffU;
}

uint8_t zb_ExtAddrByShortAddr(uint16_t shortAddr, addrExt_t extAddr, uint16_t *idx)
{
	for (zb_addr_map_entry_t *e = g_nwkAddrMap.activeHead; e != NULL; e = e->activeNext) {
		if (!e->used) {
			break;
		}

		if (e->shortAddr == shortAddr) {
			ZB_IEEE_ADDR_COPY(extAddr, e->extAddr);
			*idx = addr_map_idx_of(e);
			return RET_OK;
		}
	}

	return 0xffU;
}

addrExt_t *zb_ExtAddrPtrByShortAddr(uint16_t shortAddr)
{
	for (zb_addr_map_entry_t *e = g_nwkAddrMap.activeHead; e != NULL; e = e->activeNext) {
		if (!e->used) {
			break;
		}

		if (e->shortAddr == shortAddr) {
			return &e->extAddr;
		}
	}

	return NULL;
}

void zb_ExtAddrByIdx(uint16_t idx, addrExt_t extAddr)
{
	ZB_IEEE_ADDR_COPY(extAddr, g_nwkAddrMap.addrMap[idx].extAddr);
}

uint16_t zb_shortAddrByIdx(uint16_t idx)
{
	return g_nwkAddrMap.addrMap[idx].shortAddr;
}

uint8_t zb_idxByShortAddr(uint16_t *idx, uint16_t shortAddr)
{
	for (zb_addr_map_entry_t *e = g_nwkAddrMap.activeHead; e != NULL; e = e->activeNext) {
		if (!e->used) {
			break;
		}

		if (e->shortAddr == shortAddr) {
			*idx = addr_map_idx_of(e);
			return RET_OK;
		}
	}

	return 0xffU;
}

uint8_t zb_idxByExtAddr(uint16_t *idx, addrExt_t extAddr)
{
	for (zb_addr_map_entry_t *e = g_nwkAddrMap.activeHead; e != NULL; e = e->activeNext) {
		if (!e->used) {
			break;
		}

		if (ZB_IEEE_ADDR_CMP(e->extAddr, extAddr)) {
			*idx = addr_map_idx_of(e);
			return RET_OK;
		}
	}

	return 0xffU;
}

zb_nwk_status_t zb_NwkAddrMapAdd(uint16_t shortAddr, addrExt_t extAddr, uint16_t *ref)
{
	uint16_t idxShort = 0;
	uint16_t idxExt = 0;
	uint8_t stsShort = zb_idxByShortAddr(&idxShort, shortAddr);
	uint8_t stsExt = zb_idxByExtAddr(&idxExt, extAddr);
	uint16_t idx = idxShort;
	zb_addr_map_entry_t *entry;

	*ref = idxShort;

	if (stsExt == RET_OK) {
		/* The extended address wins; a stale short-address-only entry for the
		 * same node is dropped ("4c:".."56:"). */
		if ((stsShort == RET_OK) && (idxShort != idxExt)) {
			*ref = idxExt;
			zb_NwkAddrMapDelete(idxShort);
		}

		idx = idxExt;
		*ref = idx;
	} else if (stsShort == RET_OK) {
		/* Known short address with no extended address stored yet: fill it in.
		 * Otherwise this is a different node and needs its own entry. */
		if (!ZB_IEEE_ADDR_IS_ZERO(g_nwkAddrMap.addrMap[idx].extAddr)) {
			goto allocate;
		}
	} else {
		goto allocate;
	}

	ZB_IEEE_ADDR_COPY(g_nwkAddrMap.addrMap[idx].extAddr, extAddr);
	g_nwkAddrMap.addrMap[idx].shortAddr = shortAddr;

	return NWK_STATUS_SUCCESS;

allocate:
	entry = g_nwkAddrMap.freeHead;

	if ((entry == NULL) || entry->used) {
		return NWK_STATUS_NEIGHBOR_TABLE_FULL;
	}

	g_nwkAddrMap.validNum++;
	entry->used = 1;
	entry->shortAddr = shortAddr;

	if (extAddr != NULL) {
		ZB_IEEE_ADDR_COPY(entry->extAddr, extAddr);
	} else {
		ZB_IEEE_ADDR_ZERO(entry->extAddr);
	}

	entry->aps_dup_cnt = 0;
	entry->aps_dup_clock = 0;
	*ref = addr_map_idx_of(entry);

	zb_addrMapListDelete(ADDR_MAP_LIST_FREE, entry);
	entry->activeNext = g_nwkAddrMap.activeHead;
	g_nwkAddrMap.activeHead = entry;

	return NWK_STATUS_SUCCESS;
}

uint8_t zb_addrByShort(uint16_t shortAddr, uint8_t addIfMissing, uint8_t unused, uint16_t *idx)
{
	(void)unused;

	if (idx == NULL) {
		ZB_EXCEPTION_POST(SYS_EXCEPTTION_ZB_NWK_ADDR_IDX);
		return 0xffU;
	}

	if (zb_idxByShortAddr(idx, shortAddr) == RET_OK) {
		return RET_OK;
	}

	if (!addIfMissing) {
		return 0xffU;
	}

	return (uint8_t)zb_NwkAddrMapAdd(shortAddr, (uint8_t *)g_zero_addr, idx);
}

void zb_addrMappingForBind(uint8_t bind, uint16_t idx)
{
	zb_addr_map_entry_t *entry;

	if (ZB_NWK_ADDR_MAP_SIZE <= idx) {
		return;
	}

	entry = &g_nwkAddrMap.addrMap[idx];

	if (!entry->used) {
		return;
	}

	entry->bind = bind & 1U;
}

uint8_t zb_address_ieee_by_short(uint16_t short_addr, addrExt_t ieee_address)
{
	uint16_t idx;

	/* The vendor tail-calls the lookup, so the caller sees 0 on success and
	 * 0xff on failure - not a boolean. */
	return zb_ExtAddrByShortAddr(short_addr, ieee_address, &idx);
}

zb_addr_map_entry_t *zb_NwkAddrMapEntryGet(uint16_t orderIndex)
{
	zb_addr_map_entry_t *e = g_nwkAddrMap.activeHead;
	uint16_t i = 0;

	if (e == NULL) {
		return NULL;
	}

	while (orderIndex != i) {
		e = e->activeNext;

		if (e == NULL) {
			return NULL;
		}

		i++;
	}

	return e;
}

void nwk_nodeAddrInfoStore(void *arg)
{
	zb_addrForNeighbor_t *info = (zb_addrForNeighbor_t *)arg;
	zb_addrForNeighbor_t entry;
	itemIfno_t itemInfo;
	bool alreadyStored = FALSE;

	memset(&itemInfo, 0, sizeof(itemInfo));

	if (nv_flashReadNew(0, NV_MODULE_ADDRESS_TABLE, ITEM_FIELD_IDLE, sizeof(entry),
			    (uint8_t *)&itemInfo) == NV_SUCC) {
		for (uint16_t i = 0; i <= itemInfo.opIndex; i++) {
			if (nv_flashReadByIndex(NV_MODULE_ADDRESS_TABLE,
						NV_ITEM_ADDRESS_FOR_NEIGHBOR, itemInfo.opSect, i,
						sizeof(entry), (uint8_t *)&entry) != NV_SUCC) {
				continue;
			}

			if (ZB_IEEE_ADDR_CMP(info->extAddr, entry.extAddr)) {
				if (entry.shortAddr == info->shortAddr) {
					alreadyStored = TRUE;
					break;
				}
			} else if (entry.shortAddr != info->shortAddr) {
				continue;
			}

			/* Same node under a different short address, or the short address
			 * reused by somebody else: drop the stale record. */
			(void)nv_itemDeleteByIndex(NV_MODULE_ADDRESS_TABLE,
						   NV_ITEM_ADDRESS_FOR_NEIGHBOR, itemInfo.opSect,
						   i);
		}
	}

	if (!alreadyStored) {
		(void)nv_flashWriteNew(0, NV_MODULE_ADDRESS_TABLE, NV_ITEM_ADDRESS_FOR_NEIGHBOR,
				       sizeof(*info), (uint8_t *)info);
	}

	ev_buf_free((uint8_t *)info);
}

int32_t nwk_parentNodeInfoStore(void)
{
	zb_normal_neighbor_entry_t *parent = zb_NeighborTableSearchForParent();
	zb_addrForNeighbor_t rec;
	zb_addrForNeighbor_t entry;
	itemIfno_t itemInfo;

	memset(&itemInfo, 0, sizeof(itemInfo));

	/* Drop whatever is currently stored as the parent (relationship == 0). */
	if (nv_flashReadNew(0, NV_MODULE_ADDRESS_TABLE, ITEM_FIELD_IDLE, sizeof(entry),
			    (uint8_t *)&itemInfo) == NV_SUCC) {
		for (uint16_t i = 0; i <= itemInfo.opIndex; i++) {
			if (nv_flashReadByIndex(NV_MODULE_ADDRESS_TABLE,
						NV_ITEM_ADDRESS_FOR_NEIGHBOR, itemInfo.opSect, i,
						sizeof(entry), (uint8_t *)&entry) != NV_SUCC) {
				continue;
			}

			if (entry.relationship != 0U) {
				continue;
			}

			(void)nv_itemDeleteByIndex(NV_MODULE_ADDRESS_TABLE,
						   NV_ITEM_ADDRESS_FOR_NEIGHBOR, itemInfo.opSect,
						   i);
		}
	}

	if (parent != NULL) {
		uint16_t idx = parent->addrmapIdx;

		rec.shortAddr = g_nwkAddrMap.addrMap[idx].shortAddr;
		ZB_IEEE_ADDR_COPY(rec.extAddr, g_nwkAddrMap.addrMap[idx].extAddr);
		rec.relationship = 0;
		rec.depth = parent->depth;
		rec.deviceType = parent->deviceType;
		rec.rxOnWhileIdle = parent->rxOnWhileIdle;

		(void)nv_flashWriteNew(0, NV_MODULE_ADDRESS_TABLE, NV_ITEM_ADDRESS_FOR_NEIGHBOR,
				       sizeof(rec), (uint8_t *)&rec);
	}

	return -1;
}

int32_t nwk_nodeAddrInfoDelete(zb_addrForNeighbor_t *pAddrNv)
{
	zb_addrForNeighbor_t entry;
	itemIfno_t itemInfo;

	/* An end device also owns a timeout record; purge that first. */
	if (pAddrNv->deviceType == NWK_DEVICE_TYPE_ED) {
		nwk_endDevTimeout_nv_t timeoutInfo;

		memset(&itemInfo, 0, sizeof(itemInfo));

		if (nv_flashReadNew(0, NV_MODULE_ADDRESS_TABLE, ITEM_FIELD_IDLE,
				    sizeof(timeoutInfo), (uint8_t *)&itemInfo) == NV_SUCC) {
			for (uint16_t i = 0; i <= itemInfo.opIndex; i++) {
				if (nv_flashReadByIndex(NV_MODULE_ADDRESS_TABLE, NV_ITEM_ED_TIMEOUT,
							itemInfo.opSect, i, sizeof(timeoutInfo),
							(uint8_t *)&timeoutInfo) != NV_SUCC) {
					continue;
				}

				if (!ZB_IEEE_ADDR_CMP(pAddrNv->extAddr, timeoutInfo.extAddr)) {
					continue;
				}

				(void)nv_itemDeleteByIndex(NV_MODULE_ADDRESS_TABLE,
							   NV_ITEM_ED_TIMEOUT, itemInfo.opSect, i);
			}
		}
	}

	memset(&itemInfo, 0, sizeof(itemInfo));

	if (nv_flashReadNew(0, NV_MODULE_ADDRESS_TABLE, ITEM_FIELD_IDLE, sizeof(entry),
			    (uint8_t *)&itemInfo) != NV_SUCC) {
		return 3;
	}

	for (uint16_t i = 0; i <= itemInfo.opIndex; i++) {
		if (nv_flashReadByIndex(NV_MODULE_ADDRESS_TABLE, NV_ITEM_ADDRESS_FOR_NEIGHBOR,
					itemInfo.opSect, i, sizeof(entry),
					(uint8_t *)&entry) != NV_SUCC) {
			continue;
		}

		if (!ZB_IEEE_ADDR_CMP(pAddrNv->extAddr, entry.extAddr)) {
			continue;
		}

		if (entry.relationship != pAddrNv->relationship) {
			continue;
		}

		(void)nv_itemDeleteByIndex(NV_MODULE_ADDRESS_TABLE, NV_ITEM_ADDRESS_FOR_NEIGHBOR,
					   itemInfo.opSect, i);
		return 0;
	}

	return 3;
}

int32_t nwk_bindAddrInfoUpdate(zb_addrForBind_t *pAddrNv)
{
	zb_addrForBind_t entry;
	itemIfno_t itemInfo;

	memset(&itemInfo, 0, sizeof(itemInfo));

	if (nv_flashReadNew(0, NV_MODULE_ADDRESS_TABLE, ITEM_FIELD_IDLE, sizeof(entry),
			    (uint8_t *)&itemInfo) == NV_SUCC) {
		for (uint16_t i = 0; i <= itemInfo.opIndex; i++) {
			if (nv_flashReadByIndex(NV_MODULE_ADDRESS_TABLE, NV_ITEM_ADDRESS_FOR_BIND,
						itemInfo.opSect, i, sizeof(entry),
						(uint8_t *)&entry) != NV_SUCC) {
				continue;
			}

			if (!ZB_IEEE_ADDR_CMP(pAddrNv->dstExtAddr, entry.dstExtAddr)) {
				continue;
			}

			if (entry.mask_dstTable == pAddrNv->mask_dstTable) {
				return 0;
			}

			(void)nv_itemDeleteByIndex(NV_MODULE_ADDRESS_TABLE,
						   NV_ITEM_ADDRESS_FOR_BIND, itemInfo.opSect, i);

			if (pAddrNv->mask_dstTable == 0U) {
				return 0;
			}

			(void)nv_flashWriteNew(0, NV_MODULE_ADDRESS_TABLE, NV_ITEM_ADDRESS_FOR_BIND,
					       sizeof(*pAddrNv), (uint8_t *)pAddrNv);
			return 0;
		}
	}

	(void)nv_flashWriteNew(0, NV_MODULE_ADDRESS_TABLE, NV_ITEM_ADDRESS_FOR_BIND,
			       sizeof(*pAddrNv), (uint8_t *)pAddrNv);

	return 3;
}

int32_t nwk_bindAddrInfoDelete(zb_addrForBind_t *pAddrNv)
{
	zb_addrForBind_t entry;
	itemIfno_t itemInfo;

	memset(&itemInfo, 0, sizeof(itemInfo));

	if (nv_flashReadNew(0, NV_MODULE_ADDRESS_TABLE, ITEM_FIELD_IDLE, sizeof(entry),
			    (uint8_t *)&itemInfo) != NV_SUCC) {
		return 3;
	}

	for (uint16_t i = 0; i <= itemInfo.opIndex; i++) {
		if (nv_flashReadByIndex(NV_MODULE_ADDRESS_TABLE, NV_ITEM_ADDRESS_FOR_BIND,
					itemInfo.opSect, i, sizeof(entry),
					(uint8_t *)&entry) != NV_SUCC) {
			continue;
		}

		if (!ZB_IEEE_ADDR_CMP(pAddrNv->dstExtAddr, entry.dstExtAddr)) {
			continue;
		}

		(void)nv_itemDeleteByIndex(NV_MODULE_ADDRESS_TABLE, NV_ITEM_ADDRESS_FOR_BIND,
					   itemInfo.opSect, i);
		return 0;
	}

	return 3;
}
