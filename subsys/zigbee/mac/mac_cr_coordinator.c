/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Adapted from libzigbee/src/mac_cr_coordinator.c. Vendor file kept structurally
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

#if defined(ZB_ROUTER_ROLE)
static inline void store_le16(u8 *dst, u16 value)
{
    dst[0] = (u8)value;
    dst[1] = (u8)(value >> 8);
}

static inline u16 load_le16(const u8 *src)
{
    return (u16)src[0] | ((u16)src[1] << 8);
}

u8 tl_zbMacMlmeBeaconCmdSend(tl_zbBeaconFrame_t *beacon)
{
    tl_zb_mac_mhr_t mhr;
    zb_buf_t *buf = (zb_buf_t *)g_zbMacCtx.txRawDataBuf;
    u8 *payload;
    u8 len;

    memset(&mhr, 0, sizeof(mhr));
    mhr.srcPanId = g_zbMacPib.panId;
    if (beacon->srcAddrMode == ADDR_MODE_EXT) {
        memcpy(&mhr.srcAddr, g_zbMacPib.extAddress, EXT_ADDR_LEN);
    } else {
        memcpy(&mhr.srcAddr, &g_zbMacPib.shortAddress, SHORT_ADDR_LEN);
    }

    mhr.frameCtrl = (u16)(0x0043U |
                          ((u16)beacon->srcAddrMode << 14) |
                          ((u16)beacon->framePending << 4));
    len = (u8)(tl_zbMacHdrSize(mhr.frameCtrl) + g_zbMacPib.beaconPayloadLen + 4U);

    if ((((u8 *)buf)[OFFSETOF(zb_buf_t, hdr) + 3] & 0x08U) != 0U) {
        return MAC_SUCCESS;
    }

    ((u8 *)buf)[OFFSETOF(zb_buf_t, hdr) + 3] |= 0x08U;

    payload = tl_bufInitalloc(buf, len);
    memcpy(buf->buf, &payload, sizeof(payload));
    buf->buf[4] = len;
    memset(payload, 0, len);

    payload = tl_zbMacHdrBuilder(payload, &mhr);
    payload[0] = (u8)((((u16)beacon->superframeOrder) << 4) |
                      (beacon->beaconOrder & 0x0fU));
    payload[1] = (u8)(0x0fU |
                      ((beacon->ble & 0x01U) << 4) |
                      (g_zbMacPib.associationPermit << 7));
    memcpy(payload + 4, &g_zbMacPib.beaconPayload, g_zbMacPib.beaconPayloadLen);

    ev_timer_taskPost(tl_zbMacPacketDelaySend, NULL, (drv_u32Rand() & 0x14U) + 1U);

    return MAC_SUCCESS;
}

u8 tl_zbMacMlmeCoordRealignmentCmdSend(u8 rxOnWhenIdle, const u8 *orphanAddr, u16 shortAddr, void *arg)
{
    u8 rawMhr[26];
    u8 cmd[9];
    u8 *psdu;
    u8 *payload;
    u8 psduLen;
    u8 ack;

    memset(rawMhr, 0, sizeof(rawMhr));
    memset(cmd, 0, sizeof(cmd));

    store_le16(rawMhr + 6, MAC_SHORT_ADDR_NONE);
    store_le16(rawMhr + 16, g_zbInfo.macPib.panId);
    store_le16(rawMhr + 8, MAC_SHORT_ADDR_NONE);
    memcpy(rawMhr + 18, g_zbInfo.macPib.extAddress, EXT_ADDR_LEN);

    if (rxOnWhenIdle == 0U) {
        memcpy(rawMhr + 8, orphanAddr, EXT_ADDR_LEN);
        store_le16(rawMhr, 0xcc23U);
        psduLen = (u8)(tl_zbMacHdrSize(0xcc23U) + 9U);
        cmd[0] = MAC_CMD_COORDINATOR_REALIGNMENT;
        memcpy(cmd + 1, (u8 *)arg + 4, 2);
        store_le16(cmd + 3, g_zbInfo.macPib.shortAddress);
        cmd[5] = ((u8 *)arg)[6];
        store_le16(cmd + 6, shortAddr);
        cmd[8] = ((u8 *)arg)[7];
        ack = 1U;
    } else {
        store_le16(rawMhr, 0xc803U);
        psduLen = (u8)(tl_zbMacHdrSize(0xc803U) + 9U);
        cmd[0] = MAC_CMD_COORDINATOR_REALIGNMENT;
        memcpy(cmd + 1, (u8 *)arg + 4, 2);
        store_le16(cmd + 3, g_zbInfo.macPib.shortAddress);
        cmd[5] = ((u8 *)arg)[6];
        store_le16(cmd + 6, MAC_SHORT_ADDR_NONE);
        cmd[8] = ((u8 *)arg)[7];
        ack = 0U;
    }

    psdu = tl_bufInitalloc((zb_buf_t *)arg, psduLen);
    payload = tl_zbMacHdrBuilder(psdu, (tl_zb_mac_mhr_t *)rawMhr);
    memcpy(payload, cmd, sizeof(cmd));

    if ((load_le16(rawMhr) & 0x3000U) == 0U) {
        psduLen--;
    }

    return tl_zbMacTx((zb_buf_t *)arg, psdu, psduLen, ack, NULL);
}

