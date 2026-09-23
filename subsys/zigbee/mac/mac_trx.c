/*
 * Copyright 2026 libzigbee
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "zb_common.h"
#include <stdint.h>
#include "ev_timer.h"
#include "mac.h"
#include "mac_associate.h"
#include "mac_common.h"
#include "mac_data.h"
#include "mac_indirect_data.h"
#include "mac_mlme.h"
#include "mac_trx.h"

static ev_timer_event_t *macPendingWaitTimerEvt;
tx_data_queue *g_pTxQueue = NULL;
mac_timer_evt_t g_macTimerEvt;
static u8 tx_fifo_rptr = 0;
static u8 tx_fifo_wptr = 0;

static void mac_trxTask(void *arg);

typedef struct {
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

enum {
	MAC_TX_NO_ACK_RSSI = -110,
};

enum {
	MAC_TIMER_EVENT_IDLE = 0,
	MAC_TIMER_EVENT_TX_WAIT,
	MAC_TIMER_EVENT_ACK_WAIT,
	MAC_TIMER_EVENT_PENDING_WAIT,
};

int mac_pendingWaitTimerCb(void *arg);

int mac_waitTxIrqCb(void *arg)
{
	(void)arg;

	if (g_macTimerEvt.state != MAC_TIMER_EVENT_IDLE) {
		rf_busyFlag &= (u8)~TX_BUSY;
		rf_setTrxState(RF_STATE_RX);
		tl_zbTaskPost(mac_trxTask, (void *)MAC_TX_EV_SEND_FAIL);
	}

	return -1;
}

int mac_ackWaitingTimerCb(void *arg)
{
	(void)arg;

	if (g_macTimerEvt.state != MAC_TIMER_EVENT_IDLE) {
		tl_zbTaskPost(mac_trxTask, (void *)MAC_TX_EV_ACK_RETRY);
	}

	return -1;
}

void mac_rxDataParse(void *arg)
{
	zb_buf_t *buf = (zb_buf_t *)arg;
	mac_rx_pending_meta_t *pending = (mac_rx_pending_meta_t *)arg;
	mac_phy_ind_meta_t *meta = (mac_phy_ind_meta_t *)arg;
	u8 *raw = pending->raw;
	u32 timestamp = pending->timestamp;
	s8 rssi = pending->rssi;
	u8 len = pending->len;
	u8 frameType;
	u8 hdrSize;
	tl_zb_mac_mhr_t mhr;

	buf->hdr.rssi = rssi;
	frameType = raw[0] & 0x07U;
	hdrSize = tl_zbMacHdrParse(&mhr, raw);

	if (len <= hdrSize || g_zbMacCtx.status == ZB_MAC_STATE_ED_SCAN) {
		zb_buf_free(buf);
		return;
	}

	if (g_zbMacCtx.status == ZB_MAC_STATE_ACTIVE_SCAN && frameType == MAC_FRAME_TYPE_BEACON) {
		zb_buf_free(buf);
		return;
	}

	if (g_zbMacCtx.status == ZB_MAC_STATE_ORPHAN_SCAN) {
		/* During orphan scan only non-realignment command frames continue
		 * through the receive path.  The vendor drops every other frame
		 * type, as well as the coordinator realignment command itself. */
		if (frameType != MAC_FRAME_TYPE_COMMAND ||
		    raw[hdrSize] == MAC_CMD_COORDINATOR_REALIGNMENT) {
			zb_buf_free(buf);
			return;
		}
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
		ZB_IEEE_ADDR_COPY(meta->srcAddr, mhr.srcAddr.extAddr);
	} else {
		COPY_U16TOBUFFER(meta->srcAddr, mhr.srcAddr.shortAddr);
	}

	tl_zbPhyIndication(buf, (u8 *)&mhr, hdrSize);
}

