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
	uint32_t active_ep_count;
	uint8_t active_eps[APS_EP_NUM_IN_GROUP_TBL];
	uint8_t pending_refs[4];
	uint8_t pending_rptr;
	uint8_t pending_wptr;
} aps_group_q_t;

extern aps_group_q_t aps_group_q;

/* APS header as parsed from the received frame.  Offsets 6..7 hold the
 * destination endpoint, or the 16-bit group address for group delivery. */
typedef struct _attribute_packed_ {
	uint8_t hdrLen;
	uint8_t frameCtrl;
	uint8_t apsCounter;
	uint8_t srcEp;
	uint16_t srcShortAddr;
	uint8_t dstEp;
	uint8_t dstAddrHi;
	uint16_t clusterId;
	uint16_t profileId;
	uint8_t extHdr;
	uint8_t blockNum;
	uint8_t ackBit;
} aps_rx_hdr_t;

uint8_t aps_get_counter_value(void);
uint8_t aps_get_current_counter_value(void);
uint8_t aps_get_handle(void);
void aps_init(void);
aps_status_t aps_txBufInit(zb_buf_t **buf, void **payload, uint8_t size);
uint8_t aps_hdr_parse(uint8_t *data, aps_rx_hdr_t *hdr);
void aps_command_handle(void *arg);
void aps_process_group_addressed_packet(zb_buf_t *buf);
void aps_deliver_group_msg(void *arg);

#endif
