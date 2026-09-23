/* Copyright 2026 libzigbee
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef _ZB_NWK_NEIGHBOR_H
#define _ZB_NWK_NEIGHBOR_H

#include "zb_common.h"

void nwkNebManagePeriodic(void);
void tl_nebListAdd(u8 freeList, tl_zb_normal_neighbor_entry_t *entry);
void tl_nebListDelete(u8 freeList, tl_zb_normal_neighbor_entry_t *entry);
void tl_zbNeighborTableRst(void);
u8 tl_zbNeighborTableNumGet(void);
u8 tl_zbNeighborTableChildEDNumGet(void);
u8 tl_zbNeighborTableRouterValidNumGet(void);
tl_zb_normal_neighbor_entry_t *tl_zbNeighborTableSearchForParent(void);
u16 tl_zbNeighborParentShortAddrGet(void);
u8 tl_nwkNeighborDeleteByAddrmapIdx(u16 idx);
void tl_zbAdditionNeighborReset(void);
u8 tl_zbAdditionNeighborTableNumGet(void);
u8 tl_neighborFrameCntReset(void);
void tl_zbNeighborTableInit(void);
tl_zb_normal_neighbor_entry_t *tl_zbNeighborTabSearchForRouter(void *entry);
tl_zb_normal_neighbor_entry_t *tl_zbNeighborTabSearchForChildEndDev(void *entry);
tl_zb_normal_neighbor_entry_t *tl_zbNeighborTableSearchFromAddrmapIdx(u16 idx);

#endif
