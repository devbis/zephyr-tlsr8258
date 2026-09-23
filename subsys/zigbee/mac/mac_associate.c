/*
 * Copyright 2026 libzigbee
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "zb_common.h"
#include "ev_timer.h"
#include "mac_trx_api.h"
#include "mac.h"
#include "mac_associate.h"
#include "mac_common.h"
#include "mac_cr_coordinator.h"
#include "mac_data.h"
#include "mac_indirect_data.h"
#include "mac_mlme.h"
#include "mac_trx.h"

static ev_timer_event_t *assocRspTimeoutEvt;
void *associationReqOrigBuffer = NULL;

static inline void mac_associate_confirm_post(void *arg, u8 status)
{
	zb_mlme_associate_conf_t *cnf = (zb_mlme_associate_conf_t *)arg;

	memset(cnf, 0, sizeof(*cnf));
	cnf->status = status;
	tl_zbPrimitivePost(TL_Q_MAC2NWK, MAC_MLME_ASSOCIATE_CNF, arg);
}

static inline u32 assoc_timeout_ms(void)
{
	u32 base = g_zbInfo.macPib.respWaitTime;
	return ((((base << 4) - base) << 10) / 1000U);
}

static int tl_zbWaitForAssociationRespTimeout(void *arg)
{
	mac_associate_confirm_post(arg, MAC_STA_NO_DATA);

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
		zb_mlme_associate_req_t *assocReq;

		if (buf == NULL) {
			mac_associate_confirm_post(associationReqOrigBuffer, MAC_STA_NO_RESOURCES);
			associationReqOrigBuffer = NULL;
			return -1;
		}

		memcpy(buf->buf, associationReqOrigBuffer, sizeof(*assocReq));
		assocReq = (zb_mlme_associate_req_t *)buf->buf;

		{
			zb_mlme_data_req_cmd_t dataReq;

			dataReq.srcAddrMode = ZB_ADDR_64BIT_DEV;
			dataReq.dstAddrMode = assocReq->coordAddress.addrMode;
			ZB_IEEE_ADDR_COPY(&dataReq.srcAddr, g_zbInfo.macPib.extAddress);
			ZB_IEEE_ADDR_COPY(&dataReq.dstAddr, assocReq->coordAddress.addr.extAddr);
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
		g_zbMacCtx.status = ZB_MAC_STATE_INDIRECT_DATA;
		if (assocRspTimeoutEvt == NULL) {
			assocRspTimeoutEvt = ev_timer_taskPost(tl_zbWaitForAssociationRespTimeout,
							       req, assoc_timeout_ms());
		}
		return;
	}

	if (status == MAC_SUCCESS) {
		if (assocRspTimeoutEvt == NULL) {
			assocRspTimeoutEvt = ev_timer_taskPost(tl_zbWaitForAssociationRespTimeout,
							       req, assoc_timeout_ms());
		}
		return;
	}

	mac_associate_confirm_post(req, status);
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
		mac_associate_confirm_post(associationReqOrigBuffer, status);
		associationReqOrigBuffer = NULL;
		return;
	}

	ev_timer_taskPost(tl_zbReadyToPullParentForAssoRsp, NULL, assoc_timeout_ms());
}

void tl_zbMacAssociateRequestHandler(void *arg)
{
	u8 *req = (u8 *)arg;
	zb_mlme_associate_req_t *assocReq = (zb_mlme_associate_req_t *)arg;
	zb_buf_t *txBuf = (zb_buf_t *)g_zbMacCtx.txRawDataBuf;

	if (txBuf == NULL || txBuf->hdr.active != 0U || associationReqOrigBuffer != NULL) {
		mac_associate_confirm_post(arg, MAC_STA_TX_ACTIVE);
		associationReqOrigBuffer = NULL;
		return;
	}

	txBuf->hdr.active = 1;
	associationReqOrigBuffer = arg;
	g_zbMacCtx.curChannel = req[0];
	rf_setChannel(req[0]);

	{
		tl_zb_mac_mhr_t mhr;
		u8 *payload;
		u8 *frameStart;
		u8 hdrSize;
		u8 txStatus;

		memset(&mhr, 0, sizeof(mhr));
		mhr.dstPanId = assocReq->coordPanId;
		mhr.srcPanId = MAC_PAN_ID_BROADCAST;
		mhr.dstAddrMode = assocReq->coordAddress.addrMode;
		if (mhr.dstAddrMode == ADDR_MODE_SHORT) {
			mhr.dstAddr.shortAddr = assocReq->coordAddress.addr.shortAddr;
		} else if (mhr.dstAddrMode == ADDR_MODE_EXT) {
			ZB_IEEE_ADDR_COPY(mhr.dstAddr.extAddr, assocReq->coordAddress.addr.extAddr);
		}
		ZB_IEEE_ADDR_COPY(&mhr.srcAddr, g_zbInfo.macPib.extAddress);
		mhr.frameCtrl = MAC_FRAME_TYPE_COMMAND | MAC_FCF_ACK_REQUEST_MASK |
				((u16)mhr.dstAddrMode << MAC_FCF_DST_ADDR_MODE_POS) |
				((u16)ADDR_MODE_EXT << MAC_FCF_SRC_ADDR_MODE_POS);

		hdrSize = (u8)(tl_zbMacHdrSize(mhr.frameCtrl) + 2U);
		TL_BUF_INITIAL_ALLOC(txBuf, hdrSize, payload, u8 *);
		txBuf->hdr.handle = MAC_INTERNAL_ASSOCIATE_REQUEST_HANDLE;
		/* tl_zbMacTx() takes the frame start, not the builder's return. */
		frameStart = payload;
		payload = tl_zbMacHdrBuilder(payload, &mhr);
		payload[0] = MAC_CMD_ASSOCIATION_REQUEST;
		payload[1] = *(u8 *)&assocReq->capbilityInfo;

		txStatus = tl_zbMacTx(txBuf, frameStart, hdrSize, 1, NULL);
		if (txStatus != MAC_SUCCESS) {
			mac_associate_confirm_post(arg, MAC_STA_TX_ACTIVE);
			associationReqOrigBuffer = NULL;
		}
	}
}

