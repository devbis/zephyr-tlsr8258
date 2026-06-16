/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Adapted from libzigbee/src/mac_associate.c. Vendor file kept structurally
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

enum {
    ASSOC_REQ_CONFIRM_SIZE = 22,
};

static ev_timer_event_t *assocRspTimeoutEvt;
void *associationReqOrigBuffer = NULL;


static inline u32 assoc_timeout_ms(void)
{
    u32 base = g_zbInfo.macPib.respWaitTime;
    return ((((base << 4) - base) << 10) / 1000U);
}

static int tl_zbWaitForAssociationRespTimeout(void *arg)
{
    u8 *req = (u8 *)arg;

    memset(req, 0, ASSOC_REQ_CONFIRM_SIZE);
    req[10] = MAC_STA_NO_DATA;
    tl_zbPrimitivePost(TL_Q_MAC2NWK, MAC_MLME_ASSOCIATE_CNF, req);

    associationReqOrigBuffer = NULL;
    g_zbMacCtx.status = ZB_MAC_STATE_NORMAL;
    assocRspTimeoutEvt = NULL;

    return -1;
}

static int tl_zbReadyToPullParentForAssoRsp(void *arg)
{
    (void)arg;

    if (associationReqOrigBuffer == NULL) {
        return -1;
    }

    {
        zb_buf_t *buf = zb_buf_allocate();

        if (buf == NULL) {
            u8 *req = (u8 *)associationReqOrigBuffer;

            memset(req, 0, ASSOC_REQ_CONFIRM_SIZE);
            req[10] = MAC_STA_NO_RESOURCES;
            tl_zbPrimitivePost(TL_Q_MAC2NWK, MAC_MLME_ASSOCIATE_CNF, req);
            associationReqOrigBuffer = NULL;
            return -1;
        }

        memcpy(buf, associationReqOrigBuffer, 25);

        {
            zb_mlme_data_req_cmd_t dataReq;
            const u8 *origReq = (const u8 *)associationReqOrigBuffer;
            u8 coordAddrMode = origReq[12];

            /*
             * IEEE 802.15.4 MAC DATA-REQUEST sent by the joining device
             * to poll the parent for the pending ASSOCIATION-RESPONSE:
             *   src = us (joiner) — we don't have a short address yet
             *   dst = coordinator (parent we just sent ASSOC_REQ to)
             *
             * The vendor port had src/dst swapped (src=coord, dst=us),
             * which produced a self-loop frame the coordinator would
             * discard. It also miscoerced the coordinator's short
             * address into an 8-byte ext-addr field. Build the request
             * from the right side of the association-request buffer:
             *   origReq[12]   = coordAddrMode  (ADDR_MODE_SHORT/EXT)
             *   origReq[4..]  = coordAddress.addr (2 or 8 bytes)
             */
            memset(&dataReq, 0, sizeof(dataReq));
            dataReq.srcAddrMode = ZB_ADDR_64BIT_DEV;
            memcpy(dataReq.srcAddr.extAddr,
                   g_zbInfo.macPib.extAddress, EXT_ADDR_LEN);

            if (coordAddrMode == ADDR_MODE_SHORT) {
                dataReq.dstAddrMode = ADDR_MODE_SHORT;
                dataReq.dstAddr.shortAddr =
                    (u16)origReq[4] | ((u16)origReq[5] << 8);
            } else {
                dataReq.dstAddrMode = ADDR_MODE_EXT;
                memcpy(dataReq.dstAddr.extAddr,
                       origReq + 4, EXT_ADDR_LEN);
            }

            dataReq.cbType = 0;

            tl_zbMacMlmeDataRequestCmdSend(&dataReq, buf, MAC_STA_NO_ACK);
        }
    }

    return -1;
}

