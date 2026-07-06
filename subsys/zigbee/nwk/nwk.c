/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Adapted from libzigbee/src/nwk.c (~595 LOC). Hosts the central NWK
 * dispatch tables (g_zbNwkEventFromMacTbl, g_zbNwkEventFromHighTbl),
 * the nwk_tx / nwk_fwdPacket TX helpers, link-status periodic, and
 * passive-ack timeout accessor.
 *
 * Vendor file kept structurally one-for-one; only the include layout
 * changes (vendor zb_local.h + ev_timer.h → zb_common_stub.h +
 * nwk_internal.h + os/ev_timer.h + mac/includes).
 */
#include "zb_common_stub.h"
#include "os/ev_timer.h"
#include "mac/includes/tl_zb_mac.h"
#include "nwk/includes/nwk.h"
#include "nwk/includes/nwk_internal.h"
#include "nwk/includes/nwk_neighbor.h"

nwk_ctx_t g_zbNwkCtx;

#if defined(ZB_ROUTER_ROLE)
u8 linkStExpiry = 0;
u8 T_DBG_linkStatus = 0;
ev_timer_event_t *linkStTimer = NULL;

extern void nwk_formationScanCnfHandler(void *arg);
extern void nwk_formationStartCnfHandler(void *arg);
extern void nwk_panIdConflictCnfHandler(void *arg);
#else
extern void zdo_nlme_network_discovery_confirm_cb(void *arg);
#endif

const tl_zb_callback_t g_zbNwkEventFromMacTbl[] = {
    tl_zbMacMcpsDataConfirmHandler,
    tl_zbMacMcpsDataIndicationHandler,
    tl_zbMacMlmeScanConfirmHandler,
    tl_zbMacMlmeStartConfirmHandler,
    tl_zbMacMlmePollConfirmHandler,
    tl_zbMacMlmeAssociateConfirmHandler,
    tl_zbMacMlmeResetConfirmHandler,
    tl_zbMacMlmeOrphanIndicationHandler,
    tl_zbMacMlmeSyncLossIndicationHandler,
    tl_zbMacMlmePollIndicationHandler,
};

const tl_zb_callback_t g_zbNwkEventFromHighTbl[] = {
    tl_zbNwkNldeDataRequestHandler,
    tl_zbNwkNlmeNwkDiscRequestHandler,
    tl_zbNwkNlmeNetworkFormationRequestHandler,
    tl_zbNwkNlmePermitJoiningRequestHandler,
    tl_zbNwkNlmeStartRouterRequestHandler,
    tl_zbNwkNlmeEDScanRequestHandler,
    tl_zbNwkNlmeJoinRequestHandler,
    tl_zbNwkNlmeDirectJoinRequestHandler,
    tl_zbNwkNlmeLeaveRequestHandler,
    tl_zbNwkNlmeResetRequestHandler,
    tl_zbNwkNlmeSyncRequestHandler,
    tl_zbNwkNlmeRouteDiscRequestHandler,
};

void tl_zbMacMlmeScanConfirmHandler(void *arg)
{
    switch (g_zbNwkCtx.state) {
#if defined(ZB_ROUTER_ROLE)
    case NLME_STATE_FORMATION:
        nwk_formationScanCnfHandler(arg);
        break;
#endif
    case NLME_STATE_DISC:
        nwk_discoveryScanCnfHandler(arg);
        break;
    case NLME_STATE_REJOIN:
        nwk_rejoinScanCnfHandler(arg);
        break;
    case NLME_STATE_DIRECT_JOIN:
        nwk_directJoinScanCnfHandler(arg);
        break;
    case NLME_STATE_ED_SCAN:
        nwk_edScanCnfHandler(arg);
        break;
    default:
        zb_buf_free((zb_buf_t *)arg);
        break;
    }
}

