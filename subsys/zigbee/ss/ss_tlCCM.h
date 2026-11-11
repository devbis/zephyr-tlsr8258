/*
 * Copyright 2026 libzigbee
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef _SS_TL_CCM_H
#define _SS_TL_CCM_H

#include "zb_common.h"

void ss_tlChallengeGen(uint8_t *dst, uint8_t len);
void ss_ttlMAC(uint8_t len, uint8_t *input, uint8_t *key, uint8_t *hashOut);
uint8_t ss_keyHash(uint8_t *hashInput, uint8_t *key, uint8_t *hashOut);
uint8_t ss_ccmEncryption(uint8_t *key, uint8_t *nonce, uint8_t nwkHdrLen, uint8_t *nwkHdr, uint8_t srcMsgLen, uint8_t *srcMsg);
uint8_t ss_ccmDecryption(uint8_t *key, uint8_t *nonce, uint8_t nwkHdrLen, uint8_t *nwkHdr, uint8_t srcMsgLen, uint8_t *srcMsg);

#endif
