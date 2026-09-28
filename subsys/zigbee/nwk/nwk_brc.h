/* Copyright 2026 libzigbee
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef _NWK_BRC_H
#define _NWK_BRC_H

#include "zb_common.h"

u8 nwkBrcCheckDevMatch(u16 dstAddr);
void nwkBrcTransTabInit(void);
int nwkBrcPeriodic(void *arg);
u8 nwkBrcTimerStart(zb_buf_t *buf, nwk_hdr_t *pNwkHdr, u8 *payload, u8 payloadLen);
void nwkMsgSendCb(void *arg);
nwk_brcTransRecordEntry_t *nwkBrcTransEntryFind(u16 srcAddr, u8 seqNum);
nwk_brcTransRecordEntry_t *nwkBrcTransEntryCreate(nwk_txDataPendEntry_t *pend, u16 srcAddr,
						  u8 seqNum);
u8 nwkBrcMsgAddSender(nwk_brcTransRecordEntry_t *entry, u16 shortAddr);
void nwkBrcTransTabEntryClear(nwk_brcTransRecordEntry_t *entry);
ev_timer_event_t *nwkBrcMsgPassiveAckTimeoutStart(nwk_brcTransRecordEntry_t *entry);
void nwkBrcMsgAllEndDevStart(nwk_brcTransRecordEntry_t *entry);
void nwkBrcTransJitterSet(u32 jitter);

#endif