void tl_zbMacMlmeStartConfirmHandler(void *arg)
{
#if defined(ZB_ROUTER_ROLE)
    switch (g_zbNwkCtx.state) {
    case NLME_STATE_FORMATION:
        nwk_formationStartCnfHandler(arg);
        break;
    case NLME_STATE_ROUTER_START:
        nwk_startRouterCnfHandler(arg);
        break;
    case NLME_STATE_PANID_CONFLICT:
        nwk_panIdConflictCnfHandler(arg);
        break;
    default:
        zb_buf_free((zb_buf_t *)arg);
        break;
    }
#else
    tl_zbTaskPost(zdo_nlme_network_discovery_confirm_cb, arg);
#endif
}

void tl_zbNwkNibInit(u8 coldReset)
{
    if (coldReset) {
        memset(&g_zbNIB, 0, sizeof(g_zbNIB));
    }

#if defined(ZB_ROUTER_ROLE) || defined(ZB_ED_ROLE_LIBZIGBEE)
    /* Seed the base NIB defaults (stack profile, address-alloc method, depth,
     * rxOnWhenIdle, ...) for every role that runs the libzigbee NWK. The port
     * previously kept this router-only, which left the libzigbee-based ED with
     * a zeroed NIB (stackProfile=0) — parent selection in the beacon-notify
     * handler rejects every beacon because entry.stackProfile != g_zbNIB
     * .stackProfile, so the ED could never pick a parent. */
    memcpy(&g_zbNIB, &nwkNibDefault, sizeof(g_zbNIB));
#endif
#if defined(ZB_ROUTER_ROLE)
    g_zbNIB.maxChildren = DEFAULT_MAX_CHILDREN;
    g_zbNIB.maxRouters = NWK_MAX_ROUTERS;
    g_zbNIB.maxSourceRoute = NWK_MAX_SOURCE_ROUTE;
    g_zbNIB.maxBroadcastRetries = NWK_MAX_BROADCAST_RETRIES;
    g_zbNIB.linkStatusPeriod = ZB_NWK_LINK_STATUS_PERIOD_DEFAULT;
    g_zbNIB.routerAgeLimit = TRANSFAILURE_CNT_MAX;
    g_zbNIB.passiveAckTimeout = NWK_PASSIVE_ACK_TIMEOUT;
    g_zbNIB.nwkBroadcastDeliveryTime = NWK_BROADCAST_DELIVERY_TIME;
    g_zbNIB.useTreeRouting = (af_nodeDevTypeGet() != DEVICE_TYPE_END_DEVICE);
#endif
    g_zbNIB.seqNum = 0;
    g_zbNIB.maxDepth = NWK_MAX_DEPTH;
    g_zbNIB.leaveReqAllowed = 1;
    g_zbNIB.leaveReqWithoutRejoinAllowed = 1;
    g_zbNIB.endDevTimeoutDefault = NWK_ENDDEV_TIMEOUT_DEFAULT;
    memcpy(g_zbNIB.ieeeAddr, g_zbMacPib.extAddress, EXT_ADDR_LEN);
}

void tl_zbNwkInit(u8 coldReset)
{
    tl_zbNwkNibInit(coldReset);
    tl_zbNwkAddrMapInit();
    tl_zbNeighborTableInit();
    memset(&g_zbNwkCtx, 0, sizeof(g_zbNwkCtx));
    g_zbNwkCtx.discoverRoute = 1;
#if defined(ZB_ROUTER_ROLE)
    nwkBrcTransTabInit();
    nwkRouteDiscTabInit();
    nwkRoutingTabInit();
#endif
}

