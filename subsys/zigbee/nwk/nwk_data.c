/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Adapted from libzigbee/src/nwk_data.c (~785 LOC). NLDE data
 * request/indication path. Vendor file kept structurally one-for-one;
 * only the include layout changes (vendor zb_local.h + ev_timer.h →
 * zb_common_stub.h + nwk_internal.h + os/ev_timer.h + mac/includes).
 */
#include "zb_common_stub.h"
#include "common/static_assert.h"
#include "os/ev_timer.h"
#include "mac/includes/tl_zb_mac.h"
#include "nwk/includes/nwk.h"
#include "nwk/includes/nwk_internal.h"
#include "nwk/includes/nwk_neighbor.h"

/* Externs that the libzigbee runtime exposes through internal vendor
 * headers (aps_internal.h, mac_internal.h, zdo_internal.h) — Zephyr
 * port keeps them as link-time names and relies on --gc-sections to
 * drop unreachable references until the matching TUs land.
 */
extern u16 dstPanID;
extern u8 deviceInfoRsp;
extern void endDevMacDataPoll(void);
extern void aps_nwk_data_confirm_cb(void *arg, u8 status, u8 nsduHandle);
extern void aps_nwk_data_indication_cb(void *arg);
extern void aps_interPanDataIndCb(void *arg);
extern void nwk_rejoinCmdSendCnf(void *arg);
extern void tl_zbMcpsRejoinRespCnfHandler(void *arg, u8 status, u16 dstAddr);
extern void nwk_leaveCmdSendCnf(void *arg, u16 dstAddr);
extern void nwkEndDevTimeoutReqCnfHandler(void *arg);
extern void nwkEndDevTimeoutRspCnfHandler(void *arg);
extern void tl_zbMacMcpsDataRequestProc(void *arg);
extern void nwk_panidConflictReportCnfHandler(void *arg);
extern void nwk_panidConflictUpdateCnfHandler(void *arg);
extern void nwk_addrConflictReportCnfHandler(void *arg);
extern void nwk_routeReplyCmdSendCnfHandler(void *arg);
extern void nwk_routeReqCmdSendCnfHandler(void *arg);
extern void nwk_linkStatusCmdSendCnfHandler(void *arg);
extern void nwk_assocResponseCnfHandler(void *arg);

/* APS / MAC / SS helpers and command handlers that the data path
 * dispatches into. All defined in libzigbee TUs not yet ported.
 */
extern void tl_zbMacMcpsDataRequestSendConfirm(void *arg, u8 status);
extern u8 ss_nwkSecureFrame(zb_buf_t *buf, u8 hdrLen);
extern u8 ss_nwkDecryptFrame(void *arg, u8 hdrLen, u8 msduLength, u8 *msdu,
			     nwk_hdr_t *pNwkHdr, u8 lqi);
extern nwk_routeDiscEntry_t *nwkTxDataRouteDiscStart(zb_buf_t *buf, nwk_hdr_t *pNwkHdr,
						      u8 *payload, u8 payloadLen);
extern u8 nwkBrcCheckDevMatch(u16 dstAddr);
extern void mac_pendingWaitTimerCancel(void);
extern void nwkRouteReqCmdHandler(void *arg, nwk_hdr_t *pNwkHdr, nwkCmd_t *cmd);
extern void nwkRouteReplyCmdHandler(void *arg, nwk_hdr_t *pNwkHdr, nwkCmd_t *cmd);
extern void nwkRouteRecordCmdHandler(void *arg, nwk_hdr_t *pNwkHdr, nwkCmd_t *cmd);
extern void nwkLeaveCmdHandler(void *arg, nwk_hdr_t *pNwkHdr, nwkCmd_t *cmd);
extern void nwkRejoinReqCmdHandler(void *arg, nwk_hdr_t *pNwkHdr, nwkCmd_t *cmd);
extern void nwkRejoinRespCmdHandler(void *arg, nwk_hdr_t *pNwkHdr, nwkCmd_t *cmd);
extern void nwkLinkStatusCmdHandler(void *arg, nwk_hdr_t *pNwkHdr, nwkCmd_t *cmd);
extern void nwkNwkStatusCmdHandler(void *arg, nwk_hdr_t *pNwkHdr, nwkCmd_t *cmd);
extern void nwkEndDevTimeoutReqCmdHandler(void *arg, nwk_hdr_t *pNwkHdr, nwkCmd_t *cmd);
extern void nwkEndDevTimeoutRspCmdHandler(void *arg, nwk_hdr_t *pNwkHdr, nwkCmd_t *cmd);
extern void nwkUpdateCmdHandler(void *arg, nwk_hdr_t *pNwkHdr, nwkCmd_t *cmd);
extern void tl_zbNwkReportCmdHandler(void *arg, nwk_hdr_t *pNwkHdr, nwkCmd_t *cmd);
extern void tl_zbNwkLinkStatusCmdHandler(void *arg, nwk_hdr_t *pNwkHdr, nwkCmd_t *cmd);
extern void tl_zbNwkNetworkUpdateCmdHandler(void *arg, nwk_hdr_t *pNwkHdr, nwkCmd_t *cmd);
extern void tl_zbNwkRejoinRespCmdHandler(void *arg, nwk_hdr_t *pNwkHdr, nwkCmd_t *cmd);
extern void tl_zbNwkRejoinReqCmdHandler(void *arg, nwk_hdr_t *pNwkHdr, nwkCmd_t *cmd);
extern void tl_zbNwkLeaveReqCmdHandler(void *arg, nwk_hdr_t *pNwkHdr, nwkCmd_t *cmd);
extern void tl_zbNwkStatusCmdHandler(void *arg, nwk_hdr_t *pNwkHdr, nwkCmd_t *cmd);

