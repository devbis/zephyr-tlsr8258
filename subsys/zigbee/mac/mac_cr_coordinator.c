/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Adapted from libzigbee/src/mac_cr_coordinator.c. Vendor file kept structurally
 * one-for-one; vendor zb_local.h / mac_trx_api.h / ev_timer.h / mac_phy.h
 * are replaced by the Zephyr include set.
 */
#include <zephyr/sys/printk.h>

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
typedef struct _attribute_packed_ {
    /* Holds a pointer; vendor used u32, promoted to uintptr_t so the
     * struct works on 64-bit native_sim too. */
    uintptr_t payloadAddr;
    u8 len;
} mac_packet_delay_ctx_t;

typedef struct _attribute_packed_ {
    u8 bytes0_7[8];
    u16 shortAddr;
    u8 reserved10;
} mac_orphan_saved_t;

typedef struct _attribute_packed_ {
    u8 reserved0[2];
    u8 srcAddrBytes[8];
    u8 srcAddrMode;
    u16 dstShortAddr;
    u8 reserved13[6];
    u8 dstAddrMode;
    u8 status;
    u8 isAssoc;
} mac_orphan_comm_status_buf_t;

/* Vendor STATIC_ASSERTs pinned to -fpack-struct sizes:
 *   mac_packet_delay_ctx_t == 5, mac_orphan_saved_t == 11,
 *   mac_orphan_comm_status_buf_t == 22.
 * On 64-bit native_sim payloadAddr is 8 bytes so the assert would
 * fire; gate it like the other vendor-pinned offsets in this tree. */
#if 0
STATIC_ASSERT(sizeof(mac_packet_delay_ctx_t) == 5);
STATIC_ASSERT(sizeof(mac_orphan_saved_t) == 11);
STATIC_ASSERT(sizeof(mac_orphan_comm_status_buf_t) == 22);
#endif

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

    /*
     * Beacon FCF: frame type = BEACON (0), source addressing per srcAddrMode
     * (bits 14-15), no destination, no PAN-ID compression. The vendor value
     * 0x0043 encoded frame type 3 (MAC command) + PAN-compress — a latent bug
     * that never mattered while this port didn't actually transmit beacons; a
     * standards-compliant receiver (our own MAC RX: frameType = psdu[0] & 0x07)
     * then treats the "beacon" as a command and drops it, so a scanning joiner
     * never sees this router as a parent. Build a proper beacon FCF (0x8000 for
     * short src) so the frame is recognised.
     */
    mhr.frameCtrl = (u16)(((u16)beacon->srcAddrMode << 14) |
                          ((u16)beacon->framePending << 4));
    len = (u8)(tl_zbMacHdrSize(mhr.frameCtrl) + g_zbMacPib.beaconPayloadLen + 4U);

    if ((((u8 *)buf)[OFFSETOF(zb_buf_t, hdr) + 3] & 0x08U) != 0U) {
        return MAC_SUCCESS;
    }

    ((u8 *)buf)[OFFSETOF(zb_buf_t, hdr) + 3] |= 0x08U;

    payload = tl_bufInitalloc(buf, len);
    {
        mac_packet_delay_ctx_t ctx;

        ctx.payloadAddr = (uintptr_t)payload;
        ctx.len = len;
        memcpy(buf->buf, &ctx, sizeof(ctx));
    }
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
    mac_packet_delay_ctx_t ctx;
    u8 *payload;
    u8 len;
    u8 status;

    (void)arg;

    buf->hdr.handle = 0xe3U;
    memcpy(&ctx, buf->buf, sizeof(ctx));
    payload = (u8 *)ctx.payloadAddr;
    len = ctx.len;
    status = tl_zbMacTx(buf, payload, len, 0, NULL);

    return (status == MAC_SUCCESS) ? -1 : 0;
}

void tl_zbMacOrphanResponseHandler(void *arg)
{
    mac_orphan_saved_t saved;
    mac_orphan_saved_t *req = (mac_orphan_saved_t *)arg;
    u8 status;

    memset(&saved, 0, sizeof(saved));
    memcpy(&saved, arg, sizeof(saved));
    memcpy((u8 *)arg + 4, &g_zbInfo.macPib.panId, sizeof(g_zbInfo.macPib.panId));
    ((u8 *)arg)[6] = g_zbMacCtx.curChannel;
    ((u8 *)arg)[7] = 0;
    ((zb_buf_t *)arg)->hdr.handle = 0xe5U;

    status = tl_zbMacMlmeCoordRealignmentCmdSend(0, saved.bytes0_7, req->shortAddr, arg);
    if (status != MAC_SUCCESS) {
        tl_zbMacOrphanResponseStatusCheck(arg, MAC_STA_TRANSACTION_OVERFLOW);
    }
}

void tl_zbMacOrphanResponseStatusCheck(void *arg, u8 status)
{
    mac_orphan_saved_t saved;
    mac_orphan_comm_status_buf_t *ind = (mac_orphan_comm_status_buf_t *)arg;

    memcpy(&saved, arg, sizeof(saved));
    if (status != MAC_SUCCESS) {
        zb_buf_free((zb_buf_t *)arg);
        return;
    }

    memset(ind, 0, sizeof(*ind));
    memcpy(ind->srcAddrBytes, saved.bytes0_7, sizeof(ind->srcAddrBytes));
    ind->srcAddrMode = ADDR_MODE_EXT;
    ind->dstShortAddr = saved.shortAddr;
    ind->dstAddrMode = ADDR_MODE_SHORT;
    tl_zbPrimitivePost(TL_Q_MAC2NWK, MAC_MLME_COMM_STATUS_IND, arg);
}

void tl_zbMacMlmeBeaconSendConfirm(void *arg, u8 status)
{
    (void)arg;

    /*
     * Release the shared txRawDataBuf busy guard (hdr[3] bit 0x08) so the next
     * beacon-request can be answered. mac.c clears it via hdr.active=0, but the
     * bitfield position is arch-fragile; clear it explicitly here so repeated
     * beacons work on native_sim/64-bit too.
     */
    ((u8 *)g_zbMacCtx.txRawDataBuf)[OFFSETOF(zb_buf_t, hdr) + 3] &= (u8)~0x08U;

    if (status != MAC_SUCCESS && g_zbMacCtx.beaconTriesNum != 0U) {
        tl_zbMacBeaconRequestCb();
    }
}
#endif
