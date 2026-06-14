/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Adapted from libzigbee/src/mac_trx.c. Vendor file kept structurally
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
#include <stdint.h>

static ev_timer_event_t *macPendingWaitTimerEvt;
tx_data_queue *g_pTxQueue = NULL;
mac_timer_evt_t g_macTimerEvt;
static u8 tx_fifo_rptr = 0;
static u8 tx_fifo_wptr = 0;

typedef struct _attribute_packed_ {
    void *curTx;
    u8 state;
    u8 reserved5;
    u8 csmaBackoffCnt;
    u8 backoffExponent;
    u8 frameRetryCnt;
    u8 ackRequired;
    u8 skipFreeTxBuf;
    u8 ackSeqNum;
} mac_trx_vars_t;

static mac_trx_vars_t mac_trx_vars;

#if 0 /* vendor-pinned 32-bit offsets disabled in Zephyr port */
STATIC_ASSERT(OFFSETOF(mac_trx_vars_t, curTx) == 0);
STATIC_ASSERT(OFFSETOF(mac_trx_vars_t, state) == 4);
STATIC_ASSERT(OFFSETOF(mac_trx_vars_t, reserved5) == 5);
STATIC_ASSERT(OFFSETOF(mac_trx_vars_t, csmaBackoffCnt) == 6);
STATIC_ASSERT(OFFSETOF(mac_trx_vars_t, backoffExponent) == 7);
STATIC_ASSERT(OFFSETOF(mac_trx_vars_t, frameRetryCnt) == 8);
STATIC_ASSERT(OFFSETOF(mac_trx_vars_t, ackRequired) == 9);
STATIC_ASSERT(OFFSETOF(mac_trx_vars_t, skipFreeTxBuf) == 10);
STATIC_ASSERT(OFFSETOF(mac_trx_vars_t, ackSeqNum) == 11);
STATIC_ASSERT(sizeof(mac_trx_vars_t) == 12);
#endif

int mac_pendingWaitTimerCb(void *arg);

static inline void timer_evt_cb_set(ev_timer_callback_t cb)
{
    g_macTimerEvt.cb = cb;
}

static inline ev_timer_callback_t timer_evt_cb_get(void)
{
    return g_macTimerEvt.cb;
}

static inline void timer_evt_deadline_set(u32 tick)
{
    g_macTimerEvt.deadline = tick;
}

static inline u32 timer_evt_deadline_get(void)
{
    return g_macTimerEvt.deadline;
}

static inline u8 timer_evt_state_get(void)
{
    return g_macTimerEvt.state;
}

static inline void timer_evt_state_set(u8 state)
{
    g_macTimerEvt.state = state;
}

static inline void *mac_trx_cur_get(void)
{
    return mac_trx_vars.curTx;
}

static inline void mac_trx_cur_set(void *cur)
{
    mac_trx_vars.curTx = cur;
}

static inline u8 txq_flags_get(const tx_data_queue *entry)
{
    return ((const u8 *)entry)[0];
}

static inline void txq_flags_set(tx_data_queue *entry, u8 value)
{
    ((u8 *)entry)[0] = value;
}

int mac_waitTxIrqCb(void *arg)
{
    (void)arg;

    if (timer_evt_state_get() != 0U) {
        rf_busyFlag &= (u8)~TX_BUSY;
        rf_setTrxState(RF_STATE_RX);
        tl_zbTaskPost(mac_trxTask, (void *)MAC_TX_EV_SEND_FAIL);
    }

    return -1;
}

int mac_ackWaitingTimerCb(void *arg)
{
    (void)arg;

    if (timer_evt_state_get() != 0U) {
        tl_zbTaskPost(mac_trxTask, (void *)MAC_TX_EV_ACK_RETRY);
    }

    return -1;
}