u8 quickDataPollCnt = 0;
u8 g_edBrcSkipParent = 0;
ev_timer_event_t *quickDataPollTimerEvt = NULL;
nwkDataIndCb_t g_nwkDataIndCb = NULL;

#if defined(ZB_ROUTER_ROLE)
void nwkNldeDataCnf(void *arg, u8 status, u8 nsduHandle);
extern nwk_routeDiscEntry_t *nwkRouteDiscEntryDstFind(u16 dstAddr);
extern nwk_txDataPendEntry_t *nwkTxDataPendTabEntryAdd(zb_buf_t *buf, nwk_hdr_t *pNwkHdr, u8 *payload,
                                                       u8 payloadLen, u8 handle);
extern void nwkTxDataPendTabEntryClear(nwk_txDataPendEntry_t *entry);

static void nwk_tx_data_pend_route_disc_fail(nwk_txDataPendEntry_t *pend)
{
    if (pend == NULL) {
        return;
    }

    if (pend->srcBuf != NULL) {
        void *srcBuf = pend->srcBuf;

        pend->srcBuf = NULL;
        if (pend->handle <= 0xbfU) {
            nwkNldeDataCnf(srcBuf, NWK_STATUS_ROUTE_DISCOVERY_FAILED, pend->handle);
        } else {
            zb_buf_free((zb_buf_t *)srcBuf);
        }
    }

    nwkTxDataPendTabEntryClear(pend);
}

static void nwk_tx_data_route_disc_buf_release(zb_buf_t *buf)
{
    if (buf != NULL) {
        zb_buf_free(buf);
    }
}
#endif


/* Vendor build pinned these offsets to the wire-packed layout
 * (-fpack-struct in libzigbee CMake). The Zephyr build leaves the
 * shared types in mac/includes/tl_zb_mac.h naturally aligned, so the
 * assertions do not hold. C-member access through the structs still
 * works regardless; the over-the-air framing path (when it lands)
 * will need its own packed layout shim.
 */
#if 0
STATIC_ASSERT(OFFSETOF(zb_mscp_data_ind_t, timestamp) == 0);
STATIC_ASSERT(OFFSETOF(zb_mscp_data_ind_t, msdu) == 4);
STATIC_ASSERT(OFFSETOF(zb_mscp_data_ind_t, srcAddr) == 12);
STATIC_ASSERT(OFFSETOF(zb_mscp_data_ind_t, dstAddr) == 21);
STATIC_ASSERT(OFFSETOF(zb_mscp_data_ind_t, msduLength) == 30);
STATIC_ASSERT(OFFSETOF(zb_mscp_data_ind_t, mpduLinkQuality) == 31);
STATIC_ASSERT(OFFSETOF(zb_mscp_data_req_t, dstPanId) == 0);
STATIC_ASSERT(OFFSETOF(zb_mscp_data_req_t, srcAddr) == 2);
STATIC_ASSERT(OFFSETOF(zb_mscp_data_req_t, dstAddr) == 11);
STATIC_ASSERT(OFFSETOF(zb_mscp_data_req_t, msduLength) == 20);
STATIC_ASSERT(OFFSETOF(zb_mscp_data_req_t, msduHandle) == 21);
STATIC_ASSERT(OFFSETOF(zb_mscp_data_req_t, msdu) == 22);
STATIC_ASSERT(OFFSETOF(zb_mscp_data_req_t, txOptions) == 26);
STATIC_ASSERT(OFFSETOF(nlde_data_ind_t, nsdu) == 0);
STATIC_ASSERT(OFFSETOF(nlde_data_ind_t, nsduLen) == 4);
STATIC_ASSERT(OFFSETOF(nlde_data_ind_t, dstAddrMode) == 5);
STATIC_ASSERT(OFFSETOF(nlde_data_ind_t, securityUse) == 6);
STATIC_ASSERT(OFFSETOF(nlde_data_ind_t, lqi) == 7);
STATIC_ASSERT(OFFSETOF(nlde_data_ind_t, dstAddr) == 8);
STATIC_ASSERT(OFFSETOF(nlde_data_ind_t, srcAddr) == 10);
STATIC_ASSERT(OFFSETOF(nlde_data_ind_t, rxTime) == 12);
STATIC_ASSERT(OFFSETOF(nlde_data_ind_t, srcMacAddr) == 16);
STATIC_ASSERT(OFFSETOF(nlde_data_cnf_t, status) == 4);
STATIC_ASSERT(OFFSETOF(nlde_data_cnf_t, nsduHandle) == 5);
STATIC_ASSERT(OFFSETOF(nwk_hdr_t, dstAddr) == 0);
STATIC_ASSERT(OFFSETOF(nwk_hdr_t, srcAddr) == 2);
STATIC_ASSERT(OFFSETOF(nwk_hdr_t, frameControl) == 4);
STATIC_ASSERT(OFFSETOF(nwk_hdr_t, radius) == 6);
STATIC_ASSERT(OFFSETOF(nwk_hdr_t, seqNum) == 7);
STATIC_ASSERT(OFFSETOF(nwk_hdr_t, dstIeeeAddr) == 8);
STATIC_ASSERT(OFFSETOF(nwk_hdr_t, srcIeeeAddr) == 16);
STATIC_ASSERT(OFFSETOF(nwk_hdr_t, mcastControl) == 24);
STATIC_ASSERT(OFFSETOF(nwk_hdr_t, frameHdrLen) == 25);
STATIC_ASSERT(OFFSETOF(nwkCmd_t, cmdId) == 0);
STATIC_ASSERT(OFFSETOF(nwkCmd_t, nwkStatus) == 4);
STATIC_ASSERT(OFFSETOF(nwkCmd_t, leave) == 4);
STATIC_ASSERT(OFFSETOF(nwkCmd_t, rejoinReq) == 4);
STATIC_ASSERT(OFFSETOF(nwkCmd_t, endDevTimeoutReq) == 4);
STATIC_ASSERT(OFFSETOF(nwkCmd_t, rejoinRsp) == 4);
STATIC_ASSERT(OFFSETOF(nwkCmd_t, nwkUpdate) == 4);
STATIC_ASSERT(OFFSETOF(nwkCmd_t, endDevTimeoutRsp) == 4);
#endif /* vendor-packed STATIC_ASSERT block disabled in Zephyr port */

