/* SPDX-License-Identifier: Apache-2.0 */
/*
 * NWK address-mapping table.
 *
 * Adapted from libzigbee/src/nwk_addr_map.c (~245 LOC). Kept
 * structurally one-for-one with the vendor; adaptations:
 *
 *   * vendor "zb_local.h" → zb_common_stub.h + nwk/includes
 *   * g_nwkAddrMap storage / TL_ZB_NWK_ADDR_MAP_SIZE live in
 *     subsys/zigbee/common/zb_config.c (SDK copy); this TU only
 *     consumes the externs from nwk_addr_map.h
 *
 * Closes the forward-decl loop left by nwk_neighbor.c — the
 * subset-port deferred lookups that need this map.
 */

#include "zb_common_stub.h"
#include "nwk/includes/nwk.h"
#include "nwk/includes/nwk_addr_map.h"
#include "nwk/includes/nwk_internal.h"

#include <string.h>

#if (defined(ZB_ROUTER_ROLE) && ZB_ROUTER_ROLE) || defined(ZB_ED_ROLE)

enum {
	ADDR_MAP_LIST_ACTIVE,
	ADDR_MAP_LIST_FREE,
};

static u16 addr_map_idx_of(const tl_zb_addr_map_entry_t *entry)
{
	return (u16)(entry - &g_nwkAddrMap.addrMap[0]);
}

void tl_addrMapListAdd(u8 free_list, tl_zb_addr_map_entry_t *entry)
{
	if (free_list != 0U) {
		entry->freeNext = g_nwkAddrMap.freeHead;
		g_nwkAddrMap.freeHead = entry;
	} else {
		entry->activeNext = g_nwkAddrMap.activeHead;
		g_nwkAddrMap.activeHead = entry;
	}
}

void tl_addrMapListDelete(u8 free_list, tl_zb_addr_map_entry_t *entry)
{
	tl_zb_addr_map_entry_t *current;

	current = free_list != 0U ? g_nwkAddrMap.freeHead : g_nwkAddrMap.activeHead;
	if (current == entry) {
		if (free_list != 0U) {
			g_nwkAddrMap.freeHead = entry->freeNext;
		} else {
			g_nwkAddrMap.activeHead = entry->activeNext;
		}
		return;
	}

	while (current != NULL) {
		tl_zb_addr_map_entry_t *next =
			free_list != 0U ? current->freeNext : current->activeNext;

		if (next == entry) {
			if (free_list != 0U) {
				current->freeNext = entry->freeNext;
			} else {
				current->activeNext = entry->activeNext;
			}
			return;
		}
		current = next;
	}
}

void tl_zbNwkAddrMapRst(void)
{
	memset(&g_nwkAddrMap, 0, addrMapTblSizeGet());
	g_nwkAddrMap.freeHead = &g_nwkAddrMap.addrMap[0];

	for (u16 i = 0U; i < TL_ZB_NWK_ADDR_MAP_SIZE; i++) {
		g_nwkAddrMap.addrMap[i].freeNext =
			i + 1U < TL_ZB_NWK_ADDR_MAP_SIZE ? &g_nwkAddrMap.addrMap[i + 1U] : NULL;
	}
}

void tl_zbNwkAddrMapInit(void)
{
	tl_zbNwkAddrMapRst();
}

void tl_zbNwkAddrMapDelete(u16 idx)
{
	tl_zb_addr_map_entry_t *entry = &g_nwkAddrMap.addrMap[idx];

	if (entry->used == 0U) {
		return;
	}

	entry->used = 0U;
	entry->bind = 0U;
	tl_addrMapListDelete(ADDR_MAP_LIST_ACTIVE, entry);
	tl_addrMapListAdd(ADDR_MAP_LIST_FREE, entry);
	g_nwkAddrMap.validNum--;
}

u8 tl_zbShortAddrByExtAddr(u16 *shortAddr, addrExt_t extAddr, u16 *idx)
{
	for (tl_zb_addr_map_entry_t *entry = g_nwkAddrMap.activeHead; entry != NULL;
	     entry = entry->activeNext) {
		if (entry->used == 0U) {
			break;
		}
		if (memcmp(entry->extAddr, extAddr, EXT_ADDR_LEN) == 0) {
			if (shortAddr != NULL) {
				*shortAddr = entry->shortAddr;
			}
			if (idx != NULL) {
				*idx = addr_map_idx_of(entry);
			}
			return RET_OK;
		}
	}

	return 0xffU;
}

