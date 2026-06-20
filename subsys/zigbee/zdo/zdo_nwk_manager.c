/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Adapted from libzigbee/src/zdo_nwk_manager.c. Vendor file kept structurally
 * one-for-one; vendor zb_local.h / ev_timer.h are replaced by the
 * Zephyr include set.
 */
#include "zb_common_stub.h"
#include "common/static_assert.h"
#include "os/ev_timer.h"
#include "mac/includes/tl_zb_mac.h"
#include "mac/includes/tl_zb_mac_pib.h"
#include "nwk/includes/nwk.h"
#include "nwk/includes/nwk_internal.h"
#include "aps/aps_api.h"
#include "aps/aps_internal.h"
#include "zdo/zdo_api.h"
#include "zdo/zdp.h"
#include "zdo/zdo_internal.h"

enum {
    ZB_BUF_HDR_FLAGS3_OFFSET = OFFSETOF(zb_buf_t, hdr) + 3,
};

typedef enum {
    ZDO_NWK_MGR_STATE_IDLE = 0,
    ZDO_NWK_MGR_STATE_FORMATION = 1,
    ZDO_NWK_MGR_STATE_START_ROUTER = 2,
    ZDO_NWK_MGR_STATE_NWK_DISC = 3,
    ZDO_NWK_MGR_STATE_ASSOC_JOIN = 4,
    ZDO_NWK_MGR_STATE_REJOIN = 5,
    ZDO_NWK_MGR_STATE_DIRECT_JOIN = 6,
    ZDO_NWK_MGR_STATE_ED_SCAN = 7,
} zdo_nwk_mgr_state_t;

u8 zdo_mgmt_nwk_flag = 0;
zdo_nwk_manager_t g_zdo_nwk_manager = {0};

#if 0 /* Vendor-pinned offsets disabled in Zephyr port. */
STATIC_ASSERT(OFFSETOF(zdo_nwk_manager_t, discEvt) == 0);
STATIC_ASSERT(OFFSETOF(zdo_nwk_manager_t, authEvt) == 4);
STATIC_ASSERT(OFFSETOF(zdo_nwk_manager_t, pollEvt) == 8);
STATIC_ASSERT(OFFSETOF(zdo_nwk_manager_t, backoffEvt) == 12);
STATIC_ASSERT(OFFSETOF(zdo_nwk_manager_t, nwkDiscCb) == 16);
STATIC_ASSERT(OFFSETOF(zdo_nwk_manager_t, savedBuf) == 20);
STATIC_ASSERT(OFFSETOF(zdo_nwk_manager_t, scanChannels) == 24);
STATIC_ASSERT(OFFSETOF(zdo_nwk_manager_t, backoffTime) == 28);
STATIC_ASSERT(OFFSETOF(zdo_nwk_manager_t, backoffIter) == 30);
STATIC_ASSERT(OFFSETOF(zdo_nwk_manager_t, rejoinCnt) == 32);
STATIC_ASSERT(OFFSETOF(zdo_nwk_manager_t, scanDuration) == 33);
STATIC_ASSERT(OFFSETOF(zdo_nwk_manager_t, nwkDiscAttempt) == 34);
STATIC_ASSERT(OFFSETOF(zdo_nwk_manager_t, scanCount) == 35);
STATIC_ASSERT(OFFSETOF(zdo_nwk_manager_t, linkRetryCnt) == 36);
STATIC_ASSERT(OFFSETOF(zdo_nwk_manager_t, state) == 37);
STATIC_ASSERT(sizeof(zdo_nwk_manager_t) == 0x26);
#endif
void zdo_startup_complete(void *arg);
void zdo_startDeviceCnf(void *arg, u8 status);

#if defined(ZB_ROUTER_ROLE)
static void zdo_nlme_start_router_req(void *arg)
{
    nlme_startRouter_req_t *req = (nlme_startRouter_req_t *)arg;

    req->beaconOrder = 15;
    req->superframeOrder = 15;
    req->batteryLifeExt = 0;

    zdo_nwk_mngr()->state = ZDO_NWK_MGR_STATE_START_ROUTER;
    tl_zbPrimitivePost(TL_Q_HIGH2NWK, NWK_NLME_START_ROUTER_REQ, arg);
}
#endif

zdo_status_t zdo_nlmeNwkDiscReq(void *arg)
{
    (void)arg;

    zb_buf_t *buf = zb_buf_allocate();
    if (buf == NULL) {
        return ZDO_INSUFFICIENT_SPACE;
    }

    zdo_nwk_mngr()->state = ZDO_NWK_MGR_STATE_NWK_DISC;
    COPY_U32TOBUFFER(buf, zdo_nwk_mngr()->scanChannels);
    ((u8 *)buf)[4] = zdo_nwk_mngr()->scanDuration;
    tl_zbPrimitivePost(TL_Q_HIGH2NWK, NWK_NLME_NWK_DISCOVERY_REQ, buf);
    return ZDO_SUCCESS;
}

