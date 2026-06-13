/* SPDX-License-Identifier: Apache-2.0 */
/* Adapted from libzigbee/src/include/zdo_internal.h. */
#ifndef DRIVERS_ZIGBEE_SRC_INCLUDE_ZDO_INTERNAL_H
#define DRIVERS_ZIGBEE_SRC_INCLUDE_ZDO_INTERNAL_H

#include "zb_common_stub.h"
#include "os/ev_timer.h"
#include "nwk/includes/nwk.h"
#include "zdo/zdo_api.h"
#include "zdo/zdp.h"

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
static inline zdo_nwk_manager_t *zdo_nwk_mngr(void)
{
    return &g_zdo_nwk_manager;
}
extern u8 zdo_mgmt_nwk_flag;
extern const addrExt_t g_zero_addr;
extern const addrExt_t g_invalid_addr;

extern zdo_status_t zdo_nlmeNwkDiscReq(void *arg);
extern zdo_status_t zdo_nlme_edScan(void *arg);
extern zdo_status_t zdo_nlmeEdScanReq(u32 scanChannels, u8 scanDuration, u8 scanCount);
extern zdo_status_t zdo_nlmeLeaveReq(void *arg);
/* Signature matches zdp.h. */
extern u8 zdo_send_req(zdo_zdp_req_t *req);
extern void zdo_device_announce_send(void);
extern void zdo_parent_announce_send(void);
extern void zdo_zdpCbTblRegister(zdo_appIndCb_t *cbTbl);
extern void zdo_set_pollRate(u32 rate);
extern void secondClockRun(void);
extern void zb_info_save(void *arg);
extern bool zdo_af_get_use_tc_sec_on_nwk_key_rotation(void);
extern void zdo_bind_toggle_cb(void *arg);
extern void zdo_end_device_unbind_cb(void *arg);
extern void zdo_end_device_bind_cb(void *arg);
extern void zdo_nlme_network_discovery_confirm_cb(void *arg);
extern void zdo_nlme_start_router_confirm(void *arg);
extern void zdo_network_formation_confirm(void *arg);
extern void zdo_nlme_ed_scan_confirm(void *arg);
extern void zdo_reset_confirm_cb(void *arg);
extern void zdo_nlme_sync_confirm(void *arg);
extern void zdo_nlmePermitJoinCnf(void *arg);
extern void zdo_routeDiscCnf(void *arg);
extern void zdo_nlme_status_indication(void *arg);
extern void zdo_nlme_join_indication(void *arg);
extern void zdo_nlme_direct_join_confirm(void *arg);
extern void zdo_nlme_leave_indication_cb(void *arg);
extern void zdo_nlme_leave_confirm_cb(void *arg);
extern void zdo_nlme_join_confirm(void *arg);
#if defined(ZB_ROUTER_ROLE)
extern zdo_status_t zdo_nlmePermitJoinReq(u8 permitDuration);
extern zdo_status_t zdo_routeDiscReq(nlme_routeDisc_req_t *pRouteDiscReq);
extern void zdo_parentAnnounceIndicate(void *arg);
extern void zdo_remoteAddrNotify(void *arg);
extern void zdo_parentAnnounceNotify(void *arg);
#endif

#endif