void tl_zbMacAssocPollConfirm(u8 status)
{
    u8 *req = (u8 *)associationReqOrigBuffer;

    if (req == NULL) {
        return;
    }

    if (status == MAC_STA_FRAME_PENDING) {
        g_zbMacCtx.status = 5;
        if (assocRspTimeoutEvt == NULL) {
            assocRspTimeoutEvt = ev_timer_taskPost(tl_zbWaitForAssociationRespTimeout, req, assoc_timeout_ms());
        }
        return;
    }

    if (status == MAC_SUCCESS) {
        if (assocRspTimeoutEvt == NULL) {
            assocRspTimeoutEvt = ev_timer_taskPost(tl_zbWaitForAssociationRespTimeout, req, assoc_timeout_ms());
        }
        return;
    }

    memset(req, 0, ASSOC_REQ_CONFIRM_SIZE);
    req[10] = status;
    tl_zbPrimitivePost(TL_Q_MAC2NWK, MAC_MLME_ASSOCIATE_CNF, req);
    associationReqOrigBuffer = NULL;
    g_zbMacCtx.status = ZB_MAC_STATE_NORMAL;
}

void tl_zbMacAssociateRespReceived(void)
{
    mac_pendingWaitTimerCancel();

    if (assocRspTimeoutEvt != NULL) {
        ev_timer_taskCancel(&assocRspTimeoutEvt);
    }

    g_zbMacCtx.status = ZB_MAC_STATE_NORMAL;
}

void tl_zbMacAssociateRequestStatusCheck(void *arg, u8 status)
{
    (void)arg;

    if (associationReqOrigBuffer == NULL) {
        return;
    }

    if (status != MAC_SUCCESS && status != MAC_STA_FRAME_PENDING) {
        u8 *req = (u8 *)associationReqOrigBuffer;

        memset(req, 0, ASSOC_REQ_CONFIRM_SIZE);
        req[10] = status;
        tl_zbPrimitivePost(TL_Q_MAC2NWK, MAC_MLME_ASSOCIATE_CNF, req);
        associationReqOrigBuffer = NULL;
        return;
    }

    ev_timer_taskPost(tl_zbReadyToPullParentForAssoRsp, NULL, assoc_timeout_ms());
}

extern volatile u32 zb_nwk_ed_trace[];

void tl_zbMacAssociateRequestHandler(void *arg)
{
    u8 *req = (u8 *)arg;
    zb_buf_t *txBuf = (zb_buf_t *)g_zbMacCtx.txRawDataBuf;

    /* [5]: low 16 = AssocReqHandler hit count; bit 16 = TX_ACTIVE early exit. */
    zb_nwk_ed_trace[5] = (zb_nwk_ed_trace[5] & 0xffff0000U) |
			  ((zb_nwk_ed_trace[5] + 1U) & 0xffffU);

    if (txBuf == NULL ||
        ((((u8 *)txBuf)[OFFSETOF(zb_buf_t, hdr) + 3] & 0x08U) != 0U) ||
        associationReqOrigBuffer != NULL) {
        zb_nwk_ed_trace[5] |= 1U << 16;
        memset(req, 0, ASSOC_REQ_CONFIRM_SIZE);
        req[10] = MAC_STA_TX_ACTIVE;
        tl_zbPrimitivePost(TL_Q_MAC2NWK, MAC_MLME_ASSOCIATE_CNF, req);
        associationReqOrigBuffer = NULL;
        return;
    }

    ((u8 *)txBuf)[OFFSETOF(zb_buf_t, hdr) + 3] |= 0x08U;
    associationReqOrigBuffer = arg;
    g_zbMacCtx.curChannel = req[0];
    rf_setChannel(req[0]);

    {
        tl_zb_mac_mhr_t mhr;
        u8 *payload;
        u8 hdrSize;
        u8 txStatus;

        memset(&mhr, 0, sizeof(mhr));
        mhr.dstPanId = (u16)req[2] | ((u16)req[3] << 8);
        mhr.srcPanId = 0xffffU;
        memcpy(&mhr.dstAddr, req + 4, sizeof(mhr.dstAddr));
        memcpy(&mhr.srcAddr, g_zbInfo.macPib.extAddress, EXT_ADDR_LEN);
        {
            /*
             * Vendor formula `0xc023 | (req[12] << 10)` shifts two
             * unrelated capability_info bits into the FCF dst addr
             * mode slot. For a router/ED joining via the parent's
             * short address, dstAddrMode is always SHORT (2). Read
             * the addr_t.addrMode out of the primitive buffer at the
             * correct offset (offset 4 + 8-byte ext-addr union slot
             * = 12 for non-packed addr_t; this matches the vendor
             * layout where coordAddress sits at offset 4).
             */
            u8 dstMode = req[12];

            if (dstMode != ADDR_MODE_SHORT && dstMode != ADDR_MODE_EXT) {
                dstMode = ADDR_MODE_SHORT;
            }
            mhr.frameCtrl = (u16)(0xc023U | ((u16)dstMode << 10));
            mhr.dstAddrMode = dstMode;
        }

        hdrSize = (u8)(tl_zbMacHdrSize(mhr.frameCtrl) + 2U);
        {
            u8 *psdu = tl_bufInitalloc(txBuf, hdrSize);

            txBuf->hdr.handle = 0xe0U;
            payload = tl_zbMacHdrBuilder(psdu, &mhr);
            payload[0] = MAC_CMD_ASSOCIATION_REQUEST;
            /*
             * Vendor reads req[13], but in the Zephyr-port layout of
             * zb_mlme_associate_req_t the capability_info_t byte sits
             * at offset 14: addr_t.addrMode is at offset 12 and offset
             * 13 is the implicit u8 padding to align addr_t to 2 bytes
             * (sizeof(addr_t) == 10 here). req[13] reads padding (0)
             * and the parent silently rejects the AssocReq because the
             * cap byte advertises rx-off-when-idle / RFD-no-mains, so
             * the coord treats the joiner as a non-FFD ED instead of a
             * router. Use req[14] to pick up the real cap byte.
             */
            payload[1] = req[14];

            txStatus = tl_zbMacTx(txBuf, psdu, hdrSize, 1, NULL);
        }
        if (txStatus != MAC_SUCCESS) {
            memset(req, 0, ASSOC_REQ_CONFIRM_SIZE);
            req[10] = MAC_STA_TX_ACTIVE;
            tl_zbPrimitivePost(TL_Q_MAC2NWK, MAC_MLME_ASSOCIATE_CNF, req);
            associationReqOrigBuffer = NULL;
        }
    }
}