static void mac_csmaStart(void *arg)
{
	u32 r;

	r = drv_disable_irq();
	if (rf_performCCA() != PHY_CCA_IDLE && g_zbInfo.macPib.maxCsmaBackoffs != 0U) {
		drv_restore_irq(r);
		tl_zbTaskPost(mac_trxTask, (void *)MAC_TX_EV_CSMA_BUSY);
		return;
	}

	{
		if ((rf_busyFlag & TX_ACKPACKET) != 0U) {
			rf_busyFlag &= (u8)~TX_ACKPACKET;
		}

		mac_trx_vars.state = MAC_TX_UNDERWAY;
		rf_busyFlag |= TX_BUSY;
		rf802154_tx();
		drv_restore_irq(r);

		r = drv_disable_irq();
		if (g_macTimerEvt.state != MAC_TIMER_EVENT_IDLE) {
			ZB_EXCEPTION_POST(SYS_EXCEPTTION_ZB_MAC_TRX_TASK);
			drv_restore_irq(r);
			return;
		}

		g_macTimerEvt.cb = mac_waitTxIrqCb;
		g_macTimerEvt.deadline = mac_currentTickGet() + sysTimerPerUs * 10000U;
		g_macTimerEvt.state = MAC_TIMER_EVENT_TX_WAIT;
		drv_restore_irq(r);
		return;
	}
}

