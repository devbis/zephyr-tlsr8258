/* Copyright 2026 libzigbee
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef _ZB_NWK_ADDR_CONFLICT_H
#define _ZB_NWK_ADDR_CONFLICT_H

#include "zb_common.h"

int nwk_addrConflictCb(void *arg);
void zb_NwkNeighborAddrConflictHandle(zb_buf_t *buf, zb_normal_neighbor_entry_t *neighbor);
void zb_NwkAddrConflictStatusSend(uint16_t dstAddr, uint16_t statusAddr, uint8_t forceSeqNum);
void zb_NwkAddrConflictHandle(zb_buf_t *buf, uint16_t nwkAddr,
				zb_normal_neighbor_entry_t *neighbor);
bool zb_NwkAddrConflictDetect(void *arg, uint16_t nwkAddr, addrExt_t ieeeAddr);
void zb_NwkStatusAddrConflictInd(void *arg);

#endif
