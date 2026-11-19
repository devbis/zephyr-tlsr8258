/* Copyright 2026 libzigbee
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef _ZB_NWK_CORE_H
#define _ZB_NWK_CORE_H

#include "zb_common.h"

/* The most link status entries one frame can carry. */
#define NWK_LINK_STATUS_ENTRY_MAX_PER_FRAME 26

void zb_NwkNibInit(uint8_t coldReset);
uint32_t getPassiveAckTimeout(void);
uint8_t nwkHdrParse(nwk_hdr_t *pNwkHdr, uint8_t *msdu);
uint8_t getNwkHdrSize(nwk_hdr_t *pNwkHdr);
uint8_t *nwkHdrBuilder(uint8_t *buf, nwk_hdr_t *pNwkHdr);
void zb_NwkLinkStatusStop(void);
void zb_NwkNeighborTabAging(void);
uint8_t nwk_linkStEntryBuild(linkStatus_entry_t *list, uint8_t maxEntries);
void nwkLinkStatusCmdSend(void *arg, nwk_hdr_t *pNwkHdr, nwkCmd_t *cmd, uint8_t handle);
void zb_NwkSendLinkStatus(void);
void zb_NwkLinkStatusStart(void);
int zb_NwkLinkStatusTimerEvtCb(void *arg);
int nwk_linkStPeriodic(void *arg);
void zb_NwkSendNwkStatusCmd(void *arg, nwk_hdr_t *pNwkHdr, nwkCmd_t *pNwkStatus, uint8_t handle);

#endif
