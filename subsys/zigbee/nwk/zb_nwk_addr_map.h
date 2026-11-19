/* Copyright 2026 libzigbee
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef _ZB_NWK_ADDR_MAP_H
#define _ZB_NWK_ADDR_MAP_H

#include "zb_common.h"

uint8_t zb_addrByShort(uint16_t shortAddr, uint8_t addIfMissing, uint8_t unused, uint16_t *idx);
void zb_NwkAddrMapInit(void);
void zb_NwkAddrMapRst(void);
void zb_NwkAddrMapDelete(uint16_t idx);
uint8_t zb_ShortAddrByExtAddr(uint16_t *shortAddr, addrExt_t extAddr, uint16_t *idx);
uint8_t zb_ExtAddrByShortAddr(uint16_t shortAddr, addrExt_t extAddr, uint16_t *idx);
addrExt_t *zb_ExtAddrPtrByShortAddr(uint16_t shortAddr);
void zb_ExtAddrByIdx(uint16_t idx, addrExt_t extAddr);
uint16_t zb_shortAddrByIdx(uint16_t idx);
uint8_t zb_idxByShortAddr(uint16_t *idx, uint16_t shortAddr);
uint8_t zb_idxByExtAddr(uint16_t *idx, addrExt_t extAddr);
zb_nwk_status_t zb_NwkAddrMapAdd(uint16_t shortAddr, addrExt_t extAddr, uint16_t *ref);

#endif
