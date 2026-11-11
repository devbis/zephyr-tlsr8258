/*
 * Copyright 2026 libzigbee
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "zb_common.h"
#include "ev_timer.h"
#include "mac_phy.h"
#include "mac_trx_api.h"
#include "mac.h"
#include "mac_common.h"
#include "mac_data.h"
#include "mac_scan.h"

#include "mac_trx.h"

typedef struct {
	ev_timer_event_t *timerEvt;
	zb_mac_mlme_scan_req_t *scanReq;
	uint32_t scanChannels;
	uint8_t scanType;
	uint8_t curChannel;
	uint8_t savedChannel;
	uint8_t scanStep;
	uint8_t orphanScanGotRealignment;
	uint8_t resultCount;
	uint8_t savedTrxState;
} mac_scan_param_t;

/* Keep this global: vendor objects export g_macScanParam as a real data symbol. */
mac_scan_param_t g_macScanParam;
#if defined(ZB_ROUTER_ROLE)
static uint32_t chan_8352 __asm__("chan.9222") = ZB_MAC_CHANNEL_START;
static uint32_t edChan_8354 __asm__("edChan.9224") = 0xffU;
#else
static uint32_t chan_8352 __asm__("chan.8352") = ZB_MAC_CHANNEL_START;
static uint32_t edChan_8354 __asm__("edChan.8354") = 0xffU;
#endif

static inline uint32_t mac_scan_timeout_ms(uint8_t value)
{
	return ((((uint32_t)value << 4) - value) << 10) / 1000U;
}

enum {
	MAC_BEACON_REQUEST_FRAME_CTRL = MAC_FRAME_TYPE_COMMAND |
		((uint16_t)ADDR_MODE_SHORT << MAC_FCF_DST_ADDR_MODE_POS),
	MAC_ORPHAN_NOTIFICATION_FRAME_CTRL = MAC_FRAME_TYPE_COMMAND |
		((uint16_t)ADDR_MODE_SHORT << MAC_FCF_DST_ADDR_MODE_POS) |
		((uint16_t)ADDR_MODE_EXT << MAC_FCF_SRC_ADDR_MODE_POS),
};

uint8_t zb_MacMlmeBeaconRequestCmdSend(void)
{
	zb_mac_mhr_t mhr;
	zb_buf_t *buf = (zb_buf_t *)g_zbMacCtx.txRawDataBuf;
	uint8_t *payload;
	uint8_t *frameStart;
	uint8_t status = MAC_SUCCESS;

	if (buf->hdr.active != 0U) {
		return status;
	}

	buf->hdr.active = 1;

	memset(&mhr, 0, sizeof(mhr));
	mhr.dstPanId = MAC_PAN_ID_BROADCAST;
	mhr.dstAddr.shortAddr = MAC_SHORT_ADDR_BROADCAST;
	mhr.frameCtrl = MAC_BEACON_REQUEST_FRAME_CTRL;

	buf->hdr.handle = MAC_STA_SECURITY_ERROR;

	ZB_BUF_INITIAL_ALLOC(buf, 8, payload, uint8_t *);
	/* Keep the allocation and hand zb_MacTx() the frame start; the builder's return
	 * value only says where the command byte goes. */
	frameStart = payload;
	payload = zb_MacHdrBuilder(payload, &mhr);
	payload[0] = MAC_CMD_BEACON_REQUEST;

	status = zb_MacTx(buf, frameStart, 8, 0, NULL);

	return status;
}