void mac_rxDataParse(void *arg)
{
    zb_buf_t *buf = (zb_buf_t *)arg;
    zb_mac_rx_pending_meta_t *pending = (zb_mac_rx_pending_meta_t *)arg;
    zb_mac_rx_meta_t *meta = (zb_mac_rx_meta_t *)arg;
    u8 *raw = pending->payload;
    u32 timestamp = pending->timestamp;
    s8 rssi = pending->rssi;
    u8 len = pending->payloadLen;
    u8 frameType;
    u8 hdrSize;
    tl_zb_mac_mhr_t mhr;

    raw[194] = (u8)rssi;
    frameType = raw[0] & 0x07U;
    hdrSize = tl_zbMacHdrParse(&mhr, raw);

    printk("zb dbg rxParse: frameType=%u len=%u hdrSize=%u mac_status=%u raw=%02x%02x %02x %02x%02x\n",
           frameType, len, hdrSize, g_zbMacCtx.status,
           raw[0], raw[1], raw[2], raw[3], raw[4]);
    if (len <= hdrSize || g_zbMacCtx.status == 1U) {
        printk("zb dbg rxParse: drop (len<=hdr or ED_SCAN)\n");
        zb_buf_free(buf);
        return;
    }

    /*
     * Vendor libzigbee drops beacons here during ACTIVE_SCAN because
     * a separate IRQ-context path captures them earlier on real
     * hardware (Telink RF). On the Zephyr socket-medium and on
     * TLSR8258 alike, the only beacon RX path is the queued one, so
     * we MUST forward the beacon to tl_zbPhyIndication →
     * phy_ind_beacon_notify_post so the NWK layer can populate the
     * addition_neighbor_table during discovery. Without this, the
     * scan completes with NO_BEACON and BDB cycles indefinitely.
     */
    if (g_zbMacCtx.status == 2U && frameType == 0U) {
        /* fall through to tl_zbPhyIndication */
    }

    if (g_zbMacCtx.status == 3U && frameType == 3U && raw[hdrSize] == 8U) {
        zb_buf_free(buf);
        return;
    }

    meta->timestamp = timestamp;
    meta->payload = raw;
    meta->dstAddrMode = mhr.dstAddrMode;
    meta->srcAddrMode = mhr.srcAddrMode;
    meta->frameType = frameType;
    meta->payloadLen = len;
    meta->linkQuality = rf_getLqi(rssi);
    meta->curChannel = rf_getChannel();

    if (mhr.srcAddrMode == ADDR_MODE_EXT) {
        memcpy(meta->srcAddr, mhr.srcAddr.extAddr, EXT_ADDR_LEN);
    } else {
        meta->srcAddr[0] = (u8)mhr.srcAddr.shortAddr;
        meta->srcAddr[1] = (u8)(mhr.srcAddr.shortAddr >> 8);
    }

    tl_zbPhyIndication(buf, (u8 *)&mhr, hdrSize);
}

void mac_csmaStart(void *arg)
{
    u32 r;

    r = drv_disable_irq();
    if (rf_performCCA() == 4U) {
        if ((rf_busyFlag & TX_ACKPACKET) != 0U) {
            rf_busyFlag &= (u8)~TX_ACKPACKET;
        }

        mac_trx_vars.state = MAC_TX_UNDERWAY;
        rf_busyFlag |= TX_BUSY;
        rf802154_tx();
        drv_restore_irq(r);

        r = drv_disable_irq();
        if (timer_evt_state_get() != 0U) {
            ZB_EXCEPTION_POST(SYS_EXCEPTTION_ZB_MAC_TRX_TASK);
            drv_restore_irq(r);
            return;
        }

        timer_evt_cb_set(mac_waitTxIrqCb);
        timer_evt_deadline_set(mac_currentTickGet() + sysTimerPerUs * 10000U);
        timer_evt_state_set(1);
        drv_restore_irq(r);
        return;
    }

    if (g_zbInfo.macPib.maxCsmaBackoffs == 0U) {
        if ((rf_busyFlag & TX_ACKPACKET) != 0U) {
            rf_busyFlag &= (u8)~TX_ACKPACKET;
        }

        mac_trx_vars.state = MAC_TX_UNDERWAY;
        rf_busyFlag |= TX_BUSY;
        rf802154_tx();
        drv_restore_irq(r);

        r = drv_disable_irq();
        if (timer_evt_state_get() != 0U) {
            ZB_EXCEPTION_POST(SYS_EXCEPTTION_ZB_MAC_TRX_TASK);
            drv_restore_irq(r);
            return;
        }

        timer_evt_cb_set(mac_waitTxIrqCb);
        timer_evt_deadline_set(mac_currentTickGet() + sysTimerPerUs * 10000U);
        timer_evt_state_set(1);
        drv_restore_irq(r);
        return;
    }

    drv_restore_irq(r);
    tl_zbTaskPost(mac_trxTask, (void *)MAC_TX_EV_CSMA_BUSY);
}

