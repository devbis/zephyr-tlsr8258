/*
 * Copyright 2026 libzigbee
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "zb_common.h"
#include "mac.h"
#include "mac_associate.h"
#include "mac_common.h"
#include "mac_cr_coordinator.h"
#include "mac_data.h"
#include "mac_indirect_data.h"
#include "mac_mlme.h"
#include "mac_scan.h"

static inline uint8_t *phy_ind_raw_get(void *arg)
{
	return ((mac_phy_ind_meta_t *)arg)->payload;
}

typedef struct {
	uint32_t cmdId;
	void (*handler)(void *arg, void *raw);
} mac_mlme_phy_evt_t;

#if defined(ZB_ROUTER_ROLE) || defined(ZB_COORDINATOR_ROLE)
static void zb_MlmeCmdAssociateReqRecvd(void *arg, void *raw);
static void zb_MlmeCmdBeaconReqRecvd(void *arg, void *raw);
#endif
#if defined(ZB_COORDINATOR_ROLE)
static void zb_MlmeCmdPanIdConflictNotifyRecvd(void *arg, void *raw);
#endif
static void zb_MlmeCmdAssociateRespRecvd(void *arg, void *raw);
static void zb_MlmeCmdDataReqRecvd(void *arg, void *raw);
static void zb_MlmeCmdOrphanNotifyRecvd(void *arg, void *raw);
static void zb_MlmeCmdCoordRealignRecvd(void *arg, void *raw);

const mac_mlme_phy_evt_t g_zbMacMlmeEventFromPhyTbl[] = {
#if defined(ZB_ROUTER_ROLE) || defined(ZB_COORDINATOR_ROLE)
	{MAC_CMD_ASSOCIATION_REQUEST, zb_MlmeCmdAssociateReqRecvd},
	{MAC_CMD_BEACON_REQUEST, zb_MlmeCmdBeaconReqRecvd},
#endif
	{MAC_CMD_ASSOCIATION_RESPONSE, zb_MlmeCmdAssociateRespRecvd},
	{MAC_CMD_DISASSOCIATION_NOTIFICATION, zb_MlmeCmdDisassociateNotifyRecvd},
#if defined(ZB_COORDINATOR_ROLE)
	/* The coordinator entry sits here, not before the association response
	 * (_coordinator/mac_mlme.s:.rodata.g_zbMacMlmeEventFromPhyTbl). */
	{MAC_CMD_PAN_ID_CONFLICT_NOTIFICATION, zb_MlmeCmdPanIdConflictNotifyRecvd},
#endif
	{MAC_CMD_DATA_REQUEST, zb_MlmeCmdDataReqRecvd},
	{MAC_CMD_ORPHAN_NOTIFICATION, zb_MlmeCmdOrphanNotifyRecvd},
	{MAC_CMD_COORDINATOR_REALIGNMENT, zb_MlmeCmdCoordRealignRecvd},
};

static void zb_MlmeCmdCoordRealignRecvd(void *arg, void *raw)
{
	zb_buf_t *buf = (zb_buf_t *)arg;
	zb_mac_mhr_t *mhr = (zb_mac_mhr_t *)raw;
	uint8_t *payload = phy_ind_raw_get(arg);
	uint16_t panId = (uint16_t)payload[1] | ((uint16_t)payload[2] << 8);
	uint16_t shortAddr = (uint16_t)payload[6] | ((uint16_t)payload[7] << 8);

	if (panId != g_zbInfo.macPib.panId || payload[5] != g_zbInfo.macPib.phyChannelCur ||
	    shortAddr != g_zbInfo.macPib.shortAddress ||
	    g_zbMacCtx.status != ZB_MAC_STATE_ORPHAN_SCAN) {
		zb_buf_free(buf);
		return;
	}

	g_zbInfo.macPib.coordShortAddress = (uint16_t)payload[3] | ((uint16_t)payload[4] << 8);

	if (mhr->srcAddrMode == ADDR_MODE_EXT) {
		ZB_IEEE_ADDR_COPY(g_zbInfo.macPib.coordExtAddress, mhr->srcAddr.extAddr);
	} else {
		ZB_IEEE_ADDR_ZERO(g_zbInfo.macPib.coordExtAddress);
	}

	zb_MacOrphanScanStatusUpdate();
	zb_buf_free(buf);
}

