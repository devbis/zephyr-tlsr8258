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
static uint8_t tx_fifo_rptr = 0;
static uint8_t tx_fifo_wptr = 0;

static void mac_trxTask(void *arg);

typedef struct {
	void *curTx;
	uint8_t state;
	uint8_t unused; /* offset 5, cleared when a new frame starts CSMA */
	uint8_t csmaBackoffCnt;
	uint8_t backoffExponent;
	uint8_t frameRetryCnt;
	uint8_t ackRequired;
	uint8_t skipFreeTxBuf;
	uint8_t ackSeqNum;
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
		rf_busyFlag &= (uint8_t)~TX_BUSY;
		rf_setTrxState(RF_STATE_RX);
		zb_TaskPost(mac_trxTask, (void *)MAC_TX_EV_SEND_FAIL);
	}

	return -1;
}

int mac_ackWaitingTimerCb(void *arg)
{
	(void)arg;

	if (g_macTimerEvt.state != MAC_TIMER_EVENT_IDLE) {
		zb_TaskPost(mac_trxTask, (void *)MAC_TX_EV_ACK_RETRY);
	}

	return -1;
}

void mac_rxDataParse(void *arg)
{
	zb_buf_t *buf = (zb_buf_t *)arg;
	mac_rx_pending_meta_t *pending = (mac_rx_pending_meta_t *)arg;
	mac_phy_ind_meta_t *meta = (mac_phy_ind_meta_t *)arg;
	uint8_t *raw = pending->raw;
	uint32_t timestamp = pending->timestamp;
	int8_t rssi = pending->rssi;
	uint8_t len = pending->len;
	uint8_t linkQuality;
	uint8_t frameType;
	uint8_t hdrSize;
	zb_mac_mhr_t mhr;

	buf->hdr.rssi = rssi;
	linkQuality = rf_getLqi(rssi);
	frameType = raw[0] & 0x07U;
	hdrSize = zb_MacHdrParse(&mhr, raw);

	if (len <= hdrSize || g_zbMacCtx.status == ZB_MAC_STATE_ED_SCAN) {
		zb_buf_free(buf);
		return;
	}

	/* An active scan keeps only beacons. */
	if (g_zbMacCtx.status == ZB_MAC_STATE_ACTIVE_SCAN && frameType != MAC_FRAME_TYPE_BEACON) {
		zb_buf_free(buf);
		return;
	}

	/* An orphan scan keeps only the coordinator realignment command. */
	if (g_zbMacCtx.status == ZB_MAC_STATE_ORPHAN_SCAN &&
	    (frameType != MAC_FRAME_TYPE_COMMAND ||
	     raw[hdrSize] != MAC_CMD_COORDINATOR_REALIGNMENT)) {
		zb_buf_free(buf);
		return;
	}

	meta->timestamp = timestamp;
	meta->payload = raw;
	meta->srcPanId = mhr.srcPanId;
	meta->srcAddrMode = mhr.srcAddrMode;
	meta->payloadLen = len;
	meta->linkQuality = linkQuality;
	meta->curChannel = rf_getChannel();

	if (mhr.srcAddrMode == ADDR_MODE_EXT) {
		ZB_IEEE_ADDR_COPY(meta->srcAddr, mhr.srcAddr.extAddr);
	} else {
		COPY_U16TOBUFFER(meta->srcAddr, mhr.srcAddr.shortAddr);
	}

	zb_PhyIndication(buf, (uint8_t *)&mhr, hdrSize);
}