int zdo_nwkDiscReqTimerCb(void *arg)
{
    (void)arg;

    if (zdo_nlmeNwkDiscReq(NULL) == ZDO_SUCCESS) {
        zdo_nwk_mngr()->discEvt = NULL;
        return -1;
    }

    return 0;
}

zdo_status_t zdo_nlme_edScan(void *arg)
{
    COPY_U32TOBUFFER(arg, zdo_nwk_mngr()->scanChannels);
    ((u8 *)arg)[4] = zdo_nwk_mngr()->scanDuration;
    tl_zbPrimitivePost(TL_Q_HIGH2NWK, NWK_NLME_ED_SCAN_REQ, arg);
    return ZDO_SUCCESS;
}

#if defined(ZB_ROUTER_ROLE)
zdo_status_t zdo_nlmePermitJoinReq(u8 permitDuration)
{
    zb_buf_t *buf = zb_buf_allocate();

    if (buf == NULL) {
        return ZDO_INSUFFICIENT_SPACE;
    }

    ((u8 *)buf)[0] = permitDuration;
    tl_zbPrimitivePost(TL_Q_HIGH2NWK, NWK_NLME_PERMIT_JOINING_REQ, buf);
    return ZDO_SUCCESS;
}

void zdo_nlmePermitJoinCnf(void *arg)
{
    zb_buf_free((zb_buf_t *)arg);
}

zdo_status_t zdo_routeDiscReq(nlme_routeDisc_req_t *pRouteDiscReq)
{
    zb_buf_t *buf = zb_buf_allocate();

    if (buf == NULL) {
        return ZDO_INSUFFICIENT_SPACE;
    }

    memcpy(buf, pRouteDiscReq, sizeof(*pRouteDiscReq));
    tl_zbPrimitivePost(TL_Q_HIGH2NWK, NWK_NLME_ROUTE_DISCOVERY_REQ, buf);
    return ZDO_SUCCESS;
}

void zdo_routeDiscCnf(void *arg)
{
    zb_buf_free((zb_buf_t *)arg);
}

zdo_status_t zdo_nwkRouterStart(void)
{
    zb_buf_t *buf;

    if (zdo_nwk_mngr()->state != ZDO_NWK_MGR_STATE_IDLE) {
        return ZDO_INVALID_REQUEST;
    }

    buf = zb_buf_allocate();
    if (buf == NULL) {
        return ZDO_INSUFFICIENT_SPACE;
    }

    *(u8 *)&g_zbNIB.capabilityInfo = af_nodeMacCapabilityGet();
    zdo_nlme_start_router_req(buf);
    return ZDO_SUCCESS;
}

void zdo_nlme_start_router_confirm(void *arg)
{
    if (zdo_nwk_mngr()->state != ZDO_NWK_MGR_STATE_START_ROUTER) {
        zb_buf_free((zb_buf_t *)arg);
        return;
    }

    zdo_startDeviceCnf(arg, ((u8 *)arg)[0]);
}

zdo_status_t zdo_nwkFormationStart(u32 scanChannels, u8 scanDuration)
{
    nlme_nwkFormation_req_t *req;
    u16 nwkAddr = g_zbNIB.nwkAddr;

    if (zdo_nwk_mngr()->state != ZDO_NWK_MGR_STATE_IDLE) {
        return ZDO_INVALID_REQUEST;
    }

    req = (nlme_nwkFormation_req_t *)zb_buf_allocate();
    if (req == NULL) {
        return ZDO_INSUFFICIENT_SPACE;
    }

    req->distributedNetwork = 1;
    req->distributedNwkAddr = nwkAddr;

    if (nwkAddr == 0xfffeU) {
        do {
            nwkAddr = (u16)drv_u32Rand();
        } while (nwkAddr == 0U ||
                 nwkAddr == 0xfffeU ||
                 (nwkAddr & 0xfff8U) == 0xfff8U);
        req->distributedNwkAddr = nwkAddr;
    } else if (nwkAddr == 0U || (nwkAddr & 0xfff8U) == 0xfff8U) {
        zb_buf_free((zb_buf_t *)req);
        return 0x84;
    }

    req->scanChannels = scanChannels;
    req->batteryLifeExt = 0;
    req->scanDuration = scanDuration;
    req->beaconOrder = 15;
    req->superframeOrder = 15;

    zdo_nwk_mngr()->state = ZDO_NWK_MGR_STATE_FORMATION;
    zdo_nwk_mngr()->scanChannels = scanChannels;
    zdo_nwk_mngr()->scanDuration = scanDuration;
    *(u8 *)&g_zbNIB.capabilityInfo = af_nodeMacCapabilityGet();

    if (memcmp(aps_ib.aps_use_ext_panid, g_zero_addr, EXT_ADDR_LEN) != 0) {
        memcpy(g_zbNIB.extPANId, aps_ib.aps_use_ext_panid, EXT_ADDR_LEN);
    }

    tl_zbAdditionNeighborReset();
    tl_zbPrimitivePost(TL_Q_HIGH2NWK, NWK_NLME_NWK_FORMATION_REQ, req);
    return ZDO_SUCCESS;
}

