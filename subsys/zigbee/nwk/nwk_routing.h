/* Copyright 2026 libzigbee
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef _NWK_ROUTING_H
#define _NWK_ROUTING_H

#include "zb_common.h"

void nwkRoutingTabInit(void);
int nwkRoutingTabPeriodic(void *arg);
nwk_routingTabEntry_t *nwkRoutingTabEntryCreate(u16 dstAddr);
nwk_routingTabEntry_t *nwkRoutingTabEntryDstFind(u16 dstAddr);
nwk_routingTabEntry_t *nwkRoutingTabEntryFind(u16 dstAddr);
nwk_routingTabEntry_t *nwkRoutingTabEntryDstActiveGet(u16 dstAddr);
nwk_routingTabEntry_t *nwkRoutingTabGetNextHop(nwk_hdr_t *pNwkHdr);
void nwkRoutingTabEntryClear(nwk_routingTabEntry_t *entry);
void nwkRoutingTabEntryDstDel(u16 dstAddr);
void nwkRouteMaintenance(nwk_hdr_t *pNwkHdr, u16 macDstAddr);
void nwkRouteRepair(zb_buf_t *buf, u16 dstAddr, u16 statusDstAddr, u8 statusCode);
void nwkSrcRouteRequiredClear(u16 dstAddr);
u16 nwkSrcRouteReplayNextHop(nwk_hdr_t *pNwkHdr);
u8 nwkSourceRoutePacketRelayFilter(nwk_hdr_t *pNwkHdr);
#if defined(ZB_COORDINATOR_ROLE)
void nwkRouteRecTabEntryClear(nwk_routeRecordTabEntry_t *entry);
u8 nwkRouteRecTabActiveNumGet(void);
nwk_routeRecordTabEntry_t *nwkRouteRecTabEntryFreeGet(void);
nwk_routeRecordTabEntry_t *nwkRouteRecTabEntryFind(u16 nwkAddr);
bool nwkRouteRecTabPathMatch(nwk_routeRecordTabEntry_t *entry, u16 nwkAddr);
nwk_routeRecordTabEntry_t *nwkRouteRecTabEntryAddNew(u16 nwkAddr, nwkCmd_t *cmd);
nwk_routeRecordTabEntry_t *nwkRouteRecTabEntryCreat(u16 nwkAddr, nwkCmd_t *cmd);
void nwkRouteRecTabEntryDstDel(u16 dstAddr);
#endif

#endif
