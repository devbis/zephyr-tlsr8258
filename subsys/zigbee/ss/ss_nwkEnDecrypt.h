/*
 * Copyright 2026 libzigbee
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef _SS_NWK_EN_DECRYPT_H
#define _SS_NWK_EN_DECRYPT_H

#include "zb_common.h"

u8 ss_nwkDecryptFrame(void *p, u8 nwkHdrSize, u8 payloadSize, u8 *payloadAddr, nwk_hdr_t *nwkHdr,
		      u8 lqi);
u8 ss_nwkSecureFrame(zb_buf_t *src, u8 nwkHdrAuxLen);

#endif