void zb_macTimerEventProc(void *arg)
{
    (void)arg;

    if (timer_evt_state_get() == 0U) {
        return;
    }

    if ((s32)(timer_evt_deadline_get() - mac_currentTickGet()) > 0) {
        return;
    }

    {
        u32 r = drv_disable_irq();
        ev_timer_callback_t cb = timer_evt_cb_get();

        if (cb != NULL) {
            (void)cb(NULL);
        }
        timer_evt_state_set(0);
        drv_restore_irq(r);
    }
}

u8 mac_data_pending(void)
{
    u8 pending = (u8)(tx_fifo_wptr - tx_fifo_rptr);

    return pending ? (u8)(pending - 1U) : 0U;
}

tx_data_queue *get_next_data(void)
{
    if (tx_fifo_rptr == tx_fifo_wptr) {
        return NULL;
    }

    return &g_pTxQueue[tx_fifo_rptr & (MAC_TX_QUEUE_SIZE - 1U)];
}

void free_tx_buff(zb_buf_t *buf)
{
    (void)buf;

    {
        u32 r = drv_disable_irq();

        tx_fifo_rptr++;
        drv_restore_irq(r);
    }
}

void mac_resetTx_info(void)
{
    if (mac_trx_vars.skipFreeTxBuf == 0U) {
        free_tx_buff(NULL);
    }

    timer_evt_state_set(0);
    memset(&mac_trx_vars, 0, sizeof(mac_trx_vars));
}

void mac_sendTxCnf(tx_data_queue *entry)
{
    zb_buf_t *txBuf;
    u8 handle;
    u8 status;
    u8 needPendingWait = 0;

    mac_resetTx_info();

    txBuf = (zb_buf_t *)entry->buf;
    handle = txBuf->hdr.handle;
    status = entry->cnfStatus;

    if (status == MAC_SUCCESS && (txq_flags_get(entry) & 0x10U) != 0U) {
        status = MAC_STA_FRAME_PENDING;
        if ((u8)(handle + 24U) <= 1U) {
            needPendingWait = 1;
        }
    }

    if (g_zbInfo.macPib.rxOnWhenIdle == 0U &&
        (g_zbMacCtx.status | needPendingWait) == 0U) {
        rf_setTrxState(RF_STATE_OFF);
    }

#if defined(ZB_ROUTER_ROLE)
    if (entry->pendingList != NULL) {
        macDataPendingListManage(entry->pendingList, status);
    } else
#endif
    {
        tl_zbMaxTxConfirmCb(txBuf, status);
    }

    if (needPendingWait != 0U) {
        u32 r;

        g_zbMacCtx.indirectData = 1;

        r = drv_disable_irq();
        if (timer_evt_state_get() != 0U) {
            ZB_EXCEPTION_POST(SYS_EXCEPTTION_ZB_MAC_TRX_TASK);
            drv_restore_irq(r);
            return;
        }

        timer_evt_cb_set(mac_pendingWaitTimerCb);
        timer_evt_deadline_set(mac_currentTickGet() +
                               sysTimerPerUs * ((u32)g_zbInfo.macPib.frameTotalWaitTime << 4));
        timer_evt_state_set(3);
        drv_restore_irq(r);
        return;
    }

    mac_trigger_tx(NULL);
}

