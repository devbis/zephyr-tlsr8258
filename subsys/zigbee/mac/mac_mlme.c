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

static inline u8 *phy_ind_raw_get(void *arg)
{
	return ((mac_phy_ind_meta_t *)arg)->payload;
}

typedef struct {
	u32 cmdId;
	void (*handler)(void *arg, void *raw);
} mac_mlme_phy_evt_t;

#if defined(ZB_ROUTER_ROLE) || defined(ZB_COORDINATOR_ROLE)
static void tl_zbMlmeCmdAssociateReqRecvd(void *arg, void *raw);
static void tl_zbMlmeCmdBeaconReqRecvd(void *arg, void *raw);
#endif
#if defined(ZB_COORDINATOR_ROLE)
static void tl_zbMlmeCmdPanIdConflictNotifyRecvd(void *arg, void *raw);
#endif
static void tl_zbMlmeCmdAssociateRespRecvd(void *arg, void *raw);
static void tl_zbMlmeCmdDataReqRecvd(void *arg, void *raw);
static void tl_zbMlmeCmdOrphanNotifyRecvd(void *arg, void *raw);
static void tl_zbMlmeCmdCoordRealignRecvd(void *arg, void *raw);

const mac_mlme_phy_evt_t g_zbMacMlmeEventFromPhyTbl[] = {
#if defined(ZB_ROUTER_ROLE) || defined(ZB_COORDINATOR_ROLE)
	{MAC_CMD_ASSOCIATION_REQUEST, tl_zbMlmeCmdAssociateReqRecvd},
	{MAC_CMD_BEACON_REQUEST, tl_zbMlmeCmdBeaconReqRecvd},
#endif
	{MAC_CMD_ASSOCIATION_RESPONSE, tl_zbMlmeCmdAssociateRespRecvd},
	{MAC_CMD_DISASSOCIATION_NOTIFICATION, tl_zbMlmeCmdDisassociateNotifyRecvd},
#if defined(ZB_COORDINATOR_ROLE)
	/* The coordinator entry sits here, not before the association response
	 * (_coordinator/mac_mlme.s:.rodata.g_zbMacMlmeEventFromPhyTbl). */
	{MAC_CMD_PAN_ID_CONFLICT_NOTIFICATION, tl_zbMlmeCmdPanIdConflictNotifyRecvd},
#endif
	{MAC_CMD_DATA_REQUEST, tl_zbMlmeCmdDataReqRecvd},
	{MAC_CMD_ORPHAN_NOTIFICATION, tl_zbMlmeCmdOrphanNotifyRecvd},
	{MAC_CMD_COORDINATOR_REALIGNMENT, tl_zbMlmeCmdCoordRealignRecvd},
};

static void tl_zbMlmeCmdCoordRealignRecvd(void *arg, void *raw)
{
	zb_buf_t *buf = (zb_buf_t *)arg;
	tl_zb_mac_mhr_t *mhr = (tl_zb_mac_mhr_t *)raw;
	u8 *payload = phy_ind_raw_get(arg);
	u16 panId = (u16)payload[1] | ((u16)payload[2] << 8);
	u16 shortAddr = (u16)payload[6] | ((u16)payload[7] << 8);

	if (panId != g_zbInfo.macPib.panId || payload[5] != g_zbInfo.macPib.phyChannelCur ||
	    shortAddr != g_zbInfo.macPib.shortAddress ||
	    g_zbMacCtx.status != ZB_MAC_STATE_ORPHAN_SCAN) {
		zb_buf_free(buf);
		return;
	}

	g_zbInfo.macPib.coordShortAddress = (u16)payload[3] | ((u16)payload[4] << 8);

	if (mhr->srcAddrMode == ADDR_MODE_EXT) {
		ZB_IEEE_ADDR_COPY(g_zbInfo.macPib.coordExtAddress, mhr->srcAddr.extAddr);
	} else {
		ZB_IEEE_ADDR_ZERO(g_zbInfo.macPib.coordExtAddress);
	}

	tl_zbMacOrphanScanStatusUpdate();
	zb_buf_free(buf);
}

static void tl_zbMlmeCmdDataReqRecvd(void *arg, void *raw)
{
	(void)raw;
#if defined(ZB_ROUTER_ROLE) || defined(ZB_COORDINATOR_ROLE)
	tl_zbMacMlmeDataRequestCb(arg);
#else
	zb_buf_free((zb_buf_t *)arg);
#endif
}