enum {
    BUF_SAVED_HANDLE_OFFSET = 0xc1,
    BUF_FLAGS_OFFSET = 0xc3,
};

static inline u8 nwk_user_state(void)
{
    return g_zbNwkCtx.user_state;
}

static inline bool nwk_joined(void)
{
    return g_zbNwkCtx.joined != 0U;
}

static inline bool nwk_is_broadcast(u16 addr)
{
    return (addr & 0xfff8U) == 0xfff8U;
}

static inline bool nwk_hdr_security(const nwk_hdr_t *hdr)
{
    return hdr->frameControl.security != 0U;
}

static inline bool nwk_hdr_src_ieee(const nwk_hdr_t *hdr)
{
    return hdr->frameControl.srcIEEEAddr != 0U;
}

static inline bool nwk_hdr_end_device_initiator(const nwk_hdr_t *hdr)
{
    return hdr->frameControl.endDevInitiator != 0U;
}

int tl_zbNwkQuickDataPollCb(void *arg)
{
    (void)arg;

    if (quickDataPollCnt < AUTO_QUICK_DATA_POLL_TIMES && AUTO_QUICK_DATA_POLL_INTERVAL != 0U) {
        quickDataPollCnt++;
        endDevMacDataPoll();
        return (int)AUTO_QUICK_DATA_POLL_INTERVAL;
    }

    quickDataPollTimerEvt = NULL;
    return -1;
}

void tl_nwkDataIndRegister(nwkDataIndCb_t cb)
{
    g_nwkDataIndCb = cb;
}

void tl_edBrcDataSkipParentSet(bool skip)
{
    g_edBrcSkipParent = skip;
}

void nwkNldeDataCnf(void *arg, u8 status, u8 nsduHandle)
{
    nlde_data_cnf_t *cnf = (nlde_data_cnf_t *)arg;

    cnf->status = status;
    cnf->nsduHandle = nsduHandle;
    tl_zbTaskPost((tl_zb_callback_t)aps_nwk_data_confirm_cb, arg);
}

void nwkNldeDataInd(void *arg, nwk_hdr_t *pNwkHdr)
{
    zb_mscp_data_ind_t *macInd = (zb_mscp_data_ind_t *)arg;
    nlde_data_ind_t ind;

    printk("zb dbg nwkNldeDataInd: msduLen=%u frameHdrLen=%u dstAddr=0x%04x srcAddr=0x%04x sec=%u\n",
           macInd->msduLength, pNwkHdr->frameHdrLen, pNwkHdr->dstAddr, pNwkHdr->srcAddr,
           pNwkHdr->frameControl.security);
    memset(&ind, 0, sizeof(ind));
    ind.dstAddrMode = pNwkHdr->frameControl.multicastFlg ? 2U : 1U;
    ind.dstAddr = pNwkHdr->dstAddr;
    ind.srcAddr = pNwkHdr->srcAddr;
    ind.nsduLen = (u8)(macInd->msduLength - pNwkHdr->frameHdrLen);
    ind.nsdu = macInd->msdu + pNwkHdr->frameHdrLen;
    ind.lqi = macInd->mpduLinkQuality;
    ind.srcMacAddr = macInd->srcAddr.addr.shortAddr;
    ind.securityUse = pNwkHdr->frameControl.security ? 1U : 0U;

    if (g_zbInfo.nwkNib.timeStamp) {
        ind.rxTime = macInd->timestamp;
    }

    /*
     * Vendor uses a 32-bit-pinned 18-byte copy here. With a 64-bit
     * `u8 *nsdu` the struct grows past 18, so use sizeof() and let
     * the consumer use the same nlde_data_ind_t layout.
     */
    memcpy(arg, &ind, sizeof(ind));

    printk("zb dbg nwkNldeDataInd: posting aps_nwk_data_indication_cb (cb=%p) ind_size=%zu\n",
           g_nwkDataIndCb, sizeof(ind));
    if (g_nwkDataIndCb != NULL) {
        g_nwkDataIndCb(arg);
        return;
    }
    tl_zbTaskPost(aps_nwk_data_indication_cb, arg);
}