#if defined(ZB_ROUTER_ROLE)
void tl_zbMacAssociateResponseHandler(void *arg)
{
	u8 *req = (u8 *)arg;
	tl_zb_mac_mhr_t mhr;
	u8 pendingAddr[9];
	u32 pendingAddrLow;
	u32 pendingAddrHigh;
	mac_pending_tx_ctx_t *pendingCtx;
	u8 *frameStart;
	u8 *payload;
	u8 hdrSize;
	u8 status;

	if (g_zbNwkCtx.joined_pro) {
		zb_buf_free((zb_buf_t *)arg);
		return;
	}

	memset(&mhr, 0, sizeof(mhr));
	mhr.frameCtrl = MAC_FRAME_TYPE_COMMAND | MAC_FCF_ACK_REQUEST_MASK | MAC_FCF_INTRA_PAN_MASK |
			((u16)ADDR_MODE_EXT << MAC_FCF_DST_ADDR_MODE_POS) |
			((u16)ADDR_MODE_EXT << MAC_FCF_SRC_ADDR_MODE_POS);
	mhr.dstPanId = g_zbInfo.macPib.panId;
	mhr.srcPanId = g_zbInfo.macPib.panId;
	mhr.dstAddrMode = ADDR_MODE_EXT;
	mhr.srcAddrMode = ADDR_MODE_EXT;
	ZB_IEEE_ADDR_COPY(mhr.dstAddr.extAddr, req + 2);
	ZB_IEEE_ADDR_COPY(mhr.srcAddr.extAddr, g_zbInfo.macPib.extAddress);

	hdrSize = (u8)(tl_zbMacHdrSize(mhr.frameCtrl) + 4U);
	TL_BUF_INITIAL_ALLOC((zb_buf_t *)arg, hdrSize, payload, u8 *);
	/* "64: tadds r5,r0,#0" keeps the allocation: the pending context holds
	 * the complete frame, whose frame control byte the pending-state update
	 * later rewrites. */
	frameStart = payload;
	payload = tl_zbMacHdrBuilder(payload, &mhr);
	payload[0] = MAC_CMD_ASSOCIATION_RESPONSE;
	memcpy(payload + 1, req, 2);
	payload[3] = req[10];

	pendingAddr[0] = ADDR_MODE_EXT;
	ZB_IEEE_ADDR_COPY(pendingAddr + 1, req + 2);

	pendingCtx = (mac_pending_tx_ctx_t *)arg;
	pendingCtx->txData = frameStart;
	pendingCtx->psduLen = hdrSize;
	((zb_buf_t *)arg)->hdr.handle = MAC_INTERNAL_ASSOCIATE_RESPONSE_HANDLE;

	memcpy(&pendingAddrLow, pendingAddr + 1, sizeof(pendingAddrLow));
	memcpy(&pendingAddrHigh, pendingAddr + 5, sizeof(pendingAddrHigh));
	status = macDataPending(arg, pendingAddrLow, pendingAddrHigh, pendingAddr[0]);
	if (status != MAC_SUCCESS) {
		tl_zbMacCommStatusSend(arg, status);
	}
}
#endif

