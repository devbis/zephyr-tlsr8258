/*
 * Copyright 2026 libzigbee
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef _SS_TL_CCM_H
#define _SS_TL_CCM_H

#include "zb_common.h"

void ss_tlChallengeGen(u8 *dst, u8 len);
void ss_ttlMAC(u8 len, u8 *input, u8 *key, u8 *hashOut);
u8 ss_keyHash(u8 *padV, u8 *key, u8 *hashOut);
u8 ss_ccmEncryption(u8 *key, u8 *nonce, u8 nwkHdrLen, u8 *nwkHdr, u8 srcMsgLen, u8 *srcMsg);
u8 ss_ccmDecryption(u8 *key, u8 *nonce, u8 nwkHdrLen, u8 *nwkHdr, u8 srcMsgLen, u8 *srcMsg);

#endif