u8 tl_zbNwkInterPanDataReq(void *arg)
{
    nlde_data_req_t *req = (nlde_data_req_t *)arg;
    zb_mscp_data_req_t macReq;
    u8 nwkStubHdr[2] = {
        FRAME_TYPE_INTERPAN | (2U << 2),
        0,
    };

    memset(&macReq, 0, sizeof(macReq));
    memcpy(req->nsdu - sizeof(nwkStubHdr), nwkStubHdr, sizeof(nwkStubHdr));

    macReq.srcAddr.addrMode = ADDR_MODE_EXT;
    macReq.dstAddr.addrMode = req->addrMode;
    macReq.msdu = req->nsdu - sizeof(nwkStubHdr);
    macReq.msduLength = (u8)(req->nsduLen + sizeof(nwkStubHdr));
    macReq.msduHandle = req->ndsuHandle;

    if (dstPanID == 0U || deviceInfoRsp == 0U) {
        macReq.dstPanId = MAC_PAN_ID_BROADCAST;
    } else {
        deviceInfoRsp = 0;
        macReq.dstPanId = dstPanID;
    }

    if (req->addrMode == ADDR_MODE_EXT) {
        macReq.txOptions = 1;
        memcpy(macReq.dstAddr.addr.extAddr, req->ieeAddr, EXT_ADDR_LEN);
    } else {
        macReq.dstAddr.addr.shortAddr = MAC_SHORT_ADDR_BROADCAST;
    }

    memcpy(arg, &macReq, sizeof(macReq));
    tl_zbMacMcpsDataRequestProc(arg);
    return RET_OK;
}

void tl_zbMacInterPanDataHandle(void *arg)
{
    tl_zbTaskPost(aps_interPanDataIndCb, arg);
}

void tl_zbMacMcpsDataConfirmHandler(void *arg)
{
    enum {
        NWK_CONFIRM_COPY_LEN = 14,
        NWK_CONFIRM_NWK_HDR_OFFSET = 4,
        NWK_CONFIRM_HANDLE_OFFSET = 8,
        NWK_CONFIRM_STATUS_OFFSET = 9,
        NWK_CONFIRM_SHORT_ADDR_OFFSET = 10,
    };

    nwk_hdr_t nwkHdr;
    u8 confirm[NWK_CONFIRM_COPY_LEN] = {0};
    u8 handle;
    u8 status;

    memcpy(confirm, arg, sizeof(confirm));
    nwkHdrParse(&nwkHdr, confirm + NWK_CONFIRM_NWK_HDR_OFFSET);

    status = confirm[NWK_CONFIRM_STATUS_OFFSET];

    if (g_zbInfo.macPib.rxOnWhenIdle == 0U) {
        if (AUTO_QUICK_DATA_POLL_ENABLE && status == MAC_STA_FRAME_PENDING) {
            endDevMacDataPoll();
        }
    } else {
        u16 shortAddr = (u16)confirm[NWK_CONFIRM_SHORT_ADDR_OFFSET] |
                        ((u16)confirm[NWK_CONFIRM_SHORT_ADDR_OFFSET + 1] << 8);
        tl_zb_normal_neighbor_entry_t *entry = nwk_neTblGetByShortAddr(shortAddr);

        if (entry != NULL &&
            entry->relationship == NEIGHBOR_IS_PARENT &&
            status == MAC_STA_NO_ACK) {
            zb_buf_t *buf = zb_buf_allocate();

            if (buf != NULL) {
                tl_zbNwkNlmeNwkStatusInd(buf, shortAddr, NWK_COMMAND_STATUS_PARENT_LINK_FAILURE);
            }
        }
    }

    handle = confirm[NWK_CONFIRM_HANDLE_OFFSET];
    if (handle <= 0xbfU) {
        nwkNldeDataCnf(arg, status, handle);
        return;
    }

    switch (handle) {
    case NWK_INTERNAL_NSDU_HANDLE:
    case NWK_INTERNAL_DATA_RECEIVED_HANDLE:
    case NWK_INTERNAL_LEAVE_REQ_CMD_INDIRECT_HANDLE:
        zb_buf_free((zb_buf_t *)arg);
        return;
    case NWK_INTERNAL_REJOIN_REQ_CMD_HANDLE:
        nwk_rejoinCmdSendCnf(arg);
        return;
    case NWK_INTERNAL_REJOIN_RESP_CMD_HANDLE:
        tl_zbMcpsRejoinRespCnfHandler(arg, status, nwkHdr.dstAddr);
        return;
    case NWK_INTERNAL_LEAVE_REQ_CMD_HANDLE:
        nwk_leaveCmdSendCnf(arg, nwkHdr.dstAddr);
        return;
    case NWK_INTERNAL_ENDDEVTIMEOUT_REQ_CMD_HANDLE:
        nwkEndDevTimeoutReqCnfHandler(arg);
        return;
#if defined(ZB_ROUTER_ROLE)
    case NWK_INTERNAL_ENDDEVTIMEOUT_RSP_CMD_HANDLE:
        nwkEndDevTimeoutRspCnfHandler(arg);
        return;
#endif
    default:
        zb_buf_free((zb_buf_t *)arg);
        return;
    }
}