void zdo_network_formation_confirm(void *arg)
{
    if (zdo_nwk_mngr()->state != ZDO_NWK_MGR_STATE_FORMATION) {
        zb_buf_free((zb_buf_t *)arg);
        return;
    }

    zdo_startDeviceCnf(arg, ((u8 *)arg)[0]);
}
#endif

void zdo_startDeviceCnf(void *arg, u8 status)
{
    zdo_start_device_confirm_t *cnf = (zdo_start_device_confirm_t *)arg;

    cnf->channel_num = 0;
    cnf->pan_id = 0;
    cnf->short_addr = 0;
    cnf->status = status;

    if (status == ZDO_SUCCESS) {
        cnf->channel_num = g_zbInfo.macPib.phyChannelCur;
        cnf->pan_id = g_zbInfo.macPib.panId;
        cnf->short_addr = g_zbInfo.macPib.shortAddress;
    }

    zdo_nwk_mngr()->state = ZDO_NWK_MGR_STATE_IDLE;
    tl_zbTaskPost(zdo_startup_complete, arg);
}
zdo_status_t zdo_nwkRejoinReqSend(void *arg)
{
    (void)arg;

    zb_buf_t *buf = zb_buf_allocate();
    if (buf == NULL) {
        return ZDO_INSUFFICIENT_SPACE;
    }

    zdo_nwk_mngr()->state = ZDO_NWK_MGR_STATE_REJOIN;
    tl_zbAdditionNeighborReset();

    ((u8 *)buf)[12] = 2;
    COPY_U32TOBUFFER((u8 *)buf + 8, zdo_nwk_mngr()->scanChannels);
    ((u8 *)buf)[13] = zdo_nwk_mngr()->scanDuration;
    ((u8 *)buf)[14] = af_nodeMacCapabilityGet();

    if (memcmp(aps_ib.aps_use_ext_panid, g_zero_addr, EXT_ADDR_LEN) == 0) {
        memcpy(buf, g_zbInfo.nwkNib.extPANId, EXT_ADDR_LEN);
        ((u8 *)buf)[15] = (u8)((aps_ib.aps_use_insecure_join & 0x01U) ^ 0x01U);
    } else {
        memcpy(buf, aps_ib.aps_use_ext_panid, EXT_ADDR_LEN);
        ((u8 *)buf)[15] = (u8)((aps_ib.aps_use_insecure_join & 0x01U) ^ 0x01U);
    }

    tl_zbPrimitivePost(TL_Q_HIGH2NWK, NWK_NLME_JOIN_REQ, buf);
    return ZDO_SUCCESS;
}
int zdo_nwkRejoinBackOffCb(void *arg)
{
    (void)arg;

    if (zdo_af_get_rejoin_duration() != 0) {
        if (zdo_nwk_mngr()->rejoinCnt < zdo_af_get_rejoin_times()) {
            zdo_nwk_mngr()->rejoinCnt++;
            zdo_nwkRejoinReqSend(NULL);
            return (int)(zdo_af_get_rejoin_duration() * 1000U);
        }

        zdo_nwk_mngr()->rejoinCnt = 0;
    }

    if (zdo_af_get_rejoin_backoff_time() == 0) {
        zdo_nwk_mngr()->backoffEvt = NULL;
        return -1;
    }

    if (zdo_af_get_rejoin_backoff_iteration() == 0) {
        u16 iter = zdo_nwk_mngr()->backoffIter;
        if (iter != 0xffff) {
            iter++;
            zdo_nwk_mngr()->backoffIter = iter;
        }
    } else {
        u16 iter = zdo_nwk_mngr()->backoffIter;
        if (iter >= zdo_af_get_rejoin_backoff_iteration()) {
            zdo_nwk_mngr()->backoffTime = 0;
            iter = 0;
        }
        zdo_nwk_mngr()->backoffIter = iter + 1;
    }

    if (zdo_nwk_mngr()->backoffTime > zdo_af_get_max_rejoin_backoff_time()) {
        zdo_nwk_mngr()->backoffTime = zdo_af_get_max_rejoin_backoff_time();
    } else {
        zdo_nwk_mngr()->backoffTime =
            (u16)(zdo_af_get_rejoin_backoff_time() * zdo_nwk_mngr()->backoffIter);
    }

    zdo_nwkRejoinReqSend(NULL);
    return (int)(zdo_nwk_mngr()->backoffTime * 1000U);
}
int zdo_auth_check_timer_cb(void *arg)
{
    tl_zbNwkAddrMapInit();
    tl_zbNeighborTableInit();
    zdo_startDeviceCnf(arg, ZDO_NOT_AUTHORIZED);
    zdo_nwk_mngr()->savedBuf = NULL;
    zdo_nwk_mngr()->authEvt = NULL;
    return -1;
}

bool zb_isUnderRejoinMode(void) { return zdo_nwk_mngr()->state == ZDO_NWK_MGR_STATE_REJOIN; }

void zdo_reset_req(void *arg)
{
    zb_buf_t *buf = zb_buf_allocate();
    if (buf == NULL) {
        return;
    }

    ((u8 *)buf)[0] = (u8)(u32)arg;
    tl_zbPrimitivePost(TL_Q_HIGH2NWK, NWK_NLME_RESET_REQ, buf);
}