u8 nwkHdrParse(nwk_hdr_t *pNwkHdr, u8 *msdu)
{
    u16 fc;

    memset(pNwkHdr, 0, sizeof(*pNwkHdr));
    fc = (u16)msdu[0] | ((u16)msdu[1] << 8);
    pNwkHdr->frameControl.frameType = (u8)(fc & 0x03U);
    pNwkHdr->frameControl.protocolVer = (u8)((fc >> 2) & 0x0fU);
    pNwkHdr->frameControl.discRoute = (u8)((fc >> 6) & 0x03U);
    pNwkHdr->frameControl.multicastFlg = (u8)((fc >> 8) & 0x01U);
    pNwkHdr->frameControl.security = (u8)((fc >> 9) & 0x01U);
    pNwkHdr->frameControl.srcRoute = (u8)((fc >> 10) & 0x01U);
    pNwkHdr->frameControl.dstIEEEAddr = (u8)((fc >> 11) & 0x01U);
    pNwkHdr->frameControl.srcIEEEAddr = (u8)((fc >> 12) & 0x01U);
    pNwkHdr->frameControl.endDevInitiator = (u8)((fc >> 13) & 0x01U);
    pNwkHdr->dstAddr = (u16)msdu[2] | ((u16)msdu[3] << 8);
    pNwkHdr->srcAddr = (u16)msdu[4] | ((u16)msdu[5] << 8);
    pNwkHdr->radius = msdu[6];
    pNwkHdr->seqNum = msdu[7];
    pNwkHdr->frameHdrLen = 8;

    if (pNwkHdr->frameControl.dstIEEEAddr) {
        memcpy(pNwkHdr->dstIeeeAddr, msdu + pNwkHdr->frameHdrLen, EXT_ADDR_LEN);
        pNwkHdr->frameHdrLen += EXT_ADDR_LEN;
    }
    if (pNwkHdr->frameControl.srcIEEEAddr) {
        memcpy(pNwkHdr->srcIeeeAddr, msdu + pNwkHdr->frameHdrLen, EXT_ADDR_LEN);
        pNwkHdr->frameHdrLen += EXT_ADDR_LEN;
    }

    return pNwkHdr->frameHdrLen;
}

u8 getNwkHdrSize(nwk_hdr_t *pNwkHdr)
{
    u8 size = 8;
    if (pNwkHdr->frameControl.dstIEEEAddr) {
        size += EXT_ADDR_LEN;
    }
    if (pNwkHdr->frameControl.srcIEEEAddr) {
        size += EXT_ADDR_LEN;
    }
    if (pNwkHdr->frameControl.multicastFlg) {
        size += 1;
    }
    return size;
}

u8 *nwkHdrBuilder(u8 *buf, nwk_hdr_t *pNwkHdr)
{
    u16 fc = 0;

    fc |= (u16)(pNwkHdr->frameControl.frameType & 0x03U);
    fc |= (u16)(pNwkHdr->frameControl.protocolVer & 0x0fU) << 2;
    fc |= (u16)(pNwkHdr->frameControl.discRoute & 0x03U) << 6;
    fc |= (u16)(pNwkHdr->frameControl.multicastFlg & 0x01U) << 8;
    fc |= (u16)(pNwkHdr->frameControl.security & 0x01U) << 9;
    fc |= (u16)(pNwkHdr->frameControl.srcRoute & 0x01U) << 10;
    fc |= (u16)(pNwkHdr->frameControl.dstIEEEAddr & 0x01U) << 11;
    fc |= (u16)(pNwkHdr->frameControl.srcIEEEAddr & 0x01U) << 12;
    fc |= (u16)(pNwkHdr->frameControl.endDevInitiator & 0x01U) << 13;

    buf[0] = (u8)fc;
    buf[1] = (u8)(fc >> 8);
    buf[2] = (u8)pNwkHdr->dstAddr;
    buf[3] = (u8)(pNwkHdr->dstAddr >> 8);
    buf[4] = (u8)pNwkHdr->srcAddr;
    buf[5] = (u8)(pNwkHdr->srcAddr >> 8);
    buf[6] = pNwkHdr->radius;
    buf[7] = pNwkHdr->seqNum;

    pNwkHdr->frameHdrLen = 8;
    if (pNwkHdr->frameControl.dstIEEEAddr) {
        memcpy(buf + pNwkHdr->frameHdrLen, pNwkHdr->dstIeeeAddr, EXT_ADDR_LEN);
        pNwkHdr->frameHdrLen += EXT_ADDR_LEN;
    }
    if (pNwkHdr->frameControl.srcIEEEAddr) {
        memcpy(buf + pNwkHdr->frameHdrLen, pNwkHdr->srcIeeeAddr, EXT_ADDR_LEN);
        pNwkHdr->frameHdrLen += EXT_ADDR_LEN;
    }
    if (pNwkHdr->frameControl.multicastFlg) {
        buf[pNwkHdr->frameHdrLen++] = *(u8 *)&pNwkHdr->mcastControl;
    }

    return buf + pNwkHdr->frameHdrLen;
}

