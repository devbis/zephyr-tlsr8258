/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Adapted from libzigbee/src/mac_data.c. Vendor file kept structurally
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
    BUF_SAVED_HANDLE_OFFSET = 0xc1,
    BUF_SAVED_LQI_OFFSET = 0xc2,
};

/* Vendor build pinned these offsets to the wire-packed layout. The
 * Zephyr build leaves the shared types naturally aligned so the
 * assertions do not hold; see the matching note in nwk_data.c.
 */
#if 0
STATIC_ASSERT(OFFSETOF(zb_mscp_data_req_t, dstPanId) == 0);
STATIC_ASSERT(OFFSETOF(zb_mscp_data_req_t, srcAddr) == 2);
STATIC_ASSERT(OFFSETOF(zb_mscp_data_req_t, dstAddr) == 11);
STATIC_ASSERT(OFFSETOF(zb_mscp_data_req_t, msduLength) == 20);
STATIC_ASSERT(OFFSETOF(zb_mscp_data_req_t, msduHandle) == 21);
STATIC_ASSERT(OFFSETOF(zb_mscp_data_req_t, msdu) == 22);
STATIC_ASSERT(OFFSETOF(zb_mscp_data_req_t, txOptions) == 26);

STATIC_ASSERT(OFFSETOF(zb_mscp_data_conf_t, msdu) == 4);
STATIC_ASSERT(OFFSETOF(zb_mscp_data_conf_t, msduHandle) == 8);
STATIC_ASSERT(OFFSETOF(zb_mscp_data_conf_t, status) == 9);
STATIC_ASSERT(OFFSETOF(zb_mscp_data_conf_t, macDstAddr) == 10);
STATIC_ASSERT(OFFSETOF(zb_mscp_data_conf_t, rssi) == 12);
STATIC_ASSERT(OFFSETOF(zb_mscp_data_conf_t, lqi) == 13);

STATIC_ASSERT(OFFSETOF(zb_mscp_data_ind_t, msdu) == 4);
STATIC_ASSERT(OFFSETOF(zb_mscp_data_ind_t, srcPanId) == 8);
STATIC_ASSERT(OFFSETOF(zb_mscp_data_ind_t, dstPanId) == 10);
STATIC_ASSERT(OFFSETOF(zb_mscp_data_ind_t, srcAddr) == 12);
STATIC_ASSERT(OFFSETOF(zb_mscp_data_ind_t, dstAddr) == 21);
STATIC_ASSERT(OFFSETOF(zb_mscp_data_ind_t, msduLength) == 30);
STATIC_ASSERT(OFFSETOF(zb_mscp_data_ind_t, mpduLinkQuality) == 31);
STATIC_ASSERT(OFFSETOF(zb_mscp_data_ind_t, dsn) == 32);
#endif

static inline zb_mscp_data_req_t *mac_data_req(zb_buf_t *buf)
{
    return (zb_mscp_data_req_t *)buf;
}

static inline zb_mac_rx_meta_t *mac_data_meta(zb_buf_t *buf)
{
    return (zb_mac_rx_meta_t *)buf;
}

void tl_zbMacMcpsDataRequestSendConfirm(zb_buf_t *buf, u8 status)
{
    u16 macDstAddr = MAC_ADDR_USE_EXT;
    u8 handle = ((u8 *)buf)[21];

    if (((u8 *)buf)[19] == ADDR_MODE_SHORT) {
        macDstAddr = (u16)((u8 *)buf)[11] | ((u16)((u8 *)buf)[12] << 8);
    }

    buf->hdr.macTxFifo = 0;
    ((u8 *)buf)[8] = handle;
    ((u8 *)buf)[9] = status;

    ((u8 *)buf)[10] = (u8)macDstAddr;
    ((u8 *)buf)[11] = (u8)(macDstAddr >> 8);
    ((zb_mscp_data_conf_t *)buf)->msdu = ((zb_mscp_data_req_t *)buf)->msdu;
    ((u8 *)buf)[12] = ((u8 *)buf)[BUF_SAVED_LQI_OFFSET];
    ((u8 *)buf)[13] = rf_getLqi((s8)((u8 *)buf)[12]);

    tl_zbPrimitivePost(TL_Q_MAC2NWK, MAC_MCPS_DATA_CNF, buf);
}