#if defined(ZB_ROUTER_ROLE) || defined(ZB_COORDINATOR_ROLE)
static void tl_zbMlmeCmdAssociateReqRecvd(void *arg, void *raw)
{
	zb_buf_t *buf = (zb_buf_t *)arg;
	tl_zb_mac_mhr_t *mhr = (tl_zb_mac_mhr_t *)raw;
	u8 *payload = phy_ind_raw_get(arg);
	zb_mlme_associate_ind_t *ind = (zb_mlme_associate_ind_t *)buf;

	ZB_IEEE_ADDR_COPY(ind->devAddress, mhr->srcAddr.extAddr);
	*(u8 *)&ind->capbilityInfo = payload[1];
	ind->lqi = ((mac_phy_ind_meta_t *)buf)->linkQuality;

	if (macAppIndCb == NULL || macAppIndCb->macAssociationReqRcvCb == NULL ||
	    macAppIndCb->macAssociationReqRcvCb(arg)) {
		tl_zbPrimitivePost(TL_Q_MAC2NWK, MAC_MLME_ASSOCIATE_IND, arg);
		return;
	}

	zb_buf_free(buf);
}

static void tl_zbMlmeCmdBeaconReqRecvd(void *arg, void *raw)
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
	tl_zbMacBeaconRequestCb();
	/* Beacon transmission uses the dedicated MAC TX buffer, not this RX
	 * indication buffer. Release it after the synchronous callback so
	 * repeated beacon requests cannot exhaust the Zephyr buffer slab. */
	zb_buf_free((zb_buf_t *)arg);
}
#endif

static void tl_zbMlmeCmdOrphanNotifyRecvd(void *arg, void *raw)
{
	zb_buf_t *buf = (zb_buf_t *)arg;
	tl_zb_mac_mhr_t *mhr = (tl_zb_mac_mhr_t *)raw;
	mac_mlme_orphan_ind_t *ind = (mac_mlme_orphan_ind_t *)buf;

	if (mhr->srcAddrMode != ADDR_MODE_EXT) {
		zb_buf_free(buf);
		return;
	}

	ZB_IEEE_ADDR_COPY(ind->orphanAddr, mhr->srcAddr.extAddr);
	tl_zbPrimitivePost(TL_Q_MAC2NWK, MAC_MLME_ORPHAN_IND, arg);
}

#if defined(ZB_COORDINATOR_ROLE)
static void tl_zbMlmeCmdPanIdConflictNotifyRecvd(void *arg, void *raw)
{
	zb_mlme_sync_loss_ind_t *ind = (zb_mlme_sync_loss_ind_t *)arg;

	(void)raw;

	ind->panId = g_zbInfo.macPib.panId;
	ind->reason = ZB_SYNC_LOSS_REASON_PAN_ID_CONFLICT;
	ind->logicalChannel = g_zbInfo.macPib.phyChannelCur;
	ind->channelPage = g_zbInfo.macPib.phyPageCur;
	tl_zbPrimitivePost(TL_Q_MAC2NWK, MAC_MLME_SYNC_LOSS_IND, arg);
}

void tl_zbMacCmdPanIdConflictNotifySendCheck(void *arg, u8 status)
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
	tl_zbPrimitivePost(TL_Q_MAC2NWK, MAC_MLME_SYNC_LOSS_IND, ind);
}
#endif

static void tl_zbMlmeCmdAssociateRespRecvd(void *arg, void *raw)
{
	zb_buf_t *buf = (zb_buf_t *)arg;
	tl_zb_mac_mhr_t *mhr = (tl_zb_mac_mhr_t *)raw;
	u8 *payload = phy_ind_raw_get(arg);
	zb_mlme_associate_conf_t *cnf = (zb_mlme_associate_conf_t *)buf;

	memset(buf, 0, sizeof(*cnf));

	if (mhr->srcAddrMode == ADDR_MODE_EXT) {
		ZB_IEEE_ADDR_COPY(cnf->parentAddress, mhr->srcAddr.extAddr);
	}

	if (g_zbMacCtx.status != ZB_MAC_STATE_INDIRECT_DATA || associationReqOrigBuffer == NULL) {
		zb_buf_free(buf);
		return;
	}

	tl_zbMacAssociateRespReceived();
	zb_buf_free((zb_buf_t *)associationReqOrigBuffer);
	associationReqOrigBuffer = NULL;

	memcpy(&cnf->shortAddress, payload + 1, sizeof(cnf->shortAddress));
	COPY_BUFFERTOU16(g_zbInfo.macPib.shortAddress, payload + 1);
	cnf->status = payload[3];

	tl_zbPrimitivePost(TL_Q_MAC2NWK, MAC_MLME_ASSOCIATE_CNF, arg);
}

