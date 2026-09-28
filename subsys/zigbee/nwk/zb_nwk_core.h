/* Copyright 2026 libzigbee
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef _ZB_NWK_CORE_H
#define _ZB_NWK_CORE_H

#include "zb_common.h"

/* The most link status entries one frame can carry. */
#define NWK_LINK_STATUS_ENTRY_MAX_PER_FRAME 26

void tl_zbNwkNibInit(u8 coldReset);
u32 getPassiveAckTimeout(void);
u8 nwkHdrParse(nwk_hdr_t *pNwkHdr, u8 *msdu);
u8 getNwkHdrSize(nwk_hdr_t *pNwkHdr);
u8 *nwkHdrBuilder(u8 *buf, nwk_hdr_t *pNwkHdr);
void tl_zbNwkLinkStatusStop(void);
void tl_zbNwkNeighborTabAging(void);
u8 nwk_linkStEntryBuild(linkStatus_entry_t *list, u8 maxEntries);
void nwkLinkStatusCmdSend(void *arg, nwk_hdr_t *pNwkHdr, nwkCmd_t *cmd, u8 handle);
void tl_zbNwkSendLinkStatus(void);
void tl_zbNwkLinkStatusStart(void);
int tl_zbNwkLinkStatusTimerEvtCb(void *arg);
int nwk_linkStPeriodic(void *arg);
void tl_zbNwkSendNwkStatusCmd(void *arg, nwk_hdr_t *pNwkHdr, nwkCmd_t *pNwkStatus, u8 handle);

#endif