#if defined(ZB_ROUTER_ROLE)
void tl_zbMacAssociateResponseHandler(void *arg)
{
    u8 *req = (u8 *)arg;
    u8 rawMhr[26];
    u8 pendingAddr[9];
    u8 *payload;
    u8 hdrSize;
    u8 status;

    if (g_zbNwkCtx.joined_pro) {
        zb_buf_free((zb_buf_t *)arg);
        return;
    }

    memset(rawMhr, 0, sizeof(rawMhr));
    rawMhr[6] = g_zbInfo.macPib.panId;
    rawMhr[7] = (u8)(g_zbInfo.macPib.panId >> 8);
    rawMhr[16] = rawMhr[6];
    rawMhr[17] = rawMhr[7];
    memcpy(rawMhr + 8, req + 2, EXT_ADDR_LEN);
    memcpy(rawMhr + 18, g_zbInfo.macPib.extAddress, EXT_ADDR_LEN);
    *(u16 *)(void *)rawMhr = 0xcc63U;

    hdrSize = (u8)(tl_zbMacHdrSize(0xcc63U) + 4U);
    payload = tl_bufInitalloc((zb_buf_t *)arg, hdrSize);
    payload = tl_zbMacHdrBuilder(payload, (tl_zb_mac_mhr_t *)rawMhr);
    payload[0] = MAC_CMD_ASSOCIATION_RESPONSE;
    memcpy(payload + 1, req, 2);
    payload[3] = req[10];

    pendingAddr[0] = ADDR_MODE_EXT;
    memcpy(pendingAddr + 1, req + 2, EXT_ADDR_LEN);

    memcpy(arg, &payload, sizeof(payload));
    req[4] = hdrSize;
    ((zb_buf_t *)arg)->hdr.handle = 0xe1U;

    status = macDataPending(arg,
                            *(u32 *)(void *)(pendingAddr + 1),
                            *(u32 *)(void *)(pendingAddr + 5),
                            pendingAddr[0]);
    if (status != MAC_SUCCESS) {
        tl_zbMacCommStatusSend(arg, status);
    }
}
#endif

void tl_zbMacDisassociateNotifyCmdConfirm(void *arg, u8 status)
{
    ((u8 *)arg)[11] = status;
    tl_zbPrimitivePost(TL_Q_MAC2NWK, MAC_MLME_DISASSOCIATE_CNF, arg);
}