void mac_trxTask(void *arg)
{
    tx_data_queue *entry = (tx_data_queue *)mac_trx_cur_get();
    u8 event = (u8)(uintptr_t)arg;
    u8 extra = (u8)((uintptr_t)arg >> 8);
    u8 state = mac_trx_vars.state;

    if (entry == NULL) {
        return;
    }

    if (event == MAC_TX_EV_NEW_DATA) {
        mac_trx_vars.ackSeqNum = entry->seqNum;
        mac_trx_vars.ackRequired = (u8)(txq_flags_get(entry) & 0x0fU);
        mac_trx_vars.frameRetryCnt = 0;
        rf802154_tx_ready(entry->txData, entry->psduLen);
    }

    switch (state) {
    case MAC_TX_IDLE:
    case MAC_TX_RETRY:
        if (event != MAC_TX_EV_NEW_DATA) {
            return;
        }

        mac_trx_vars.csmaBackoffCnt = 0;
        mac_trx_vars.backoffExponent = g_zbInfo.macPib.minBe;
        mac_trx_vars.state = MAC_TX_CSMA;
        mac_trx_vars.reserved5 = 0;
        state = MAC_TX_CSMA;
        /* fall through */

    case MAC_TX_CSMA:
        if (event != MAC_TX_EV_NEW_DATA && event != MAC_TX_EV_CSMA_BUSY) {
            return;
        }

        if (mac_trx_vars.csmaBackoffCnt <= g_zbInfo.macPib.maxCsmaBackoffs) {
            u16 backoffUs = 200U;

            mac_trx_vars.csmaBackoffCnt++;
            rf_TrxStateGet();
            rf_setTrxState(RF_STATE_OFF);
            rf_setTrxState(RF_STATE_RX);

            if (mac_trx_vars.csmaBackoffCnt == 1U) {
                mac_csmaStart(entry);
                return;
            }

            if (mac_trx_vars.backoffExponent != 0U) {
                u32 mod = (1UL << mac_trx_vars.backoffExponent) - 1UL;
                u32 slots = drv_u32Rand() & 0xffffU;

                slots = mod ? (slots % mod) : 0U;
                backoffUs = (u16)(slots * 320U);
                if (backoffUs == 0U) {
                    backoffUs = 200U;
                }
            }

            if (drv_hwTmr_set(3, backoffUs, (timerCb_t)mac_csmaStart, entry) != 0) {
                drv_disable_irq();
                ZB_EXCEPTION_POST(SYS_EXCEPTTION_ZB_MAC_TX_TIMER);
            }

            if (mac_trx_vars.backoffExponent < g_zbInfo.macPib.maxBe) {
                mac_trx_vars.backoffExponent++;
            }
            return;
        }

        if (mac_trx_vars.frameRetryCnt < g_zbInfo.macPib.frameRetryNum) {
            mac_trx_vars.frameRetryCnt++;
            g_sysDiags.macTxUcastRetry++;
            mac_trx_vars.state = MAC_TX_RETRY;
            tl_zbTaskPost(mac_trxTask, (void *)MAC_TX_EV_NEW_DATA);
            return;
        }

        /* sys_diagnostics_t in subsys/zigbee/zb_common_stub.h lacks
         * macTxCcaFail; the vendor SDK definition has it as the
         * coordinator-side ZCL diagnostics attribute. Skip the
         * counter increment here until the field is added.
         */
        mac_trx_vars.state = MAC_TX_DONE;
        entry->cnfStatus = MAC_STA_CHANNEL_ACCESS_FAILURE;
        mac_sendTxCnf(entry);
        return;

    case MAC_TX_UNDERWAY:
        if (event == MAC_TX_EV_SEND_FAIL) {
            mac_trx_vars.state = MAC_TX_DONE;
            entry->cnfStatus = MAC_TX_ABORTED;
            g_sysDiags.macTxIrqTimeoutCnt++;
            mac_sendTxCnf(entry);
        } else if (event == MAC_TX_EV_SEND_SUCC && timer_evt_state_get() == 1U) {
            u32 r;

            timer_evt_state_set(0);
            if (mac_trx_vars.ackRequired == 0U) {
                mac_trx_vars.state = MAC_TX_DONE;
                entry->cnfStatus = MAC_SUCCESS;
                tl_zbTaskPost((tl_zb_callback_t)mac_sendTxCnf, entry);
                return;
            }

            mac_trx_vars.state = MAC_TX_WAIT_ACK;
            r = drv_disable_irq();
            if (timer_evt_state_get() != 0U) {
                ZB_EXCEPTION_POST(SYS_EXCEPTTION_ZB_MAC_TRX_TASK);
                drv_restore_irq(r);
                return;
            }

            timer_evt_cb_set(mac_ackWaitingTimerCb);
            timer_evt_deadline_set(mac_currentTickGet() + sysTimerPerUs * 2000U);
            timer_evt_state_set(2);
            drv_restore_irq(r);
        }
        return;

    case MAC_TX_WAIT_ACK:
        if (event == MAC_TX_EV_ACK_RETRY) {
            if (mac_trx_vars.frameRetryCnt < g_zbInfo.macPib.frameRetryNum) {
                mac_trx_vars.frameRetryCnt++;
                g_sysDiags.macTxUcastRetry++;
                mac_trx_vars.state = MAC_TX_RETRY;
                tl_zbTaskPost(mac_trxTask, (void *)MAC_TX_EV_NEW_DATA);
                return;
            }

            g_sysDiags.macTxUcastFail++;
            mac_trx_vars.state = MAC_TX_DONE;
            entry->cnfStatus = MAC_STA_NO_ACK;
            ((u8 *)entry->buf)[0xc2] = 0x92;
            mac_sendTxCnf(entry);
            return;
        }

        if (event == MAC_TX_EV_ACK_RECV && timer_evt_state_get() == 2U) {
            u8 flags = (u8)(txq_flags_get(entry) & 0x0fU);

            timer_evt_state_set(0);
            if (extra != 0U) {
                flags |= 0x10U;
            }
            txq_flags_set(entry, flags);

            mac_trx_vars.state = MAC_TX_DONE;
            entry->cnfStatus = MAC_SUCCESS;
            ((u8 *)entry->buf)[0xc2] = extra;
            mac_sendTxCnf(entry);
        }
        return;

    default:
        return;
    }
}
void mac_trigger_tx(void *arg)
{
    (void)arg;

    if (mac_trx_vars.state != 0U || tx_fifo_wptr == tx_fifo_rptr) {
        return;
    }

    /* Active scan queues a BeaconReq via tl_zbMacTx(); allow that one
     * scan-time transmit to run even though the MAC status is not NORMAL.
     */
    if (g_zbMacCtx.status != 0U &&
        g_zbMacCtx.status != ZB_MAC_STATE_ACTIVE_SCAN) {
        return;
    }

    {
        void *next = get_next_data();
        if (next != NULL) {
            mac_trx_cur_set(next);
            mac_trxTask(NULL);
        }
    }
}

