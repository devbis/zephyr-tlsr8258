/* Copyright 2026 libzigbee
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef _ZB_NWK_NEIGHBOR_H
#define _ZB_NWK_NEIGHBOR_H

#include "zb_common.h"

void nwkNebManagePeriodic(void);
void zb_nebListAdd(uint8_t freeList, zb_normal_neighbor_entry_t *entry);
void zb_nebListDelete(uint8_t freeList, zb_normal_neighbor_entry_t *entry);
void zb_NeighborTableRst(void);
uint8_t zb_NeighborTableNumGet(void);
uint8_t zb_NeighborTableChildEDNumGet(void);
uint8_t zb_NeighborTableRouterValidNumGet(void);
zb_normal_neighbor_entry_t *zb_NeighborTableSearchForParent(void);
uint16_t zb_NeighborParentShortAddrGet(void);
uint8_t zb_nwkNeighborDeleteByAddrmapIdx(uint16_t idx);
void zb_AdditionNeighborReset(void);
uint8_t zb_AdditionNeighborTableNumGet(void);
uint8_t zb_neighborFrameCntReset(void);
void zb_NeighborTableInit(void);
zb_normal_neighbor_entry_t *zb_NeighborTabSearchForRouter(void *entry);
zb_normal_neighbor_entry_t *zb_NeighborTabSearchForChildEndDev(void *entry);
zb_normal_neighbor_entry_t *zb_NeighborTableSearchFromAddrmapIdx(uint16_t idx);

#endif