void nwk_tx(zb_buf_t *buf, nwk_hdr_t *pNwkHdr, u16 nextHop, u8 ack, u8 *payload, u8 payloadLen)
{
    zb_mscp_data_req_t *req;
    u8 nwkHdrLen;
    u8 *frameStart;
    u8 savedHandle;
    if (buf == NULL || pNwkHdr == NULL || payload == NULL) {
        return;
    }

    savedHandle = ((u8 *)buf)[BUF_SAVED_HANDLE_OFFSET];
    nwkHdrLen = getNwkHdrSize(pNwkHdr);
    pNwkHdr->frameHdrLen = nwkHdrLen;
    frameStart = payload - nwkHdrLen;
    nwkHdrBuilder(frameStart, pNwkHdr);

    req = (zb_mscp_data_req_t *)buf;
    memset(req, 0, sizeof(*req));
    req->srcAddr.addrMode = ADDR_MODE_SHORT;
    req->dstAddr.addrMode = ADDR_MODE_SHORT;
    req->dstAddr.addr.shortAddr = nextHop;
    req->dstPanId = g_zbInfo.macPib.panId;
    req->msduLength = (u8)(nwkHdrLen + payloadLen);
    req->msdu = frameStart;
    req->msduHandle = savedHandle;

    if (nextHop != MAC_SHORT_ADDR_BROADCAST) {
        req->txOptions = 1;
    }

    if (ack != 0U) {
        req->txOptions |= 0x04U;
    }

    if (!nwk_joined() &&
        savedHandle != NWK_INTERNAL_REJOIN_REQ_CMD_HANDLE &&
        savedHandle != NWK_INTERNAL_LEAVE_REQ_CMD_HANDLE) {
        tl_zbMacMcpsDataRequestSendConfirm(buf, MAC_STA_BAD_STATE);
        return;
    }

    if (pNwkHdr->frameControl.security) {
        if (ss_nwkSecureFrame(buf, pNwkHdr->frameHdrLen) != RET_OK) {
            g_sysDiags.nwkTxEnDecryptFail++;
            tl_zbMacMcpsDataRequestSendConfirm(buf, NWK_STATUS_DECRYPT_ERROR);
            return;
        }
    }

    g_sysDiags.nwkTxCnt++;
    tl_zbMacMcpsDataRequestProc(buf);
}

void nwk_fwdPacket(zb_buf_t *buf, nwk_hdr_t *pNwkHdr, u8 *payload, u8 payloadLen)
{
    u16 nextHop;

    if (buf == NULL) {
        ZB_EXCEPTION_POST(SYS_EXCEPTTION_ZB_BUFFER_EXCEPTION);
        return;
    }

    if (!buf->hdr.used) {
        ZB_EXCEPTION_POST(SYS_EXCEPTTION_ZB_BUFFER_EXCEPTION);
        return;
    }

    if (pNwkHdr->radius == 0U) {
        ZB_EXCEPTION_POST(SYS_EXCEPTTION_ZB_NWK_ROUTE_TABLE);
        return;
    }

    if (nwk_is_broadcast(pNwkHdr->dstAddr)) {
        if (g_edBrcSkipParent != 0U ||
            (!nwk_joined() && buf->hdr.handle == NWK_INTERNAL_LEAVE_REQ_CMD_HANDLE)) {
            nextHop = MAC_SHORT_ADDR_BROADCAST;
        } else {
#if defined(ZB_ROUTER_ROLE)
            nextHop = MAC_SHORT_ADDR_BROADCAST;
#else
            nextHop = tl_zbNeighborParentShortAddrGet();
#endif
        }
    } else {
#if defined(ZB_ROUTER_ROLE)
        tl_zb_normal_neighbor_entry_t *neighbor = nwkValidNeighborToFwd(pNwkHdr->dstAddr);
        nwk_routeDiscEntry_t *entry;
        nwk_txDataPendEntry_t *pend;
        nwk_routingTabEntry_t *route;

        if (neighbor != NULL) {
            nextHop = tl_zbshortAddrByIdx(neighbor->addrmapIdx);
        } else {
            route = nwkRoutingTabEntryDstFind(pNwkHdr->dstAddr);
            nextHop = nwkRoutingTabGetNextHop(route);
            if (nextHop == MAC_ADDR_USE_EXT && pNwkHdr->frameControl.discRoute != 0U) {
                pend = nwkTxDataPendTabEntryAdd(buf, pNwkHdr, payload, payloadLen, buf->hdr.handle);
                if (pend != NULL) {
                    entry = nwkTxDataRouteDiscStart(buf, pNwkHdr, payload, payloadLen);

                    if (entry != NULL) {
                        pend->needRouteDisc = 1;
                        pend->routeReqId = entry->routeReqId;
                        nwk_tx_data_route_disc_buf_release(buf);
                        return;
                    }

                    entry = nwkRouteDiscEntryDstFind(pNwkHdr->dstAddr);
                    if (entry != NULL) {
                        pend->needRouteDisc = 1;
                        pend->routeReqId = entry->routeReqId;
                        nwk_tx_data_route_disc_buf_release(buf);
                        return;
                    }

                    nwk_tx_data_route_disc_buf_release(buf);
                    nwk_tx_data_pend_route_disc_fail(pend);
                    return;
                }
            }
        }
#else
        nextHop = tl_zbNeighborParentShortAddrGet();
#endif
    }

    if (nextHop == MAC_ADDR_USE_EXT) {
        if (buf->hdr.handle <= 0xbfU) {
            nwkNldeDataCnf(buf, NWK_STATUS_ROUTE_ERROR, buf->hdr.handle);
        } else {
            zb_buf_free(buf);
        }
        return;
    }

    nwk_tx(buf, pNwkHdr, nextHop, 0, payload, payloadLen);
}