#if defined(ZB_ROUTER_ROLE)
u32 getPassiveAckTimeout(void)
{
    return g_zbNIB.passiveAckTimeout ? g_zbNIB.passiveAckTimeout : NWK_PASSIVE_ACK_TIMEOUT;
}

void tl_zbNwkLinkStatusStop(void)
{
    linkStExpiry = 0;
    if (linkStTimer != NULL) {
        ev_timer_taskCancel(&linkStTimer);
    }
}

void tl_zbNwkNeighborTabAging(void)
{
    for (u8 i = 0; i < TL_ZB_NEIGHBOR_TABLE_SIZE; i++) {
        tl_zb_normal_neighbor_entry_t *entry = &g_zb_neighborTbl.neighborTbl[i];

        if (!entry->used || entry->relationship == NEIGHBOR_IS_PARENT) {
            continue;
        }

        if (entry->age != 0xffU) {
            entry->age++;
        }

        if (entry->deviceType == NWK_DEVICE_TYPE_ROUTER && entry->age >= g_zbNIB.routerAgeLimit) {
            entry->outgoingCost = 0;
        }
    }
}

u8 *nwk_linkStEntryBuild(u8 *payload, u8 *entryCnt)
{
    u8 count = 0;
    linkStatus_entry_t *list = (linkStatus_entry_t *)payload;

    for (u8 i = 0; i < TL_ZB_NEIGHBOR_TABLE_SIZE; i++) {
        tl_zb_normal_neighbor_entry_t *entry = &g_zb_neighborTbl.neighborTbl[i];

        if (!entry->used || entry->outgoingCost == 0U) {
            continue;
        }
        if (entry->deviceType != NWK_DEVICE_TYPE_ROUTER &&
            entry->deviceType != NWK_DEVICE_TYPE_COORDINATOR) {
            continue;
        }

        list[count].neighborNwkAddr = tl_zbshortAddrByIdx(entry->addrmapIdx);
        list[count].linkStatus.incomingCost = rf_lqi2cost(entry->lqi) & 0x07U;
        list[count].linkStatus.outingCost = entry->outgoingCost & 0x07U;
        count++;
    }

    if (entryCnt != NULL) {
        *entryCnt = count;
    }
    return payload + (count * sizeof(linkStatus_entry_t));
}

void nwkLinkStatusCmdSend(u8 firstFrame, u8 lastFrame)
{
    zb_buf_t *buf = zb_buf_allocate();
    nwk_hdr_t hdr;
    u8 *payload;
    u8 entryCnt = 0;

    if (buf == NULL) {
        return;
    }

    memset(&hdr, 0, sizeof(hdr));
    hdr.dstAddr = NWK_BROADCAST_ROUTER_COORDINATOR;
    hdr.srcAddr = g_zbNIB.nwkAddr;
    hdr.radius = 1;
    hdr.seqNum = g_zbNIB.seqNum++;
    hdr.frameControl.frameType = FRAME_TYPE_COMMAND;
    hdr.frameControl.protocolVer = ZB_PROTOCOL_VERSION;

    payload = buf->buf;
    payload[0] = NWK_CMD_LINK_STATUS;
    payload[1] = 0;
    if (firstFrame) {
        payload[1] |= 0x20U;
    }
    if (lastFrame) {
        payload[1] |= 0x40U;
    }

    nwk_linkStEntryBuild(payload + 2, &entryCnt);
    payload[1] |= (entryCnt & 0x1fU);

    nwk_tx(buf, &hdr, MAC_SHORT_ADDR_BROADCAST, 0, payload, (u8)(2U + entryCnt * sizeof(linkStatus_entry_t)));
}