static void mac_csmaStart(void *arg)
{
	uint32_t r;

	r = drv_disable_irq();
	if (rf_performCCA() != PHY_CCA_IDLE && g_zbInfo.macPib.maxCsmaBackoffs != 0U) {
		drv_restore_irq(r);
		zb_TaskPost(mac_trxTask, (void *)MAC_TX_EV_CSMA_BUSY);
		return;
	}

	{
		if ((rf_busyFlag & TX_ACKPACKET) != 0U) {
			rf_busyFlag &= (uint8_t)~TX_ACKPACKET;
		}

		mac_trx_vars.state = MAC_TX_UNDERWAY;
		rf_busyFlag |= TX_BUSY;
		drv_restore_irq(r);
		/*
		 * The platform transmits synchronously and waits for the radio
		 * interrupt, so the transmission cannot run with interrupts locked
		 * as in the vendor code. Its completion is posted to this thread
		 * and handled only after the TX wait below is armed.
		 */
		rf802154_tx();

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

	if ((int32_t)(g_macTimerEvt.deadline - mac_currentTickGet()) > 0) {
		return;
	}

	{
		uint32_t r = drv_disable_irq();
		void (*cb)(void *) = g_macTimerEvt.cb;

		if (cb != NULL) {
			cb(NULL);
		}
		g_macTimerEvt.state = MAC_TIMER_EVENT_IDLE;
		drv_restore_irq(r);
	}
}

uint8_t mac_data_pending(void)
{
	uint8_t pending = (uint8_t)(tx_fifo_wptr - tx_fifo_rptr);

	return pending ? (uint8_t)(pending - 1U) : 0U;
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
		uint32_t r = drv_disable_irq();

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
	uint8_t handle;
	uint8_t status;
	uint8_t needPendingWait = 0;

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
		zb_MaxTxConfirmCb(txBuf, status);
	}

	if (needPendingWait != 0U) {
		uint32_t r;

		g_zbMacCtx.indirectData = 1;

		r = drv_disable_irq();
		if (g_macTimerEvt.state != MAC_TIMER_EVENT_IDLE) {
			ZB_EXCEPTION_POST(SYS_EXCEPTTION_ZB_MAC_TRX_TASK);
			drv_restore_irq(r);
			return;
		}

		g_macTimerEvt.cb = mac_pendingWaitTimerCb;
		{
			uint32_t now = mac_currentTickGet();
			int32_t waitMs = ((int32_t)g_zbInfo.macPib.frameTotalWaitTime << 4) / 1000;
			uint32_t ticksPerMs = sysTimerPerUs * 1000U;

			g_macTimerEvt.deadline = now + ticksPerMs * (uint32_t)waitMs;
		}
		g_macTimerEvt.state = MAC_TIMER_EVENT_PENDING_WAIT;
		drv_restore_irq(r);
		return;
	}

	mac_trigger_tx(NULL);
}