void tl_zbSwitchOffRx(void)
{
    if (g_zbInfo.macPib.rxOnWhenIdle == 0U) {
        rf_setTrxState(RF_STATE_OFF);
    }

    timer_evt_state_set(0);
    tl_zbTaskPost(mac_trigger_tx, NULL);
}

int mac_pendingWaitTimerCb(void *arg)
{
    (void)arg;

    if (timer_evt_state_get() != 0U) {
        tl_zbSwitchOffRx();
    }

    return -1;
}

void mac_pendingWaitTimerCancel(void)
{
    if (timer_evt_state_get() == 3U) {
        timer_evt_state_set(0);
        tl_zbSwitchOffRx();
    }
}

u8 tl_zbMacTx(zb_buf_t *txBuf, u8 *txData, u8 psduLen, u8 ack, void *pendingList)
{
    u8 status = MAC_STA_NO_RESOURCES;
    u32 r;
    u8 depth;
    tx_data_queue *entry;
    u8 ackReq = ack ? 1U : 0U;

    if ((s8)psduLen < 0) {
        mac_trigger_tx(NULL);
        return MAC_STA_FRAME_TOO_LONG;
    }

    r = drv_disable_irq();
    depth = (u8)(tx_fifo_wptr - tx_fifo_rptr);
    if (depth < MAC_TX_QUEUE_SIZE) {
        entry = &g_pTxQueue[tx_fifo_wptr & (MAC_TX_QUEUE_SIZE - 1U)];
        tx_fifo_wptr++;

        entry->buf = (u8 *)txBuf;
        txq_flags_set(entry, (u8)((txq_flags_get(entry) & 0xf0U) | ackReq));
        entry->psduLen = psduLen;
        entry->txData = txData;
        entry->seqNum = txBuf->buf[2];
        entry->pendingList = pendingList;
        status = MAC_SUCCESS;
    }
    drv_restore_irq(r);

    mac_trigger_tx(NULL);
    return status;
}