u8 tl_zbExtAddrByShortAddr(u16 shortAddr, addrExt_t extAddr, u16 *idx)
{
	for (tl_zb_addr_map_entry_t *entry = g_nwkAddrMap.activeHead; entry != NULL;
	     entry = entry->activeNext) {
		if (entry->used == 0U) {
			break;
		}
		if (entry->shortAddr == shortAddr) {
			memcpy(extAddr, entry->extAddr, EXT_ADDR_LEN);
			if (idx != NULL) {
				*idx = addr_map_idx_of(entry);
			}
			return RET_OK;
		}
	}

	return 0xffU;
}

addrExt_t *tl_zbExtAddrPtrByShortAddr(u16 shortAddr)
{
	for (tl_zb_addr_map_entry_t *entry = g_nwkAddrMap.activeHead; entry != NULL;
	     entry = entry->activeNext) {
		if (entry->used == 0U) {
			break;
		}
		if (entry->shortAddr == shortAddr) {
			return &entry->extAddr;
		}
	}
	return NULL;
}

void tl_zbExtAddrByIdx(u16 idx, addrExt_t extAddr)
{
	memcpy(extAddr, g_nwkAddrMap.addrMap[idx].extAddr, EXT_ADDR_LEN);
}

u16 tl_zbshortAddrByIdx(u16 idx)
{
	return g_nwkAddrMap.addrMap[idx].shortAddr;
}

u8 tl_idxByShortAddr(u16 *idx, u16 shortAddr)
{
	for (tl_zb_addr_map_entry_t *entry = g_nwkAddrMap.activeHead; entry != NULL;
	     entry = entry->activeNext) {
		if (entry->used == 0U) {
			break;
		}
		if (entry->shortAddr == shortAddr) {
			if (idx != NULL) {
				*idx = addr_map_idx_of(entry);
			}
			return RET_OK;
		}
	}
	return 0xffU;
}

u8 tl_idxByExtAddr(u16 *idx, addrExt_t extAddr)
{
	for (tl_zb_addr_map_entry_t *entry = g_nwkAddrMap.activeHead; entry != NULL;
	     entry = entry->activeNext) {
		if (entry->used == 0U) {
			break;
		}
		if (memcmp(entry->extAddr, extAddr, EXT_ADDR_LEN) == 0) {
			if (idx != NULL) {
				*idx = addr_map_idx_of(entry);
			}
			return RET_OK;
		}
	}
	return 0xffU;
}

zb_nwk_status_t tl_zbNwkAddrMapAdd(u16 shortAddr, addrExt_t extAddr, u16 *ref)
{
	u16 short_idx = 0U;
	u16 ext_idx = 0U;
	u16 idx;
	u8 short_status = tl_idxByShortAddr(&short_idx, shortAddr);
	u8 ext_status = tl_idxByExtAddr(&ext_idx, extAddr);
	tl_zb_addr_map_entry_t *entry;

	if (ext_status == RET_OK) {
		if ((short_status == RET_OK) && (short_idx != ext_idx)) {
			tl_zbNwkAddrMapDelete(short_idx);
		}
		idx = ext_idx;
	} else if ((short_status == RET_OK) &&
		   (memcmp(g_nwkAddrMap.addrMap[short_idx].extAddr, g_zero_addr,
			   EXT_ADDR_LEN) == 0)) {
		idx = short_idx;
	} else {
		entry = g_nwkAddrMap.freeHead;
		if ((entry == NULL) || entry->used) {
			return NWK_STATUS_NEIGHBOR_TABLE_FULL;
		}

		g_nwkAddrMap.validNum++;
		entry->used = 1U;
		entry->shortAddr = shortAddr;
		memcpy(entry->extAddr, extAddr, EXT_ADDR_LEN);
		entry->aps_dup_cnt = 0U;
		entry->aps_dup_clock = 0U;
		tl_addrMapListDelete(ADDR_MAP_LIST_FREE, entry);
		tl_addrMapListAdd(ADDR_MAP_LIST_ACTIVE, entry);
		if (ref != NULL) {
			*ref = addr_map_idx_of(entry);
		}
		return NWK_STATUS_SUCCESS;
	}

	entry = &g_nwkAddrMap.addrMap[idx];
	entry->shortAddr = shortAddr;
	memcpy(entry->extAddr, extAddr, EXT_ADDR_LEN);
	if (ref != NULL) {
		*ref = idx;
	}

	return NWK_STATUS_SUCCESS;
}

u8 tl_addrByShort(u16 shortAddr, u8 addIfMissing, u8 unused, u16 *idx)
{
	ARG_UNUSED(unused);

	if (idx == NULL) {
		ZB_EXCEPTION_POST(SYS_EXCEPTTION_ZB_NWK_ADDR_IDX);
		return 0xffU;
	}
	if (tl_idxByShortAddr(idx, shortAddr) == RET_OK) {
		return RET_OK;
	}
	if (addIfMissing == 0U) {
		return 0xffU;
	}

	return (u8)tl_zbNwkAddrMapAdd(shortAddr, (addrExt_t) { 0 }, idx);
}

