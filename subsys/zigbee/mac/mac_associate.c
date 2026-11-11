/*
 * Copyright 2026 libzigbee
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <zephyr/logging/log.h>

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

LOG_MODULE_REGISTER(zigbee_mac_associate, CONFIG_ZIGBEE_LOG_LEVEL);

static ev_timer_event_t *assocRspTimeoutEvt;
void *associationReqOrigBuffer = NULL;

static inline void mac_associate_confirm_post(void *arg, uint8_t status)
{
	zb_mlme_associate_conf_t *cnf = (zb_mlme_associate_conf_t *)arg;

	memset(cnf, 0, sizeof(*cnf));
	cnf->status = status;
	zb_PrimitivePost(ZB_Q_MAC2NWK, MAC_MLME_ASSOCIATE_CNF, arg);
}

static inline uint32_t assoc_timeout_ms(void)
{
	uint32_t base = g_zbInfo.macPib.respWaitTime;
	return ((((base << 4) - base) << 10) / 1000U);
}

static int zb_WaitForAssociationRespTimeout(void *arg)
{
	LOG_WRN("no association response from the parent");
	mac_associate_confirm_post(arg, MAC_STA_NO_DATA);

	associationReqOrigBuffer = NULL;
	g_zbMacCtx.status = ZB_MAC_STATE_NORMAL;
	assocRspTimeoutEvt = NULL;

	return -1;
}

static int zb_ReadyToPullParentForAssoRsp(void *arg)
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

			zb_MacMlmeDataRequestCmdSend(&dataReq, buf, MAC_STA_NO_ACK);
		}
	}

	return -1;
}

void zb_MacAssocPollConfirm(uint8_t status)
{
	uint8_t *req = (uint8_t *)associationReqOrigBuffer;

	if (req == NULL) {
		return;
	}

	if (status == MAC_STA_FRAME_PENDING) {
		g_zbMacCtx.status = ZB_MAC_STATE_INDIRECT_DATA;
		if (assocRspTimeoutEvt == NULL) {
			assocRspTimeoutEvt = ev_timer_taskPost(zb_WaitForAssociationRespTimeout,
							       req, assoc_timeout_ms());
		}
		return;
	}

	if (status == MAC_SUCCESS) {
		if (assocRspTimeoutEvt == NULL) {
			assocRspTimeoutEvt = ev_timer_taskPost(zb_WaitForAssociationRespTimeout,
							       req, assoc_timeout_ms());
		}
		return;
	}

	LOG_WRN("association response poll failed: status %u", status);
	mac_associate_confirm_post(req, status);
	associationReqOrigBuffer = NULL;
	g_zbMacCtx.status = ZB_MAC_STATE_NORMAL;
}

void zb_MacAssociateRespReceived(void)
{
	LOG_DBG("association response received");
	mac_pendingWaitTimerCancel();

	if (assocRspTimeoutEvt != NULL) {
		ev_timer_taskCancel(&assocRspTimeoutEvt);
	}

	g_zbMacCtx.status = ZB_MAC_STATE_NORMAL;
}

void zb_MacAssociateRequestStatusCheck(void *arg, uint8_t status)
{
	(void)arg;

	if (associationReqOrigBuffer == NULL) {
		return;
	}

	if (status != MAC_SUCCESS && status != MAC_STA_FRAME_PENDING) {
		LOG_WRN("association request not delivered: status %u", status);
		mac_associate_confirm_post(associationReqOrigBuffer, status);
		associationReqOrigBuffer = NULL;
		return;
	}

	ev_timer_taskPost(zb_ReadyToPullParentForAssoRsp, NULL, assoc_timeout_ms());
}

void zb_MacAssociateRequestHandler(void *arg)
{
	uint8_t *req = (uint8_t *)arg;
	zb_mlme_associate_req_t *assocReq = (zb_mlme_associate_req_t *)arg;
	zb_buf_t *txBuf = (zb_buf_t *)g_zbMacCtx.txRawDataBuf;

	if (txBuf == NULL || txBuf->hdr.active != 0U || associationReqOrigBuffer != NULL) {
		LOG_WRN("association request rejected: transmitter busy");
		mac_associate_confirm_post(arg, MAC_STA_TX_ACTIVE);
		associationReqOrigBuffer = NULL;
		return;
	}

	txBuf->hdr.active = 1;
	associationReqOrigBuffer = arg;
	g_zbMacCtx.curChannel = req[0];
	rf_setChannel(req[0]);

	{
		zb_mac_mhr_t mhr;
		uint8_t *payload;
		uint8_t *frameStart;
		uint8_t hdrSize;
		uint8_t txStatus;

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
				((uint16_t)mhr.dstAddrMode << MAC_FCF_DST_ADDR_MODE_POS) |
				((uint16_t)ADDR_MODE_EXT << MAC_FCF_SRC_ADDR_MODE_POS);

		hdrSize = (uint8_t)(zb_MacHdrSize(mhr.frameCtrl) + 2U);
		ZB_BUF_INITIAL_ALLOC(txBuf, hdrSize, payload, uint8_t *);
		txBuf->hdr.handle = MAC_INTERNAL_ASSOCIATE_REQUEST_HANDLE;
		/* zb_MacTx() takes the frame start, not the builder's return. */
		frameStart = payload;
		payload = zb_MacHdrBuilder(payload, &mhr);
		payload[0] = MAC_CMD_ASSOCIATION_REQUEST;
		payload[1] = *(uint8_t *)&assocReq->capbilityInfo;

		txStatus = zb_MacTx(txBuf, frameStart, hdrSize, 1, NULL);
		if (txStatus != MAC_SUCCESS) {
			mac_associate_confirm_post(arg, MAC_STA_TX_ACTIVE);
			associationReqOrigBuffer = NULL;
		}
	}
}

