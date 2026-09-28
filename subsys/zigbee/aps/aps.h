/*
 * Copyright 2026 libzigbee
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef _APS_H
#define _APS_H

#include "zb_common.h"
#include "aps_stackUse.h"

typedef struct {
	zb_buf_t *active_buf;
	u32 active_ep_count;
	u8 active_eps[APS_EP_NUM_IN_GROUP_TBL];
	u8 pending_refs[4];
	u8 pending_rptr;
	u8 pending_wptr;
} aps_group_q_t;

extern aps_group_q_t aps_group_q;

/* APS header as parsed from the received frame.  Offsets 6..7 hold the
 * destination endpoint, or the 16-bit group address for group delivery. */
typedef struct _attribute_packed_ {
	u8 hdrLen;
	u8 frameCtrl;
	u8 apsCounter;
	u8 srcEp;
	u16 srcShortAddr;
	u8 dstEp;
	u8 dstAddrHi;
	u16 clusterId;
	u16 profileId;
	u8 extHdr;
	u8 blockNum;
	u8 ackBit;
} aps_rx_hdr_t;

u8 aps_get_counter_value(void);
u8 aps_get_current_counter_value(void);
u8 aps_get_handle(void);
void aps_init(void);
aps_status_t aps_txBufInit(zb_buf_t **buf, void **payload, u8 size);
u8 aps_hdr_parse(u8 *data, aps_rx_hdr_t *hdr);
void aps_command_handle(void *arg);
void aps_process_group_addressed_packet(zb_buf_t *buf);
void aps_deliver_group_msg(void *arg);

#endif
