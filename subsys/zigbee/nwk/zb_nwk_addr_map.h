/* Copyright 2026 libzigbee
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef _ZB_NWK_ADDR_MAP_H
#define _ZB_NWK_ADDR_MAP_H

#include "zb_common.h"

u8 tl_addrByShort(u16 shortAddr, u8 addIfMissing, u8 unused, u16 *idx);
void tl_zbNwkAddrMapInit(void);
void tl_zbNwkAddrMapRst(void);
void tl_zbNwkAddrMapDelete(u16 idx);
u8 tl_zbShortAddrByExtAddr(u16 *shortAddr, addrExt_t extAddr, u16 *idx);
u8 tl_zbExtAddrByShortAddr(u16 shortAddr, addrExt_t extAddr, u16 *idx);
addrExt_t *tl_zbExtAddrPtrByShortAddr(u16 shortAddr);
void tl_zbExtAddrByIdx(u16 idx, addrExt_t extAddr);
u16 tl_zbshortAddrByIdx(u16 idx);
u8 tl_idxByShortAddr(u16 *idx, u16 shortAddr);
u8 tl_idxByExtAddr(u16 *idx, addrExt_t extAddr);
zb_nwk_status_t tl_zbNwkAddrMapAdd(u16 shortAddr, addrExt_t extAddr, u16 *ref);

#endif
