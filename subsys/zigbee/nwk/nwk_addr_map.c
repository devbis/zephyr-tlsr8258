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
 * tl_zbShortAddrByExtAddr is the one exception to the used-bit check: it
 * compares every node until the active-list link is NULL.  The other lookup
 * variants stop at the first entry whose used bit is clear.  The table index
 * handed back to callers is recomputed from the entry address, which is why
 * the vendor object calls __udivsi3 with a divisor of 20
 * (sizeof(tl_zb_addr_map_entry_t)).
 *
 * The list selector of tl_addrMapListAdd/Delete is 0 for the active list and
 * non-zero for the free list ("4: tjne 3a" in both).
 */

#define ADDR_MAP_LIST_ACTIVE 0
#define ADDR_MAP_LIST_FREE   1

static u16 addr_map_idx_of(const tl_zb_addr_map_entry_t *entry)
{
	return (u16)(entry - &g_nwkAddrMap.addrMap[0]);
}

void tl_addrMapListAdd(u8 freeList, tl_zb_addr_map_entry_t *entry)
{
	if (freeList) {
		entry->freeNext = g_nwkAddrMap.freeHead;
		g_nwkAddrMap.freeHead = entry;
	} else {
		entry->activeNext = g_nwkAddrMap.activeHead;
		g_nwkAddrMap.activeHead = entry;
	}
}

