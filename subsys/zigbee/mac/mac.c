/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Adapted from libzigbee/src/mac.c. Vendor file kept structurally
 * one-for-one; vendor zb_local.h / mac_trx_api.h / ev_timer.h / mac_phy.h
 * are replaced by the Zephyr include set.
 */
#include "zb_common_stub.h"
#include "common/static_assert.h"
#include "os/ev_timer.h"
#include "mac/includes/tl_zb_mac.h"
#include "mac/includes/tl_zb_mac_pib.h"
#include "mac/includes/mac_phy.h"
#include "mac/includes/mac_trx_api.h"
#include "nwk/includes/nwk.h"
#include "nwk/includes/nwk_neighbor.h"
#include "mac/includes/mac_internal.h"

mac_appIndCb_t *macAppIndCb = NULL;
tl_zb_mac_ctx_t g_zbMacCtx;


static inline u8 *phy_ind_payload_ptr_get(void *arg)
{
    zb_mac_rx_meta_t *meta = (zb_mac_rx_meta_t *)arg;
    u8 *ptr = NULL;

    memcpy(&ptr, &meta->payload, sizeof(ptr));
    return ptr;
}

static inline void phy_ind_payload_ptr_set(void *arg, u8 *ptr)
{
    zb_mac_rx_meta_t *meta = (zb_mac_rx_meta_t *)arg;

    memcpy(&meta->payload, &ptr, sizeof(ptr));
}

static inline u8 phy_ind_payload_len_get(void *arg)
{
    return ((zb_mac_rx_meta_t *)arg)->payloadLen;
}

static inline void phy_ind_payload_len_set(void *arg, u8 len)
{
    ((zb_mac_rx_meta_t *)arg)->payloadLen = len;
}

static inline u8 *phy_ind_payload_advance(void *arg, u8 hdrLen)
{
    u8 *payload = phy_ind_payload_ptr_get(arg) + hdrLen;
    phy_ind_payload_ptr_set(arg, payload);
    phy_ind_payload_len_set(arg, (u8)(phy_ind_payload_len_get(arg) - hdrLen));
    return payload;
}

typedef struct {
    u32 primitive;
    tl_zb_callback_t handler;
} mac_nwk_evt_t;

static bool phy_ind_beacon_notify_post(zb_buf_t *buf, tl_zb_mac_mhr_t *mhr, u8 *payload, u8 payloadLen)
{
    zb_mac_rx_meta_t *meta = (zb_mac_rx_meta_t *)buf;
    enum {
        BEACON_NOTIFY_SIZE = sizeof(zb_mlme_beacon_notify_ind_t),
        BEACON_FIXED_OVERHEAD = 4,
        ZIGBEE_BEACON_PAYLOAD_LEN = 11,
    };

    u8 *safe;
    u8 shortPendingNum;
    u8 extPendingNum;
    u8 addrListLen;
    u8 beaconPayloadOffset;
    zb_mlme_beacon_notify_ind_t ind;

    if (payloadLen <= BEACON_FIXED_OVERHEAD ||
        payload[0] != 0xffU ||
        (payload[1] & 0x0fU) != 0x0fU ||
        payload[2] != 0U) {
        return FALSE;
    }

    shortPendingNum = payload[3] & 0x07U;
    extPendingNum = (payload[3] >> 4) & 0x07U;
    addrListLen = (u8)(shortPendingNum * 2U + extPendingNum * 8U);
    beaconPayloadOffset = (u8)(BEACON_FIXED_OVERHEAD + addrListLen);

    if (payloadLen < (u8)(beaconPayloadOffset + ZIGBEE_BEACON_PAYLOAD_LEN)) {
        return FALSE;
    }

    safe = (u8 *)buf + BEACON_NOTIFY_SIZE;
    memmove(safe, payload, payloadLen);

    memset(&ind, 0, sizeof(ind));
    ind.panDesc.timestamp = meta->timestamp;
    ind.panDesc.coordPanId = mhr->srcPanId;
    ind.panDesc.superframeSpec = (u16)safe[0] | ((u16)safe[1] << 8);
    ind.panDesc.coordAddr.addrMode = mhr->srcAddrMode;
    if (mhr->srcAddrMode == ADDR_MODE_EXT) {
        memcpy(ind.panDesc.coordAddr.addr.extAddr, mhr->srcAddr.extAddr, EXT_ADDR_LEN);
    } else {
        ind.panDesc.coordAddr.addr.shortAddr = mhr->srcAddr.shortAddr;
    }
    ind.panDesc.logicalChannel = meta->curChannel;
    ind.panDesc.gtsPermit = safe[2];
    ind.panDesc.linkQuality = meta->linkQuality;
    ind.pAddrList = (addrListLen != 0U) ? (safe + BEACON_FIXED_OVERHEAD) : NULL;
    ind.psdu = safe + beaconPayloadOffset;
    ind.bsn = mhr->seqNum;
    ind.pendAddrSpec = safe[3];
    ind.psduLength = (u8)(payloadLen - beaconPayloadOffset);

    memcpy(buf, &ind, sizeof(ind));
    tl_zbPrimitivePost(TL_Q_MAC2NWK, MAC_MLME_BEACON_NOTIFY_IND, buf);

    if (g_zbMacCtx.status == ZB_MAC_STATE_ACTIVE_SCAN) {
        tl_zbMacActiveScanListAdd();
    }

    return TRUE;
}

