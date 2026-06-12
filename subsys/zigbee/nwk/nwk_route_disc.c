/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Adapted from libzigbee/src/nwk_route_disc.c (~620 LOC). Vendor file
 * kept structurally one-for-one. Only the include layout changes:
 * vendor "zb_local.h" + "ev_timer.h" → zb_common_stub.h +
 * nwk_internal.h + os/ev_timer.h + mac/includes.
 */
#include "zb_common_stub.h"
#include "os/ev_timer.h"
#include "mac/includes/tl_zb_mac.h"
#include "nwk/includes/nwk.h"
#include "nwk/includes/nwk_neighbor.h"
#include "nwk/includes/nwk_internal.h"

#if defined(ZB_ROUTER_ROLE)

enum {
    NWK_ROUTE_DISC_TABLE_SIZE = 10,
};

extern void nwkTxDataPendTabEntryClear(nwk_txDataPendEntry_t *entry);
extern nwk_txDataPendEntry_t *nwkTxDataPendTabEntryRtDiscFind(u16 dstAddr, u8 routeReqId);
extern nwk_txDataPendEntry_t *nwkTxDataPendTabEntryAdd(zb_buf_t *buf, nwk_hdr_t *pNwkHdr, u8 *payload,
                                                       u8 payloadLen, u8 handle);
extern void nwkNldeDataCnf(void *arg, u8 status, u8 nsduHandle);
extern void tl_zbNwkSendNwkStatusCmd(void *arg, nwk_hdr_t *pNwkHdr, nwkCmd_t *pNwkStatus, u8 handle);
extern void zdo_nlme_status_indication(void *arg);
void nwkRouteDiscStatusCodeSend(zb_buf_t *buf, u16 srcAddr, u16 dstAddr, u8 status);
nwk_routeDiscEntry_t *nwkRouteDiscStart(void *buf, nwk_hdr_t *pNwkHdr, u8 *payload, u8 payloadLen);

nwk_routeDiscEntry_t g_routeDiscTab[NWK_ROUTE_DISC_TABLE_SIZE];
ev_timer_event_t *concentratorDiscoveryTimer = NULL;
u8 g_routeReqId = 0;
u8 g_routeDiscTabCnt = 0;

static inline bool nwk_route_disc_used(const nwk_routeDiscEntry_t *entry)
{
    return entry != NULL && entry->used != 0U;
}

enum {
    NWK_ROUTE_REQ_RETRY_DELAY = 0xfe,
};

static inline u16 nwk_route_disc_prev_hop(void *arg)
{
    return ((zb_mscp_data_ind_t *)arg)->srcAddr.addr.shortAddr;
}

static void nwk_disc_cached_packet_replay(void *arg)
{
    nwk_route_disc_cache_buf_t *cache = (nwk_route_disc_cache_buf_t *)arg;

    if (cache == NULL) {
        return;
    }

    nwk_fwdPacket((zb_buf_t *)cache, &cache->hdr, cache->payload, cache->payloadLen);
}

static void nwk_route_disc_confirm(void *arg, u8 status)
{
    nlme_routeDisc_cnf_t *cnf = (nlme_routeDisc_cnf_t *)arg;

    cnf->status = status;
    cnf->nwkStatusCode = status;
    tl_zbTaskPost(zdo_nlme_status_indication, arg);
}

u8 nwkDiscDataPendSend(u16 dstAddr, u8 routeReqId)
{
    nwk_txDataPendEntry_t *pend;
    /* Router asm falls through with r0 == 0 even when no pending entries match.
     * Keep the reconstructed FRAME_NOT_BUFFERED status so VALIDATE_ROUTE can still
     * surface when validation expires with nothing buffered.
     */
    u8 status = NWK_STATUS_FRAME_NOT_BUFFERED;

    while ((pend = nwkTxDataPendTabEntryRtDiscFind(dstAddr, routeReqId)) != NULL) {
        if (pend->srcBuf != NULL) {
            nwk_route_disc_cache_buf_t *cache = (nwk_route_disc_cache_buf_t *)pend->srcBuf;

            cache->savedHandle = pend->handle;
            pend->needRouteDisc = 0;
            pend->srcBuf = NULL;
            tl_zbTaskPost(nwk_disc_cached_packet_replay, cache);
        }
        status = 0;
        nwkTxDataPendTabEntryClear(pend);
    }

    return status;
}

