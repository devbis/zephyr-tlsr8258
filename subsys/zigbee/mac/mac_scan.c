/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Adapted from libzigbee/src/mac_scan.c. Vendor file kept structurally
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

typedef struct {
    ev_timer_event_t *timerEvt;
    zb_mac_mlme_scan_req_t *scanReq;
    u32 scanChannels;
    u8 scanType;
    u8 curChannel;
    u8 savedChannel;
    u8 scanStep;
    u8 orphanScanGotRealignment;
    u8 resultCount;
    u8 savedTrxState;
} mac_scan_param_t;

/* Keep this global: vendor objects export g_macScanParam as a real data symbol. */
mac_scan_param_t g_macScanParam;
#if defined(ZB_ROUTER_ROLE)
static u32 chan_8352 __asm__("chan.9222") = TL_ZB_MAC_CHANNEL_START;
static u32 edChan_8354 __asm__("edChan.9224") = 0xffU;
#else
static u32 chan_8352 __asm__("chan.8352") = TL_ZB_MAC_CHANNEL_START;
static u32 edChan_8354 __asm__("edChan.8354") = 0xffU;
#endif


static inline u32 mac_scan_timeout_ms(u8 value)
{
    return ((((u32)value << 4) - value) << 10) / 1000U;
}

extern volatile u32 zb_nwk_ed_trace[];

u8 tl_zbMacMlmeBeaconRequestCmdSend(void)
{
    tl_zb_mac_mhr_t mhr;
    zb_buf_t *buf = (zb_buf_t *)g_zbMacCtx.txRawDataBuf;
    u8 *psdu;
    u8 *payload;
    u8 status = MAC_SUCCESS;

    /* [8]: low 16 = BeaconReq TX attempts; bit 16 = txBuf busy abort. */
    zb_nwk_ed_trace[8] = (zb_nwk_ed_trace[8] & 0xffff0000U) |
			  ((zb_nwk_ed_trace[8] + 1U) & 0xffffU);

    if (buf == NULL) {
        return status;
    }

    if ((((u8 *)buf)[OFFSETOF(zb_buf_t, hdr) + 3] & 0x08U) != 0U) {
        zb_nwk_ed_trace[8] |= 1U << 16;
        return status;
    }

    ((u8 *)buf)[OFFSETOF(zb_buf_t, hdr) + 3] |= 0x08U;

    memset(&mhr, 0, sizeof(mhr));
    mhr.dstPanId = MAC_PAN_ID_BROADCAST;
    mhr.srcPanId = MAC_PAN_ID_BROADCAST;
    mhr.dstAddr.shortAddr = MAC_SHORT_ADDR_BROADCAST;
    mhr.frameCtrl = 0x0803U;

    ((u8 *)buf)[OFFSETOF(zb_buf_t, hdr) + 1] = MAC_STA_SECURITY_ERROR;

    psdu = tl_bufInitalloc(buf, 8);
    payload = tl_zbMacHdrBuilder(psdu, &mhr);
    payload[0] = MAC_CMD_BEACON_REQUEST;

    status = tl_zbMacTx(buf, psdu, 8, 0, NULL);

    return status;
}

int tl_zbMacScanRunning(void *arg)
{
    (void)arg;

    zb_buf_t *buf = (zb_buf_t *)g_macScanParam.scanReq;
    u8 scanType = g_macScanParam.scanType;

    if (chan_8352 <= TL_ZB_MAC_CHANNEL_STOP && g_macScanParam.orphanScanGotRealignment == 0U) {
        if (scanType == ED_SCAN && edChan_8354 != 0xffU) {
            u8 idx = g_macScanParam.resultCount;
            u8 rssi = rf_stopEDScan();

            buf->buf[8 + idx] = rssi;
            g_macScanParam.resultCount = (u8)(idx + 1U);

            if (chan_8352 > TL_ZB_MAC_CHANNEL_STOP) {
                goto scan_done;
            }
        }

        if (scanType != ED_SCAN || edChan_8354 == 0xffU) {
            u32 mask = g_macScanParam.scanChannels;

            while (chan_8352 <= 26U && (mask & (1UL << chan_8352)) == 0U) {
                chan_8352++;
            }

            if (chan_8352 > 26U) {
                goto scan_done;
            }
        }

        tl_zbMacChannelSet((u8)chan_8352);
        g_macScanParam.curChannel = (u8)chan_8352;

        if (scanType == ED_SCAN) {
            rf_startEDScan();
            edChan_8354 = chan_8352;
        } else if (scanType == ORPHAN_SCAN) {
            tl_zb_mac_mhr_t mhr;
            zb_buf_t *txBuf = (zb_buf_t *)g_zbMacCtx.txRawDataBuf;

            if ((((u8 *)txBuf)[OFFSETOF(zb_buf_t, hdr) + 3] & 0x08U) == 0U) {
                u8 *payload;
                u8 hdrSize;

                ((u8 *)txBuf)[OFFSETOF(zb_buf_t, hdr) + 3] |= 0x08U;
                memset(&mhr, 0xff, sizeof(mhr));
                memcpy(&mhr.srcAddr, g_zbInfo.macPib.extAddress, EXT_ADDR_LEN);
                mhr.frameCtrl = 0xc803U;

                hdrSize = (u8)(tl_zbMacHdrSize(0xc803U) + 1U);
                ((u8 *)txBuf)[OFFSETOF(zb_buf_t, hdr) + 1] = MAC_STA_NO_BEACON;
                payload = tl_bufInitalloc(txBuf, hdrSize);
                payload = tl_zbMacHdrBuilder(payload, &mhr);
                payload[0] = MAC_CMD_ORPHAN_NOTIFICATION;
                tl_zbMacTx(txBuf, payload, hdrSize, 0, NULL);
            }
        } else if (scanType == ACTIVE_SCAN) {
            tl_zbMacMlmeBeaconRequestCmdSend();
        }

        g_macScanParam.scanChannels &= ~(1UL << chan_8352);

        return 0;
    }

scan_done:
    /* [16]: low 16 = scan_done call count; bits 16..23 = MAC status at entry.
     * If this stays zero after a steering attempt, the scan timer never fired.
     * If this is non-zero but slot[2] is still zero, the MAC_MLME_SCAN_CNF
     * was either dropped by a full TL_Q_MAC2NWK queue or never consumed.
     */
    zb_nwk_ed_trace[16] = (zb_nwk_ed_trace[16] & 0xff000000U) |
                           (((u32)g_zbMacCtx.status & 0xffU) << 16) |
                           ((zb_nwk_ed_trace[16] + 1U) & 0xffffU);
    chan_8352 = TL_ZB_MAC_CHANNEL_START;
    edChan_8354 = 0xffU;

    buf->buf[1] = scanType;
    buf->buf[0] = MAC_STA_NO_BEACON;
    buf->buf[3] = g_macScanParam.resultCount;

    if (scanType == ED_SCAN) {
        buf->buf[0] = MAC_SUCCESS;
    } else if (g_macScanParam.resultCount != 0U || g_macScanParam.orphanScanGotRealignment != 0U) {
        buf->buf[0] = MAC_SUCCESS;
    }

    tl_zbPrimitivePost(TL_Q_MAC2NWK, MAC_MLME_SCAN_CNF, buf);

    g_zbMacCtx.status = ZB_MAC_STATE_NORMAL;
    tl_zbMacChannelSet(g_macScanParam.savedChannel);
    rf_setTrxState(g_macScanParam.savedTrxState);
    g_macScanParam.timerEvt = NULL;

    return -1;
}

