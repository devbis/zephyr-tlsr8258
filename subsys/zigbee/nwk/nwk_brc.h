/* Copyright 2026 libzigbee
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef _NWK_BRC_H
#define _NWK_BRC_H

#include "zb_common.h"

uint8_t nwkBrcCheckDevMatch(uint16_t dstAddr);
void nwkBrcTransTabInit(void);
int nwkBrcPeriodic(void *arg);
uint8_t nwkBrcTimerStart(zb_buf_t *buf, nwk_hdr_t *pNwkHdr, uint8_t *payload, uint8_t payloadLen);
void nwkMsgSendCb(void *arg);
nwk_brcTransRecordEntry_t *nwkBrcTransEntryFind(uint16_t srcAddr, uint8_t seqNum);
nwk_brcTransRecordEntry_t *nwkBrcTransEntryCreate(nwk_txDataPendEntry_t *pend, uint16_t srcAddr,
						  uint8_t seqNum);
uint8_t nwkBrcMsgAddSender(nwk_brcTransRecordEntry_t *entry, uint16_t shortAddr);
void nwkBrcTransTabEntryClear(nwk_brcTransRecordEntry_t *entry);
ev_timer_event_t *nwkBrcMsgPassiveAckTimeoutStart(nwk_brcTransRecordEntry_t *entry);
void nwkBrcMsgAllEndDevStart(nwk_brcTransRecordEntry_t *entry);
void nwkBrcTransJitterSet(uint32_t jitter);

#endif