void tl_zbMacDisassociateNotifyCmdConfirm(void *arg, u8 status)
{
	zb_mlme_disassociate_conf_t *cnf = (zb_mlme_disassociate_conf_t *)arg;

	cnf->status = status;
	tl_zbPrimitivePost(TL_Q_MAC2NWK, MAC_MLME_DISASSOCIATE_CNF, arg);
}

u8 tl_macMlmeDisassociateNotifyCmdSend(void *arg, u8 txIndirect)
{
	zb_mlme_disassociate_req_t *req = (zb_mlme_disassociate_req_t *)arg;
	tl_zb_mac_mhr_t mhr;
	zb_buf_t *txBuf = (zb_buf_t *)arg;
	u8 *payload;
	u8 *frameStart;
	u8 hdrSize;
	u8 status;
#if defined(ZB_COORDINATOR_ROLE)
	u8 pendingAddrMode;
	u32 pendingAddr[2];
#endif

	memset(&mhr, 0, sizeof(mhr));
	mhr.dstPanId = g_zbInfo.macPib.panId;
	mhr.srcPanId = mhr.dstPanId;

	if (req->disassociateReason == 1U) {
		ZB_IEEE_ADDR_COPY(&mhr.dstAddr, req->devAddr.addr.extAddr);
	} else if (req->disassociateReason == 2U) {
		if (req->devAddr.addrMode == ZB_ADDR_16BIT_DEV_OR_BROADCAST) {
			mhr.dstAddr.shortAddr = g_zbInfo.macPib.coordShortAddress;
		} else {
			ZB_IEEE_ADDR_COPY(&mhr.dstAddr, g_zbInfo.macPib.coordExtAddress);
		}
	} else {
		ZB_IEEE_ADDR_COPY(&mhr.dstAddr, g_zbInfo.macPib.extAddress);
	}

	ZB_IEEE_ADDR_COPY(&mhr.srcAddr, g_zbInfo.macPib.extAddress);
	mhr.frameCtrl = MAC_FRAME_TYPE_COMMAND | MAC_FCF_ACK_REQUEST_MASK | MAC_FCF_INTRA_PAN_MASK |
			((u16)req->devAddr.addrMode << MAC_FCF_DST_ADDR_MODE_POS) |
			((u16)ADDR_MODE_EXT << MAC_FCF_SRC_ADDR_MODE_POS);

	hdrSize = (u8)(tl_zbMacHdrSize(mhr.frameCtrl) + 2U);
	TL_BUF_INITIAL_ALLOC(txBuf, hdrSize, payload, u8 *);
	frameStart = payload;
	payload = tl_zbMacHdrBuilder(payload, &mhr);
	txBuf->hdr.handle = MAC_INTERNAL_DISASSOCIATE_NOTIFICATION_HANDLE;
	payload[0] = MAC_CMD_DISASSOCIATION_NOTIFICATION;
	payload[1] = req->disassociateReason;

#if defined(ZB_COORDINATOR_ROLE)
	if (txIndirect != 0U) {
		pendingAddrMode = req->devAddr.addrMode;
		ZB_IEEE_ADDR_COPY(pendingAddr, &mhr.dstAddr);
		status = macDataPending(txBuf, pendingAddr[0], pendingAddr[1], pendingAddrMode);
	} else
#endif
	{
		status = tl_zbMacTx(txBuf, frameStart, hdrSize, 1, NULL);
	}
	if (status != MAC_SUCCESS) {
		return MAC_STA_CHANNEL_ACCESS_FAILURE;
	}

	return MAC_SUCCESS;
}