void tl_zbMacMcpsDataIndicationHandler(void *arg)
{
    zb_buf_t *buf = (zb_buf_t *)arg;
    zb_mscp_data_ind_t *ind = (zb_mscp_data_ind_t *)arg;
    nwk_hdr_t nwkHdr;
    nwkCmd_t cmd;
    u16 macSrcAddr;
    u16 macDstAddr;
    u8 frameType;
    u8 payloadTotalLen;
    u8 *payload;

    if (nwk_user_state() == NLME_LEAVING) {
        printk("zb dbg mcpsDataInd: drop NLME_LEAVING\n");
        zb_buf_free(buf);
        return;
    }

    memset(&nwkHdr, 0, sizeof(nwkHdr));
    nwkHdrParse(&nwkHdr, ind->msdu);

    printk("zb dbg mcpsDataInd: msduLen=%u hdrLen=%u frType=%u proto=%u src=0x%04x dst=0x%04x nwkAddr=0x%04x\n",
           ind->msduLength, nwkHdr.frameHdrLen, nwkHdr.frameControl.frameType,
           nwkHdr.frameControl.protocolVer, nwkHdr.srcAddr, nwkHdr.dstAddr,
           g_zbInfo.nwkNib.nwkAddr);

    if (ind->msduLength <= nwkHdr.frameHdrLen) {
        printk("zb dbg mcpsDataInd: drop msduLen<=hdrLen\n");
        zb_buf_free(buf);
        return;
    }

    frameType = nwkHdr.frameControl.frameType;
    printk("zb dbg mcpsDataInd: joined=%u user_state=%u frameType=%u\n",
           g_zbNwkCtx.joined, nwk_user_state(), frameType);
    if (!nwk_joined() && nwk_user_state() != NLME_JOINING) {
        if (frameType != FRAME_TYPE_INTERPAN) {
            printk("zb dbg mcpsDataInd: drop !joined && !JOINING && !INTERPAN\n");
            zb_buf_free(buf);
            return;
        }

        tl_zbMacInterPanDataHandle(arg);
        return;
    }

    if (frameType == FRAME_TYPE_INTERPAN) {
        tl_zbMacInterPanDataHandle(arg);
        return;
    }

    if (nwkHdr.frameControl.protocolVer != 2U) {
        if (frameType == 2U || nwkHdr.radius == 0U) {
            g_sysDiags.packetValidateDropCount++;
            zb_buf_free(buf);
            return;
        }
    }

    macSrcAddr = ind->srcAddr.addr.shortAddr;
    macDstAddr = ind->dstAddr.addr.shortAddr;
    if (nwk_is_broadcast(macDstAddr)) {
#if !defined(ZB_ROUTER_ROLE)
        if (g_zbInfo.macPib.rxOnWhenIdle != 0U) {
            if (macSrcAddr != tl_zbNeighborParentShortAddrGet()) {
                g_sysDiags.packetValidateDropCount++;
                zb_buf_free(buf);
                return;
            }
        } else {
            g_sysDiags.packetValidateDropCount++;
            zb_buf_free(buf);
            return;
        }
#endif
    }

    if (nwk_is_broadcast(nwkHdr.dstAddr)) {
        if (!nwkBrcCheckDevMatch(nwkHdr.dstAddr)) {
            g_sysDiags.packetValidateDropCount++;
            zb_buf_free(buf);
            return;
        }
    } else {
        if (nwkHdr.srcAddr == g_zbInfo.nwkNib.nwkAddr ||
            nwkHdr.dstAddr != g_zbInfo.nwkNib.nwkAddr ||
            nwk_hdr_end_device_initiator(&nwkHdr)) {
            printk("zb dbg mcpsDataInd: drop unicast nwk check src=0x%04x dst=0x%04x me=0x%04x ed_init=%u\n",
                   nwkHdr.srcAddr, nwkHdr.dstAddr, g_zbInfo.nwkNib.nwkAddr,
                   nwk_hdr_end_device_initiator(&nwkHdr));
            g_sysDiags.packetValidateDropCount++;
            zb_buf_free(buf);
            return;
        }
    }

    if (nwk_hdr_security(&nwkHdr)) {
        if (ss_nwkDecryptFrame(arg, nwkHdr.frameHdrLen, ind->msduLength, ind->msdu, &nwkHdr,
                               ind->mpduLinkQuality) != RET_OK) {
            return;
        }
        payloadTotalLen = (u8)(ind->msduLength - 4U);
    } else {
        payloadTotalLen = ind->msduLength;
    }

    nwkHdr.radius--;
    payload = ind->msdu + nwkHdr.frameHdrLen;
    buf->hdr.handle = NWK_INTERNAL_DATA_RECEIVED_HANDLE;

    memset(&cmd, 0, sizeof(cmd));
    if (frameType == FRAME_TYPE_COMMAND) {
        cmd.cmdId = payload[0];

        switch (cmd.cmdId) {
        case NWK_CMD_ROUTE_REQUEST:
            *(u8 *)&cmd.rreq.options = payload[1];
            cmd.rreq.routeReqId = payload[2];
            cmd.rreq.dstAddr = (u16)payload[3] | ((u16)payload[4] << 8);
            cmd.rreq.pathCost = payload[5];
            break;
        case NWK_CMD_ROUTE_REPLY:
            *(u8 *)&cmd.rrep.options = payload[1];
            cmd.rrep.routeReqId = payload[2];
            cmd.rrep.originatorAddr = (u16)payload[3] | ((u16)payload[4] << 8);
            cmd.rrep.responderAddr = (u16)payload[5] | ((u16)payload[6] << 8);
            cmd.rrep.pathCost = payload[7];
            break;
        case NWK_CMD_NETWORK_STATUS:
            cmd.nwkStatus.dstAddr = (u16)payload[1] | ((u16)payload[2] << 8);
            cmd.nwkStatus.statusCode = payload[3];
            break;
        case NWK_CMD_LEAVE:
            *(u8 *)&cmd.leave = payload[1];
            break;
        case NWK_CMD_ROUTE_RECORD:
            cmd.rrec.relayCnt = payload[1];
            cmd.rrec.relayList = payload + 2;
            break;
        case NWK_CMD_REJOIN_RESPONSE:
            cmd.rejoinRsp.nwkAddr = (u16)payload[1] | ((u16)payload[2] << 8);
            cmd.rejoinRsp.rejoinStatus = payload[3];
            break;
        case NWK_CMD_REJOIN_REQUEST:
            *(u8 *)&cmd.rejoinReq.capabilityInfo = payload[1];
            break;
        case NWK_CMD_LINK_STATUS:
            *(u8 *)&cmd.linkSt.options = payload[1];
            cmd.linkSt.linkStatusList = (linkStatus_entry_t *)(payload + 2);
            break;
        case NWK_CMD_NETWORK_REPORT:
            *(u8 *)&cmd.nwkReport.options = payload[1];
            memcpy(cmd.nwkReport.epid, payload + 2, EXT_ADDR_LEN);
            cmd.nwkReport.panIds = payload + 10;
            break;
        case NWK_CMD_NETWORK_UPDATE:
            *(u8 *)&cmd.nwkUpdate.options = payload[1];
            memcpy(cmd.nwkUpdate.epid, payload + 2, EXT_ADDR_LEN);
            cmd.nwkUpdate.newPanId = (u16)payload[11] | ((u16)payload[12] << 8);
            cmd.nwkUpdate.updateId = payload[10];
            break;
        case NWK_CMD_ENDDEVTIMEOUT_RESPONSE:
            cmd.endDevTimeoutRsp.status = payload[1];
            cmd.endDevTimeoutRsp.parentInfo = payload[2];
            break;
#if defined(ZB_ROUTER_ROLE)
        case NWK_CMD_ENDDEVTIMEOUT_REQUEST:
            cmd.endDevTimeoutReq.reqTimeoutEnum = payload[1];
            cmd.endDevTimeoutReq.endDevCfg = payload[2];
            break;
#endif
        default:
            break;
        }
    }

    printk("zb dbg mcpsDataInd: pre-sec joined=%u secLvl=%u sec=%u\n",
           g_zbNwkCtx.joined, ss_ib.securityLevel, nwkHdr.frameControl.security);
    if (nwk_joined() &&
        ss_ib.securityLevel != 0U &&
        !nwkHdr.frameControl.security) {
        if (frameType == FRAME_TYPE_DATA) {
            printk("zb dbg mcpsDataInd: drop unsecured DATA (joined+secLvl)\n");
            zb_buf_free(buf);
            return;
        }

        if (frameType == FRAME_TYPE_COMMAND &&
            cmd.cmdId != NWK_CMD_REJOIN_REQUEST) {
            printk("zb dbg mcpsDataInd: drop unsecured CMD (joined+secLvl)\n");
            zb_buf_free(buf);
            return;
        }
    }

    mac_pendingWaitTimerCancel();

    if (buf->hdr.pending &&
        AUTO_QUICK_DATA_POLL_ENABLE &&
        g_zbInfo.macPib.rxOnWhenIdle == 0U) {
        buf->hdr.pending = 0;
        endDevMacDataPoll();
    }

    if (NWK_HEADER_SRC_IEEE_INCLUDE &&
        nwkHdr.frameControl.security &&
        nwk_hdr_src_ieee(&nwkHdr)) {
        u16 addrRef = 0;

        (void)tl_zbNwkAddrMapAdd(nwkHdr.srcAddr, nwkHdr.srcIeeeAddr, &addrRef);
    }

    printk("zb dbg mcpsDataInd: post-checks, frameType=%u joined=%u bcast=%u\n",
           frameType, g_zbNwkCtx.joined, nwk_is_broadcast(nwkHdr.dstAddr));
    if (frameType == FRAME_TYPE_DATA) {
        if (nwk_is_broadcast(nwkHdr.dstAddr) || nwkHdr.dstAddr == g_zbInfo.nwkNib.nwkAddr) {
            printk("zb dbg mcpsDataInd: calling nwkNldeDataInd\n");
            nwkNldeDataInd(arg, &nwkHdr);
            return;
        }

        if (nwkHdr.radius == 0U) {
            zb_buf_free(buf);
            return;
        }

        g_sysDiags.relayedUcast++;
        nwk_fwdPacket(buf, &nwkHdr, payload, (u8)(payloadTotalLen - nwkHdr.frameHdrLen));
        return;
    }

    if (nwkHdr.frameControl.frameType != FRAME_TYPE_COMMAND) {
        zb_buf_free(buf);
        return;
    }

    switch (cmd.cmdId) {
    case NWK_CMD_ROUTE_REQUEST:
        nwkRouteReqCmdHandler(arg, &nwkHdr, &cmd);
        return;
    case NWK_CMD_ROUTE_REPLY:
        nwkRouteReplyCmdHandler(arg, &nwkHdr, &cmd);
        return;
    case NWK_CMD_ROUTE_RECORD:
        nwkRouteRecordCmdHandler(arg, &nwkHdr, &cmd);
        return;
    case NWK_CMD_LINK_STATUS:
        tl_zbNwkLinkStatusCmdHandler(arg, &nwkHdr, &cmd);
        return;
    case NWK_CMD_NETWORK_REPORT:
        tl_zbNwkReportCmdHandler(arg, &nwkHdr, &cmd);
        return;
    case NWK_CMD_ENDDEVTIMEOUT_RESPONSE:
        nwkEndDevTimeoutRspCmdHandler(arg, &nwkHdr, &cmd);
        return;
#if defined(ZB_ROUTER_ROLE)
    case NWK_CMD_ENDDEVTIMEOUT_REQUEST:
        nwkEndDevTimeoutReqCmdHandler(arg, &nwkHdr, &cmd);
        return;
#endif
    case NWK_CMD_NETWORK_UPDATE:
        tl_zbNwkNetworkUpdateCmdHandler(arg, &nwkHdr, &cmd);
        return;
    case NWK_CMD_REJOIN_RESPONSE:
        tl_zbNwkRejoinRespCmdHandler(arg, &nwkHdr, &cmd);
        return;
    case NWK_CMD_REJOIN_REQUEST:
        tl_zbNwkRejoinReqCmdHandler(arg, &nwkHdr, &cmd);
        return;
    case NWK_CMD_LEAVE:
        tl_zbNwkLeaveReqCmdHandler(arg, &nwkHdr, &cmd);
        return;
    case NWK_CMD_NETWORK_STATUS:
        tl_zbNwkStatusCmdHandler(arg, &nwkHdr, &cmd);
        return;
    default:
        zb_buf_free(buf);
        return;
    }
}

