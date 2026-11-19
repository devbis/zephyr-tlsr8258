/*
 * Copyright 2026 libzigbee
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef _MAC_DATA_H
#define _MAC_DATA_H

#include "zb_common.h"

typedef struct {
	uint8_t *txData;
	uint8_t psduLen;
} mac_pending_tx_ctx_t;

void zb_MacMcpsDataRequestProc(void *arg);
void zb_PhyMldeIndication(zb_buf_t *buf, uint8_t *raw, uint8_t len);
void zb_MacMcpsDataRequestSendConfirm(zb_buf_t *buf, uint8_t status);

#endif
