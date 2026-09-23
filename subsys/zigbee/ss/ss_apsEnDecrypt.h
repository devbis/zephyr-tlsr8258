/*
 * Copyright 2026 libzigbee
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef _SS_APS_EN_DECRYPT_H
#define _SS_APS_EN_DECRYPT_H

#include "zb_common.h"

u8 ss_apsEnAuxHdrFill(u8 *auxHdr, void *keyInfo, u8 extNonceOpt);
u8 ss_apsSecureFrame(void *p, u8 apsHdrAuxLen, u8 apsHdrLen, addrExt_t extAddr);
u8 ss_apsDecryptFrame(void *arg);

#endif
