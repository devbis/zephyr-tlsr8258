/*
 * Copyright 2026 libzigbee
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef _SS_NWK_EN_DECRYPT_H
#define _SS_NWK_EN_DECRYPT_H

#include "zb_common.h"

uint8_t ss_nwkDecryptFrame(void *p, uint8_t nwkHdrSize, uint8_t payloadSize, uint8_t *payloadAddr, nwk_hdr_t *nwkHdr,
		      uint8_t lqi);
uint8_t ss_nwkSecureFrame(zb_buf_t *src, uint8_t nwkHdrAuxLen);

#endif
