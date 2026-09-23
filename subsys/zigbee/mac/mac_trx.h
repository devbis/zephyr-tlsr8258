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
	u32 deadline;
	u8 state;
	u8 reserved[3];
} mac_timer_evt_t;

typedef struct {
	u8 *raw;
	u32 timestamp;
	s8 rssi;
	u8 len;
} mac_rx_pending_meta_t;

extern mac_timer_evt_t g_macTimerEvt;

void mac_trxInit(void);
u8 tl_zbMacTx(zb_buf_t *txBuf, u8 *txData, u8 psduLen, u8 ack, void *pendingList);

#endif