void zb_macTimerEventProc(void *arg)
{
	(void)arg;

	if (g_macTimerEvt.state == MAC_TIMER_EVENT_IDLE) {
		return;
	}

	if ((s32)(g_macTimerEvt.deadline - mac_currentTickGet()) > 0) {
		return;
	}

	{
		u32 r = drv_disable_irq();
		void (*cb)(void *) = g_macTimerEvt.cb;

		if (cb != NULL) {
			cb(NULL);
		}
		g_macTimerEvt.state = MAC_TIMER_EVENT_IDLE;
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

	g_macTimerEvt.state = MAC_TIMER_EVENT_IDLE;
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

	if (status == MAC_SUCCESS && entry->fFramePending != 0U) {
		status = MAC_STA_FRAME_PENDING;
		if (handle == MAC_INTERNAL_DATA_REQUEST_HANDLE ||
		    handle == MAC_INTERNAL_MLME_DATA_REQUEST_HANDLE) {
			needPendingWait = 1;
		}
	}

	if (g_zbInfo.macPib.rxOnWhenIdle == 0U &&
	    (g_zbMacCtx.status | needPendingWait) == ZB_MAC_STATE_NORMAL) {
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
		if (g_macTimerEvt.state != MAC_TIMER_EVENT_IDLE) {
			ZB_EXCEPTION_POST(SYS_EXCEPTTION_ZB_MAC_TRX_TASK);
			drv_restore_irq(r);
			return;
		}

		g_macTimerEvt.cb = mac_pendingWaitTimerCb;
		g_macTimerEvt.deadline =
			mac_currentTickGet() +
			sysTimerPerUs * ((u32)g_zbInfo.macPib.frameTotalWaitTime << 4);
		g_macTimerEvt.state = MAC_TIMER_EVENT_PENDING_WAIT;
		drv_restore_irq(r);
		return;
	}

	mac_trigger_tx(NULL);
}

static void mac_trxTask(void *arg)
{
	tx_data_queue *entry = (tx_data_queue *)mac_trx_vars.curTx;
	u8 event = (u8)(uintptr_t)arg;
	s8 rssi = (s8)((uintptr_t)arg >> 8);
	u8 framePending = (u8)((uintptr_t)arg >> 16);
	u8 state = mac_trx_vars.state;

	if (entry == NULL) {
		return;
	}

	if (event == MAC_TX_EV_NEW_DATA) {
		mac_trx_vars.ackSeqNum = entry->seqNum;
		mac_trx_vars.ackRequired = entry->fAck;
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

		g_sysDiags.macTxCcaFail++;
		mac_trx_vars.state = MAC_TX_DONE;
		entry->cnfStatus = MAC_STA_CHANNEL_ACCESS_FAILURE;
		tl_zbTaskPost((tl_zb_callback_t)mac_sendTxCnf, entry);
		return;

	case MAC_TX_UNDERWAY:
		if (event == MAC_TX_EV_SEND_FAIL) {
			mac_trx_vars.state = MAC_TX_DONE;
			entry->cnfStatus = MAC_TX_ABORTED;
			g_sysDiags.macTxIrqTimeoutCnt++;
			mac_sendTxCnf(entry);
		} else if (event == MAC_TX_EV_SEND_SUCC &&
			   g_macTimerEvt.state == MAC_TIMER_EVENT_TX_WAIT) {
			u32 r;

			g_macTimerEvt.state = MAC_TIMER_EVENT_IDLE;
			if (mac_trx_vars.ackRequired == 0U) {
				mac_trx_vars.state = MAC_TX_DONE;
				entry->cnfStatus = MAC_SUCCESS;
				tl_zbTaskPost((tl_zb_callback_t)mac_sendTxCnf, entry);
				return;
			}

			mac_trx_vars.state = MAC_TX_WAIT_ACK;
			r = drv_disable_irq();
			if (g_macTimerEvt.state != MAC_TIMER_EVENT_IDLE) {
				ZB_EXCEPTION_POST(SYS_EXCEPTTION_ZB_MAC_TRX_TASK);
				drv_restore_irq(r);
				return;
			}

			g_macTimerEvt.cb = mac_ackWaitingTimerCb;
			g_macTimerEvt.deadline = mac_currentTickGet() + sysTimerPerUs * 2000U;
			g_macTimerEvt.state = MAC_TIMER_EVENT_ACK_WAIT;
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
			((zb_buf_t *)entry->buf)->hdr.rssi = MAC_TX_NO_ACK_RSSI;
			mac_sendTxCnf(entry);
			return;
		}

		if (event == MAC_TX_EV_ACK_RECV &&
		    g_macTimerEvt.state == MAC_TIMER_EVENT_ACK_WAIT) {
			g_macTimerEvt.state = MAC_TIMER_EVENT_IDLE;
			entry->fFramePending = (framePending != 0U);

			mac_trx_vars.state = MAC_TX_DONE;
			entry->cnfStatus = MAC_SUCCESS;
			((zb_buf_t *)entry->buf)->hdr.rssi = rssi;
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

	if (mac_trx_vars.state != 0U || tx_fifo_wptr == tx_fifo_rptr ||
	    g_zbMacCtx.indirectData != 0U) {
		return;
	}

	{
		void *next = get_next_data();
		if (next != NULL) {
			mac_trx_vars.curTx = next;
			mac_trxTask(NULL);
		}
	}
}

void tl_zbSwitchOffRx(void)
{
	if (g_zbInfo.macPib.rxOnWhenIdle == 0U) {
		rf_setTrxState(RF_STATE_OFF);
	}

	g_zbMacCtx.indirectData = 0;
	tl_zbTaskPost(mac_trigger_tx, NULL);
}

int mac_pendingWaitTimerCb(void *arg)
{
	(void)arg;

	if (g_macTimerEvt.state != MAC_TIMER_EVENT_IDLE) {
		tl_zbSwitchOffRx();
	}

	return -1;
}

void mac_pendingWaitTimerCancel(void)
{
	if (g_macTimerEvt.state == MAC_TIMER_EVENT_PENDING_WAIT) {
		g_macTimerEvt.state = MAC_TIMER_EVENT_IDLE;
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
		entry->fAck = ackReq;
		entry->psduLen = psduLen;
		entry->txData = txData;
		/* "a6: tloadrb r2,[r4,#2]" - the sequence number comes from the
		 * queued frame, whose third byte it is. */
		entry->seqNum = txData[2];
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

u8 tl_zbMacStateBusy(void)
{
	if ((u8)(g_zbMacCtx.status - ZB_MAC_STATE_ACTIVE_SCAN) <= 1U) {
		return 1;
	}

	if (g_macTimerEvt.state != MAC_TIMER_EVENT_IDLE || mac_trx_vars.state != MAC_TX_IDLE) {
		return 1;
	}

	if (tx_fifo_wptr > tx_fifo_rptr) {
		return (u8)(tx_fifo_wptr - tx_fifo_rptr - 1U);
	}

	return 0;
}

_attribute_ram_code_ u8 *zb_macDataFilter(u8 *macPld, u8 len, u8 *needDrop, u8 *ackPkt)
{
	u8 frameType = macPld[0] & MAC_FCF_FRAME_TYPE_MASK;
	u8 dstAddrMode = macPld[1] & MAC_FCF_DST_ADDR_BIT;
	u8 srcAddrMode = macPld[1] & MAC_FCF_SRC_ADDR_BIT;
	u8 *srcAddr = macPld + 3;

	if (srcAddrMode != ADDR_MODE_NONE && (macPld[0] & 0x80U) == 0U) {
		srcAddr += 2;
	}

	/* The vendor checks the MAC payload length after subtracting FCF/DSN. */
	if ((u8)(len - 3U) > 126U) {
		*needDrop = 9;
		g_sysDiags.phyLengthError++;
		return NULL;
	}

	if (mac_trx_vars.state == MAC_TX_WAIT_ACK) {
		if (frameType == MAC_FRAME_TYPE_ACK) {
			if (len == 5U) {
				*ackPkt = 1;
				return NULL;
			}

			*needDrop = frameType;
			return NULL;
		}
	}

	if (frameType > MAC_FRAME_TYPE_COMMAND) {
		*needDrop = 3;
		return NULL;
	}

	if (frameType == MAC_FRAME_TYPE_BEACON && macPld[0] == 0U) {
		if (macPld[1] != 0x80U && macPld[1] != 0xc0U) {
			*needDrop = 4;
			return NULL;
		}

		return NULL;
	}

	if (dstAddrMode != ADDR_MODE_NONE) {
		u16 dstPanId = (u16)macPld[3] | ((u16)macPld[4] << 8);

		srcAddr = macPld + 5;
		if (srcAddrMode != ADDR_MODE_NONE && (macPld[0] & 0x80U) == 0U) {
			srcAddr += 2;
		}

		if (dstPanId != MAC_INVALID_PANID && dstPanId != g_zbInfo.macPib.panId) {
			*needDrop = 5;
			return NULL;
		}

		if (dstAddrMode == 0x08U) {
			u16 dstShort = (u16)macPld[5] | ((u16)macPld[6] << 8);

			if (dstShort != MAC_SHORT_ADDR_BROADCAST &&
			    dstShort != g_zbInfo.macPib.shortAddress) {
				*needDrop = 6;
			}

			return srcAddr + 2;
		}

		if (dstAddrMode == MAC_FCF_DST_ADDR_BIT) {
			const u8 *localExt = g_zbInfo.macPib.extAddress;

			if (macPld[5] != localExt[0] || macPld[6] != localExt[1] ||
			    macPld[7] != localExt[2] || macPld[8] != localExt[3] ||
			    macPld[9] != localExt[4] || macPld[10] != localExt[5] ||
			    macPld[11] != localExt[6] || macPld[12] != localExt[7]) {
				*needDrop = 7;
			}

			return srcAddr + 8;
		}
	}

	if (dstAddrMode == ADDR_MODE_NONE) {
		*needDrop = 8;
	}
	return srcAddr;
}

/* .ram_code in the vendor object (_router/mac_trx.s:.ram_code+0x15c). */
_attribute_ram_code_ void zb_macDataRecvHandler(u8 *rxBuf, u8 *data, u8 len, u8 ackPkt,
						u32 timestamp, s8 rssi)
{
	zb_buf_t *buf = (zb_buf_t *)tl_phyRxBufTozbBuf(rxBuf);

	if (ackPkt != 0U) {
		u8 frameCtrl = data[0];
		u8 seqNum = data[2];

		if (mac_trx_vars.state == MAC_TX_WAIT_ACK && mac_trx_vars.ackSeqNum == seqNum) {
			u32 event = MAC_TX_EV_ACK_RECV | ((u32)(u8)rssi << 8) |
				    ((u32)(frameCtrl & MAC_FCF_FRAME_PENDING_MASK) << 16) |
				    ((u32)seqNum << 24);

			mac_trxTask((void *)(uintptr_t)event);
		} else {
			zb_buf_free(buf);
		}
		return;
	}

	{
		mac_rx_pending_meta_t *pending = (mac_rx_pending_meta_t *)buf;

		pending->raw = data;
		pending->timestamp = timestamp;
		pending->rssi = rssi;
		pending->len = (u8)(len - 2U);
	}

	rf_busyFlag &= (u8)~RX_BUSY;
	if (tl_zbUserTaskQNum() >= (u8)(ZB_TASKQ_USERUSE_SIZE - 5U)) {
		zb_buf_free(buf);
		return;
	}

	tl_zbTaskPost(mac_rxDataParse, buf);
}

_attribute_ram_code_ void zb_macDataSendHandler(void)
{
	mac_trxTask((void *)MAC_TX_EV_SEND_SUCC);
}