void zdo_reset_confirm_cb(void *arg)
{
    nlme_reset_cnf_t cnf;

    if (zdoAppIndCbLst != NULL && zdoAppIndCbLst->zdpResetCnfCb != NULL) {
        cnf.status = ((u8 *)arg)[0];
        zdoAppIndCbLst->zdpResetCnfCb(&cnf);
    }

    zb_buf_free((zb_buf_t *)arg);
}
zdo_status_t zdo_nlmeEdScanReq(u32 scanChannels, u8 scanDuration, u8 scanCount)
{
    if (scanCount == 0 || zdo_nwk_mngr()->state != ZDO_NWK_MGR_STATE_IDLE) {
        return ZDO_INVALID_REQUEST;
    }

    zb_buf_t *buf = zb_buf_allocate();
    if (buf == NULL) {
        return ZDO_INSUFFICIENT_SPACE;
    }

    zdo_nwk_mngr()->state = ZDO_NWK_MGR_STATE_ED_SCAN;
    zdo_nwk_mngr()->scanChannels = scanChannels;
    zdo_nwk_mngr()->scanDuration = scanDuration;
    zdo_nwk_mngr()->scanCount = scanCount;
    zdo_nlme_edScan(buf);
    return ZDO_SUCCESS;
}

void zdo_nlme_ed_scan_confirm(void *arg)
{
    if (zdo_nwk_mngr()->state == ZDO_NWK_MGR_STATE_ED_SCAN) {
        u8 scanCount = (u8)(zdo_nwk_mngr()->scanCount - 1U);

        zdo_nwk_mngr()->scanCount = scanCount;
        if (scanCount != 0) {
            zdo_nlme_edScan(arg);
            return;
        }

        zdo_nwk_mngr()->state = ZDO_NWK_MGR_STATE_IDLE;
        if ((zdo_mgmt_nwk_flag & 0x01U) != 0U) {
            zdo_mgmt_nwk_flag &= (u8)~0x01U;
            tl_zbTaskPost(zdo_nwkUpdateNotifyRespSend, arg);
            return;
        }
    }

    zb_buf_free((zb_buf_t *)arg);
}
void zdo_nlme_sync_confirm(void *arg)
{
    nlme_sync_cnf_t cnf;
    u8 status = ((u8 *)arg)[0];

    if (zdoAppIndCbLst != NULL && zdoAppIndCbLst->zdoNlmeSyncCnfCb != NULL) {
        cnf.status = status;
        zdoAppIndCbLst->zdoNlmeSyncCnfCb(&cnf);
    }

    if (status != 0xe1U && status != 0xe9U) {
        zdo_nwk_mngr()->linkRetryCnt = 0;
    }

    zb_buf_free((zb_buf_t *)arg);
}

void zdo_syncReq(void *arg)
{
    (void)arg;

    zb_buf_t *buf = zb_buf_allocate();
    if (buf == NULL) {
        return;
    }

    ((u8 *)buf)[0] = 0;
    tl_zbPrimitivePost(TL_Q_HIGH2NWK, NWK_NLME_SYNC_REQ, buf);
}

int pollRateCb(void *arg)
{
    (void)arg;

    zdo_syncReq(NULL);
    return 0;
}

void zdo_set_pollRate(u32 rate)
{
    if (g_zbInfo.macPib.rxOnWhenIdle != 0U) {
        return;
    }

    if (zdo_af_get_syn_rate() == rate) {
        if (rate == 0U) {
            if (zdo_nwk_mngr()->pollEvt != NULL) {
                ev_timer_taskCancel(&zdo_nwk_mngr()->pollEvt);
            }
        }

        return;
    }

    zdo_af_set_syn_rate(rate);
    if (rate != 0U) {
        ev_timer_event_t *evt = zdo_nwk_mngr()->pollEvt;

        if (evt != NULL) {
            ev_on_timer(evt, rate);
        } else {
            evt = ev_timer_taskPost(pollRateCb, NULL, rate);
            zdo_nwk_mngr()->pollEvt = evt;
        }
        return;
    }

    if (zdo_nwk_mngr()->pollEvt != NULL) {
        ev_timer_taskCancel(&zdo_nwk_mngr()->pollEvt);
    }
}

