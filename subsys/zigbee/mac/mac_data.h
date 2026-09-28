/*
 * Copyright 2026 libzigbee
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef _MAC_DATA_H
#define _MAC_DATA_H

#include "zb_common.h"

typedef struct {
	u8 *txData;
	u8 psduLen;
} mac_pending_tx_ctx_t;

void tl_zbMacMcpsDataRequestProc(void *arg);
void tl_zbPhyMldeIndication(zb_buf_t *buf, u8 *raw, u8 len);
void tl_zbMacMcpsDataRequestSendConfirm(zb_buf_t *buf, u8 status);

#endif