int zb_MacScanRunning(void *arg)
{
	(void)arg;

	zb_buf_t *buf = (zb_buf_t *)g_macScanParam.scanReq;
	uint8_t scanType = g_macScanParam.scanType;

	if (chan_8352 <= ZB_MAC_CHANNEL_STOP && g_macScanParam.orphanScanGotRealignment == 0U) {
		if (scanType == ED_SCAN && edChan_8354 != 0xffU) {
			uint8_t idx = g_macScanParam.resultCount;
			uint8_t rssi = rf_stopEDScan();

			buf->buf[8 + idx] = rssi;
			g_macScanParam.resultCount = (uint8_t)(idx + 1U);

			if (chan_8352 > ZB_MAC_CHANNEL_STOP) {
				goto scan_done;
			}
		}

		{
			uint32_t mask = g_macScanParam.scanChannels;

			while (chan_8352 <= ZB_MAC_CHANNEL_STOP &&
			       (mask & (1UL << chan_8352)) == 0U) {
				chan_8352++;
			}

			if (chan_8352 > ZB_MAC_CHANNEL_STOP) {
				goto scan_done;
			}
		}

		zb_MacChannelSet((uint8_t)chan_8352);
		g_macScanParam.curChannel = (uint8_t)chan_8352;

		if (scanType == ED_SCAN) {
			rf_startEDScan();
			edChan_8354 = chan_8352;
		} else if (scanType == ORPHAN_SCAN) {
			zb_mac_mhr_t mhr;
			zb_buf_t *txBuf = (zb_buf_t *)g_zbMacCtx.txRawDataBuf;

			if (txBuf->hdr.active == 0U) {
				uint8_t *payload;
				uint8_t *frameStart;
				uint8_t hdrSize;

				txBuf->hdr.active = 1;
				memset(&mhr, 0xff, sizeof(mhr));
				ZB_IEEE_ADDR_COPY(&mhr.srcAddr, g_zbInfo.macPib.extAddress);
				mhr.frameCtrl = MAC_ORPHAN_NOTIFICATION_FRAME_CTRL;

				hdrSize = (uint8_t)(zb_MacHdrSize(MAC_ORPHAN_NOTIFICATION_FRAME_CTRL) +
					       1U);
				txBuf->hdr.handle = MAC_STA_NO_BEACON;
				ZB_BUF_INITIAL_ALLOC(txBuf, hdrSize, payload, uint8_t *);
				frameStart = payload;
				payload = zb_MacHdrBuilder(payload, &mhr);
				payload[0] = MAC_CMD_ORPHAN_NOTIFICATION;
				zb_MacTx(txBuf, frameStart, hdrSize, 0, NULL);
			}
		} else if (scanType == ACTIVE_SCAN) {
			zb_MacMlmeBeaconRequestCmdSend();
		}

		g_macScanParam.scanChannels &= ~(1UL << chan_8352);

		return 0;
	}

scan_done:
	chan_8352 = ZB_MAC_CHANNEL_START;
	edChan_8354 = 0xffU;

	{
		zb_mac_mlme_scan_conf_t *cnf = (zb_mac_mlme_scan_conf_t *)buf;

		cnf->scanType = scanType;
		cnf->status = MAC_STA_NO_BEACON;
		cnf->resultListSize = g_macScanParam.resultCount;

		if (scanType == ED_SCAN) {
			cnf->status = MAC_SUCCESS;
		} else if (g_macScanParam.resultCount != 0U ||
			   g_macScanParam.orphanScanGotRealignment != 0U) {
			cnf->status = MAC_SUCCESS;
		}
	}

	zb_PrimitivePost(ZB_Q_MAC2NWK, MAC_MLME_SCAN_CNF, buf);

	g_zbMacCtx.status = ZB_MAC_STATE_NORMAL;
	zb_MacChannelSet(g_macScanParam.savedChannel);
	rf_setTrxState(g_macScanParam.savedTrxState);
	g_macScanParam.timerEvt = NULL;

	return -1;
}

void zb_MacActiveScanListAdd(void)
{
	g_macScanParam.resultCount++;
}

void zb_MacOrphanScanStatusUpdate(void)
{
	g_macScanParam.orphanScanGotRealignment = 1;
}

void zb_MacScanRequestHandler(zb_mac_mlme_scan_req_t *req)
{
	uint8_t status = MAC_STA_INVALID_PARAMETER;
	uint8_t scanType = req->scanType;

	if (req->scanDuration <= 7U && (g_zbMacCtx.status == ZB_MAC_STATE_NORMAL ||
					g_zbMacCtx.status > ZB_MAC_STATE_ORPHAN_SCAN)) {
		g_macScanParam.scanType = scanType;
		g_macScanParam.scanChannels = req->scanChannels;
		g_macScanParam.scanStep = (uint8_t)((1U << req->scanDuration) + 1U);
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

		zb_MacScanRunning(NULL);

		if (g_macScanParam.timerEvt != NULL) {
			ev_timer_taskCancel(&g_macScanParam.timerEvt);
		}

		{
			uint8_t timeoutBase = (scanType == ORPHAN_SCAN) ? g_zbInfo.macPib.respWaitTime
								   : g_macScanParam.scanStep;
			uint32_t timeout = mac_scan_timeout_ms(timeoutBase);

			g_macScanParam.timerEvt =
				ev_timer_taskPost(zb_MacScanRunning, NULL, timeout);
		}

		return;
	}

post_invalid:
	req->scanType = scanType;
	req->scanChannels = req->scanChannels;
	((uint8_t *)req)[0] = status;
	((uint8_t *)req)[1] = scanType;
	zb_PrimitivePost(ZB_Q_MAC2NWK, MAC_MLME_SCAN_CNF, req);
}
