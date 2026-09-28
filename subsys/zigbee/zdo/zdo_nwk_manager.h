/*
 * Copyright 2026 libzigbee
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef _ZDO_NWK_MANAGER_H
#define _ZDO_NWK_MANAGER_H

#include "zb_common.h"

typedef struct {
	ev_timer_event_t *discEvt;
	ev_timer_event_t *authEvt;
	ev_timer_event_t *pollEvt;
	ev_timer_event_t *backoffEvt;
	nwkDiscoveryUserCb_t nwkDiscCb;
	void *savedBuf;
	u32 scanChannels;
	u16 backoffTime;
	u16 backoffIter;
	u8 rejoinCnt;
	u8 scanDuration;
	u8 nwkDiscAttempt;
	u8 scanCount;
	u8 linkRetryCnt;
	u8 state;
} zdo_nwk_manager_t;

extern zdo_nwk_manager_t g_zdo_nwk_manager;
extern u8 zdo_mgmt_nwk_flag;

static inline zdo_nwk_manager_t *zdo_nwk_mngr(void)
{
	return &g_zdo_nwk_manager;
}

zdo_status_t zdo_nlmeEdScanReq(u32 scanChannels, u8 scanDuration, u8 scanCount);
zdo_status_t zdo_nlmeLeaveReq(void *arg);
#if defined(ZB_ROUTER_ROLE)
zdo_status_t zdo_nlmePermitJoinReq(u8 permitDuration);
zdo_status_t zdo_routeDiscReq(nlme_routeDisc_req_t *pRouteDiscReq);
#endif
u8 zdo_syncReq(void);
u8 zdo_set_pollRate(u32 rate);
#if defined(ZB_COORDINATOR_ROLE)
void zdo_manyToOneRouteDisc(void *arg);
#endif
void zdo_routeDiscCnf(void *arg);
void zdo_nlmePermitJoinCnf(void *arg);
void zdo_reset_confirm_cb(void *arg);
void zdo_nlme_network_discovery_confirm_cb(void *arg);
void zdo_nlme_start_router_confirm(void *arg);
void zdo_network_formation_confirm(void *arg);
void zdo_nlme_ed_scan_confirm(void *arg);
void zdo_nlme_sync_confirm(void *arg);
void zdo_nlme_direct_join_confirm(void *arg);
void zdo_nlme_join_indication(void *arg);
void zdo_nlme_join_confirm(void *arg);
void zdo_nlme_status_indication(void *arg);
void zdo_nlme_leave_indication_cb(void *arg);
void zdo_nlme_leave_confirm_cb(void *arg);

#endif