void tl_zbMacDisassociateRequestHandler(void *arg)
{
	zb_mlme_disassociate_req_t *req = (zb_mlme_disassociate_req_t *)arg;
	zb_mlme_disassociate_conf_t *cnf = (zb_mlme_disassociate_conf_t *)arg;
	u16 localPanId = g_zbInfo.macPib.panId;

	if (req->devPanId != localPanId) {
		cnf->status = MAC_STA_INVALID_PARAMETER;
		tl_zbPrimitivePost(TL_Q_MAC2NWK, MAC_MLME_DISASSOCIATE_CNF, arg);
		return;
	}

	if (req->devAddr.addrMode == ZB_ADDR_16BIT_DEV_OR_BROADCAST) {
		u16 parentShort = g_zbInfo.macPib.coordShortAddress;

		if (req->devAddr.addr.shortAddr != parentShort) {
			cnf->status = MAC_STA_INVALID_PARAMETER;
			tl_zbPrimitivePost(TL_Q_MAC2NWK, MAC_MLME_DISASSOCIATE_CNF, arg);
			return;
		}
	} else if (req->devAddr.addrMode == ZB_ADDR_64BIT_DEV) {
		if (!ZB_IEEE_ADDR_CMP(req->devAddr.addr.extAddr, g_zbInfo.macPib.coordExtAddress)) {
			cnf->status = MAC_STA_INVALID_PARAMETER;
			tl_zbPrimitivePost(TL_Q_MAC2NWK, MAC_MLME_DISASSOCIATE_CNF, arg);
			return;
		}
	} else {
		cnf->status = MAC_STA_INVALID_PARAMETER;
		tl_zbPrimitivePost(TL_Q_MAC2NWK, MAC_MLME_DISASSOCIATE_CNF, arg);
		return;
	}

	{
		u8 status;

#if defined(ZB_COORDINATOR_ROLE)
		status = tl_macMlmeDisassociateNotifyCmdSend(arg, req->txIndirect);
#else
		status = tl_macMlmeDisassociateNotifyCmdSend(arg, 0);
#endif

		if (status != MAC_SUCCESS) {
			cnf->status = status;
			tl_zbPrimitivePost(TL_Q_MAC2NWK, MAC_MLME_DISASSOCIATE_CNF, arg);
		}
	}
}

void tl_zbMlmeCmdDisassociateNotifyRecvd(void *arg, void *raw)
{
	zb_mlme_disassociate_ind_t *ind = (zb_mlme_disassociate_ind_t *)arg;
	tl_zb_mac_mhr_t *mhr = (tl_zb_mac_mhr_t *)raw;
	mac_phy_ind_meta_t *meta = (mac_phy_ind_meta_t *)arg;
	u8 *reasonSrc = meta->payload;

	ZB_IEEE_ADDR_COPY(ind->devAddress, mhr->srcAddr.extAddr);
	ind->disassociateReason = reasonSrc[1];

	tl_zbPrimitivePost(TL_Q_MAC2NWK, MAC_MLME_DISASSOCIATE_IND, arg);
}
