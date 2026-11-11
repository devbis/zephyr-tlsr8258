/* Copyright 2026 libzigbee
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef _NWK_ROUTING_H
#define _NWK_ROUTING_H

#include "zb_common.h"

void nwkRoutingTabInit(void);
int nwkRoutingTabPeriodic(void *arg);
nwk_routingTabEntry_t *nwkRoutingTabEntryCreate(uint16_t dstAddr);
nwk_routingTabEntry_t *nwkRoutingTabEntryDstFind(uint16_t dstAddr);
nwk_routingTabEntry_t *nwkRoutingTabEntryFind(uint16_t dstAddr);
nwk_routingTabEntry_t *nwkRoutingTabEntryDstActiveGet(uint16_t dstAddr);
nwk_routingTabEntry_t *nwkRoutingTabGetNextHop(nwk_hdr_t *pNwkHdr);
void nwkRoutingTabEntryClear(nwk_routingTabEntry_t *entry);
void nwkRoutingTabEntryDstDel(uint16_t dstAddr);
void nwkRouteMaintenance(nwk_hdr_t *pNwkHdr, uint16_t macDstAddr);
void nwkRouteRepair(zb_buf_t *buf, uint16_t dstAddr, uint16_t statusDstAddr, uint8_t statusCode);
void nwkSrcRouteRequiredClear(uint16_t dstAddr);
uint16_t nwkSrcRouteReplayNextHop(nwk_hdr_t *pNwkHdr);
uint8_t nwkSourceRoutePacketRelayFilter(nwk_hdr_t *pNwkHdr);
#if defined(ZB_COORDINATOR_ROLE)
void nwkRouteRecTabEntryClear(nwk_routeRecordTabEntry_t *entry);
uint8_t nwkRouteRecTabActiveNumGet(void);
nwk_routeRecordTabEntry_t *nwkRouteRecTabEntryFreeGet(void);
nwk_routeRecordTabEntry_t *nwkRouteRecTabEntryFind(uint16_t nwkAddr);
bool nwkRouteRecTabPathMatch(nwk_routeRecordTabEntry_t *entry, uint16_t nwkAddr);
nwk_routeRecordTabEntry_t *nwkRouteRecTabEntryAddNew(uint16_t nwkAddr, nwkCmd_t *cmd);
nwk_routeRecordTabEntry_t *nwkRouteRecTabEntryCreat(uint16_t nwkAddr, nwkCmd_t *cmd);
void nwkRouteRecTabEntryDstDel(uint16_t dstAddr);
#endif

#endif