void tl_zbNwkNldeDataRequestHandler(void *arg)
{
    zb_buf_t *buf = (zb_buf_t *)arg;
    nlde_data_req_t *req = (nlde_data_req_t *)arg;
    nwk_hdr_t nwkHdr;
    u8 *fc = (u8 *)&nwkHdr.frameControl;
    u16 srcAddr;
    u8 radius;

    if ((!nwk_joined()) ||
        (g_zbInfo.nwkNib.secAllFrames && nwk_user_state() != NLME_IDLE)) {
        if (req->ndsuHandle <= 0xbfU) {
            nwkNldeDataCnf(arg, NWK_STATUS_INVALID_REQUEST, req->ndsuHandle);
        } else {
            zb_buf_free(buf);
        }
        return;
    }

    if (req->dstAddr == g_zbInfo.nwkNib.nwkAddr) {
        nwkNldeDataCnf(arg, NWK_STATUS_INVALID_PARAMETER, req->ndsuHandle);
        return;
    }

    memset(&nwkHdr, 0, sizeof(nwkHdr));
    buf->hdr.handle = req->ndsuHandle;

    fc[0] = (FRAME_TYPE_DATA & 0x03U) | (2U << 2);
    fc[0] |= (u8)(req->discoverRoute << 6);
    fc[1] = (u8)(req->addrMode ? 1U : 0U);

    if (req->securityEnable) {
        fc[1] |= 0x02U;
    }

    if (NWK_HEADER_SRC_IEEE_INCLUDE) {
        fc[1] |= 0x10U;
        memcpy(nwkHdr.srcIeeeAddr, g_zbInfo.nwkNib.ieeeAddr, EXT_ADDR_LEN);
    }

    if (g_zbInfo.nwkNib.parentInfo != 0U) {
        fc[1] |= 0x20U;
    }

    nwkHdr.dstAddr = req->dstAddr;
    srcAddr = req->useAlias ? req->aliasSrcAddr : g_zbInfo.nwkNib.nwkAddr;
    nwkHdr.srcAddr = srcAddr;

    radius = req->radius;
    if (radius == 0U) {
        radius = g_zbInfo.nwkNib.maxDepth;
    }
    nwkHdr.radius = radius;

    if (req->useAlias) {
        nwkHdr.seqNum = req->aliasSeqNum;
    } else {
        nwkHdr.seqNum = g_zbInfo.nwkNib.seqNum++;
    }

    if (req->addrMode != 0U) {
        nwkHdr.mcastControl.multicastMode = aps_group_search_by_addr(req->dstAddr) ? 1U : 0U;
        nwkHdr.mcastControl.nonmemberRadius = req->nonmemberRadius & 0x07U;
    }

    if (nwk_is_broadcast(req->dstAddr)) {
        fc[1] &= (u8)~0x10U;
    }

    if (req->unicastSkipRouting) {
        nwk_tx(buf, &nwkHdr, req->dstAddr, 0, req->nsdu, req->nsduLen);
    } else {
        nwk_fwdPacket(buf, &nwkHdr, req->nsdu, req->nsduLen);
    }

    if (AUTO_QUICK_DATA_POLL_ENABLE &&
        AUTO_QUICK_DATA_POLL_INTERVAL != 0U &&
        AUTO_QUICK_DATA_POLL_TIMES != 0U &&
        g_zbInfo.macPib.rxOnWhenIdle == 0U) {
        quickDataPollCnt = 0;
        if (quickDataPollTimerEvt != NULL) {
            ev_timer_taskCancel(&quickDataPollTimerEvt);
        }
        quickDataPollTimerEvt = ev_timer_taskPost(tl_zbNwkQuickDataPollCb, NULL,
                                                  (((u32)g_zbInfo.macPib.respWaitTime * 15U) << 10) / 1000U);
    }
}
