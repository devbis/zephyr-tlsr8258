/* Copyright 2026 libzigbee
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef _ZB_NWK_ADDR_CONFLICT_H
#define _ZB_NWK_ADDR_CONFLICT_H

#include "zb_common.h"

int nwk_addrConflictCb(void *arg);
void tl_zbNwkNeighborAddrConflictHandle(zb_buf_t *buf, tl_zb_normal_neighbor_entry_t *neighbor);
void tl_zbNwkAddrConflictStatusSend(u16 dstAddr, u16 statusAddr, u8 forceSeqNum);
void tl_zbNwkAddrConflictHandle(zb_buf_t *buf, u16 nwkAddr,
				tl_zb_normal_neighbor_entry_t *neighbor);
bool tl_zbNwkAddrConflictDetect(void *arg, u16 nwkAddr, addrExt_t ieeeAddr);
void tl_zbNwkStatusAddrConflictInd(void *arg);

#endif