#if defined(ZB_ROUTER_ROLE)
const mac_nwk_evt_t g_zbMacEventFromNwkTbl[] = {
    {MAC_MLME_ASSOCIATE_REQ, tl_zbMacAssociateRequestHandler},
    {MAC_MLME_ASSOCIATE_RES, tl_zbMacAssociateResponseHandler},
    {MAC_MLME_ORPHAN_RES, tl_zbMacOrphanResponseHandler},
    {MAC_MLME_POLL_REQ, tl_zbMacPollRequestHandler},
    {MAC_MLME_RESET_REQ, tl_zbMacResetRequestHandler},
    {MAC_MLME_SCAN_REQ, (tl_zb_callback_t)tl_zbMacScanRequestHandler},
    {MAC_MLME_START_REQ, tl_zbMacStartRequestHandler},
    {MAC_MLME_DISASSOCIATE_REQ, tl_zbMacDisassociateRequestHandler},
};
#else
const tl_zb_callback_t g_zbMacEventFromNwkTbl[] = {
    tl_zbMacMcpsDataRequestProc,
    NULL,
    tl_zbMacAssociateRequestHandler,
    tl_zbMacDisassociateRequestHandler,
    NULL,
    tl_zbMacResetRequestHandler,
    NULL,
    (tl_zb_callback_t)tl_zbMacScanRequestHandler,
    NULL,
    tl_zbMacStartRequestHandler,
    tl_zbMacPollRequestHandler,
};
#endif

extern volatile u32 zb_nwk_ed_trace[];

void tl_zbPhyIndication(void *arg, u8 *raw, u8 len)
{
    zb_buf_t *buf = (zb_buf_t *)arg;
    tl_zb_mac_mhr_t *mhr = (tl_zb_mac_mhr_t *)raw;
    u8 *macPld;
    u8 frameType;

    if (arg == NULL) {
        return;
    }

    if (raw == NULL || len == 0U) {
        zb_buf_free(buf);
        return;
    }

    macPld = phy_ind_payload_ptr_get(arg);
    frameType = macPld[0] & 0x07U;

    /* [10]: low 16 = phy_ind RX count; bits 16..18 = frameType (last). */
    zb_nwk_ed_trace[10] = (zb_nwk_ed_trace[10] & 0xfff80000U) |
			   (((u32)frameType & 0x7U) << 16) |
			   ((zb_nwk_ed_trace[10] + 1U) & 0xffffU);


    if (mhr->dstAddrMode == ADDR_MODE_SHORT &&
        mhr->dstAddr.shortAddr == MAC_SHORT_ADDR_BROADCAST) {
        g_sysDiags.macRxBcast++;
    }

    if (macAppIndCb != NULL) {
        if (frameType == MAC_FRAME_TYPE_BEACON &&
            macAppIndCb->macBeaconRcvCb != NULL &&
            !macAppIndCb->macBeaconRcvCb(arg)) {
            zb_buf_free(buf);
            return;
        }

        if (frameType == MAC_FRAME_TYPE_COMMAND &&
            macAppIndCb->macBeaconReqRcvCb != NULL &&
            macPld[len] == MAC_CMD_BEACON_REQUEST &&
            !macAppIndCb->macBeaconReqRcvCb(arg)) {
            zb_buf_free(buf);
            return;
        }
    }

    macPld = phy_ind_payload_advance(arg, len);

    if (frameType == MAC_FRAME_TYPE_DATA) {
        tl_zbPhyMldeIndication(buf, raw, len);
        return;
    }

    if (frameType == MAC_FRAME_TYPE_COMMAND) {
        tl_zbPhyMlmeIndicate(buf, raw, len);
        return;
    }

    if (frameType != MAC_FRAME_TYPE_BEACON) {
        zb_buf_free(buf);
        return;
    }

    if (g_zbMacPib.autoReq != 0U) {
        if (g_zbMacCtx.status == ZB_MAC_STATE_ACTIVE_SCAN) {
            tl_zbMacActiveScanListAdd();
        }
        zb_buf_free(buf);
        return;
    }

    if (!phy_ind_beacon_notify_post(buf, mhr, macPld, phy_ind_payload_len_get(arg))) {
        zb_buf_free(buf);
    } else {
    }
}

