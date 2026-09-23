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

enum {
	MAC_BEACON_REQUEST_FRAME_CTRL = MAC_FRAME_TYPE_COMMAND |
		((u16)ADDR_MODE_SHORT << MAC_FCF_DST_ADDR_MODE_POS),
	MAC_ORPHAN_NOTIFICATION_FRAME_CTRL = MAC_FRAME_TYPE_COMMAND |
		((u16)ADDR_MODE_SHORT << MAC_FCF_DST_ADDR_MODE_POS) |
		((u16)ADDR_MODE_EXT << MAC_FCF_SRC_ADDR_MODE_POS),
};

u8 tl_zbMacMlmeBeaconRequestCmdSend(void)
{
	tl_zb_mac_mhr_t mhr;
	zb_buf_t *buf = (zb_buf_t *)g_zbMacCtx.txRawDataBuf;
	u8 *payload;
	u8 *frameStart;
	u8 status = MAC_SUCCESS;

	if (buf->hdr.active != 0U) {
		return status;
	}

	buf->hdr.active = 1;

	memset(&mhr, 0, sizeof(mhr));
	mhr.dstPanId = MAC_PAN_ID_BROADCAST;
	mhr.dstAddr.shortAddr = MAC_SHORT_ADDR_BROADCAST;
	mhr.frameCtrl = MAC_BEACON_REQUEST_FRAME_CTRL;

	buf->hdr.handle = MAC_STA_SECURITY_ERROR;

	TL_BUF_INITIAL_ALLOC(buf, 8, payload, u8 *);
	/* "58: tadds r7,r0,#0 ... 68: tadds r1,r7,#0" - the vendor keeps the
	 * allocation and hands tl_zbMacTx() the frame start; the builder's return
	 * value only says where the command byte goes. */
	frameStart = payload;
	payload = tl_zbMacHdrBuilder(payload, &mhr);
	payload[0] = MAC_CMD_BEACON_REQUEST;

	status = tl_zbMacTx(buf, frameStart, 8, 0, NULL);

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

		{
			u32 mask = g_macScanParam.scanChannels;

			while (chan_8352 <= TL_ZB_MAC_CHANNEL_STOP &&
			       (mask & (1UL << chan_8352)) == 0U) {
				chan_8352++;
			}

			if (chan_8352 > TL_ZB_MAC_CHANNEL_STOP) {
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

			if (txBuf->hdr.active == 0U) {
				u8 *payload;
				u8 *frameStart;
				u8 hdrSize;

				txBuf->hdr.active = 1;
				memset(&mhr, 0xff, sizeof(mhr));
				ZB_IEEE_ADDR_COPY(&mhr.srcAddr, g_zbInfo.macPib.extAddress);
				mhr.frameCtrl = MAC_ORPHAN_NOTIFICATION_FRAME_CTRL;

				hdrSize = (u8)(tl_zbMacHdrSize(MAC_ORPHAN_NOTIFICATION_FRAME_CTRL) +
					       1U);
				txBuf->hdr.handle = MAC_STA_NO_BEACON;
				TL_BUF_INITIAL_ALLOC(txBuf, hdrSize, payload, u8 *);
				frameStart = payload;
				payload = tl_zbMacHdrBuilder(payload, &mhr);
				payload[0] = MAC_CMD_ORPHAN_NOTIFICATION;
				tl_zbMacTx(txBuf, frameStart, hdrSize, 0, NULL);
			}
		} else if (scanType == ACTIVE_SCAN) {
			tl_zbMacMlmeBeaconRequestCmdSend();
		}

		g_macScanParam.scanChannels &= ~(1UL << chan_8352);

		return 0;
	}

scan_done:
	chan_8352 = TL_ZB_MAC_CHANNEL_START;
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

	if (req->scanDuration <= 7U && (g_zbMacCtx.status == ZB_MAC_STATE_NORMAL ||
					g_zbMacCtx.status > ZB_MAC_STATE_ORPHAN_SCAN)) {
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

			g_macScanParam.timerEvt =
				ev_timer_taskPost(tl_zbMacScanRunning, NULL, timeout);
		}

		return;
	}

post_invalid:
	req->scanType = scanType;
	req->scanChannels = req->scanChannels;
	((u8 *)req)[0] = status;
	((u8 *)req)[1] = scanType;
	tl_zbPrimitivePost(TL_Q_MAC2NWK, MAC_MLME_SCAN_CNF, req);
}