static void zb_MlmeCmdDataReqRecvd(void *arg, void *raw)
{
	(void)raw;
#if defined(ZB_ROUTER_ROLE) || defined(ZB_COORDINATOR_ROLE)
	zb_MacMlmeDataRequestCb(arg);
#else
	zb_buf_free((zb_buf_t *)arg);
#endif
}

#if defined(ZB_ROUTER_ROLE) || defined(ZB_COORDINATOR_ROLE)
static void zb_MlmeCmdAssociateReqRecvd(void *arg, void *raw)
{
	zb_buf_t *buf = (zb_buf_t *)arg;
	zb_mac_mhr_t *mhr = (zb_mac_mhr_t *)raw;
	uint8_t *payload = phy_ind_raw_get(arg);
	zb_mlme_associate_ind_t *ind = (zb_mlme_associate_ind_t *)buf;

	ZB_IEEE_ADDR_COPY(ind->devAddress, mhr->srcAddr.extAddr);
	*(uint8_t *)&ind->capbilityInfo = payload[1];
	ind->lqi = ((mac_phy_ind_meta_t *)buf)->linkQuality;

	if (macAppIndCb == NULL || macAppIndCb->macAssociationReqRcvCb == NULL ||
	    macAppIndCb->macAssociationReqRcvCb(arg)) {
		zb_PrimitivePost(ZB_Q_MAC2NWK, MAC_MLME_ASSOCIATE_IND, arg);
		return;
	}

	zb_buf_free(buf);
}

static void zb_MlmeCmdBeaconReqRecvd(void *arg, void *raw)
{
	(void)raw;

	if (!g_zbNwkCtx.joined || g_zbNwkCtx.joined_pro) {
		zb_buf_free((zb_buf_t *)arg);
		return;
	}

	if (((mac_phy_ind_meta_t *)arg)->linkQuality < NWK_NEIGHBORTBL_ADD_LQITHRESHOLD) {
		zb_buf_free((zb_buf_t *)arg);
		return;
	}

	g_zbMacCtx.beaconTriesNum = 3;
	zb_MacBeaconRequestCb();
	/* Beacon transmission uses the dedicated MAC TX buffer, not this RX
	 * indication buffer. Release it after the synchronous callback so
	 * repeated beacon requests cannot exhaust the Zephyr buffer slab. */
	zb_buf_free((zb_buf_t *)arg);
}
#endif

static void zb_MlmeCmdOrphanNotifyRecvd(void *arg, void *raw)
{
	zb_buf_t *buf = (zb_buf_t *)arg;
	zb_mac_mhr_t *mhr = (zb_mac_mhr_t *)raw;
	mac_mlme_orphan_ind_t *ind = (mac_mlme_orphan_ind_t *)buf;

	if (mhr->srcAddrMode != ADDR_MODE_EXT) {
		zb_buf_free(buf);
		return;
	}

	ZB_IEEE_ADDR_COPY(ind->orphanAddr, mhr->srcAddr.extAddr);
	zb_PrimitivePost(ZB_Q_MAC2NWK, MAC_MLME_ORPHAN_IND, arg);
}

#if defined(ZB_COORDINATOR_ROLE)
static void zb_MlmeCmdPanIdConflictNotifyRecvd(void *arg, void *raw)
{
	zb_mlme_sync_loss_ind_t *ind = (zb_mlme_sync_loss_ind_t *)arg;

	(void)raw;

	ind->panId = g_zbInfo.macPib.panId;
	ind->reason = ZB_SYNC_LOSS_REASON_PAN_ID_CONFLICT;
	ind->logicalChannel = g_zbInfo.macPib.phyChannelCur;
	ind->channelPage = g_zbInfo.macPib.phyPageCur;
	zb_PrimitivePost(ZB_Q_MAC2NWK, MAC_MLME_SYNC_LOSS_IND, arg);
}