void zdo_startup_complete(void *arg)
{
    zdo_start_device_confirm_t *cnf = (zdo_start_device_confirm_t *)arg;

    if (cnf->status != ZDO_SUCCESS) {
        if (g_zbNwkCtx.is_factory_new) {
            ZB_IEEE_ADDR_ZERO(ss_ib.trust_center_address);
        }

        g_zbNwkCtx.joined = 0;
        g_zbNwkCtx.joined_pro = 0;
        zdo_set_pollRate(0);
        keepaliveMsgSendStop();
    } else {
        g_zbNwkCtx.joined = 1;
        g_zbNwkCtx.is_tc = 0;
        aps_ib.aps_authenticated = 1;
        aps_ib.aps_use_insecure_join = 0;
        zdo_device_announce_send();
        g_zbNwkCtx.is_factory_new = 0;
    }

    g_zbNwkCtx.user_state = NLME_IDLE;
    tl_zbAdditionNeighborReset();

    if (zdoAppIndCbLst != NULL && zdoAppIndCbLst->zdpStartDevCnfCb != NULL) {
        zdoAppIndCbLst->zdpStartDevCnfCb(cnf);
    }

    zb_buf_free((zb_buf_t *)arg);
}
void zdo_nlmeForgetDev(addrExt_t nodeIeeeAddr, bool rejoin)
{
    u16 shortAddr = (u16)-1;
    u16 addrMapIdx = 0;

    if (tl_zbShortAddrByExtAddr(&shortAddr, nodeIeeeAddr, &addrMapIdx) != RET_OK) {
        return;
    }

    if (!rejoin) {
        aps_bindingTblEntryDelByDstExtAddr(nodeIeeeAddr);
    }

    {
        tl_zb_normal_neighbor_entry_t *entry = nwk_neTblGetByExtAddr(nodeIeeeAddr);

        if (entry != NULL) {
            zb_addrForNeighbor_t addrInfo;

            memset(&addrInfo, 0, sizeof(addrInfo));
            addrInfo.shortAddr = tl_zbshortAddrByIdx(entry->addrmapIdx);
            tl_zbExtAddrByIdx(entry->addrmapIdx, addrInfo.extAddr);
            addrInfo.relationship = entry->relationship;

            (void)nwk_nodeAddrInfoDelete(&addrInfo);
            tl_zbNeighborTableDelete(entry);
            return;
        }
    }

    tl_zbNwkAddrMapDelete(addrMapIdx);
}
void zdo_nlme_leave_indication_cb(void *arg)
{
    nlme_leave_ind_t ind;

    memcpy(&ind, arg, sizeof(ind));
    zb_buf_free((zb_buf_t *)arg);
    zdo_nlmeForgetDev(ind.deviceAddr, ind.rejoin);

    if (zdoAppIndCbLst != NULL && zdoAppIndCbLst->zdpLeaveIndCb != NULL) {
        zdoAppIndCbLst->zdpLeaveIndCb(&ind);
    }
}

zdo_status_t zdo_nlmeLeaveReq(void *arg)
{
    zb_buf_t *buf = zb_buf_allocate();
    if (buf == NULL) {
        return ZDO_INSUFFICIENT_SPACE;
    }

    memcpy(buf, arg, sizeof(nlme_leave_req_t));
    tl_zbPrimitivePost(TL_Q_HIGH2NWK, NWK_NLME_LEAVE_REQ, buf);
    return ZDO_SUCCESS;
}

void zdo_nwkAuthTimeoutStart(void *arg)
{
    if (zdo_nwk_mngr()->authEvt != NULL) {
        zb_buf_free((zb_buf_t *)arg);
        return;
    }

    zdo_nwk_mngr()->savedBuf = arg;
    zdo_nwk_mngr()->authEvt = ev_timer_taskPost(zdo_auth_check_timer_cb, NULL, TRANSPORT_NETWORK_KEY_WAIT_TIME);
}