void tl_zbNwkSendLinkStatus(void)
{
    if (!g_zbNwkCtx.joined || tl_zbNeighborTableRouterValidNumGet() == 0U) {
        return;
    }

    nwkLinkStatusCmdSend(1, 1);
}

void tl_zbNwkLinkStatusStart(void)
{
    tl_zbNwkSendLinkStatus();
    linkStExpiry = g_zbNIB.linkStatusPeriod ? g_zbNIB.linkStatusPeriod : ZB_NWK_LINK_STATUS_PERIOD_DEFAULT;
}

int tl_zbNwkLinkStatusTimerEvtCb(void *arg)
{
    (void)arg;
    linkStTimer = NULL;
    T_DBG_linkStatus++;
    tl_zbNwkNeighborTabAging();
    tl_zbNwkLinkStatusStart();
    return -1;
}

int nwk_linkStPeriodic(void *arg)
{
    (void)arg;

    if (linkStExpiry != 0U) {
        linkStExpiry--;
    }

    if (linkStExpiry == 0U && linkStTimer == NULL) {
        linkStTimer = ev_timer_taskPost(tl_zbNwkLinkStatusTimerEvtCb, NULL, 1);
    }

    return 0;
}

u8 tl_nwkGetAverageLqi(u8 oldLqi, u8 newLqi)
{
    if (oldLqi == 0U) {
        return newLqi;
    }

    return (u8)(((u16)oldLqi + (u16)newLqi) / 2U);
}

void tl_zbNwkLinkStatusCmdHandler(void *arg, nwk_hdr_t *pNwkHdr, nwkCmd_t *cmd)
{
    linkStatus_entry_t *list = cmd->linkSt.linkStatusList;
    u8 entryCnt = cmd->linkSt.options.entryCnt;

    for (u8 i = 0; i < entryCnt; i++) {
        tl_zb_normal_neighbor_entry_t *entry = nwk_neTblGetByShortAddr(list[i].neighborNwkAddr);

        if (entry == NULL) {
            continue;
        }

        entry->outgoingCost = list[i].linkStatus.incomingCost & 0x07U;
        entry->lqi = tl_nwkGetAverageLqi(entry->lqi, (u8)(list[i].linkStatus.outingCost * 32U));
        entry->age = 0;
    }

    (void)pNwkHdr;
    zb_buf_free((zb_buf_t *)arg);
}

void tl_zbNwkReportCmdHandler(void *arg, nwk_hdr_t *pNwkHdr, nwkCmd_t *cmd)
{
    if (pNwkHdr->srcAddr != g_zbNIB.managerAddr) {
        if (pNwkHdr->radius == 0U) {
            zb_buf_free((zb_buf_t *)arg);
            return;
        }

        nwkReportCmdSend((zb_buf_t *)arg, pNwkHdr, cmd, NWK_INTERNAL_NETWORK_REPORT_CMD_HANDLE);
        return;
    }

    nwkReportCmdHandler(arg, cmd);
}

void tl_zbNwkSendNwkStatusCmd(void *arg, nwk_hdr_t *pNwkHdr, nwkCmd_t *pNwkStatus, u8 handle)
{
    zb_buf_t *buf = (zb_buf_t *)arg;
    u8 payload[1 + sizeof(nwkCmd_nwkStatus_t)];

    if (buf == NULL || pNwkHdr == NULL || pNwkStatus == NULL) {
        return;
    }

    payload[0] = NWK_CMD_NETWORK_STATUS;
    memcpy(payload + 1, &pNwkStatus->nwkStatus, sizeof(nwkCmd_nwkStatus_t));
    buf->hdr.handle = handle;
    nwk_tx(buf, pNwkHdr, pNwkHdr->dstAddr, 0, payload, sizeof(payload));
}