void tl_zbMacMcpsDataRequestProc(void *arg)
{
    zb_buf_t *buf = (zb_buf_t *)arg;
    zb_mscp_data_req_t *req;
    tl_zb_mac_mhr_t mhr;
    u8 hdrSize;
    u8 psduLen;
    u8 *txData;
    u8 txStatus;

    /* Caller guarantees buf is valid; the vendor path omits a local null guard. */
    req = mac_data_req(buf);

    if (req->dstAddr.addrMode == ADDR_MODE_SHORT) {
        if (req->dstAddr.addr.shortAddr == MAC_SHORT_ADDR_BROADCAST) {
            g_sysDiags.macTxBcast++;
        } else {
            g_sysDiags.macTxUcast++;
        }
    }

    memset(&mhr, 0, sizeof(mhr));
    mhr.dstPanId = req->dstPanId;
    mhr.srcPanId = g_zbMacPib.panId;
    memcpy(&mhr.dstAddr, &req->dstAddr.addr, sizeof(mhr.dstAddr));
    memcpy(&mhr.srcAddr, &req->srcAddr.addr, sizeof(mhr.srcAddr));

    mhr.frameCtrl = MAC_FRAME_TYPE_DATA |
                    (((u16)req->txOptions & 0x01U) << 5) |
                    ((u16)req->dstAddr.addrMode << MAC_FCF_DST_ADDR_MODE_POS) |
                    ((u16)req->srcAddr.addrMode << MAC_FCF_SRC_ADDR_MODE_POS);

    if ((req->srcAddr.addrMode != ADDR_MODE_NONE) &&
        (req->dstAddr.addrMode != ADDR_MODE_NONE) &&
        (req->dstPanId == g_zbMacPib.panId)) {
        mhr.frameCtrl |= MAC_FCF_INTRA_PAN_MASK;
    }

    hdrSize = tl_zbMacHdrSize(mhr.frameCtrl);
    ((u8 *)buf)[BUF_SAVED_HANDLE_OFFSET] = req->msduHandle;
    psduLen = (u8)(hdrSize + req->msduLength);
    txData = req->msdu - hdrSize;
    txData = tl_zbMacHdrBuilder(txData, &mhr);

    buf->hdr.macTxFifo = 1;
#if defined(ZB_ROUTER_ROLE)
    if ((req->txOptions & MAC_TX_OPTION_INDIRECT_TRANSMISSION_BIT) != 0U) {
        u8 pendingInfo[9];

        memcpy(buf, &txData, sizeof(txData));
        ((u8 *)buf)[4] = psduLen;
        pendingInfo[8] = req->dstAddr.addrMode;
        memcpy(pendingInfo, &req->dstAddr.addr, 8);

        txStatus = macDataPending(buf,
                                  *(u32 *)(void *)pendingInfo,
                                  *(u32 *)(void *)(pendingInfo + 4),
                                  pendingInfo[8]);
    } else
#endif
    {
        txStatus = tl_zbMacTx(buf, txData, psduLen, (mhr.frameCtrl & MAC_FCF_ACK_REQ_BIT) ? 1U : 0U, NULL);
    }

    if (txStatus != MAC_SUCCESS) {
        tl_zbMacMcpsDataRequestSendConfirm(buf, txStatus);
    }
}

void tl_zbPhyMldeIndication(zb_buf_t *buf, u8 *raw, u8 len)
{
    /*
     * Vendor libzigbee wrote into buf via 32-bit-pinned byte offsets
     * (`((u8 *)buf)[8..32]`) that assumed `u8 *msdu` is 4 bytes wide
     * and that addr_t is packed to 9 bytes. On native_sim/native/64
     * the pointer is 8 bytes and addr_t aligns to 10, so those byte
     * writes clobber the upper half of `ind->msdu` and land the
     * src/dst addr fields at the wrong offsets. Use the struct
     * fields directly so the layout follows whatever sizeof()
     * decides on each target.
     */
    zb_mac_rx_meta_t *meta = mac_data_meta(buf);
    tl_zb_mac_mhr_t *mhr = (tl_zb_mac_mhr_t *)raw;
    u8 *msdu;
    u8 msduLen = (u8)(meta->payloadLen - len);
    u8 linkQuality = meta->linkQuality;
    bool frame_pending_set =
        (g_zbMacPib.rxOnWhenIdle == 0U) && ((raw[0] & MAC_FCF_FRAME_PENDING_MASK) != 0U);
    zb_mscp_data_ind_t *ind;

    /* meta and ind alias the same buf; preserve meta->payload (== msdu)
     * before we start clobbering by writing ind fields. */
    memcpy(&msdu, &meta->payload, sizeof(msdu));

    ind = (zb_mscp_data_ind_t *)buf;
    memset(ind, 0, sizeof(*ind));
    ind->msdu = msdu;
    ind->msduLength = msduLen;
    ind->mpduLinkQuality = linkQuality;
    ind->dsn = mhr->seqNum;

    ind->dstPanId = mhr->dstPanId;
    ind->dstAddr.addrMode = mhr->dstAddrMode;
    memcpy(&ind->dstAddr.addr, &mhr->dstAddr, sizeof(mhr->dstAddr));

    if (mhr->panIdMode != 0U) {
        ind->srcPanId = mhr->srcPanId;
    }
    ind->srcAddr.addrMode = mhr->srcAddrMode;
    memcpy(&ind->srcAddr.addr, &mhr->srcAddr, sizeof(mhr->srcAddr));

    if (frame_pending_set) {
        buf->hdr.pending = 1;
    }

    tl_zbPrimitivePost(TL_Q_MAC2NWK, MAC_MCPS_DATA_IND, buf);
}