void tl_zbPhyMlmeIndicate(void *arg, u8 *raw, u8 len)
{
	u8 *payload;
	u8 cmdId;

	(void)len;

	payload = phy_ind_raw_get(arg);
	cmdId = payload[0];

	if (g_zbMacCtx.status == ZB_MAC_STATE_INDIRECT_DATA &&
	    cmdId != MAC_CMD_ASSOCIATION_RESPONSE && cmdId != MAC_CMD_DATA_REQUEST) {
		zb_buf_free((zb_buf_t *)arg);
		return;
	}

	for (u8 i = 0; i < ARRAY_SIZE(g_zbMacMlmeEventFromPhyTbl); i++) {
		if (g_zbMacMlmeEventFromPhyTbl[i].cmdId == cmdId &&
		    g_zbMacMlmeEventFromPhyTbl[i].handler != NULL) {
			g_zbMacMlmeEventFromPhyTbl[i].handler(arg, raw);
			return;
		}
	}

	zb_buf_free((zb_buf_t *)arg);
}

void tl_zbMacPollRequestHandler(void *arg)
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
	tl_zbMacMlmeDataRequestCmdSend(&req, (zb_buf_t *)arg, MAC_STA_INVALID_PARAMETER);
}

void tl_zbMacResetRequestHandler(void *arg)
{
	((mac_mlme_reset_conf_t *)arg)->status = MAC_SUCCESS;
	tl_zbPrimitivePost(TL_Q_MAC2NWK, MAC_MLME_RESET_CNF, arg);
}

void tl_zbMacStartReqConfirm(void *arg, u8 status)
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
		tl_zbMacChannelSet(req->logicalChannel);
		rf_setTrxState(RF_STATE_RX);
	}

	cnf->status = status;
	tl_zbPrimitivePost(TL_Q_MAC2NWK, MAC_MLME_START_CNF, arg);
}

void tl_zbMacStartRequestHandler(void *arg)
{
	zb_mac_mlme_start_req_t *req = (zb_mac_mlme_start_req_t *)arg;
	u8 status = MAC_STA_INVALID_PARAMETER;

	if (req->beaconOrder <= 15U &&
	    (req->beaconOrder >= req->superframeOrder || req->superframeOrder == 15U)) {
		/* Follows vendor router behavior for coordinator-less start requests. */
		status = (g_zbInfo.macPib.shortAddress == MAC_SHORT_ADDR_NONE)
				 ? MAC_STA_NO_SHORT_ADDRESS
				 : MAC_SUCCESS;
	}

	if (g_zbNwkCtx.joined_pro) {
		tl_zbMacStartReqConfirm(arg, MAC_STA_INVALID_PARAMETER);
		return;
	}

	if (status != MAC_SUCCESS) {
		tl_zbMacStartReqConfirm(arg, status);
		return;
	}

	if (req->coordRealignment == 0U) {
		tl_zbMacStartReqConfirm(arg, MAC_SUCCESS);
		return;
	}

#if defined(ZB_ROUTER_ROLE) || defined(ZB_COORDINATOR_ROLE)
	((zb_buf_t *)arg)->hdr.handle = MAC_INTERNAL_START_REQUEST_HANDLE;
	status = tl_zbMacMlmeCoordRealignmentCmdSend(1, 0, 0, arg);
	if (status != MAC_SUCCESS) {
		tl_zbMacStartReqConfirm(arg, MAC_STA_CHANNEL_ACCESS_FAILURE);
	}
#else
	tl_zbMacStartReqConfirm(arg, MAC_SUCCESS);
#endif
}

void tl_zbMacCommStatusSend(void *arg, u8 status)
{
	tl_zb_mac_mhr_t mhr;
	mac_pending_tx_ctx_t *pendingCtx = (mac_pending_tx_ctx_t *)arg;
	zb_mlme_comm_status_ind_t *ind = (zb_mlme_comm_status_ind_t *)arg;
	u8 *raw = pendingCtx->txData;

	tl_zbMacHdrParse(&mhr, raw);

	memset(ind, 0, sizeof(*ind));
	ind->panId = mhr.dstPanId;
	ind->srcAddr.addrMode = mhr.srcAddrMode;
	if (mhr.srcAddrMode == ADDR_MODE_EXT) {
		ZB_IEEE_ADDR_COPY(ind->srcAddr.addr.extAddr, mhr.srcAddr.extAddr);
	} else if (mhr.srcAddrMode == ADDR_MODE_SHORT) {
		ind->srcAddr.addr.shortAddr = mhr.srcAddr.shortAddr;
	}
	ind->dstAddr.addrMode = mhr.dstAddrMode;
	if (mhr.dstAddrMode == ADDR_MODE_EXT) {
		ZB_IEEE_ADDR_COPY(ind->dstAddr.addr.extAddr, mhr.dstAddr.extAddr);
	} else if (mhr.dstAddrMode == ADDR_MODE_SHORT) {
		ind->dstAddr.addr.shortAddr = mhr.dstAddr.shortAddr;
	}
	ind->status = status;
	ind->isAssoc = TRUE;

	tl_zbPrimitivePost(TL_Q_MAC2NWK, MAC_MLME_COMM_STATUS_IND, arg);
}
