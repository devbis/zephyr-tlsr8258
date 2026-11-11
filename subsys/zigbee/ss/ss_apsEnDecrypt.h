/*
 * Copyright 2026 libzigbee
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef _SS_APS_EN_DECRYPT_H
#define _SS_APS_EN_DECRYPT_H

#include "zb_common.h"

uint8_t ss_apsEnAuxHdrFill(uint8_t *auxHdr, void *keyInfo, uint8_t extNonceOpt);
uint8_t ss_apsSecureFrame(void *p, uint8_t apsHdrAuxLen, uint8_t apsHdrLen, addrExt_t extAddr);
uint8_t ss_apsDecryptFrame(void *arg);

#endif