void tl_zbMacChannelSet(u8 chan)
{
    g_zbMacCtx.curChannel = chan;
    rf_setChannel(chan);
}

void mac_pibNvInit(u8 coldReset)
{
    if (!coldReset) {
        tl_zbMacChannelSet(g_zbMacPib.phyChannelCur);
        return;
    }

    memcpy(&g_zbMacPib, &macPibDefault, sizeof(g_zbMacPib));
    generateIEEEAddr();
    g_zbMacPib.seqNum = (u8)drv_u32Rand();
    g_zbMacPib.beaconSeqNum = (u8)drv_u32Rand();

    if (g_zbMacPib.maxBe < g_zbMacPib.minBe) {
        g_zbMacPib.maxBe = g_zbMacPib.minBe;
    }

    tl_zbMacChannelSet(g_zbMacPib.phyChannelCur);
}

void tl_zbMacReset(void)
{
    mac_pibNvInit(1);
    g_zbMacPib.associationPermit = 0;
}

void tl_zbMacInit(u8 coldReset)
{
    mac_pibNvInit(coldReset);
    mac_trxInit();
    g_zbMacPib.associationPermit = 0;
    g_zbMacCtx.txRawDataBuf = (u8 *)zb_buf_allocate();
}

void tl_zbMaxTxConfirmCb(void *arg, u8 status)
{
    zb_buf_t *buf = (zb_buf_t *)arg;
    u8 handle;

    if (buf == NULL) {
        return;
    }

    if (buf == (zb_buf_t *)g_zbMacCtx.txRawDataBuf) {
        buf->hdr.active = 0;
    }

    handle = buf->hdr.handle;
    if (handle == 0xe0U) {
        tl_zbMacAssociateRequestStatusCheck(arg, status);
        return;
    }

    if (handle == 0xe1U) {
        tl_zbMacCommStatusSend(arg, status);
        return;
    }

    if (handle == 0xe2U) {
        tl_zbMacDisassociateNotifyCmdConfirm(arg, status);
        return;
    }

    if (handle == 0xe8U || handle == 0xe9U) {
        tl_zbMacDataRequestStatusCheck(buf, status);
        return;
    }

    if (handle == 0xe6U) {
        tl_zbMacStartReqConfirm(arg, status);
        return;
    }

#if defined(ZB_ROUTER_ROLE)
    if (handle == 0xe3U) {
        tl_zbMacMlmeBeaconSendConfirm(arg, status);
        return;
    }

    if (handle == 0xe5U) {
        tl_zbMacOrphanResponseStatusCheck(arg, status);
        return;
    }
#endif

    if (handle == 0xeaU || handle == 0xe4U) {
        return;
    }

    tl_zbMacMcpsDataRequestSendConfirm(buf, status);
}

void tl_zbMacTaskProc(void)
{
    tl_zb_task_t taskInfo;
    tl_zb_task_t *task = tl_zbTaskQPop(TL_Q_NWK2MAC, &taskInfo);

    if (task != NULL && taskInfo.data != NULL) {
        u8 primitive = ((zb_buf_t *)taskInfo.data)->hdr.id;


#if defined(ZB_ROUTER_ROLE)
        for (u8 i = 0; i < ARRAY_SIZE(g_zbMacEventFromNwkTbl); i++) {
            if (g_zbMacEventFromNwkTbl[i].primitive == primitive &&
                g_zbMacEventFromNwkTbl[i].handler != NULL) {
                g_zbMacEventFromNwkTbl[i].handler(taskInfo.data);
                break;
            }
        }
#else
        switch (primitive) {
        case MAC_MCPS_DATA_REQ:
            tl_zbMacMcpsDataRequestProc(taskInfo.data);
            break;
        case MAC_MLME_ASSOCIATE_REQ:
            tl_zbMacAssociateRequestHandler(taskInfo.data);
            break;
        case MAC_MLME_DISASSOCIATE_REQ:
            tl_zbMacDisassociateRequestHandler(taskInfo.data);
            break;
        case MAC_MLME_RESET_REQ:
            tl_zbMacResetRequestHandler(taskInfo.data);
            break;
        case MAC_MLME_SCAN_REQ:
            tl_zbMacScanRequestHandler((zb_mac_mlme_scan_req_t *)taskInfo.data);
            break;
        case MAC_MLME_START_REQ:
            tl_zbMacStartRequestHandler(taskInfo.data);
            break;
        case MAC_MLME_POLL_REQ:
            tl_zbMacPollRequestHandler(taskInfo.data);
            break;
        default:
            break;
        }
#endif
    }

    zb_macTimerEventProc(NULL);
}

void mac_appIndCbRegister(mac_appIndCb_t *cb)
{
    macAppIndCb = cb;
}
