/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Adapted from libzigbee/src/nwk_nlme.c (~280 LOC). Vendor file kept
 * structurally one-for-one. Only the include layout changes
 * (vendor zb_local.h + ev_timer.h → zb_common_stub.h + nwk_internal.h
 * + os/ev_timer.h + mac/includes).
 */
#include "zb_common_stub.h"
#include "os/ev_timer.h"
#include "mac/includes/tl_zb_mac.h"
#include "nwk/includes/nwk.h"
#include "nwk/includes/nwk_internal.h"

/* ZDO confirm callbacks invoked through tl_zbTaskPost — provided by
 * the upcoming ZDO port. Declared here as opaque externs so this TU
 * links once the dispatcher is wired.
 */
extern void zdo_nlme_start_router_confirm(void *arg);
extern void zdo_nlme_ed_scan_confirm(void *arg);
extern void zdo_reset_confirm_cb(void *arg);
extern void zdo_nlme_sync_confirm(void *arg);
extern void zdo_nlme_status_indication(void *arg);

/* Beacon-payload / NIB-init / leave helpers in already-ported NWK
 * TUs that aren't exposed via nwk.h.
 */
extern void tl_zbNwkBeaconPayloadUpdate(void);
extern void tl_zbNwkNibInit(u8 coldReset);
extern int nwkLeaveReqSend(void *arg, nwk_hdr_t *pNwkHdr, nwkCmd_t *cmd, u8 handle);

enum {
    NWK_CTX_FLAGS0_OFFSET = 45,
    NWK_CTX_STATE_OFFSET = 47,
    UNKNOWN_ENDDEV_ADDR_INIT = 0xfffe,
};

static u16 unknownEndDevAddr_8570 __asm__("unknowEndDevAddr.8570") = UNKNOWN_ENDDEV_ADDR_INIT;

#if defined(ZB_ROUTER_ROLE)
void nwk_nlmeStartRouterCnf(void *arg, u8 status)
{
    ((u8 *)arg)[0] = status;
    g_zbNwkCtx.state = NLME_STATE_IDLE;
    tl_zbTaskPost(zdo_nlme_start_router_confirm, arg);
}

void tl_zbNwkNlmeStartRouterRequestHandler(void *arg)
{
    nlme_startRouter_req_t *req = (nlme_startRouter_req_t *)arg;
    u8 *raw = (u8 *)arg;

    if (g_zbNwkCtx.state != NLME_STATE_IDLE) {
        nwk_nlmeStartRouterCnf(arg, NWK_STATUS_INVALID_REQUEST);
        return;
    }

    g_zbNwkCtx.state = NLME_STATE_ROUTER_START;
    raw[4] = (u8)g_zbNIB.panId;
    raw[5] = (u8)(g_zbNIB.panId >> 8);
    raw[6] = g_zbNIB.updateId;
    raw[7] = 0;
    raw[8] = req->beaconOrder;
    raw[9] = req->superframeOrder;
    raw[10] = 0;
    raw[11] = req->batteryLifeExt ? 1U : 0U;
    tl_zbPrimitivePost(TL_Q_NWK2MAC, MAC_MLME_START_REQ, arg);
}

void nwk_startRouterCnfHandler(void *arg)
{
    u8 status = ((u8 *)arg)[0];

    if (status == MAC_SUCCESS) {
        g_zbNwkCtx.joined = 1;
        g_zbNwkCtx.router_started = 1;
        tl_zbNwkBeaconPayloadUpdate();
        tl_zbNwkLinkStatusStart();
    }

    nwk_nlmeStartRouterCnf(arg, status);
}
#endif


void tl_zbNwkNlmeEDScanRequestHandler(void *arg)
{
    u8 *req = (u8 *)arg;

    req[5] = req[4];
    req[4] = ED_SCAN;
    g_zbNwkCtx.state = NLME_STATE_ED_SCAN;

    tl_zbPrimitivePost(TL_Q_NWK2MAC, MAC_MLME_SCAN_REQ, arg);
}

void nwk_edScanCnfHandler(void *arg)
{
    g_zbNwkCtx.state = NLME_STATE_IDLE;
    tl_zbTaskPost(zdo_nlme_ed_scan_confirm, arg);
}

void tl_zbNwkNlmeResetRequestHandler(void *arg)
{
    nlme_reset_req_t *req = (nlme_reset_req_t *)arg;
    u8 state = g_zbNwkCtx.state;

    if (state != NLME_STATE_IDLE || g_zbNwkCtx.joined) {
        req->warmStart = 0;
        tl_zbTaskPost(zdo_reset_confirm_cb, arg);
        return;
    }

    if (req->warmStart) {
        tl_zbAdditionNeighborReset();
        req->warmStart = 4;
        tl_zbTaskPost(zdo_reset_confirm_cb, arg);
        return;
    }

    req->warmStart = 1;
    tl_zbPrimitivePost(TL_Q_NWK2MAC, MAC_MLME_RESET_REQ, arg);
}

void tl_zbMacMlmeResetConfirmHandler(void *arg)
{
    u8 status = ((u8 *)arg)[0];

    tl_zbNwkNibInit(1);
    tl_zbNeighborTableInit();
    memset(&g_zbNwkCtx, 0, 0x4f);
    g_zbNwkCtx.discoverRoute = 1;

    ((u8 *)arg)[0] = status;
    tl_zbTaskPost(zdo_reset_confirm_cb, arg);
}

