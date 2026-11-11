/*
 * Copyright 2026 libzigbee
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef _MAC_TRX_H
#define _MAC_TRX_H

#include "zb_common.h"

typedef struct {
	void *cb;
	uint32_t deadline;
	uint8_t state;
	uint8_t pad[3]; /* offsets 9-11, unused */
} mac_timer_evt_t;

typedef struct {
	uint8_t *raw;
	uint32_t timestamp;
	int8_t rssi;
	uint8_t len;
} mac_rx_pending_meta_t;

extern mac_timer_evt_t g_macTimerEvt;

void mac_trxInit(void);
uint8_t zb_MacTx(zb_buf_t *txBuf, uint8_t *txData, uint8_t psduLen, uint8_t ack, void *pendingList);

#endif