void tl_zbMacActiveScanListAdd(void)
{
    g_macScanParam.resultCount++;
}

void tl_zbMacOrphanScanStatusUpdate(void)
{
    g_macScanParam.orphanScanGotRealignment = 1;
}

void tl_zbMacScanRequestHandler(zb_mac_mlme_scan_req_t *req)
{
    u8 status = MAC_STA_INVALID_PARAMETER;
    u8 scanType = req->scanType;

    if (req->scanDuration <= 7U && g_zbMacCtx.status == ZB_MAC_STATE_NORMAL) {
        g_macScanParam.scanType = scanType;
        g_macScanParam.scanChannels = req->scanChannels;
        g_macScanParam.scanStep = (u8)((1U << req->scanDuration) + 1U);
        g_macScanParam.scanReq = req;
        g_macScanParam.resultCount = 0;
        g_macScanParam.orphanScanGotRealignment = 0;
        g_macScanParam.savedChannel = rf_getChannel();
        g_macScanParam.savedTrxState = rf_TrxStateGet();

        if (scanType == ED_SCAN) {
            g_zbMacCtx.status = ZB_MAC_STATE_ED_SCAN;
        } else if (scanType == ACTIVE_SCAN) {
            g_zbMacCtx.status = ZB_MAC_STATE_ACTIVE_SCAN;
        } else if (scanType == PASSIVE_SCAN) {
            g_zbMacCtx.status = ZB_MAC_STATE_PASSIVE_SCAN;
        } else if (scanType == ORPHAN_SCAN) {
            g_zbMacCtx.status = ZB_MAC_STATE_ORPHAN_SCAN;
        } else {
            goto post_invalid;
        }

        tl_zbMacScanRunning(NULL);

        if (g_macScanParam.timerEvt != NULL) {
            ev_timer_taskCancel(&g_macScanParam.timerEvt);
        }

        {
            u8 timeoutBase = (scanType == ORPHAN_SCAN) ? g_zbInfo.macPib.respWaitTime
                                                       : g_macScanParam.scanStep;
            u32 timeout = mac_scan_timeout_ms(timeoutBase);

            g_macScanParam.timerEvt = ev_timer_taskPost(tl_zbMacScanRunning, NULL, timeout);
        }

        /* [18]: bits 7..0 = timer-post call count; bits 15..8 = scanStep at
         * the time of the post (allows timeout verification during decode);
         * bit 16 = last ev_timer_taskPost returned NULL (timer pool full,
         * scan will never complete).
         */
        zb_nwk_ed_trace[18] = (zb_nwk_ed_trace[18] & 0xff000000U) |
                               (g_macScanParam.timerEvt == NULL ? (1U << 16) : 0U) |
                               (((u32)g_macScanParam.scanStep & 0xffU) << 8) |
                               ((zb_nwk_ed_trace[18] + 1U) & 0xffU);

        return;
    }

post_invalid:
    /* [17]: low 16 = scan-rejection count; bits 16..23 = g_zbMacCtx.status
     * at the time of rejection.  Rejection occurs when any of the following
     * conditions are true at scan-request time: scanDuration > 7 (invalid
     * parameter), g_zbMacCtx.status != ZB_MAC_STATE_NORMAL (MAC already
     * busy with another scan or operation), or an unrecognised scanType
     * was specified (falls through the scanType dispatch to post_invalid).
     */
    zb_nwk_ed_trace[17] = (zb_nwk_ed_trace[17] & 0xff000000U) |
                           (((u32)g_zbMacCtx.status & 0xffU) << 16) |
                           ((zb_nwk_ed_trace[17] + 1U) & 0xffffU);
    req->scanType = scanType;
    req->scanChannels = req->scanChannels;
    ((u8 *)req)[0] = status;
    ((u8 *)req)[1] = scanType;
    tl_zbPrimitivePost(TL_Q_MAC2NWK, MAC_MLME_SCAN_CNF, req);
}