u8 tl_macMlmeDisassociateNotifyCmdSend(void *arg)
{
    u8 *req = (u8 *)arg;
    tl_zb_mac_mhr_t mhr;
    zb_buf_t *txBuf = (zb_buf_t *)arg;
    u8 *payload;
    u8 hdrSize;
    u8 status;

    memset(&mhr, 0, sizeof(mhr));
    mhr.dstPanId = g_zbInfo.macPib.panId;
    mhr.srcPanId = mhr.dstPanId;

    if (req[11] == 1U) {
        memcpy(&mhr.dstAddr, req + 2, EXT_ADDR_LEN);
    } else if (req[11] == 2U) {
        if (req[10] == ZB_ADDR_16BIT_DEV_OR_BROADCAST) {
            mhr.dstAddr.shortAddr = g_zbInfo.macPib.coordShortAddress;
        } else {
            memcpy(&mhr.dstAddr, g_zbInfo.macPib.coordExtAddress, EXT_ADDR_LEN);
        }
    } else {
        memcpy(&mhr.dstAddr, g_zbInfo.macPib.extAddress, EXT_ADDR_LEN);
    }

    memcpy(&mhr.srcAddr, g_zbInfo.macPib.extAddress, EXT_ADDR_LEN);
    mhr.frameCtrl = (u16)(0xc063U | ((u16)req[10] << 10));

    hdrSize = (u8)(tl_zbMacHdrSize(mhr.frameCtrl) + 2U);
    payload = tl_bufInitalloc(txBuf, hdrSize);
    payload = tl_zbMacHdrBuilder(payload, &mhr);
    txBuf->hdr.handle = 0xe2U;
    payload[0] = MAC_CMD_DISASSOCIATION_NOTIFICATION;
    payload[1] = req[11];

    status = tl_zbMacTx(txBuf, payload, hdrSize, 1, NULL);
    if (status != MAC_SUCCESS) {
        return MAC_STA_CHANNEL_ACCESS_FAILURE;
    }

    return MAC_SUCCESS;
}

void tl_zbMacDisassociateRequestHandler(void *arg)
{
    u8 *req = (u8 *)arg;
    u16 localPanId = g_zbInfo.macPib.panId;

    if (((u16)req[0] | ((u16)req[1] << 8)) != localPanId) {
        req[11] = MAC_STA_INVALID_PARAMETER;
        tl_zbPrimitivePost(TL_Q_MAC2NWK, MAC_MLME_DISASSOCIATE_CNF, arg);
        return;
    }

    if (req[10] == ZB_ADDR_16BIT_DEV_OR_BROADCAST) {
        u16 parentShort = g_zbInfo.macPib.coordShortAddress;

        if (((u16)req[2] | ((u16)req[3] << 8)) != parentShort) {
            req[11] = MAC_STA_INVALID_PARAMETER;
            tl_zbPrimitivePost(TL_Q_MAC2NWK, MAC_MLME_DISASSOCIATE_CNF, arg);
            return;
        }
    } else if (req[10] == ZB_ADDR_64BIT_DEV) {
        if (memcmp(req + 2, g_zbInfo.macPib.coordExtAddress, EXT_ADDR_LEN) != 0) {
            req[11] = MAC_STA_INVALID_PARAMETER;
            tl_zbPrimitivePost(TL_Q_MAC2NWK, MAC_MLME_DISASSOCIATE_CNF, arg);
            return;
        }
    } else {
        req[11] = MAC_STA_INVALID_PARAMETER;
        tl_zbPrimitivePost(TL_Q_MAC2NWK, MAC_MLME_DISASSOCIATE_CNF, arg);
        return;
    }

    {
        u8 status = tl_macMlmeDisassociateNotifyCmdSend(arg);

        if (status != MAC_SUCCESS) {
            req[11] = status;
            tl_zbPrimitivePost(TL_Q_MAC2NWK, MAC_MLME_DISASSOCIATE_CNF, arg);
        }
    }
}

void tl_zbMlmeCmdDisassociateNotifyRecvd(void *arg, void *raw)
{
    u8 *ind = (u8 *)arg;
    u8 *src = (u8 *)raw;
    u8 *reasonSrc = *(u8 **)(ind + 4);

    memcpy(ind, src + 18, EXT_ADDR_LEN);
    ind[8] = reasonSrc[1];

    tl_zbPrimitivePost(TL_Q_MAC2NWK, MAC_MLME_DISASSOCIATE_IND, arg);
}