void my_tl_zbNwkSendNwkStatusCmd(void *arg, u16 dstAddr, nwk_statusCode_t status)
{
    nwk_hdr_t hdr;
    nwkCmd_t cmd;

    memset(&hdr, 0, sizeof(hdr));
    memset(&cmd, 0, sizeof(cmd));
    hdr.dstAddr = dstAddr;
    hdr.srcAddr = g_zbNIB.nwkAddr;
    hdr.radius = g_zbNIB.maxDepth;
    hdr.seqNum = g_zbNIB.seqNum++;
    hdr.frameControl.frameType = FRAME_TYPE_COMMAND;
    hdr.frameControl.protocolVer = ZB_PROTOCOL_VERSION;
    cmd.cmdId = NWK_CMD_NETWORK_STATUS;
    cmd.nwkStatus.dstAddr = dstAddr;
    cmd.nwkStatus.statusCode = status;
    tl_zbNwkSendNwkStatusCmd(arg, &hdr, &cmd, NWK_INTERNAL_NETWORK_STATUS_CMD_HANDLE);
}
#endif

void tl_zbNwkStatusCmdHandler(void *arg, nwk_hdr_t *pNwkHdr, nwkCmd_t *cmd)
{
    (void)pNwkHdr;
    tl_zbNwkNlmeNwkStatusInd(arg, cmd->nwkStatus.dstAddr, cmd->nwkStatus.statusCode);
}

void tl_zbNwkTaskProc(void)
{
    tl_zb_task_t taskInfo;
    tl_zb_task_t *task = tl_zbTaskQPop(TL_Q_HIGH2NWK, &taskInfo);

    if (task != NULL && task->data != NULL) {
        u8 primitive = ((zb_buf_t *)task->data)->hdr.id;

        switch (primitive) {
        case NWK_NLDE_DATA_REQ:
            tl_zbNwkNldeDataRequestHandler(task->data);
            break;
        case NWK_NLME_NWK_DISCOVERY_REQ:
            tl_zbNwkNlmeNwkDiscRequestHandler(task->data);
            break;
        case NWK_NLME_NWK_FORMATION_REQ:
            tl_zbNwkNlmeNetworkFormationRequestHandler(task->data);
            break;
        case NWK_NLME_PERMIT_JOINING_REQ:
            tl_zbNwkNlmePermitJoiningRequestHandler(task->data);
            break;
        case NWK_NLME_START_ROUTER_REQ:
            tl_zbNwkNlmeStartRouterRequestHandler(task->data);
            break;
        case NWK_NLME_ED_SCAN_REQ:
            tl_zbNwkNlmeEDScanRequestHandler(task->data);
            break;
        case NWK_NLME_JOIN_REQ:
            tl_zbNwkNlmeJoinRequestHandler(task->data);
            break;
        case NWK_NLME_DIRECT_JOIN_REQ:
            tl_zbNwkNlmeDirectJoinRequestHandler(task->data);
            break;
        case NWK_NLME_LEAVE_REQ:
            tl_zbNwkNlmeLeaveRequestHandler(task->data);
            break;
        case NWK_NLME_RESET_REQ:
            tl_zbNwkNlmeResetRequestHandler(task->data);
            break;
        case NWK_NLME_SYNC_REQ:
            tl_zbNwkNlmeSyncRequestHandler(task->data);
            break;
        case NWK_NLME_ROUTE_DISCOVERY_REQ:
            tl_zbNwkNlmeRouteDiscRequestHandler(task->data);
            break;
        default:
            break;
        }
    }

    task = tl_zbTaskQPop(TL_Q_MAC2NWK, &taskInfo);
    if (task == NULL || task->data == NULL) {
        return;
    }

    switch (((zb_buf_t *)task->data)->hdr.id) {
    case MAC_MCPS_DATA_CNF:
        tl_zbMacMcpsDataConfirmHandler(task->data);
        break;
    case MAC_MCPS_DATA_IND:
        tl_zbMacMcpsDataIndicationHandler(task->data);
        break;
    case MAC_MLME_SCAN_CNF:
        tl_zbMacMlmeScanConfirmHandler(task->data);
        break;
    case MAC_MLME_BEACON_NOTIFY_IND:
        tl_zbMacMlmeBeaconNotifyIndicationHandler(task->data);
        break;
    case MAC_MLME_START_CNF:
        tl_zbMacMlmeStartConfirmHandler(task->data);
        break;
    case MAC_MLME_POLL_CNF:
        tl_zbMacMlmePollConfirmHandler(task->data);
        break;
    case MAC_MLME_ASSOCIATE_CNF:
        tl_zbMacMlmeAssociateConfirmHandler(task->data);
        break;
#if defined(ZB_ROUTER_ROLE)
    case MAC_MLME_ASSOCIATE_IND:
        tl_zbMacMlmeAssociateIndicationHandler(task->data);
        break;
    case MAC_MLME_COMM_STATUS_IND:
        tl_zbMacMlmeCommStatusIndicationHandler(task->data);
        break;
#endif
    case MAC_MLME_RESET_CNF:
        tl_zbMacMlmeResetConfirmHandler(task->data);
        break;
    case MAC_MLME_ORPHAN_IND:
        tl_zbMacMlmeOrphanIndicationHandler(task->data);
        break;
    case MAC_MLME_SYNC_LOSS_IND:
        tl_zbMacMlmeSyncLossIndicationHandler(task->data);
        break;
    case MAC_MLME_POLL_IND:
        tl_zbMacMlmePollIndicationHandler(task->data);
        break;
    default:
        break;
    }
}

