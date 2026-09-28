/* Copyright 2026 libzigbee
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef _NWK_ROUTE_DISC_H
#define _NWK_ROUTE_DISC_H

#include "zb_common.h"

void nwkRouteDiscTabInit(void);
int nwkRouteDiscPeriodic(void *arg);
nwk_routeDiscEntry_t *nwkRouteDiscEntryDstFind(u16 dstAddr);
u8 nwkTxDataRouteDiscStart(nwk_hdr_t *pNwkHdr);
void nwkRouteRecordCmdSend(zb_buf_t *buf, nwk_hdr_t *pNwkHdr, nwkCmd_t *cmd, u8 handle);

#endif