void mac_trxInit(void)
{
    tx_fifo_rptr = 0;
    tx_fifo_wptr = 0;
    memset(&mac_trx_vars, 0, sizeof(mac_trx_vars));
    g_pTxQueue = g_txQueue;
    rf_init();
}

u8 mac_getTrxState(void)
{
    return mac_trx_vars.state;
}

bool tl_zbMacStateBusy(void)
{
    if ((u8)(g_zbMacCtx.status - ZB_MAC_STATE_ACTIVE_SCAN) <= 1U) {
        return 1;
    }

    if (timer_evt_state_get() != 0U || mac_trx_vars.state != 0U) {
        return 1;
    }

    if (tx_fifo_wptr > tx_fifo_rptr) {
        return (u8)(tx_fifo_wptr - tx_fifo_rptr - 1U);
    }

    return 0;
}

_attribute_ram_code_ u8 *zb_macDataFilter(u8 *macPld, u8 len, u8 *needDrop, u8 *ackPkt)
{
    (void)len;
    if (needDrop != NULL) {
        *needDrop = 0;
    }
    if (ackPkt != NULL) {
        *ackPkt = 0;
    }
    return macPld;
}

void zb_macDataRecvHandler(u8 *rxBuf, u8 *data, u8 len, u8 ackPkt, u32 timestamp, s8 rssi)
{
    zb_buf_t *buf = (zb_buf_t *)tl_phyRxBufTozbBuf(rxBuf);
    zb_mac_rx_pending_meta_t *meta;

    printk("zb dbg macRecv: ackPkt=%u len=%u data=%p buf=%p first=%02x\n",
           ackPkt, len, data, buf, data ? data[0] : 0);
    if (ackPkt != 0U) {
        u8 frameCtrl = data[0];
        u8 seqNum = data[2];

        if (mac_trx_vars.state == MAC_TX_WAIT_ACK && mac_trx_vars.ackSeqNum == seqNum) {
            u32 event = MAC_TX_EV_ACK_RECV |
                        ((u32)(u8)rssi << 8) |
                        ((u32)(frameCtrl & MAC_FCF_FRAME_PENDING_MASK) << 16) |
                        ((u32)seqNum << 24);

            mac_trxTask((void *)(uintptr_t)event);
        } else {
            zb_buf_free(buf);
        }
        return;
    }

    meta = (zb_mac_rx_pending_meta_t *)buf;
    meta->payload = data;
    meta->payloadLen = (u8)(len - 2U);
    meta->timestamp = timestamp;
    meta->rssi = rssi;

    rf_busyFlag &= (u8)~RX_BUSY;
    if (tl_zbUserTaskQNum() >= (u8)(ZB_TASKQ_USERUSE_SIZE - 5U)) {
        zb_buf_free(buf);
        return;
    }

    tl_zbTaskPost(mac_rxDataParse, buf);
}

void zb_macDataSendHandler(void)
{
    mac_trxTask((void *)MAC_TX_EV_SEND_SUCC);
}