void tl_zbNwkNibSet(void *arg)
{
    nlme_set_req_t *req = (nlme_set_req_t *)arg;

    if (req == NULL || req->nibAttrVal == NULL) {
        return;
    }

    switch (req->nibAttr) {
    case NIB_ATTRIBUTE_SEQUENCE_NUMBER:
        g_zbNIB.seqNum = req->nibAttrVal[0];
        break;
    case NIB_ATTRIBUTE_NETWORK_ADDRESS:
        memcpy(&g_zbNIB.nwkAddr, req->nibAttrVal, sizeof(g_zbNIB.nwkAddr));
        break;
    case NIB_ATTRIBUTE_PAN_ID:
        memcpy(&g_zbNIB.panId, req->nibAttrVal, sizeof(g_zbNIB.panId));
        break;
    case NIB_ATTRIBUTE_EXTENDED_PANID:
        memcpy(g_zbNIB.extPANId, req->nibAttrVal, EXT_ADDR_LEN);
        break;
    case NIB_ATTRIBUTE_UPDATE_ID:
        g_zbNIB.updateId = req->nibAttrVal[0];
        break;
    case NIB_ATTRIBUTE_PARENT_INFORMATION:
        g_zbNIB.parentInfo = req->nibAttrVal[0];
        break;
#if defined(ZB_ROUTER_ROLE)
    case NIB_ATTRIBUTE_PASSIVE_ASK_TIMEOUT:
        memcpy(&g_zbNIB.passiveAckTimeout, req->nibAttrVal, sizeof(g_zbNIB.passiveAckTimeout));
        break;
    case NIB_ATTRIBUTE_MAX_BROADCAST_RETRIES:
        g_zbNIB.maxBroadcastRetries = req->nibAttrVal[0];
        break;
    case NIB_ATTRIBUTE_BROADCAST_DELIVERY_TIME:
        memcpy(&g_zbNIB.nwkBroadcastDeliveryTime, req->nibAttrVal, sizeof(g_zbNIB.nwkBroadcastDeliveryTime));
        break;
    case NIB_ATTRIBUTE_ROUTE_DISCOVERY_RETRIES_PERMITTED:
        NWKC_RREQ_RETRIES = req->nibAttrVal[0];
        break;
    case NIB_ATTRIBUTE_LINK_STATUS_PERIOD:
        g_zbNIB.linkStatusPeriod = req->nibAttrVal[0];
        break;
    case NIB_ATTRIBUTE_ROUTER_AGE_LIMIT:
        g_zbNIB.routerAgeLimit = req->nibAttrVal[0];
        break;
#endif
    default:
        break;
    }
}

u8 is_device_factory_new(void)
{
    return (memcmp(g_zbNIB.extPANId, g_zero_addr, EXT_ADDR_LEN) == 0) ? 1U : 0U;
}
