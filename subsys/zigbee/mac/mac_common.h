/*
 * Copyright 2026 libzigbee
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef _MAC_COMMON_H
#define _MAC_COMMON_H

#include "zb_common.h"

uint8_t *zb_MacHdrBuilder(uint8_t *buf, zb_mac_mhr_t *mhr);
uint8_t zb_MacHdrParse(zb_mac_mhr_t *mhr, uint8_t *buf);

#endif