zdo_status_t zdo_nwkAssocJoinStart(void)
{
    zb_buf_t *buf;

    if (zdo_nwk_mngr()->state != ZDO_NWK_MGR_STATE_IDLE) {
        return ZDO_INVALID_REQUEST;
    }

    buf = zb_buf_allocate();
    if (buf == NULL) {
        return ZDO_INSUFFICIENT_SPACE;
    }

    g_zbNwkCtx.joined = 0;
    zdo_nwk_mngr()->state = ZDO_NWK_MGR_STATE_ASSOC_JOIN;

    ((u8 *)buf)[12] = 0;
    memcpy(buf, aps_ib.aps_use_ext_panid, EXT_ADDR_LEN);
    ((u8 *)buf)[14] = af_nodeMacCapabilityGet();

    tl_zbPrimitivePost(TL_Q_HIGH2NWK, NWK_NLME_JOIN_REQ, buf);
    return ZDO_SUCCESS;
}
zdo_status_t zdo_nwkRejoinStart(u32 scanChannels, u8 scanDuration)
{
    if (zdo_nwk_mngr()->state != ZDO_NWK_MGR_STATE_IDLE) {
        return ZDO_INVALID_REQUEST;
    }

    if (memcmp(aps_ib.aps_use_ext_panid, g_zero_addr, EXT_ADDR_LEN) == 0 &&
        memcmp(g_zbInfo.nwkNib.extPANId, g_zero_addr, EXT_ADDR_LEN) == 0) {
        return ZDO_INVALID_REQUEST;
    }

    g_zbNwkCtx.joined = 0;
    zdo_nwk_mngr()->scanChannels = scanChannels;
    zdo_nwk_mngr()->scanDuration = scanDuration;
    return zdo_nwkRejoinReqSend(NULL);
}
int zdo_selfLeaveProcessCb(void *arg)
{
    tl_zbNwkNlmeLeaveRequest(arg);
    return -1;
}
void zdo_nwkRejoinWithBackOffStop(void)
{
    void *evt = zdo_nwk_mngr()->backoffEvt;

    if (evt != NULL) {
        ev_timer_taskCancel(&zdo_nwk_mngr()->backoffEvt);
    }

    zdo_nwk_mngr()->rejoinCnt = 0;
    zdo_nwk_mngr()->backoffIter = 0;
    zdo_nwk_mngr()->backoffTime = 0;
}
void zdo_nlme_leave_confirm_cb(void *arg)
{
    nlme_leave_cnf_t cnf;
    bool rejoin;

    memcpy(&cnf, arg, sizeof(cnf));
    rejoin = ((((u8 *)arg)[ZB_BUF_HDR_FLAGS3_OFFSET] >> 2) & 0x01U) != 0U;
    zb_buf_free((zb_buf_t *)arg);

    if (memcmp(cnf.deviceAddr, g_zero_addr, EXT_ADDR_LEN) == 0 ||
        memcmp(cnf.deviceAddr, g_zbInfo.macPib.extAddress, EXT_ADDR_LEN) == 0) {
        zdo_nwkRejoinWithBackOffStop();
        g_zbNwkCtx.user_state = NLME_LEAVING;
        zdo_set_pollRate(0);
        keepaliveMsgSendStop();
        ev_timer_taskPost(zdo_selfLeaveProcessCb, (void *)(u32)rejoin, 200);
        return;
    }

    if (cnf.status != 0x20U && cnf.status != 0U) {
        zdo_nlmeForgetDev(cnf.deviceAddr, rejoin);
    }
}
zdo_status_t zdo_nwkRejoinWithBackOff(u32 scanChannels, u8 scanDuration)
{
    if (zdo_nwk_mngr()->state != ZDO_NWK_MGR_STATE_IDLE) {
        return ZDO_INVALID_REQUEST;
    }

    if (memcmp(aps_ib.aps_use_ext_panid, g_zero_addr, EXT_ADDR_LEN) == 0 &&
        memcmp(g_zbInfo.nwkNib.extPANId, g_zero_addr, EXT_ADDR_LEN) == 0) {
        return ZDO_INVALID_REQUEST;
    }

    if (zdo_af_get_rejoin_times() == 0 ||
        zdo_nwk_mngr()->backoffEvt != NULL) {
        return ZDO_INVALID_REQUEST;
    }

    g_zbNwkCtx.joined = 0;
    zdo_nwk_mngr()->scanChannels = scanChannels;
    zdo_nwk_mngr()->scanDuration = scanDuration;
    zdo_nwk_mngr()->rejoinCnt = 0;
    zdo_nwk_mngr()->backoffIter = 0;
    zdo_nwk_mngr()->backoffTime = 0;

    ev_timer_event_t *evt = ev_timer_taskPost(zdo_nwkRejoinBackOffCb, NULL, drv_u32Rand() % 1000U);
    zdo_nwk_mngr()->backoffEvt = evt;
    return ZDO_SUCCESS;
}
zdo_status_t zdo_nwkDirectJoinStart(u32 scanChannels, u8 scanDuration)
{
    zb_buf_t *buf;

    if (zdo_nwk_mngr()->state != ZDO_NWK_MGR_STATE_IDLE) {
        return ZDO_INVALID_REQUEST;
    }

    if (memcmp(aps_ib.aps_use_ext_panid, g_zero_addr, EXT_ADDR_LEN) == 0 &&
        memcmp(g_zbInfo.nwkNib.extPANId, g_zero_addr, EXT_ADDR_LEN) == 0) {
        return ZDO_INVALID_REQUEST;
    }

    buf = zb_buf_allocate();
    if (buf == NULL) {
        return ZDO_INSUFFICIENT_SPACE;
    }

    g_zbNwkCtx.joined = 0;
    zdo_nwk_mngr()->state = ZDO_NWK_MGR_STATE_DIRECT_JOIN;
    zdo_nwk_mngr()->scanChannels = scanChannels;
    zdo_nwk_mngr()->scanDuration = scanDuration;

    ((u8 *)buf)[12] = 1;
    COPY_U32TOBUFFER((u8 *)buf + 8, scanChannels);
    ((u8 *)buf)[13] = scanDuration;
    ((u8 *)buf)[14] = af_nodeMacCapabilityGet();

    if (memcmp(aps_ib.aps_use_ext_panid, g_zero_addr, EXT_ADDR_LEN) == 0) {
        memcpy(buf, g_zbInfo.nwkNib.extPANId, EXT_ADDR_LEN);
    } else {
        memcpy(buf, aps_ib.aps_use_ext_panid, EXT_ADDR_LEN);
    }

    tl_zbPrimitivePost(TL_Q_HIGH2NWK, NWK_NLME_JOIN_REQ, buf);
    return ZDO_SUCCESS;
}

#if defined(ZB_ROUTER_ROLE)
typedef struct _attribute_packed_ {
    addrExt_t srcAddr;
    addrExt_t devAddr;
    u16 devShortAddr;
    u8 useParent;
    u8 rejoinNwk;
    bool secureRejoin;
} zdo_child_auth_req_t;