void zb_MacCmdPanIdConflictNotifySendCheck(void *arg, uint8_t status)
{
	zb_mlme_sync_loss_ind_t *ind;

	(void)arg;

	if (status != MAC_SUCCESS) {
		return;
	}

	ind = (zb_mlme_sync_loss_ind_t *)zb_buf_allocate();
	if (ind == NULL) {
		return;
	}

	ind->panId = g_zbInfo.macPib.panId;
	ind->reason = ZB_SYNC_LOSS_REASON_PAN_ID_CONFLICT;
	ind->logicalChannel = g_zbInfo.macPib.phyChannelCur;
	ind->channelPage = g_zbInfo.macPib.phyPageCur;
	zb_PrimitivePost(ZB_Q_MAC2NWK, MAC_MLME_SYNC_LOSS_IND, ind);
}
#endif

static void zb_MlmeCmdAssociateRespRecvd(void *arg, void *raw)
{
	zb_buf_t *buf = (zb_buf_t *)arg;
	zb_mac_mhr_t *mhr = (zb_mac_mhr_t *)raw;
	uint8_t *payload = phy_ind_raw_get(arg);
	zb_mlme_associate_conf_t *cnf = (zb_mlme_associate_conf_t *)buf;

	memset(buf, 0, sizeof(*cnf));

	if (mhr->srcAddrMode == ADDR_MODE_EXT) {
		ZB_IEEE_ADDR_COPY(cnf->parentAddress, mhr->srcAddr.extAddr);
	}

	/*
	 * The vendor also requires the indirect-data wait here, which its TX
	 * interrupt enters as soon as the poll's ACK reports a pending frame.
	 * On this port the poll completion is handled later on the Zigbee
	 * thread, and the response, which follows the ACK within a few
	 * milliseconds, can be parsed first. The outstanding request is what
	 * ties the response to this device.
	 */
	if (associationReqOrigBuffer == NULL) {
		zb_buf_free(buf);
		return;
	}

	zb_MacAssociateRespReceived();
	zb_buf_free((zb_buf_t *)associationReqOrigBuffer);
	associationReqOrigBuffer = NULL;

	memcpy(&cnf->shortAddress, payload + 1, sizeof(cnf->shortAddress));
	COPY_BUFFERTOU16(g_zbInfo.macPib.shortAddress, payload + 1);
	cnf->status = payload[3];

	zb_PrimitivePost(ZB_Q_MAC2NWK, MAC_MLME_ASSOCIATE_CNF, arg);
}

void zb_PhyMlmeIndicate(void *arg, uint8_t *raw, uint8_t len)
{
	uint8_t *payload;
	uint8_t cmdId;

	(void)len;

	payload = phy_ind_raw_get(arg);
	cmdId = payload[0];

	if (g_zbMacCtx.status == ZB_MAC_STATE_INDIRECT_DATA &&
	    cmdId != MAC_CMD_ASSOCIATION_RESPONSE) {
		zb_buf_free((zb_buf_t *)arg);
		return;
	}

	for (uint8_t i = 0; i < ARRAY_SIZE(g_zbMacMlmeEventFromPhyTbl); i++) {
		if (g_zbMacMlmeEventFromPhyTbl[i].cmdId == cmdId &&
		    g_zbMacMlmeEventFromPhyTbl[i].handler != NULL) {
			g_zbMacMlmeEventFromPhyTbl[i].handler(arg, raw);
			return;
		}
	}

	zb_buf_free((zb_buf_t *)arg);
}

void zb_MacPollRequestHandler(void *arg)
{
	mac_mlme_poll_req_t *pollReq = (mac_mlme_poll_req_t *)arg;
	zb_mlme_data_req_cmd_t req;

	memset(&req, 0, sizeof(req));

	if (g_zbInfo.macPib.shortAddress <= 0xfffdU) {
		req.srcAddrMode = ADDR_MODE_SHORT;
		req.srcAddr.shortAddr = g_zbInfo.macPib.shortAddress;
	} else {
		req.srcAddrMode = ADDR_MODE_EXT;
		ZB_IEEE_ADDR_COPY(req.srcAddr.extAddr, g_zbInfo.macPib.extAddress);
	}

	req.dstAddrMode = pollReq->coordAddrMode;
	if (pollReq->coordAddrMode == ADDR_MODE_EXT) {
		ZB_IEEE_ADDR_COPY(req.dstAddr.extAddr, pollReq->coordAddr.extAddr);
	} else {
		req.dstAddr.shortAddr = pollReq->coordAddr.shortAddr;
	}

	req.cbType = MAC_POLL_REQUEST_CALLBACK;
	zb_MacMlmeDataRequestCmdSend(&req, (zb_buf_t *)arg, MAC_STA_INVALID_PARAMETER);
}