void tl_zbMacBeaconRequestCb(void)
{
    tl_zbBeaconFrame_t beacon;

    if (g_zbNIB.capabilityInfo.devType == 0U || g_zbMacPib.beaconPayloadLen == 0U) {
        return;
    }

    tl_zbNwkBeaconPayloadUpdate();

    memset(&beacon, 0, sizeof(beacon));
    beacon.srcAddrMode = ADDR_MODE_SHORT;
    beacon.ble = g_zbMacPib.battLifeExt;
    beacon.beaconOrder = g_zbMacPib.beaconOrder;
    beacon.superframeOrder = g_zbMacPib.superframeOrder;
    tl_zbMacMlmeBeaconCmdSend(&beacon);
    g_zbMacCtx.beaconTriesNum--;
}

int tl_zbMacPacketDelaySend(void *arg)
{
    zb_buf_t *buf = (zb_buf_t *)g_zbMacCtx.txRawDataBuf;
    u8 *payload;
    u8 len;
    u8 status;
    u32 payloadAddr;

    (void)arg;

    buf->hdr.handle = 0xe3U;
    payloadAddr = (u32)buf->buf[0] |
                  ((u32)buf->buf[1] << 8) |
                  ((u32)buf->buf[2] << 16) |
                  ((u32)buf->buf[3] << 24);
    payload = (u8 *)payloadAddr;
    len = buf->buf[4];
    status = tl_zbMacTx(buf, payload, len, 0, NULL);

    return (status == MAC_SUCCESS) ? -1 : 0;
}

void tl_zbMacOrphanResponseHandler(void *arg)
{
    u8 saved[11];
    u16 shortAddr;
    u8 status;

    memset(saved, 0, sizeof(saved));
    memcpy(saved, arg, sizeof(saved));
    memcpy((u8 *)arg + 4, &g_zbInfo.macPib.panId, sizeof(g_zbInfo.macPib.panId));
    ((u8 *)arg)[6] = g_zbMacCtx.curChannel;
    ((u8 *)arg)[7] = 0;
    ((zb_buf_t *)arg)->hdr.handle = 0xe5U;
    memcpy(&shortAddr, saved + 8, sizeof(shortAddr));

    status = tl_zbMacMlmeCoordRealignmentCmdSend(0, saved, shortAddr, arg);
    if (status != MAC_SUCCESS) {
        tl_zbMacOrphanResponseStatusCheck(arg, MAC_STA_TRANSACTION_OVERFLOW);
    }
}

void tl_zbMacOrphanResponseStatusCheck(void *arg, u8 status)
{
    u8 saved[11];
    u8 *buf = (u8 *)arg;

    memcpy(saved, buf, sizeof(saved));
    if (status != MAC_SUCCESS) {
        zb_buf_free((zb_buf_t *)arg);
        return;
    }

    buf[20] = 0;
    buf[21] = 0;
    memcpy(buf + 2, saved, 8);
    buf[10] = ADDR_MODE_EXT;
    memcpy(buf + 11, saved + 8, 2);
    buf[19] = ADDR_MODE_SHORT;
    tl_zbPrimitivePost(TL_Q_MAC2NWK, MAC_MLME_COMM_STATUS_IND, arg);
}

void tl_zbMacMlmeBeaconSendConfirm(void *arg, u8 status)
{
    (void)arg;

    if (status != MAC_SUCCESS && g_zbMacCtx.beaconTriesNum != 0U) {
        tl_zbMacBeaconRequestCb();
    }
}
#endif