void tl_addrMappingForBind(u8 bind, u16 idx)
{
	if ((idx < TL_ZB_NWK_ADDR_MAP_SIZE) && g_nwkAddrMap.addrMap[idx].used) {
		g_nwkAddrMap.addrMap[idx].bind = bind & 1U;
	}
}

u8 zb_address_ieee_by_short(u16 short_addr, addrExt_t ieee_address)
{
	return tl_zbExtAddrByShortAddr(short_addr, ieee_address, NULL);
}

tl_zb_addr_map_entry_t *tl_zbNwkAddrMapEntryGet(u16 orderIndex)
{
	tl_zb_addr_map_entry_t *entry = g_nwkAddrMap.activeHead;

	while ((entry != NULL) && (orderIndex > 0U)) {
		entry = entry->activeNext;
		orderIndex--;
	}

	return entry;
}

void nwk_nodeAddrInfoStore(void *arg)
{
	zb_addrForNeighbor_t *info = (zb_addrForNeighbor_t *)arg;
	zb_addrForNeighbor_t entry;
	itemIfno_t item_info;
	bool stored = false;

	memset(&item_info, 0, sizeof(item_info));
	if (nv_flashReadNew(0, NV_MODULE_ADDRESS_TABLE, ITEM_FIELD_IDLE, sizeof(entry),
			    (u8 *)&item_info) == NV_SUCC) {
		for (u16 i = 0U; i <= item_info.opIndex; i++) {
			if (nv_flashReadByIndex(NV_MODULE_ADDRESS_TABLE,
						NV_ITEM_ADDRESS_FOR_NEIGHBOR, item_info.opSect, i,
						sizeof(entry), (u8 *)&entry) != NV_SUCC) {
				continue;
			}
			if ((memcmp(info->extAddr, entry.extAddr, EXT_ADDR_LEN) == 0) &&
			    (entry.shortAddr == info->shortAddr)) {
				stored = true;
				break;
			}
			if ((memcmp(info->extAddr, entry.extAddr, EXT_ADDR_LEN) == 0) ||
			    (entry.shortAddr == info->shortAddr)) {
				(void)nv_itemDeleteByIndex(NV_MODULE_ADDRESS_TABLE,
							   NV_ITEM_ADDRESS_FOR_NEIGHBOR,
							   item_info.opSect, i);
			}
		}
	}
	if (stored == false) {
		(void)nv_flashWriteNew(0, NV_MODULE_ADDRESS_TABLE, NV_ITEM_ADDRESS_FOR_NEIGHBOR,
				       sizeof(*info), (u8 *)info);
	}
	ev_buf_free((u8 *)info);
}

s32 nwk_parentNodeInfoStore(void)
{
	tl_zb_normal_neighbor_entry_t *parent = tl_zbNeighborTableSearchForParent();
	zb_addrForNeighbor_t entry;
	zb_addrForNeighbor_t record;
	itemIfno_t item_info;

	memset(&item_info, 0, sizeof(item_info));
	if (nv_flashReadNew(0, NV_MODULE_ADDRESS_TABLE, ITEM_FIELD_IDLE, sizeof(entry),
			    (u8 *)&item_info) == NV_SUCC) {
		for (u16 i = 0U; i <= item_info.opIndex; i++) {
			if ((nv_flashReadByIndex(NV_MODULE_ADDRESS_TABLE,
						 NV_ITEM_ADDRESS_FOR_NEIGHBOR, item_info.opSect, i,
						 sizeof(entry), (u8 *)&entry) == NV_SUCC) &&
			    (entry.relationship == 0U)) {
				(void)nv_itemDeleteByIndex(NV_MODULE_ADDRESS_TABLE,
							   NV_ITEM_ADDRESS_FOR_NEIGHBOR,
							   item_info.opSect, i);
			}
		}
	}

	if (parent != NULL) {
		u16 idx = parent->addrmapIdx;

		record.shortAddr = g_nwkAddrMap.addrMap[idx].shortAddr;
		memcpy(record.extAddr, g_nwkAddrMap.addrMap[idx].extAddr, EXT_ADDR_LEN);
		record.relationship = 0U;
		record.depth = parent->depth;
		record.deviceType = parent->deviceType;
		record.rxOnWhileIdle = parent->rxOnWhileIdle;
		(void)nv_flashWriteNew(0, NV_MODULE_ADDRESS_TABLE, NV_ITEM_ADDRESS_FOR_NEIGHBOR,
				       sizeof(record), (u8 *)&record);
	}

	return -1;
}