#if 0 /* Vendor-pinned sizes disabled in Zephyr port. */
STATIC_ASSERT(sizeof(zdo_child_auth_req_t) == 21);
STATIC_ASSERT(sizeof(nlme_directJoin_req_t) == 11);
#endif
extern void ss_zdoChildAuthStart(void *arg);

zdo_status_t zdo_nwkDirectJoinAccept(nlme_directJoin_req_t *pReq)
{
    zb_buf_t *buf = zb_buf_allocate();

    if (buf == NULL) {
        return ZDO_INSUFFICIENT_SPACE;
    }

    memcpy(buf, pReq, sizeof(*pReq));
    tl_zbPrimitivePost(TL_Q_HIGH2NWK, NWK_NLME_DIRECT_JOIN_REQ, buf);
    return ZDO_SUCCESS;
}

void zdo_nlme_direct_join_confirm(void *arg)
{
    zb_buf_free((zb_buf_t *)arg);
}

void zdo_nlme_join_indication(void *arg)
{
    zdo_child_auth_req_t req;

    memset(&req, 0, sizeof(req));
    memcpy(req.devAddr, arg, EXT_ADDR_LEN);
    req.devShortAddr = ((u8 *)arg)[8] | ((u16)((u8 *)arg)[9] << 8);
    req.rejoinNwk = ((u8 *)arg)[11];
    req.secureRejoin = ((u8 *)arg)[12];

    memcpy(arg, &req, sizeof(req));
    ss_zdoChildAuthStart(arg);
}
#endif

void zdo_nlme_status_indication(void *arg)
{
    u8 status = ((u8 *)arg)[2];

    if (status == 17U || status == 18U) {
        if (zdoAppIndCbLst != NULL && zdoAppIndCbLst->nwkStatusIndCb != NULL) {
            zdo_nwk_status_ind_t ind;

            ind.shortAddr = (u16)((u8 *)arg)[0] | ((u16)((u8 *)arg)[1] << 8);
            ind.status = status;
            zdoAppIndCbLst->nwkStatusIndCb(&ind);
        }

        zb_buf_free((zb_buf_t *)arg);
        return;
    }

    if (status != 9U) {
        zb_buf_free((zb_buf_t *)arg);
        return;
    }

    zb_buf_free((zb_buf_t *)arg);
    if (zdo_af_get_link_retry_threshold() == 0U) {
        zdo_nwk_mngr()->linkRetryCnt = 0;
        return;
    }

    zdo_nwk_mngr()->linkRetryCnt++;

    if (zdo_nwk_mngr()->linkRetryCnt <= zdo_af_get_link_retry_threshold() || g_bdbCtx.forceJoin != 0U) {
        return;
    }

    if (zdo_nwkDirectJoinStart(1UL << g_zbInfo.macPib.phyChannelCur,
                               zdo_cfg_attributes.config_nwk_scan_duration) == ZDO_SUCCESS) {
        zdo_set_pollRate(0);
        zdo_nwk_mngr()->linkRetryCnt = 0;
    }
}