void tl_addrMapListDelete(u8 freeList, tl_zb_addr_map_entry_t *entry)
{
	tl_zb_addr_map_entry_t *cur;
	tl_zb_addr_map_entry_t *next;

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

void tl_zbNwkAddrMapRst(void)
{
	tl_zb_addr_map_entry_t *entry;

	memset(&g_nwkAddrMap, 0, addrMapTblSizeGet());

	g_nwkAddrMap.activeHead = NULL;
	g_nwkAddrMap.freeHead = &g_nwkAddrMap.addrMap[0];

	entry = &g_nwkAddrMap.addrMap[0];

	/* "34: tcmp r7,#1; 36: tjle 6e" - signed, so a size of 0 or 1 skips the
	 * chaining loop entirely and only the first entry is terminated. */
	if ((s16)TL_ZB_NWK_ADDR_MAP_SIZE > 1) {
		for (u16 i = 0; i < (u16)(TL_ZB_NWK_ADDR_MAP_SIZE - 1U); i++) {
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

void tl_zbNwkAddrMapInit(void)
{
	tl_zbNwkAddrMapRst();
}

void tl_zbNwkAddrMapDelete(u16 idx)
{
	tl_zb_addr_map_entry_t *entry = &g_nwkAddrMap.addrMap[idx];

	if (!entry->used) {
		return;
	}

	/* The entry keeps its addresses; only the flags and the list membership
	 * change ("24:".."30:" clears used and bind, then 36: unlinks). */
	entry->used = 0;
	entry->bind = 0;
	tl_addrMapListDelete(ADDR_MAP_LIST_ACTIVE, entry);

	entry->freeNext = g_nwkAddrMap.freeHead;
	g_nwkAddrMap.freeHead = entry;
	g_nwkAddrMap.validNum--;
}

u8 tl_zbShortAddrByExtAddr(u16 *shortAddr, addrExt_t extAddr, u16 *idx)
{
	for (tl_zb_addr_map_entry_t *e = g_nwkAddrMap.activeHead; e != NULL; e = e->activeNext) {
		if (ZB_IEEE_ADDR_CMP(e->extAddr, extAddr)) {
			*shortAddr = e->shortAddr;
			*idx = addr_map_idx_of(e);
			return RET_OK;
		}
	}

	return 0xffU;
}

u8 tl_zbExtAddrByShortAddr(u16 shortAddr, addrExt_t extAddr, u16 *idx)
{
	for (tl_zb_addr_map_entry_t *e = g_nwkAddrMap.activeHead; e != NULL; e = e->activeNext) {
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

addrExt_t *tl_zbExtAddrPtrByShortAddr(u16 shortAddr)
{
	for (tl_zb_addr_map_entry_t *e = g_nwkAddrMap.activeHead; e != NULL; e = e->activeNext) {
		if (!e->used) {
			break;
		}

		if (e->shortAddr == shortAddr) {
			return &e->extAddr;
		}
	}

	return NULL;
}

void tl_zbExtAddrByIdx(u16 idx, addrExt_t extAddr)
{
	ZB_IEEE_ADDR_COPY(extAddr, g_nwkAddrMap.addrMap[idx].extAddr);
}

u16 tl_zbshortAddrByIdx(u16 idx)
{
	return g_nwkAddrMap.addrMap[idx].shortAddr;
}

u8 tl_idxByShortAddr(u16 *idx, u16 shortAddr)
{
	for (tl_zb_addr_map_entry_t *e = g_nwkAddrMap.activeHead; e != NULL; e = e->activeNext) {
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

u8 tl_idxByExtAddr(u16 *idx, addrExt_t extAddr)
{
	for (tl_zb_addr_map_entry_t *e = g_nwkAddrMap.activeHead; e != NULL; e = e->activeNext) {
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

zb_nwk_status_t tl_zbNwkAddrMapAdd(u16 shortAddr, addrExt_t extAddr, u16 *ref)
{
	u16 idxShort = 0;
	u16 idxExt = 0;
	u8 stsShort = tl_idxByShortAddr(&idxShort, shortAddr);
	u8 stsExt = tl_idxByExtAddr(&idxExt, extAddr);
	u16 idx = idxShort;
	tl_zb_addr_map_entry_t *entry;

	*ref = idxShort;

	if (stsExt == RET_OK) {
		/* The extended address wins; a stale short-address-only entry for the
		 * same node is dropped ("4c:".."56:"). */
		if ((stsShort == RET_OK) && (idxShort != idxExt)) {
			*ref = idxExt;
			tl_zbNwkAddrMapDelete(idxShort);
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

	tl_addrMapListDelete(ADDR_MAP_LIST_FREE, entry);
	entry->activeNext = g_nwkAddrMap.activeHead;
	g_nwkAddrMap.activeHead = entry;

	return NWK_STATUS_SUCCESS;
}

u8 tl_addrByShort(u16 shortAddr, u8 addIfMissing, u8 unused, u16 *idx)
{
	(void)unused;

	if (idx == NULL) {
		ZB_EXCEPTION_POST(SYS_EXCEPTTION_ZB_NWK_ADDR_IDX);
		return 0xffU;
	}

	if (tl_idxByShortAddr(idx, shortAddr) == RET_OK) {
		return RET_OK;
	}

	if (!addIfMissing) {
		return 0xffU;
	}

	return (u8)tl_zbNwkAddrMapAdd(shortAddr, (u8 *)g_zero_addr, idx);
}

void tl_addrMappingForBind(u8 bind, u16 idx)
{
	tl_zb_addr_map_entry_t *entry;

	if (TL_ZB_NWK_ADDR_MAP_SIZE <= idx) {
		return;
	}

	entry = &g_nwkAddrMap.addrMap[idx];

	if (!entry->used) {
		return;
	}

	entry->bind = bind & 1U;
}

u8 zb_address_ieee_by_short(u16 short_addr, addrExt_t ieee_address)
{
	u16 idx;

	/* The vendor tail-calls the lookup, so the caller sees 0 on success and
	 * 0xff on failure - not a boolean. */
	return tl_zbExtAddrByShortAddr(short_addr, ieee_address, &idx);
}

tl_zb_addr_map_entry_t *tl_zbNwkAddrMapEntryGet(u16 orderIndex)
{
	tl_zb_addr_map_entry_t *e = g_nwkAddrMap.activeHead;
	u16 i = 0;

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
			    (u8 *)&itemInfo) == NV_SUCC) {
		for (u16 i = 0; i <= itemInfo.opIndex; i++) {
			if (nv_flashReadByIndex(NV_MODULE_ADDRESS_TABLE,
						NV_ITEM_ADDRESS_FOR_NEIGHBOR, itemInfo.opSect, i,
						sizeof(entry), (u8 *)&entry) != NV_SUCC) {
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
				       sizeof(*info), (u8 *)info);
	}

	ev_buf_free((u8 *)info);
}

s32 nwk_parentNodeInfoStore(void)
{
	tl_zb_normal_neighbor_entry_t *parent = tl_zbNeighborTableSearchForParent();
	zb_addrForNeighbor_t rec;
	zb_addrForNeighbor_t entry;
	itemIfno_t itemInfo;

	memset(&itemInfo, 0, sizeof(itemInfo));

	/* Drop whatever is currently stored as the parent (relationship == 0). */
	if (nv_flashReadNew(0, NV_MODULE_ADDRESS_TABLE, ITEM_FIELD_IDLE, sizeof(entry),
			    (u8 *)&itemInfo) == NV_SUCC) {
		for (u16 i = 0; i <= itemInfo.opIndex; i++) {
			if (nv_flashReadByIndex(NV_MODULE_ADDRESS_TABLE,
						NV_ITEM_ADDRESS_FOR_NEIGHBOR, itemInfo.opSect, i,
						sizeof(entry), (u8 *)&entry) != NV_SUCC) {
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
		u16 idx = parent->addrmapIdx;

		rec.shortAddr = g_nwkAddrMap.addrMap[idx].shortAddr;
		ZB_IEEE_ADDR_COPY(rec.extAddr, g_nwkAddrMap.addrMap[idx].extAddr);
		rec.relationship = 0;
		rec.depth = parent->depth;
		rec.deviceType = parent->deviceType;
		rec.rxOnWhileIdle = parent->rxOnWhileIdle;

		(void)nv_flashWriteNew(0, NV_MODULE_ADDRESS_TABLE, NV_ITEM_ADDRESS_FOR_NEIGHBOR,
				       sizeof(rec), (u8 *)&rec);
	}

	return -1;
}

s32 nwk_nodeAddrInfoDelete(zb_addrForNeighbor_t *pAddrNv)
{
	zb_addrForNeighbor_t entry;
	itemIfno_t itemInfo;

	/* An end device also owns a timeout record; purge that first. */
	if (pAddrNv->deviceType == NWK_DEVICE_TYPE_ED) {
		nwk_endDevTimeout_nv_t timeoutInfo;

		memset(&itemInfo, 0, sizeof(itemInfo));

		if (nv_flashReadNew(0, NV_MODULE_ADDRESS_TABLE, ITEM_FIELD_IDLE,
				    sizeof(timeoutInfo), (u8 *)&itemInfo) == NV_SUCC) {
			for (u16 i = 0; i <= itemInfo.opIndex; i++) {
				if (nv_flashReadByIndex(NV_MODULE_ADDRESS_TABLE, NV_ITEM_ED_TIMEOUT,
							itemInfo.opSect, i, sizeof(timeoutInfo),
							(u8 *)&timeoutInfo) != NV_SUCC) {
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
			    (u8 *)&itemInfo) != NV_SUCC) {
		return 3;
	}

	for (u16 i = 0; i <= itemInfo.opIndex; i++) {
		if (nv_flashReadByIndex(NV_MODULE_ADDRESS_TABLE, NV_ITEM_ADDRESS_FOR_NEIGHBOR,
					itemInfo.opSect, i, sizeof(entry),
					(u8 *)&entry) != NV_SUCC) {
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

s32 nwk_bindAddrInfoUpdate(zb_addrForBind_t *pAddrNv)
{
	zb_addrForBind_t entry;
	itemIfno_t itemInfo;

	memset(&itemInfo, 0, sizeof(itemInfo));

	if (nv_flashReadNew(0, NV_MODULE_ADDRESS_TABLE, ITEM_FIELD_IDLE, sizeof(entry),
			    (u8 *)&itemInfo) == NV_SUCC) {
		for (u16 i = 0; i <= itemInfo.opIndex; i++) {
			if (nv_flashReadByIndex(NV_MODULE_ADDRESS_TABLE, NV_ITEM_ADDRESS_FOR_BIND,
						itemInfo.opSect, i, sizeof(entry),
						(u8 *)&entry) != NV_SUCC) {
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
					       sizeof(*pAddrNv), (u8 *)pAddrNv);
			return 0;
		}
	}

	(void)nv_flashWriteNew(0, NV_MODULE_ADDRESS_TABLE, NV_ITEM_ADDRESS_FOR_BIND,
			       sizeof(*pAddrNv), (u8 *)pAddrNv);

	return 3;
}

s32 nwk_bindAddrInfoDelete(zb_addrForBind_t *pAddrNv)
{
	zb_addrForBind_t entry;
	itemIfno_t itemInfo;

	memset(&itemInfo, 0, sizeof(itemInfo));

	if (nv_flashReadNew(0, NV_MODULE_ADDRESS_TABLE, ITEM_FIELD_IDLE, sizeof(entry),
			    (u8 *)&itemInfo) != NV_SUCC) {
		return 3;
	}

	for (u16 i = 0; i <= itemInfo.opIndex; i++) {
		if (nv_flashReadByIndex(NV_MODULE_ADDRESS_TABLE, NV_ITEM_ADDRESS_FOR_BIND,
					itemInfo.opSect, i, sizeof(entry),
					(u8 *)&entry) != NV_SUCC) {
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