s32 nwk_nodeAddrInfoDelete(zb_addrForNeighbor_t *pAddrNv)
{
	zb_addrForNeighbor_t entry;
	itemIfno_t item_info;

	if (pAddrNv->deviceType == NWK_DEVICE_TYPE_ED) {
		nwk_endDevTimeout_nv_t timeout_info;

		memset(&item_info, 0, sizeof(item_info));
		if (nv_flashReadNew(0, NV_MODULE_ADDRESS_TABLE, ITEM_FIELD_IDLE,
					    sizeof(timeout_info), (u8 *)&item_info) == NV_SUCC) {
			for (u16 i = 0U; i <= item_info.opIndex; i++) {
				if ((nv_flashReadByIndex(NV_MODULE_ADDRESS_TABLE, NV_ITEM_ED_TIMEOUT,
							item_info.opSect, i, sizeof(timeout_info),
							(u8 *)&timeout_info) == NV_SUCC) &&
				    (memcmp(pAddrNv->extAddr, timeout_info.extAddr, EXT_ADDR_LEN) == 0)) {
					(void)nv_itemDeleteByIndex(NV_MODULE_ADDRESS_TABLE,
								   NV_ITEM_ED_TIMEOUT, item_info.opSect, i);
				}
			}
		}
	}

	memset(&item_info, 0, sizeof(item_info));
	if (nv_flashReadNew(0, NV_MODULE_ADDRESS_TABLE, ITEM_FIELD_IDLE, sizeof(entry),
			    (u8 *)&item_info) != NV_SUCC) {
		return 3;
	}
	for (u16 i = 0U; i <= item_info.opIndex; i++) {
		if ((nv_flashReadByIndex(NV_MODULE_ADDRESS_TABLE, NV_ITEM_ADDRESS_FOR_NEIGHBOR,
						 item_info.opSect, i, sizeof(entry), (u8 *)&entry) == NV_SUCC) &&
		    (memcmp(pAddrNv->extAddr, entry.extAddr, EXT_ADDR_LEN) == 0) &&
		    (entry.relationship == pAddrNv->relationship)) {
			(void)nv_itemDeleteByIndex(NV_MODULE_ADDRESS_TABLE, NV_ITEM_ADDRESS_FOR_NEIGHBOR,
						   item_info.opSect, i);
			return 0;
		}
	}

	return 3;
}

s32 nwk_bindAddrInfoUpdate(zb_addrForBind_t *pAddrNv)
{
	zb_addrForBind_t entry;
	itemIfno_t item_info;

	memset(&item_info, 0, sizeof(item_info));
	if (nv_flashReadNew(0, NV_MODULE_ADDRESS_TABLE, ITEM_FIELD_IDLE, sizeof(entry),
			    (u8 *)&item_info) == NV_SUCC) {
		for (u16 i = 0U; i <= item_info.opIndex; i++) {
			if ((nv_flashReadByIndex(NV_MODULE_ADDRESS_TABLE, NV_ITEM_ADDRESS_FOR_BIND,
						 item_info.opSect, i, sizeof(entry), (u8 *)&entry) != NV_SUCC) ||
			    (memcmp(pAddrNv->dstExtAddr, entry.dstExtAddr, EXT_ADDR_LEN) != 0)) {
				continue;
			}
			if (entry.mask_dstTable == pAddrNv->mask_dstTable) {
				return 0;
			}
			(void)nv_itemDeleteByIndex(NV_MODULE_ADDRESS_TABLE, NV_ITEM_ADDRESS_FOR_BIND,
						   item_info.opSect, i);
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
	itemIfno_t item_info;

	memset(&item_info, 0, sizeof(item_info));
	if (nv_flashReadNew(0, NV_MODULE_ADDRESS_TABLE, ITEM_FIELD_IDLE, sizeof(entry),
			    (u8 *)&item_info) != NV_SUCC) {
		return 3;
	}
	for (u16 i = 0U; i <= item_info.opIndex; i++) {
		if ((nv_flashReadByIndex(NV_MODULE_ADDRESS_TABLE, NV_ITEM_ADDRESS_FOR_BIND,
						 item_info.opSect, i, sizeof(entry), (u8 *)&entry) == NV_SUCC) &&
		    (memcmp(pAddrNv->dstExtAddr, entry.dstExtAddr, EXT_ADDR_LEN) == 0)) {
			(void)nv_itemDeleteByIndex(NV_MODULE_ADDRESS_TABLE, NV_ITEM_ADDRESS_FOR_BIND,
						   item_info.opSect, i);
			return 0;
		}
	}

	return 3;
}

#endif /* ZB_ROUTER_ROLE */
