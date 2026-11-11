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
	uint32_t scanChannels;
	uint16_t backoffTime;
	uint16_t backoffIter;
	uint8_t rejoinCnt;
	uint8_t scanDuration;
	uint8_t nwkDiscAttempt;
	uint8_t scanCount;
	uint8_t linkRetryCnt;
	uint8_t state;
} zdo_nwk_manager_t;

extern zdo_nwk_manager_t g_zdo_nwk_manager;
extern uint8_t zdo_mgmt_nwk_flag;

static inline zdo_nwk_manager_t *zdo_nwk_mngr(void)
{
	return &g_zdo_nwk_manager;
}

zdo_status_t zdo_nlmeEdScanReq(uint32_t scanChannels, uint8_t scanDuration, uint8_t scanCount);
zdo_status_t zdo_nlmeLeaveReq(void *arg);
#if defined(ZB_ROUTER_ROLE)
zdo_status_t zdo_nlmePermitJoinReq(uint8_t permitDuration);
zdo_status_t zdo_routeDiscReq(nlme_routeDisc_req_t *pRouteDiscReq);
#endif
uint8_t zdo_syncReq(void);
uint8_t zdo_set_pollRate(uint32_t rate);
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
