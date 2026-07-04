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

#include <string.h>

#if (defined(ZB_ROUTER_ROLE) && ZB_ROUTER_ROLE) || defined(ZB_ED_ROLE_LIBZIGBEE)

static tl_zb_addr_map_entry_t *addr_map_alloc_entry(void)
{
	for (u8 i = 0; i < TL_ZB_NWK_ADDR_MAP_NUM; i++) {
		tl_zb_addr_map_entry_t *entry = &g_nwkAddrMap.addrMap[i];

		if (!entry->used) {
			memset(entry, 0, sizeof(*entry));
			entry->used = 1;
			g_nwkAddrMap.validNum++;
			return entry;
		}
	}

	return NULL;
}

void tl_addrMapListAdd(tl_zb_addr_map_entry_t *entry)
{
	if (entry == NULL) {
		return;
	}

	entry->activeNext = g_nwkAddrMap.activeHead;
	g_nwkAddrMap.activeHead = entry;
}

void tl_addrMapListDelete(tl_zb_addr_map_entry_t *entry)
{
	tl_zb_addr_map_entry_t **pp = &g_nwkAddrMap.activeHead;

	while (*pp != NULL) {
		if (*pp == entry) {
			*pp = entry->activeNext;
			entry->activeNext = NULL;
			return;
		}
		pp = &(*pp)->activeNext;
	}
}

void tl_zbNwkAddrMapRst(void)
{
	memset(&g_nwkAddrMap, 0, sizeof(g_nwkAddrMap));
}

void tl_zbNwkAddrMapInit(void)
{
	tl_zbNwkAddrMapRst();
}

void tl_zbNwkAddrMapDelete(u16 idx)
{
	if (idx >= TL_ZB_NWK_ADDR_MAP_NUM) {
		return;
	}

	if (g_nwkAddrMap.addrMap[idx].used) {
		tl_addrMapListDelete(&g_nwkAddrMap.addrMap[idx]);
		memset(&g_nwkAddrMap.addrMap[idx], 0, sizeof(g_nwkAddrMap.addrMap[idx]));
		g_nwkAddrMap.validNum--;
	}
}

u8 tl_zbShortAddrByExtAddr(u16 *shortAddr, addrExt_t extAddr, u16 *idx)
{
	for (u8 i = 0; i < TL_ZB_NWK_ADDR_MAP_NUM; i++) {
		tl_zb_addr_map_entry_t *entry = &g_nwkAddrMap.addrMap[i];

		if (entry->used && memcmp(entry->extAddr, extAddr, EXT_ADDR_LEN) == 0) {
			if (shortAddr != NULL) {
				*shortAddr = entry->shortAddr;
			}
			if (idx != NULL) {
				*idx = i;
			}
			return RET_OK;
		}
	}

	return RET_ERROR;
}

u8 tl_zbExtAddrByShortAddr(u16 shortAddr, addrExt_t extAddr, u16 *idx)
{
	for (u8 i = 0; i < TL_ZB_NWK_ADDR_MAP_NUM; i++) {
		tl_zb_addr_map_entry_t *entry = &g_nwkAddrMap.addrMap[i];

		if (entry->used && entry->shortAddr == shortAddr) {
			memcpy(extAddr, entry->extAddr, EXT_ADDR_LEN);
			if (idx != NULL) {
				*idx = i;
			}
			return RET_OK;
		}
	}

	return 0xffU;
}

addrExt_t *tl_zbExtAddrPtrByShortAddr(u16 shortAddr)
{
	for (u8 i = 0; i < TL_ZB_NWK_ADDR_MAP_NUM; i++) {
		if (g_nwkAddrMap.addrMap[i].used &&
		    g_nwkAddrMap.addrMap[i].shortAddr == shortAddr) {
			return &g_nwkAddrMap.addrMap[i].extAddr;
		}
	}
	return NULL;
}

void tl_zbExtAddrByIdx(u16 idx, addrExt_t extAddr)
{
	if (idx < TL_ZB_NWK_ADDR_MAP_NUM && g_nwkAddrMap.addrMap[idx].used) {
		memcpy(extAddr, g_nwkAddrMap.addrMap[idx].extAddr, EXT_ADDR_LEN);
	} else {
		memset(extAddr, 0, EXT_ADDR_LEN);
	}
}

u16 tl_zbshortAddrByIdx(u16 idx)
{
	if (idx < TL_ZB_NWK_ADDR_MAP_NUM && g_nwkAddrMap.addrMap[idx].used) {
		return g_nwkAddrMap.addrMap[idx].shortAddr;
	}
	return ZB_UNKNOWN_SHORT_ADDR;
}