void zb_MacResetRequestHandler(void *arg)
{
	((mac_mlme_reset_conf_t *)arg)->status = MAC_SUCCESS;
	zb_PrimitivePost(ZB_Q_MAC2NWK, MAC_MLME_RESET_CNF, arg);
}

void zb_MacStartReqConfirm(void *arg, uint8_t status)
{
	zb_mac_mlme_start_req_t *req = (zb_mac_mlme_start_req_t *)arg;
	mac_mlme_startCnf_t *cnf = (mac_mlme_startCnf_t *)arg;

	if (status == MAC_SUCCESS) {
		g_zbInfo.macPib.beaconOrder = req->beaconOrder;
		g_zbInfo.macPib.superframeOrder =
			(req->beaconOrder == 15U) ? 15U : req->superframeOrder;
		g_zbInfo.macPib.panId = req->panId;
		g_zbInfo.macPib.phyPageCur = req->channelPage;
		g_zbInfo.macPib.phyChannelCur = req->logicalChannel;
		zb_MacChannelSet(req->logicalChannel);
		rf_setTrxState(RF_STATE_RX);
	}

	cnf->status = status;
	zb_PrimitivePost(ZB_Q_MAC2NWK, MAC_MLME_START_CNF, arg);
}

void zb_MacStartRequestHandler(void *arg)
{
	zb_mac_mlme_start_req_t *req = (zb_mac_mlme_start_req_t *)arg;
	uint8_t status = MAC_STA_INVALID_PARAMETER;

	if (req->beaconOrder <= 15U &&
	    (req->beaconOrder >= req->superframeOrder || req->superframeOrder == 15U)) {
		/* Follows vendor router behavior for coordinator-less start requests. */
		status = (g_zbInfo.macPib.shortAddress == MAC_SHORT_ADDR_NONE)
				 ? MAC_STA_NO_SHORT_ADDRESS
				 : MAC_SUCCESS;
	}

	if (g_zbNwkCtx.joined_pro) {
		zb_MacStartReqConfirm(arg, MAC_STA_INVALID_PARAMETER);
		return;
	}

	if (status != MAC_SUCCESS) {
		zb_MacStartReqConfirm(arg, status);
		return;
	}

	if (req->coordRealignment == 0U) {
		zb_MacStartReqConfirm(arg, MAC_SUCCESS);
		return;
	}

#if defined(ZB_ROUTER_ROLE) || defined(ZB_COORDINATOR_ROLE)
	((zb_buf_t *)arg)->hdr.handle = MAC_INTERNAL_START_REQUEST_HANDLE;
	status = zb_MacMlmeCoordRealignmentCmdSend(1, 0, 0, arg);
	if (status != MAC_SUCCESS) {
		zb_MacStartReqConfirm(arg, MAC_STA_CHANNEL_ACCESS_FAILURE);
	}
#else
	zb_MacStartReqConfirm(arg, MAC_SUCCESS);
#endif
}

void zb_MacCommStatusSend(void *arg, uint8_t status)
{
	zb_mac_mhr_t mhr;
	mac_pending_tx_ctx_t *pendingCtx = (mac_pending_tx_ctx_t *)arg;
	zb_mlme_comm_status_ind_t *ind = (zb_mlme_comm_status_ind_t *)arg;
	uint8_t *raw = pendingCtx->txData;

	zb_MacHdrParse(&mhr, raw);

	ZB_IEEE_ADDR_COPY(ind->srcAddr.addr.extAddr, mhr.srcAddr.extAddr);
	ind->srcAddr.addrMode = mhr.srcAddrMode;
	ZB_IEEE_ADDR_COPY(ind->dstAddr.addr.extAddr, mhr.dstAddr.extAddr);
	ind->dstAddr.addrMode = mhr.dstAddrMode;
	ind->status = status;
	ind->isAssoc = TRUE;

	zb_PrimitivePost(ZB_Q_MAC2NWK, MAC_MLME_COMM_STATUS_IND, arg);
}