#if defined(ZB_ROUTER_ROLE)
void zb_MacAssociateResponseHandler(void *arg)
{
	uint8_t *req = (uint8_t *)arg;
	zb_mac_mhr_t mhr;
	uint8_t pendingAddr[9]; /* address mode, then the extended address */
	uint32_t pendingAddrLow;
	uint32_t pendingAddrHigh;
	mac_pending_tx_ctx_t *pendingCtx;
	uint8_t *frameStart;
	uint8_t *payload;
	uint8_t hdrSize;
	uint8_t status;

	if (g_zbNwkCtx.joined_pro) {
		zb_buf_free((zb_buf_t *)arg);
		return;
	}

	memset(&mhr, 0, sizeof(mhr));
	mhr.frameCtrl = MAC_FRAME_TYPE_COMMAND | MAC_FCF_ACK_REQUEST_MASK | MAC_FCF_INTRA_PAN_MASK |
			((uint16_t)ADDR_MODE_EXT << MAC_FCF_DST_ADDR_MODE_POS) |
			((uint16_t)ADDR_MODE_EXT << MAC_FCF_SRC_ADDR_MODE_POS);
	mhr.dstPanId = g_zbInfo.macPib.panId;
	mhr.srcPanId = g_zbInfo.macPib.panId;
	mhr.dstAddrMode = ADDR_MODE_EXT;
	mhr.srcAddrMode = ADDR_MODE_EXT;
	ZB_IEEE_ADDR_COPY(mhr.dstAddr.extAddr, req + 2);
	ZB_IEEE_ADDR_COPY(mhr.srcAddr.extAddr, g_zbInfo.macPib.extAddress);

	hdrSize = (uint8_t)(zb_MacHdrSize(mhr.frameCtrl) + 4U);
	ZB_BUF_INITIAL_ALLOC((zb_buf_t *)arg, hdrSize, payload, uint8_t *);
	/* Keep the start of the allocation: the pending context holds the
	 * complete frame, whose frame control byte the pending-state update
	 * later rewrites. */
	frameStart = payload;
	payload = zb_MacHdrBuilder(payload, &mhr);
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
		zb_MacCommStatusSend(arg, status);
	}
}
#endif

void zb_MacDisassociateNotifyCmdConfirm(void *arg, uint8_t status)
{
	zb_mlme_disassociate_conf_t *cnf = (zb_mlme_disassociate_conf_t *)arg;

	cnf->status = status;
	zb_PrimitivePost(ZB_Q_MAC2NWK, MAC_MLME_DISASSOCIATE_CNF, arg);
}