void nwkDiscDataPendClear(u16 dstAddr, u8 routeReqId)
{
    nwk_txDataPendEntry_t *pend;

    while ((pend = nwkTxDataPendTabEntryRtDiscFind(dstAddr, routeReqId)) != NULL) {
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
}

void nwkRouteDiscTabEntryRst(nwk_routeDiscEntry_t *entry)
{
    if (entry == NULL) {
        return;
    }

    if (entry->retryTimer != NULL) {
        ev_timer_taskCancel(&entry->retryTimer);
    }

    memset(entry, 0, sizeof(*entry));
}

void nwkRouteDiscTabEntryClear(nwk_routeDiscEntry_t *entry)
{
    if (!nwk_route_disc_used(entry)) {
        return;
    }

    nwkRouteDiscTabEntryRst(entry);
    if (g_routeDiscTabCnt != 0U) {
        g_routeDiscTabCnt--;
    }
}

void nwkRouteDiscTabInit(void)
{
    g_routeDiscTabCnt = 0;
    for (u8 i = 0; i < NWK_ROUTE_DISC_TABLE_SIZE; i++) {
        nwkRouteDiscTabEntryRst(&g_routeDiscTab[i]);
    }
}

nwk_routeDiscEntry_t *nwkRouteDiscEntryFind(u16 srcAddr, u8 routeReqId)
{
    for (u8 i = 0; i < NWK_ROUTE_DISC_TABLE_SIZE; i++) {
        nwk_routeDiscEntry_t *entry = &g_routeDiscTab[i];

        if (nwk_route_disc_used(entry) && entry->srcAddr == srcAddr && entry->routeReqId == routeReqId) {
            return entry;
        }
    }

    return NULL;
}

nwk_routeDiscEntry_t *nwkRouteDiscEntryDstFind(u16 dstAddr)
{
    for (u8 i = 0; i < NWK_ROUTE_DISC_TABLE_SIZE; i++) {
        nwk_routeDiscEntry_t *entry = &g_routeDiscTab[i];

        if (nwk_route_disc_used(entry) && entry->dstAddr == dstAddr) {
            return entry;
        }
    }

    return NULL;
}

nwk_routeDiscEntry_t *nwkManyToOneRouteDiscEntryInitFind(void)
{
    return nwkRouteDiscEntryDstFind(NWK_BROADCAST_ROUTER_COORDINATOR);
}

nwk_routeDiscEntry_t *nwkRouteDiscEntryCreate(u16 srcAddr, u16 dstAddr, u16 senderAddr, u8 forwardCost,
                                              u8 lastCost, u8 routeReqId)
{
    nwk_routeDiscEntry_t *entry = NULL;

    for (u8 i = 0; i < NWK_ROUTE_DISC_TABLE_SIZE; i++) {
        if (!g_routeDiscTab[i].used) {
            entry = &g_routeDiscTab[i];
            break;
        }
    }

    if (entry == NULL) {
        return NULL;
    }

    memset(entry, 0, sizeof(*entry));
    entry->srcAddr = srcAddr;
    entry->dstAddr = dstAddr;
    entry->senderAddr = senderAddr;
    entry->expiry = 10U;
    entry->forwardCost = forwardCost;
    entry->residCost = 0xffU;
    entry->lastCost = lastCost;
    entry->routeReqId = routeReqId;
    entry->retries = 0;
    entry->used = 1;
    g_routeDiscTabCnt++;

    return entry;
}

void nwkRouteReqCmdSend(nwk_routeDiscEntry_t *entry)
{
    zb_buf_t *buf;
    nwk_hdr_t hdr;
    u8 payload[1 + sizeof(nwkCmd_routeReq_t)] = {0};
    nwkCmd_routeReq_t *req = (nwkCmd_routeReq_t *)(payload + 1);

    if (entry == NULL) {
        return;
    }

    buf = zb_buf_allocate();
    if (buf == NULL) {
        return;
    }

    memset(&hdr, 0, sizeof(hdr));
    hdr.dstAddr = NWK_BROADCAST_ROUTER_COORDINATOR;
    hdr.srcAddr = entry->srcAddr;
    hdr.radius = g_zbNIB.maxDepth;
    hdr.seqNum = g_zbNIB.seqNum++;
    hdr.frameControl.frameType = FRAME_TYPE_COMMAND;
    hdr.frameControl.protocolVer = ZB_PROTOCOL_VERSION;
    hdr.frameControl.discRoute = 1;

    payload[0] = NWK_CMD_ROUTE_REQUEST;
    memset(req, 0, sizeof(*req));
    req->routeReqId = entry->routeReqId;
    req->dstAddr = entry->dstAddr;
    req->pathCost = entry->forwardCost;

    nwk_tx(buf, &hdr, MAC_SHORT_ADDR_BROADCAST, 0, payload, sizeof(payload));
}

int nwkRouteReqRetry(void *arg)
{
    nwk_routeDiscEntry_t *entry = (nwk_routeDiscEntry_t *)arg;

    if (entry == NULL) {
        return -1;
    }

    if (entry->retries >= NWKC_RREQ_RETRIES) {
        nwkDiscDataPendClear(entry->dstAddr, entry->routeReqId);
        nwkRouteDiscTabEntryClear(entry);
        return -1;
    }

    entry->retries++;
    nwkRouteReqCmdSend(entry);
    return NWK_ROUTE_REQ_RETRY_DELAY;
}

int nwkRouteReqInitialRetry(void *arg)
{
    nwk_routeDiscEntry_t *entry = (nwk_routeDiscEntry_t *)arg;

    if (entry == NULL) {
        return -1;
    }

    if (entry->retries >= NWKC_INITIAL_RREQ_RETRIES) {
        return nwkRouteReqRetry(arg);
    }

    entry->retries++;
    nwkRouteReqCmdSend(entry);
    return 0;
}

void nwkRouteReplyCmdSend(u16 dstAddr, u16 srcAddr, u16 originatorAddr, u16 responderAddr,
                          u8 routeReqId, u8 pathCost)
{
    zb_buf_t *buf = zb_buf_allocate();
    nwk_hdr_t hdr;
    u8 payload[1 + sizeof(nwkCmd_routeReply_t)] = {0};
    nwkCmd_routeReply_t *reply = (nwkCmd_routeReply_t *)(payload + 1);

    if (buf == NULL) {
        return;
    }

    memset(&hdr, 0, sizeof(hdr));
    hdr.dstAddr = dstAddr;
    hdr.srcAddr = srcAddr;
    hdr.radius = g_zbNIB.maxDepth;
    hdr.seqNum = g_zbNIB.seqNum++;
    hdr.frameControl.frameType = FRAME_TYPE_COMMAND;
    hdr.frameControl.protocolVer = ZB_PROTOCOL_VERSION;

    payload[0] = NWK_CMD_ROUTE_REPLY;
    reply->originatorAddr = originatorAddr;
    reply->responderAddr = responderAddr;
    reply->routeReqId = routeReqId;
    reply->pathCost = pathCost;

    nwk_fwdPacket(buf, &hdr, payload, sizeof(payload));
}

void nwkRouteRecordCmdSend(u16 dstAddr, srcRouteSubframe_t *subframe)
{
    zb_buf_t *buf = zb_buf_allocate();
    nwk_hdr_t hdr;
    u8 payload[2 + (NWK_MAX_SOURCE_ROUTE * sizeof(u16))] = {0};
    u8 relayLen;

    if (buf == NULL || subframe == NULL) {
        return;
    }

    memset(&hdr, 0, sizeof(hdr));
    hdr.dstAddr = dstAddr;
    hdr.srcAddr = g_zbNIB.nwkAddr;
    hdr.radius = g_zbNIB.maxDepth;
    hdr.seqNum = g_zbNIB.seqNum++;
    hdr.frameControl.frameType = FRAME_TYPE_COMMAND;
    hdr.frameControl.protocolVer = ZB_PROTOCOL_VERSION;

    payload[0] = NWK_CMD_ROUTE_RECORD;
    payload[1] = subframe->relayCnt;
    relayLen = (u8)(subframe->relayCnt * sizeof(u16));
    if (subframe->relayList != NULL && subframe->relayCnt != 0U) {
        memcpy(payload + 2, subframe->relayList, relayLen);
    }

    nwk_fwdPacket(buf, &hdr, payload, (u8)(2U + relayLen));
}

u8 *nwkRouteDiscCachePacketCopy(nwk_routeDiscEntry_t *entry, zb_buf_t *dst)
{
    if (entry == NULL || entry->buf == NULL || dst == NULL) {
        return NULL;
    }

    memcpy(dst, entry->buf, sizeof(*dst));
    return dst->buf;
}

void nwkRouteReqConcentInitiation(void)
{
    nwk_routeDiscEntry_t *entry = nwkManyToOneRouteDiscEntryInitFind();

    if (entry != NULL) {
        nwkRouteReqCmdSend(entry);
    }
}

int nwkConcentDiscTimerCb(void *arg)
{
    (void)arg;
    concentratorDiscoveryTimer = NULL;
    nwkRouteReqConcentInitiation();
    return -1;
}

void nwkRouteReplySend(void *arg)
{
    nwk_routeDiscEntry_t *entry = (nwk_routeDiscEntry_t *)arg;

    if (entry != NULL) {
        nwkRouteReplyCmdSend(entry->srcAddr, entry->dstAddr, entry->srcAddr, entry->dstAddr,
                             entry->routeReqId, entry->forwardCost);
    }
}

int nwkRouteReplySendDelay(void *arg)
{
    nwkRouteReplySend(arg);
    return -1;
}

nwk_routeDiscEntry_t *nwkRouteDiscStart(void *buf, nwk_hdr_t *pNwkHdr, u8 *payload, u8 payloadLen)
{
    nwk_routeDiscEntry_t *entry;
    u16 srcAddr;
    u16 dstAddr;

    (void)payload;
    (void)payloadLen;

    if (pNwkHdr == NULL) {
        return NULL;
    }

    srcAddr = (buf == NULL) ? g_zbNIB.nwkAddr : pNwkHdr->srcAddr;
    dstAddr = pNwkHdr->dstAddr;
    entry = nwkRouteDiscEntryCreate(srcAddr, dstAddr, g_zbNIB.nwkAddr, 0, 0, ++g_routeReqId);
    if (entry == NULL) {
        return NULL;
    }
    entry->buf = buf;

    (void)nwkRoutingTabEntryCreate(dstAddr);
    if (ev_timer_enough()) {
        entry->retryTimer = ev_timer_taskPost(nwkRouteReqInitialRetry, entry,
                                              (drv_u32Rand() % (NWK_BRC_JITTER + 1U)) + 1U);
    } else {
        nwkRouteReqInitialRetry(entry);
    }

    return entry;
}

void tl_zbNwkNlmeRouteDiscRequestHandler(void *arg)
{
    nlme_routeDisc_req_t *req = (nlme_routeDisc_req_t *)arg;
    nwk_hdr_t hdr;

    memset(&hdr, 0, sizeof(hdr));
    hdr.dstAddr = req->dstAddr;
    hdr.srcAddr = g_zbNIB.nwkAddr;
    hdr.radius = req->radius;
    hdr.frameControl.frameType = FRAME_TYPE_COMMAND;
    hdr.frameControl.protocolVer = ZB_PROTOCOL_VERSION;

    if (!g_zbNwkCtx.joined) {
        nwk_route_disc_confirm(arg, NWK_STATUS_INVALID_REQUEST);
        return;
    }

    if (nwkRouteDiscStart(arg, &hdr, NULL, 0) == NULL) {
        nwk_route_disc_confirm(arg, NWK_STATUS_ROUTE_DISCOVERY_FAILED);
        return;
    }

    nwk_route_disc_confirm(arg, NWK_STATUS_SUCCESS);
}

u8 nwkRouteReqDstChk(u16 dstAddr, u8 pathCost)
{
    tl_zb_normal_neighbor_entry_t *entry = nwk_neTblGetByShortAddr(dstAddr);

    if (entry == NULL) {
        return pathCost;
    }

    return (u8)(pathCost + rf_lqi2cost(entry->lqi));
}

void nwkRouteReqCmdHandler(void *arg, nwk_hdr_t *pNwkHdr, nwkCmd_t *cmd)
{
    nwkCmd_routeReq_t *rreq = &cmd->rreq;
    u16 prevHop = nwk_route_disc_prev_hop(arg);
    u8 totalCost = nwkRouteReqDstChk(prevHop, rreq->pathCost);
    nwk_routingTabEntry_t *route = nwkRoutingTabEntryCreate(pNwkHdr->srcAddr);

    if (route != NULL) {
        route->nextHopAddr = prevHop;
        route->status = NWK_ROUTE_STATE_ACTIVE;
        route->residCost = totalCost;
        route->forgetCnt = 0;
    }

    if (rreq->dstAddr == g_zbNIB.nwkAddr) {
        nwkRouteReplyCmdSend(pNwkHdr->srcAddr, g_zbNIB.nwkAddr, pNwkHdr->srcAddr, g_zbNIB.nwkAddr,
                             rreq->routeReqId, totalCost);
        zb_buf_free((zb_buf_t *)arg);
        return;
    }

    if (pNwkHdr->radius > 1U) {
        nwk_routeDiscEntry_t *entry = nwkRouteDiscEntryFind(pNwkHdr->srcAddr, rreq->routeReqId);

        if (entry == NULL) {
            entry = nwkRouteDiscEntryCreate(pNwkHdr->srcAddr, rreq->dstAddr, prevHop, totalCost, totalCost,
                                            rreq->routeReqId);
        }
        if (entry != NULL && (entry->forwardCost == 0U || totalCost <= entry->forwardCost)) {
            entry->senderAddr = prevHop;
            entry->forwardCost = totalCost;
            nwkRouteReqCmdSend(entry);
        }
    }

    zb_buf_free((zb_buf_t *)arg);
}

void nwkRouteReplyCmdHandler(void *arg, nwk_hdr_t *pNwkHdr, nwkCmd_t *cmd)
{
    nwkCmd_routeReply_t *rrep = &cmd->rrep;
    u16 prevHop = nwk_route_disc_prev_hop(arg);
    nwk_routingTabEntry_t *route = nwkRoutingTabEntryCreate(rrep->responderAddr);
    nwk_routeDiscEntry_t *entry = nwkRouteDiscEntryFind(rrep->originatorAddr, rrep->routeReqId);

    if (route != NULL) {
        route->nextHopAddr = prevHop;
        route->status = NWK_ROUTE_STATE_ACTIVE;
        route->residCost = rrep->pathCost;
        route->forgetCnt = 0;
    }

    if (entry != NULL) {
        if (rrep->originatorAddr == g_zbNIB.nwkAddr) {
            nwkDiscDataPendSend(entry->dstAddr, entry->routeReqId);
        } else {
            nwkRouteReplyCmdSend(rrep->originatorAddr, rrep->responderAddr, rrep->originatorAddr,
                                 rrep->responderAddr, rrep->routeReqId, rrep->pathCost);
        }
        nwkRouteDiscTabEntryClear(entry);
    }

    zb_buf_free((zb_buf_t *)arg);
}

void nwkRouteRecordInitiation(u16 dstAddr, srcRouteSubframe_t *subframe)
{
    nwkRouteRecordCmdSend(dstAddr, subframe);
}

void nwkRouteRecordCmdHandler(void *arg, nwk_hdr_t *pNwkHdr, nwkCmd_t *cmd)
{
    srcRouteSubframe_t subframe;

    memset(&subframe, 0, sizeof(subframe));
    subframe.relayCnt = cmd->rrec.relayCnt;
    subframe.relayList = cmd->rrec.relayList;

    nwkRouteRecordInitiation(pNwkHdr->srcAddr, &subframe);
    zb_buf_free((zb_buf_t *)arg);
}

nwk_routeDiscEntry_t *nwkTxDataRouteDiscStart(zb_buf_t *buf, nwk_hdr_t *pNwkHdr, u8 *payload, u8 payloadLen)
{
    nwk_routeDiscEntry_t *entry;

    if (buf == NULL || pNwkHdr == NULL) {
        return NULL;
    }

    entry = nwkRouteDiscStart(NULL, pNwkHdr, payload, payloadLen);
    return entry;
}

void nwkRouteDiscStatusCodeSend(zb_buf_t *buf, u16 srcAddr, u16 dstAddr, u8 status)
{
    nwk_hdr_t hdr;
    nwkCmd_t cmd;

    if (buf == NULL) {
        return;
    }

    memset(&hdr, 0, sizeof(hdr));
    memset(&cmd, 0, sizeof(cmd));
    hdr.dstAddr = dstAddr;
    hdr.srcAddr = srcAddr;
    hdr.radius = g_zbNIB.maxDepth;
    hdr.seqNum = g_zbNIB.seqNum++;
    hdr.frameControl.frameType = FRAME_TYPE_COMMAND;
    hdr.frameControl.protocolVer = ZB_PROTOCOL_VERSION;
    cmd.cmdId = NWK_CMD_NETWORK_STATUS;
    cmd.nwkStatus.dstAddr = dstAddr;
    cmd.nwkStatus.statusCode = status;
    tl_zbNwkSendNwkStatusCmd(buf, &hdr, &cmd, NWK_INTERNAL_NETWORK_STATUS_CMD_HANDLE);
}

int nwkRouteDiscPeriodic(void *arg)
{
    (void)arg;

    for (u8 i = 0; i < NWK_ROUTE_DISC_TABLE_SIZE; i++) {
        nwk_routeDiscEntry_t *entry = &g_routeDiscTab[i];

        if (!nwk_route_disc_used(entry)) {
            continue;
        }

        if (entry->expiry != 0U) {
            entry->expiry--;
        }

        if (entry->expiry == 0U && entry->buf == NULL) {
            nwk_routingTabEntry_t *route = NULL;

            if (!ZB_NWK_IS_ADDRESS_BROADCAST(entry->dstAddr)) {
                route = nwkRoutingTabEntryDstFind(entry->dstAddr);
            }

            if (route == NULL) {
                /* Router skips pend-clear for broadcast destinations here.
                 * That's safe for the current reconstruction because many-to-one
                 * discovery does not bind unicast pend entries to a broadcast dst.
                 */
                nwkDiscDataPendClear(entry->dstAddr, entry->routeReqId);
            } else {
                switch (route->status) {
                case NWK_ROUTE_STATE_ACTIVE:
                    nwkDiscDataPendSend(entry->dstAddr, entry->routeReqId);
                    break;
                case NWK_ROUTE_STATE_DISCOVERY_UNDERWAY:
                    route->status = NWK_ROUTE_STATE_DISCOVERY_FAILED;
                    nwkDiscDataPendClear(entry->dstAddr, entry->routeReqId);
                    break;
                case NWK_ROUTE_STATE_VALIDATION_UNDERWAY:
                    if (entry->srcAddr == g_zbNIB.nwkAddr) {
                        if (nwkDiscDataPendSend(entry->dstAddr, entry->routeReqId) != 0U) {
                            zb_buf_t *buf = zb_buf_allocate();

                            if (buf != NULL) {
                                nwkRouteDiscStatusCodeSend(buf, entry->dstAddr, entry->dstAddr,
                                                           NWK_COMMAND_STATUS_VALIDATE_ROUTE);
                            }
                        }
                    } else {
                        nwkDiscDataPendClear(entry->dstAddr, entry->routeReqId);
                    }
                    break;
                default:
                    nwkDiscDataPendClear(entry->dstAddr, entry->routeReqId);
                    break;
                }
            }
            nwkRouteDiscTabEntryClear(entry);
        }
    }

    return 0;
}

#else

#include "zb_local.h"

#endif