void zdo_nlme_join_confirm(void *arg)
{
    u8 state = zdo_nwk_mngr()->state;
    u8 status = ((u8 *)arg)[2];
    extern volatile u32 zb_nwk_ed_trace[];

    /*
     * slot[43] layout, decoded byte-by-byte:
     *   byte0 (low 8)   = total invocations & 0xff
     *   byte1 (bits 8-15) = invocations dropped because state was not
     *                       ASSOC_JOIN / REJOIN / DIRECT_JOIN
     *   byte2 (bits 16-23) = LAST observed `state` value
     *   byte3 (bits 24-31) = LAST observed `status` value
     *
     * Lets the SWS reader correlate "we got the SUCCESS CNF from MAC"
     * (slot[6]) with "but ZDO refused it because state was IDLE
     * already" — the actual gate that prevents zdo_nwkAuthTimeoutStart
     * from arming the authEvt that ss_zdoTransportKeyIndHandle needs.
     */
    {
        u32 prev = zb_nwk_ed_trace[43];
        u32 count = (prev & 0xffU) + 1U;
        u32 drops = (prev >> 8) & 0xffU;
        bool wrong_state = (state != ZDO_NWK_MGR_STATE_ASSOC_JOIN &&
                            state != ZDO_NWK_MGR_STATE_REJOIN &&
                            state != ZDO_NWK_MGR_STATE_DIRECT_JOIN);

        if (wrong_state) {
            drops = (drops + 1U) & 0xffU;
        }
        zb_nwk_ed_trace[43] = (count & 0xffU) |
                              ((drops & 0xffU) << 8) |
                              (((u32)state & 0xffU) << 16) |
                              (((u32)status & 0xffU) << 24);
    }

    if (state != ZDO_NWK_MGR_STATE_ASSOC_JOIN &&
        state != ZDO_NWK_MGR_STATE_REJOIN &&
        state != ZDO_NWK_MGR_STATE_DIRECT_JOIN) {
        /*
         * Late-success CNF arrived after the NWK-side NO_DATA retry
         * already aborted the cycle to IDLE. On TLSR8258 this is the
         * common case — the wait timer beats the deferred AssocResp
         * dispatch by 5-8 s (zephyr-docs/router-rx-fix-step0-1-2-
         * progress-2026-06-20.md). If we drop the late SUCCESS at
         * the state gate, zdo_nwkAuthTimeoutStart never arms authEvt
         * and ss_zdoTransportKeyIndHandle silently drops every
         * inbound Transport-Key frame.
         *
         * Adopt the late SUCCESS: snap the state back to ASSOC_JOIN
         * so the rest of this function runs and arms the auth timer.
         * Non-SUCCESS late confirms keep the original "drop at gate"
         * behaviour — replaying a failure into a finished cycle has
         * no upside.
         */
        if (status == 0U) {
            zdo_nwk_mngr()->state = ZDO_NWK_MGR_STATE_ASSOC_JOIN;
            state = ZDO_NWK_MGR_STATE_ASSOC_JOIN;
        } else {
            zb_buf_free((zb_buf_t *)arg);
            return;
        }
    }

    if (state == ZDO_NWK_MGR_STATE_DIRECT_JOIN) {
        zdo_startDeviceCnf(arg, (status == 0U) ? ZDO_SUCCESS : ZDO_NETWORK_LOST);
        return;
    }

    if (state == ZDO_NWK_MGR_STATE_REJOIN) {
        ((u8 *)arg)[ZB_BUF_HDR_FLAGS3_OFFSET] |= 0x20U;
        if (status != 0U) {
            if (zdo_nwk_mngr()->backoffEvt != NULL) {
                zdo_nwk_mngr()->state = ZDO_NWK_MGR_STATE_IDLE;
                zb_buf_free((zb_buf_t *)arg);
                return;
            }

            zdo_startDeviceCnf(arg, status);
            return;
        }

        zdo_nwkRejoinWithBackOffStop();
    } else if (status != 0U) {
        zdo_startDeviceCnf(arg, status);
        return;
    }

    zdo_set_pollRate(500U);
    if (aps_ib.aps_authenticated || ss_ib.securityLevel == 0U) {
        zdo_startDeviceCnf(arg, ZDO_SUCCESS);
        return;
    }

    zdo_nwkAuthTimeoutStart(arg);
}
zdo_status_t zdo_nwkDiscoveryStart(nlme_nwkDisc_req_t *pReq, nwkDiscoveryUserCb_t cb)
{
    if (zdo_af_get_scan_attempts() == 0U ||
        zdo_af_get_nwk_time_btwn_scans() == 0U ||
        cb == NULL) {
        return ZDO_NOT_PERMITTED;
    }

    if (zdo_nwk_mngr()->state != ZDO_NWK_MGR_STATE_IDLE || zdo_nwk_mngr()->discEvt != NULL) {
        return ZDO_INVALID_REQUEST;
    }

    tl_zbAdditionNeighborReset();
    zdo_nwk_mngr()->scanChannels = pReq->scanChannels;
    zdo_nwk_mngr()->scanDuration = pReq->scanDuration;
    zdo_nwk_mngr()->nwkDiscAttempt = 0;
    zdo_nwk_mngr()->nwkDiscCb = cb;

    return zdo_nlmeNwkDiscReq(NULL);
}

void zdo_nwkDiscoveryStop(void)
{
    if (zdo_nwk_mngr()->discEvt != NULL) {
        ev_timer_taskCancel(&zdo_nwk_mngr()->discEvt);
    }

    zdo_nwk_mngr()->nwkDiscAttempt = 0;
    zdo_nwk_mngr()->nwkDiscCb = NULL;
}

void zdo_nlme_network_discovery_confirm_cb(void *arg)
{
    nwkDiscoveryUserCb_t cb;
    u8 attempt;

    zb_buf_free((zb_buf_t *)arg);

    if (zdo_nwk_mngr()->state != ZDO_NWK_MGR_STATE_NWK_DISC) {
        return;
    }

    zdo_nwk_mngr()->state = ZDO_NWK_MGR_STATE_IDLE;
    cb = zdo_nwk_mngr()->nwkDiscCb;
    if (cb == NULL) {
        zdo_nwk_mngr()->nwkDiscAttempt = 0;
        return;
    }

    attempt = (u8)(zdo_nwk_mngr()->nwkDiscAttempt + 1U);
    zdo_nwk_mngr()->nwkDiscAttempt = attempt;
    if (attempt < zdo_af_get_scan_attempts()) {
        zdo_nwk_mngr()->discEvt = ev_timer_taskPost(zdo_nwkDiscReqTimerCb,
                                                     NULL,
                                                     zdo_af_get_nwk_time_btwn_scans());
        return;
    }

    zdo_nwk_mngr()->nwkDiscAttempt = 0;
    cb();
}

bool zdo_ifZdoNwkManagerIdle(void)
{
    return zdo_nwk_mngr()->state == ZDO_NWK_MGR_STATE_IDLE;
}