u8 tl_idxByShortAddr(u16 *idx, u16 shortAddr)
{
	for (u8 i = 0; i < TL_ZB_NWK_ADDR_MAP_NUM; i++) {
		if (g_nwkAddrMap.addrMap[i].used &&
		    g_nwkAddrMap.addrMap[i].shortAddr == shortAddr) {
			if (idx != NULL) {
				*idx = i;
			}
			return RET_OK;
		}
	}
	return RET_ERROR;
}

u8 tl_idxByExtAddr(u16 *idx, addrExt_t extAddr)
{
	for (u8 i = 0; i < TL_ZB_NWK_ADDR_MAP_NUM; i++) {
		if (g_nwkAddrMap.addrMap[i].used &&
		    memcmp(g_nwkAddrMap.addrMap[i].extAddr, extAddr, EXT_ADDR_LEN) == 0) {
			if (idx != NULL) {
				*idx = i;
			}
			return RET_OK;
		}
	}
	return RET_ERROR;
}

zb_nwk_status_t tl_zbNwkAddrMapAdd(u16 shortAddr, addrExt_t extAddr, u16 *ref)
{
	u16 idx;

	if (tl_idxByShortAddr(&idx, shortAddr) == RET_OK ||
	    tl_idxByExtAddr(&idx, extAddr) == RET_OK) {
		tl_zb_addr_map_entry_t *entry = &g_nwkAddrMap.addrMap[idx];

		entry->shortAddr = shortAddr;
		memcpy(entry->extAddr, extAddr, EXT_ADDR_LEN);
		if (ref != NULL) {
			*ref = idx;
		}
		return NWK_STATUS_SUCCESS;
	}

	{
		tl_zb_addr_map_entry_t *entry = addr_map_alloc_entry();
		if (entry == NULL) {
			return NWK_STATUS_BT_TABLE_FULL;
		}
		entry->shortAddr = shortAddr;
		memcpy(entry->extAddr, extAddr, EXT_ADDR_LEN);
		tl_addrMapListAdd(entry);
		if (ref != NULL) {
			*ref = (u16)(entry - &g_nwkAddrMap.addrMap[0]);
		}
	}

	return NWK_STATUS_SUCCESS;
}

u16 tl_addrByShort(u16 shortAddr)
{
	return shortAddr;
}

void tl_addrMappingForBind(u8 bind, u16 idx)
{
	if (idx < TL_ZB_NWK_ADDR_MAP_NUM && g_nwkAddrMap.addrMap[idx].used) {
		g_nwkAddrMap.addrMap[idx].bind = bind ? 1U : 0U;
	}
}

u8 zb_address_ieee_by_short(u16 short_addr, addrExt_t ieee_address)
{
	return (tl_zbExtAddrByShortAddr(short_addr, ieee_address, NULL) == RET_OK)
		       ? 1U : 0U;
}

tl_zb_addr_map_entry_t *tl_zbNwkAddrMapEntryGet(u16 orderIndex)
{
	if (orderIndex >= TL_ZB_NWK_ADDR_MAP_NUM) {
		return NULL;
	}
	return &g_nwkAddrMap.addrMap[orderIndex];
}

void nwk_nodeAddrInfoStore(void *arg)
{
	zb_addrForNeighbor_t *info = (zb_addrForNeighbor_t *)arg;
	(void)tl_zbNwkAddrMapAdd(info->shortAddr, info->extAddr, NULL);
}

s32 nwk_parentNodeInfoStore(void)
{
	return 0;
}

s32 nwk_nodeAddrInfoDelete(zb_addrForNeighbor_t *pAddrNv)
{
	u16 idx;
	if (tl_idxByShortAddr(&idx, pAddrNv->shortAddr) == RET_OK) {
		tl_zbNwkAddrMapDelete(idx);
		return 0;
	}
	return -1;
}

s32 nwk_bindAddrInfoUpdate(zb_addrForBind_t *pAddrNv)
{
	u16 idx;
	if (tl_idxByExtAddr(&idx, pAddrNv->dstExtAddr) == RET_OK) {
		tl_addrMappingForBind((u8)(pAddrNv->mask_dstTable != 0U), idx);
		return 0;
	}
	return -1;
}

s32 nwk_bindAddrInfoDelete(zb_addrForBind_t *pAddrNv)
{
	return nwk_bindAddrInfoUpdate(pAddrNv);
}

#endif /* ZB_ROUTER_ROLE */