static void mac_trxTask(void *arg)
{
	tx_data_queue *entry = (tx_data_queue *)mac_trx_vars.curTx;
	uint8_t event = (uint8_t)(uintptr_t)arg;
	int8_t rssi = (int8_t)((uintptr_t)arg >> 8);
	uint8_t framePending = (uint8_t)((uintptr_t)arg >> 16);
	uint8_t state = mac_trx_vars.state;

	if (entry == NULL) {
		return;
	}

	if (event == MAC_TX_EV_NEW_DATA) {
		mac_trx_vars.ackSeqNum = entry->seqNum;
		mac_trx_vars.ackRequired = entry->fAck;
		/*
		 * A retry is posted as a new-data event as well, and its count must survive:
		 * clearing it for a retry never lets it reach macMaxFrameRetries, so a frame that
		 * no one acknowledges is sent again for ever.
		 */
		if (state != MAC_TX_RETRY) {
			mac_trx_vars.frameRetryCnt = 0;
		}
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
		mac_trx_vars.unused = 0;
		state = MAC_TX_CSMA;
		/* fall through */

	case MAC_TX_CSMA:
		if (event != MAC_TX_EV_NEW_DATA && event != MAC_TX_EV_CSMA_BUSY) {
			return;
		}

		if (mac_trx_vars.csmaBackoffCnt <= g_zbInfo.macPib.maxCsmaBackoffs) {
			uint16_t backoffUs = 200U;

			mac_trx_vars.csmaBackoffCnt++;
			rf_TrxStateGet();
			rf_setTrxState(RF_STATE_OFF);
			rf_setTrxState(RF_STATE_RX);

			if (mac_trx_vars.csmaBackoffCnt == 1U) {
				mac_csmaStart(entry);
				return;
			}

			if (mac_trx_vars.backoffExponent != 0U) {
				uint32_t mod = (1UL << mac_trx_vars.backoffExponent) - 1UL;
				uint32_t slots = drv_u32Rand() & 0xffffU;

				slots = mod ? (slots % mod) : 0U;
				backoffUs = (uint16_t)(slots * 320U);
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
			zb_TaskPost(mac_trxTask, (void *)MAC_TX_EV_NEW_DATA);
			return;
		}

		g_sysDiags.macTxCcaFail++;
		mac_trx_vars.state = MAC_TX_DONE;
		entry->cnfStatus = MAC_STA_CHANNEL_ACCESS_FAILURE;
		zb_TaskPost((zb_callback_t)mac_sendTxCnf, entry);
		return;

	case MAC_TX_UNDERWAY:
		if (event == MAC_TX_EV_SEND_FAIL) {
			mac_trx_vars.state = MAC_TX_DONE;
			entry->cnfStatus = MAC_TX_ABORTED;
			g_sysDiags.macTxIrqTimeoutCnt++;
			mac_sendTxCnf(entry);
		} else if (event == MAC_TX_EV_SEND_SUCC &&
			   g_macTimerEvt.state == MAC_TIMER_EVENT_TX_WAIT) {
			uint32_t r;

			g_macTimerEvt.state = MAC_TIMER_EVENT_IDLE;
			if (mac_trx_vars.ackRequired == 0U) {
				mac_trx_vars.state = MAC_TX_DONE;
				entry->cnfStatus = MAC_SUCCESS;
				zb_TaskPost((zb_callback_t)mac_sendTxCnf, entry);
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
				zb_TaskPost(mac_trxTask, (void *)MAC_TX_EV_NEW_DATA);
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

void zb_SwitchOffRx(void)
{
	if (g_zbInfo.macPib.rxOnWhenIdle == 0U) {
		rf_setTrxState(RF_STATE_OFF);
	}

	g_zbMacCtx.indirectData = 0;
	zb_TaskPost(mac_trigger_tx, NULL);
}

int mac_pendingWaitTimerCb(void *arg)
{
	(void)arg;

	if (g_macTimerEvt.state != MAC_TIMER_EVENT_IDLE) {
		zb_SwitchOffRx();
	}

	return -1;
}

void mac_pendingWaitTimerCancel(void)
{
	if (g_macTimerEvt.state == MAC_TIMER_EVENT_PENDING_WAIT) {
		g_macTimerEvt.state = MAC_TIMER_EVENT_IDLE;
		zb_SwitchOffRx();
	}
}

uint8_t zb_MacTx(zb_buf_t *txBuf, uint8_t *txData, uint8_t psduLen, uint8_t ack, void *pendingList)
{
	uint8_t status = MAC_STA_NO_RESOURCES;
	uint32_t r;
	uint8_t depth;
	tx_data_queue *entry;
	uint8_t ackReq = ack ? 1U : 0U;

	if ((int8_t)psduLen < 0) {
		mac_trigger_tx(NULL);
		return MAC_STA_FRAME_TOO_LONG;
	}

	r = drv_disable_irq();
	depth = (uint8_t)(tx_fifo_wptr - tx_fifo_rptr);
	if (depth < MAC_TX_QUEUE_SIZE) {
		entry = &g_pTxQueue[tx_fifo_wptr & (MAC_TX_QUEUE_SIZE - 1U)];

		tx_fifo_wptr++;

		entry->buf = (uint8_t *)txBuf;
		entry->fAck = ackReq;
		entry->psduLen = psduLen;
		entry->txData = txData;
		/* The sequence number is the third byte of the queued frame. */
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
	zb_mac_phy_init();
}

uint8_t mac_getTrxState(void)
{
	return mac_trx_vars.state;
}

uint8_t zb_MacStateBusy(void)
{
	if ((uint8_t)(g_zbMacCtx.status - ZB_MAC_STATE_ACTIVE_SCAN) <= 1U) {
		return 1;
	}

	if (g_macTimerEvt.state != MAC_TIMER_EVENT_IDLE || mac_trx_vars.state != MAC_TX_IDLE) {
		return 1;
	}

	if (tx_fifo_wptr > tx_fifo_rptr) {
		return (uint8_t)(tx_fifo_wptr - tx_fifo_rptr - 1U);
	}

	return 0;
}

_attribute_ram_code_ uint8_t *zb_macDataFilter(uint8_t *macPld, uint8_t len, uint8_t *needDrop, uint8_t *ackPkt)
{
	uint8_t frameType = macPld[0] & MAC_FCF_FRAME_TYPE_MASK;
	uint8_t dstAddrMode = macPld[1] & MAC_FCF_DST_ADDR_BIT;
	uint8_t srcAddrMode = macPld[1] & MAC_FCF_SRC_ADDR_BIT;
	uint8_t *srcAddr = macPld + 3;

	if (srcAddrMode != ADDR_MODE_NONE && (macPld[0] & 0x80U) == 0U) {
		srcAddr += 2;
	}

	/* The vendor checks the MAC payload length after subtracting FCF/DSN. */
	if ((uint8_t)(len - 3U) > 126U) {
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
		uint16_t dstPanId = (uint16_t)macPld[3] | ((uint16_t)macPld[4] << 8);

		srcAddr = macPld + 5;
		if (srcAddrMode != ADDR_MODE_NONE && (macPld[0] & 0x80U) == 0U) {
			srcAddr += 2;
		}

		if (dstPanId != MAC_INVALID_PANID && dstPanId != g_zbInfo.macPib.panId) {
			*needDrop = 5;
			return NULL;
		}

		if (dstAddrMode == 0x08U) {
			uint16_t dstShort = (uint16_t)macPld[5] | ((uint16_t)macPld[6] << 8);

			if (dstShort != MAC_SHORT_ADDR_BROADCAST &&
			    dstShort != g_zbInfo.macPib.shortAddress) {
				*needDrop = 6;
			}

			return srcAddr + 2;
		}

		if (dstAddrMode == MAC_FCF_DST_ADDR_BIT) {
			const uint8_t *localExt = g_zbInfo.macPib.extAddress;

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
_attribute_ram_code_ void zb_macDataRecvHandler(uint8_t *rxBuf, uint8_t *data, uint8_t len, uint8_t ackPkt,
						uint32_t timestamp, int8_t rssi)
{
	zb_buf_t *buf;

	if (ackPkt != 0U) {
		uint8_t frameCtrl = data[0];
		uint8_t seqNum = data[2];

		if (mac_trx_vars.state == MAC_TX_WAIT_ACK && mac_trx_vars.ackSeqNum == seqNum) {
			uint32_t event = MAC_TX_EV_ACK_RECV | ((uint32_t)(uint8_t)rssi << 8) |
				    ((uint32_t)(frameCtrl & MAC_FCF_FRAME_PENDING_MASK) << 16) |
				    ((uint32_t)seqNum << 24);

			mac_trxTask((void *)(uintptr_t)event);
		}
		return;
	}

	buf = (zb_buf_t *)zb_phyRxBufTozbBuf(rxBuf);

	if (buf == NULL) {
		rf_busyFlag &= (uint8_t)~RX_BUSY;
		return;
	}

	{
		mac_rx_pending_meta_t *pending = (mac_rx_pending_meta_t *)buf;

		pending->raw = data;
		pending->timestamp = timestamp;
		pending->rssi = rssi;
		pending->len = (uint8_t)(len - 2U);
	}

	rf_busyFlag &= (uint8_t)~RX_BUSY;
	if (zb_UserTaskQNum() >= (uint8_t)(ZB_TASKQ_USERUSE_SIZE - 5U)) {
		zb_buf_free(buf);
		return;
	}

	zb_TaskPost(mac_rxDataParse, buf);
}

_attribute_ram_code_ void zb_macDataSendHandler(void)
{
	mac_trxTask((void *)MAC_TX_EV_SEND_SUCC);
}