uint8_t zb_macMlmeDisassociateNotifyCmdSend(void *arg, uint8_t txIndirect)
{
	zb_mlme_disassociate_req_t *req = (zb_mlme_disassociate_req_t *)arg;
	zb_mac_mhr_t mhr;
	zb_buf_t *txBuf = (zb_buf_t *)arg;
	uint8_t *payload;
	uint8_t *frameStart;
	uint8_t hdrSize;
	uint8_t status;
#if defined(ZB_COORDINATOR_ROLE)
	uint8_t pendingAddrMode;
	uint32_t pendingAddr[2];
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
			((uint16_t)req->devAddr.addrMode << MAC_FCF_DST_ADDR_MODE_POS) |
			((uint16_t)ADDR_MODE_EXT << MAC_FCF_SRC_ADDR_MODE_POS);

	hdrSize = (uint8_t)(zb_MacHdrSize(mhr.frameCtrl) + 2U);
	ZB_BUF_INITIAL_ALLOC(txBuf, hdrSize, payload, uint8_t *);
	frameStart = payload;
	payload = zb_MacHdrBuilder(payload, &mhr);
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
		status = zb_MacTx(txBuf, frameStart, hdrSize, 1, NULL);
	}
	if (status != MAC_SUCCESS) {
		return MAC_STA_CHANNEL_ACCESS_FAILURE;
	}

	return MAC_SUCCESS;
}

void zb_MacDisassociateRequestHandler(void *arg)
{
	zb_mlme_disassociate_req_t *req = (zb_mlme_disassociate_req_t *)arg;
	zb_mlme_disassociate_conf_t *cnf = (zb_mlme_disassociate_conf_t *)arg;
	uint16_t localPanId = g_zbInfo.macPib.panId;

	if (req->devPanId != localPanId) {
		cnf->status = MAC_STA_INVALID_PARAMETER;
		zb_PrimitivePost(ZB_Q_MAC2NWK, MAC_MLME_DISASSOCIATE_CNF, arg);
		return;
	}

#if !defined(ZB_COORDINATOR_ROLE)
	if (req->devAddr.addrMode == ZB_ADDR_16BIT_DEV_OR_BROADCAST) {
		uint16_t parentShort = g_zbInfo.macPib.coordShortAddress;

		if (req->devAddr.addr.shortAddr != parentShort) {
			cnf->status = MAC_STA_INVALID_PARAMETER;
			zb_PrimitivePost(ZB_Q_MAC2NWK, MAC_MLME_DISASSOCIATE_CNF, arg);
			return;
		}
	} else if (req->devAddr.addrMode == ZB_ADDR_64BIT_DEV) {
		if (!ZB_IEEE_ADDR_CMP(req->devAddr.addr.extAddr, g_zbInfo.macPib.coordExtAddress)) {
			cnf->status = MAC_STA_INVALID_PARAMETER;
			zb_PrimitivePost(ZB_Q_MAC2NWK, MAC_MLME_DISASSOCIATE_CNF, arg);
			return;
		}
	} else {
		cnf->status = MAC_STA_INVALID_PARAMETER;
		zb_PrimitivePost(ZB_Q_MAC2NWK, MAC_MLME_DISASSOCIATE_CNF, arg);
		return;
	}
#endif

	{
		uint8_t status;

#if defined(ZB_COORDINATOR_ROLE)
		status = zb_macMlmeDisassociateNotifyCmdSend(arg, req->txIndirect);
#else
		status = zb_macMlmeDisassociateNotifyCmdSend(arg, 0);
#endif

		if (status != MAC_SUCCESS) {
			cnf->status = MAC_STA_INVALID_PARAMETER;
			zb_PrimitivePost(ZB_Q_MAC2NWK, MAC_MLME_DISASSOCIATE_CNF, arg);
		}
	}
}

void zb_MlmeCmdDisassociateNotifyRecvd(void *arg, void *raw)
{
	zb_mlme_disassociate_ind_t *ind = (zb_mlme_disassociate_ind_t *)arg;
	zb_mac_mhr_t *mhr = (zb_mac_mhr_t *)raw;
	mac_phy_ind_meta_t *meta = (mac_phy_ind_meta_t *)arg;
	uint8_t *reasonSrc = meta->payload;

	ZB_IEEE_ADDR_COPY(ind->devAddress, mhr->srcAddr.extAddr);
	ind->disassociateReason = reasonSrc[1];

	zb_PrimitivePost(ZB_Q_MAC2NWK, MAC_MLME_DISASSOCIATE_IND, arg);
}