void endDevMacDataPoll(void)
{
    tl_zb_normal_neighbor_entry_t *parent = tl_zbNeighborTableSearchForParent();

    if (parent == NULL) {
        return;
    }

    zb_buf_t *buf = zb_buf_allocate();

    if (buf == NULL) {
        return;
    }

    buf->buf[10] = 2;
    {
        u16 dst = tl_zbshortAddrByIdx(parent->addrmapIdx);
        u16 src = g_zbInfo.macPib.shortAddress;

        buf->buf[2] = (u8)dst;
        buf->buf[3] = (u8)(dst >> 8);
        buf->buf[0] = (u8)src;
        buf->buf[1] = (u8)(src >> 8);
    }

    tl_zbPrimitivePost(TL_Q_NWK2MAC, MAC_MLME_POLL_REQ, buf);
}

void tl_zbNwkNlmeSyncRequestHandler(void *arg)
{
    tl_zb_normal_neighbor_entry_t *parent = tl_zbNeighborTableSearchForParent();
    nlme_sync_req_t *req = (nlme_sync_req_t *)arg;

    if (parent != NULL && req->track == 0U) {
        ((u8 *)arg)[10] = 2;

        {
            u16 dst = tl_zbshortAddrByIdx(parent->addrmapIdx);
            u16 src = g_zbInfo.macPib.shortAddress;

            ((u8 *)arg)[2] = (u8)dst;
            ((u8 *)arg)[3] = (u8)(dst >> 8);
            ((u8 *)arg)[0] = (u8)src;
            ((u8 *)arg)[1] = (u8)(src >> 8);
        }

        tl_zbPrimitivePost(TL_Q_NWK2MAC, MAC_MLME_POLL_REQ, arg);
        return;
    }

    ((u8 *)arg)[0] = NWK_STATUS_INVALID_PARAMETER;
    tl_zbTaskPost(zdo_nlme_sync_confirm, arg);
}

void tl_zbMacMlmeSyncLossIndicationHandler(void *arg)
{
    zb_buf_free((zb_buf_t *)arg);
}

void tl_zbNwkNlmeNwkStatusInd(void *arg, u16 nwkAddr, nwk_statusCode_t status)
{
    nlme_nwkStatus_ind_t *ind = (nlme_nwkStatus_ind_t *)arg;

    ind->nwkAddr = nwkAddr;
    ind->status = (u8)status;

    tl_zbTaskPost(zdo_nlme_status_indication, arg);
}

void tl_zbMacMlmePollConfirmHandler(void *arg)
{
    u8 status = ((u8 *)arg)[0];
    u8 state = g_zbNwkCtx.state;

    if (state == NLME_STATE_REJOIN) {
        zb_buf_free((zb_buf_t *)arg);
        return;
    }

    ((u8 *)arg)[0] = status;
    tl_zbTaskPost(zdo_nlme_sync_confirm, arg);

    {
        tl_zb_normal_neighbor_entry_t *parent = tl_zbNeighborTableSearchForParent();

        if (status == MAC_STA_NO_ACK) {
            if (parent != NULL) {
                zb_buf_t *buf = zb_buf_allocate();

                if (buf != NULL) {
                    tl_zbNwkNlmeNwkStatusInd(buf,
                                             tl_zbshortAddrByIdx(parent->addrmapIdx),
                                             NWK_COMMAND_STATUS_PARENT_LINK_FAILURE);
                }
            }

            return;
        }

        if (parent != NULL) {
            parent->timeoutCnt = parent->devTimeout;
        }
    }
}

void tl_zbMacMlmePollIndicationHandler(void *arg)
{
    u8 *pollInd = (u8 *)arg;

    if (!g_zbNwkCtx.joined ||
        (g_zbInfo.nwkNib.parentInfo & END_DEV_TIMEOUT_REQ_KEEPALIVE_BIT) == 0U) {
        zb_buf_free((zb_buf_t *)arg);
        return;
    }

    if (pollInd[0] != ZB_ADDR_16BIT_DEV_OR_BROADCAST) {
        zb_buf_free((zb_buf_t *)arg);
        return;
    }

    {
        u16 shortAddr = (u16)pollInd[1] | ((u16)pollInd[2] << 8);
        tl_zb_normal_neighbor_entry_t *entry = nwk_neTblGetByShortAddr(shortAddr);

        if (entry != NULL) {
            if (entry->devTimeout == 0U) {
                zb_buf_free((zb_buf_t *)arg);
                return;
            }

            entry->timeoutCnt = entry->devTimeout;
            entry->keepaliveRcvd = 1;
            zb_buf_free((zb_buf_t *)arg);
            return;
        }

        if (unknownEndDevAddr_8570 != UNKNOWN_ENDDEV_ADDR_INIT) {
            if (unknownEndDevAddr_8570 == shortAddr) {
                unknownEndDevAddr_8570 = UNKNOWN_ENDDEV_ADDR_INIT;
            }

            zb_buf_free((zb_buf_t *)arg);
            return;
        }

        unknownEndDevAddr_8570 = shortAddr;

        {
            nwk_hdr_t nwkHdr;
            nwkCmd_t cmd;
            u8 *hdr = (u8 *)&nwkHdr;

            memset(&nwkHdr, 0, sizeof(nwkHdr));
            memset(&cmd, 0, sizeof(cmd));

            nwkHdr.dstAddr = shortAddr;
            if (!ZB_NWK_IS_ADDRESS_BROADCAST(shortAddr) && zb_address_ieee_by_short(shortAddr, nwkHdr.dstIeeeAddr) == 0U) {
                hdr[5] |= 0x08U;
            }

            cmd.cmdId = NWK_CMD_LEAVE;
            cmd.leave.options.rejoin = 1;
            cmd.leave.options.request = 1;

            if (shortAddr == NWK_BROADCAST_RX_ON_WHEN_IDLE) {
                cmd.leave.options.request = 0;
            }

            nwkLeaveReqSend(arg, &nwkHdr, &cmd, NWK_INTERNAL_LEAVE_REQ_CMD_INDIRECT_HANDLE);
        }
    }
}
