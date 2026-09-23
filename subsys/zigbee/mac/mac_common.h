/*
 * Copyright 2026 libzigbee
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef _MAC_COMMON_H
#define _MAC_COMMON_H

#include "zb_common.h"

u8 *tl_zbMacHdrBuilder(u8 *buf, tl_zb_mac_mhr_t *mhr);
u8 tl_zbMacHdrParse(tl_zb_mac_mhr_t *mhr, u8 *buf);

#endif
