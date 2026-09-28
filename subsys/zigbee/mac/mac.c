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
#include "mac_trx.h"

mac_appIndCb_t *macAppIndCb = NULL;
tl_zb_mac_ctx_t g_zbMacCtx;

/*
 * Advances only the payload pointer.  The recorded length still covers the
 * MAC header, because tl_zbPhyMldeIndication() and tl_zbPhyMlmeIndicate()
 * subtract the header length themselves.  Only the beacon path shortens the
 * stored length.
 */
static inline u8 *phy_ind_payload_advance(void *arg, u8 hdrLen)
{
	mac_phy_ind_meta_t *meta = (mac_phy_ind_meta_t *)arg;
	u8 *payload = meta->payload + hdrLen;

	meta->payload = payload;
	return payload;
}

typedef struct {
	u32 primitive;
	tl_zb_callback_t handler;
} mac_nwk_evt_t;

static bool phy_ind_beacon_notify_post(zb_buf_t *buf, tl_zb_mac_mhr_t *mhr, u8 *payload,
				       u8 payloadLen)
{
	zb_mlme_beacon_notify_ind_t *ind = (zb_mlme_beacon_notify_ind_t *)buf;
	u8 linkQuality = ((mac_phy_ind_meta_t *)buf)->linkQuality;
	u8 curChannel = ((mac_phy_ind_meta_t *)buf)->curChannel;
	u8 beaconPayloadOffset;
	u8 copyOffset;
	u8 *safe;

	if (payload[0] != MAC_SF_OCTET0_NON_BEACON ||
	    (payload[1] & MAC_SF_FINAL_CAP_SLOT_MASK) != MAC_SF_FINAL_CAP_SLOT_LAST ||
	    payload[2] != 0U || payloadLen <= MAC_BEACON_HDR_LEN) {
		return FALSE;
	}

	beaconPayloadOffset =
		(u8)(MAC_BEACON_HDR_LEN +
		     (payload[3] & MAC_BEACON_PEND_COUNT_MASK) * MAC_BEACON_PEND_SHORT_LEN +
		     ((payload[3] >> MAC_BEACON_PEND_EXT_SHIFT) & MAC_BEACON_PEND_COUNT_MASK) *
			     MAC_BEACON_PEND_EXT_LEN);

	/* A Zigbee beacon payload is 15 octets. */
	if (payloadLen < beaconPayloadOffset + MAC_ZB_BEACON_PAYLOAD_LEN) {
		return FALSE;
	}

	/*
	 * The indication is written in place over the receive metadata; the
	 * beacon frame is first moved out of its way, inside the buffer.
	 */
	copyOffset = (payloadLen > (MAC_BEACON_IND_COPY_MIN - 1U)) ? payloadLen
								   : MAC_BEACON_IND_COPY_MIN;
	if ((u32)(payload - (u8 *)buf) + payloadLen + copyOffset > MAC_RX_BUF_PAYLOAD_LIMIT) {
		return FALSE;
	}

	safe = payload + copyOffset;
	memcpy(safe, payload, payloadLen);
	memcpy(&ind->panDesc.superframeSpec, safe, 2);
	ind->panDesc.gtsPermit = safe[2];
	if (safe[2] != 0U) {
		return FALSE;
	}
	if ((((u8 *)&ind->panDesc.superframeSpec)[0] & MAC_SF_BEACON_ORDER_MASK) !=
	    MAC_SF_BEACON_ORDER_NONE) {
		return FALSE;
	}

	memcpy(&ind->panDesc.coordPanId, &mhr->srcPanId, 2);
	ind->panDesc.coordAddr.addrMode = mhr->srcAddrMode;
	ind->panDesc.linkQuality = linkQuality;
	ind->panDesc.logicalChannel = curChannel;
	memcpy(&ind->panDesc.coordAddr.addr, &mhr->srcAddr, 8);
	ind->bsn = mhr->seqNum;
	ind->pAddrList = NULL;
	ind->psduLength = MAC_ZB_BEACON_PAYLOAD_LEN;
	ind->psdu = safe + beaconPayloadOffset;

	tl_zbPrimitivePost(TL_Q_MAC2NWK, MAC_MLME_BEACON_NOTIFY_IND, buf);

	if (g_zbMacCtx.status == ZB_MAC_STATE_ACTIVE_SCAN) {
		tl_zbMacActiveScanListAdd();
	}

	return TRUE;
}

/* Entry order follows the vendor object; the end-device build simply drops the
 * two coordinator responses ("_ed/mac.s:.rodata.g_zbMacEventFromNwkTbl"). */
const mac_nwk_evt_t g_zbMacEventFromNwkTbl[] = {
	{MAC_MLME_ASSOCIATE_REQ, tl_zbMacAssociateRequestHandler},
#if defined(ZB_ROUTER_ROLE)
	{MAC_MLME_ASSOCIATE_RES, tl_zbMacAssociateResponseHandler},
	{MAC_MLME_ORPHAN_RES, tl_zbMacOrphanResponseHandler},
#endif
	{MAC_MLME_POLL_REQ, tl_zbMacPollRequestHandler},
	{MAC_MLME_RESET_REQ, tl_zbMacResetRequestHandler},
	{MAC_MLME_SCAN_REQ, (tl_zb_callback_t)tl_zbMacScanRequestHandler},
	{MAC_MLME_START_REQ, tl_zbMacStartRequestHandler},
	{MAC_MLME_DISASSOCIATE_REQ, tl_zbMacDisassociateRequestHandler},
};

void tl_zbPhyIndication(void *arg, u8 *raw, u8 len)
{
	zb_buf_t *buf = (zb_buf_t *)arg;
	tl_zb_mac_mhr_t *mhr = (tl_zb_mac_mhr_t *)raw;
	mac_phy_ind_meta_t *meta = (mac_phy_ind_meta_t *)arg;
	u8 *macPld;
	u8 frameType;

	macPld = meta->payload;
	frameType = macPld[0] & MAC_FCF_FRAME_TYPE_FIELD_MASK;

	if (mhr->dstAddrMode == ADDR_MODE_SHORT &&
	    mhr->dstAddr.shortAddr == MAC_SHORT_ADDR_BROADCAST) {
		g_sysDiags.macRxBcast++;
	} else if (mhr->dstAddrMode == ADDR_MODE_SHORT) {
		g_sysDiags.macRxUcast++;
	}

	if (macAppIndCb != NULL) {
		if (frameType == MAC_FRAME_TYPE_BEACON && macAppIndCb->macBeaconRcvCb != NULL &&
		    !macAppIndCb->macBeaconRcvCb(macPld)) {
			zb_buf_free(buf);
			return;
		}

		if (frameType == MAC_FRAME_TYPE_COMMAND && macAppIndCb->macBeaconReqRcvCb != NULL &&
		    macPld[len] == MAC_CMD_BEACON_REQUEST &&
		    !macAppIndCb->macBeaconReqRcvCb(macPld)) {
			zb_buf_free(buf);
			return;
		}
	}

	macPld = phy_ind_payload_advance(arg, len);

	if (frameType == MAC_FRAME_TYPE_DATA) {
		tl_zbPhyMldeIndication(buf, raw, len);
		return;
	}

	if (frameType == MAC_FRAME_TYPE_COMMAND) {
		tl_zbPhyMlmeIndicate(buf, raw, len);
		return;
	}

	if (frameType != MAC_FRAME_TYPE_BEACON) {
		zb_buf_free(buf);
		return;
	}

	meta->payloadLen = (u8)(meta->payloadLen - len);

	if (g_zbMacPib.autoReq != 0U) {
		if (g_zbMacCtx.status == ZB_MAC_STATE_ACTIVE_SCAN) {
			tl_zbMacActiveScanListAdd();
		}
		zb_buf_free(buf);
		return;
	}

	if (!phy_ind_beacon_notify_post(buf, mhr, macPld, meta->payloadLen)) {
		zb_buf_free(buf);
	}
}

void tl_zbMacChannelSet(u8 chan)
{
	g_zbMacCtx.curChannel = chan;
	rf_setChannel(chan);
}

void mac_pibNvInit(u8 coldReset)
{
	if (!coldReset) {
		tl_zbMacChannelSet(g_zbMacPib.phyChannelCur);
		return;
	}

	memcpy(&g_zbMacPib, &macPibDefault, sizeof(g_zbMacPib));
	generateIEEEAddr();
	g_zbMacPib.seqNum = (u8)drv_u32Rand();
	g_zbMacPib.beaconSeqNum = (u8)drv_u32Rand();

	if (g_zbMacPib.maxBe < g_zbMacPib.minBe) {
		g_zbMacPib.maxBe = g_zbMacPib.minBe;
	}
	{
		u8 backoffCount = (u8)(g_zbMacPib.maxBe - g_zbMacPib.minBe);
		u32 totalWait = 0;

		if (backoffCount > g_zbMacPib.maxCsmaBackoffs) {
			backoffCount = g_zbMacPib.maxCsmaBackoffs;
		}

		for (u8 i = 0; i < backoffCount; i++) {
			totalWait += 1UL << (g_zbMacPib.minBe + i);
		}

		totalWait += ((1UL << g_zbMacPib.maxBe) - 1UL) *
				     (g_zbMacPib.maxCsmaBackoffs - backoffCount) +
			     1UL;
		g_zbMacPib.frameTotalWaitTime = (u16)(totalWait * MAC_TOTAL_WAIT_UNIT_US);
	}

	tl_zbMacChannelSet(g_zbMacPib.phyChannelCur);
}

void tl_zbMacReset(void)
{
	mac_pibNvInit(1);
	g_zbMacPib.associationPermit = 0;
}

void tl_zbMacInit(u8 coldReset)
{
	mac_pibNvInit(coldReset);
	mac_trxInit();
	g_zbMacPib.associationPermit = 0;
	g_zbMacCtx.txRawDataBuf = (u8 *)zb_buf_allocate();
}

void tl_zbMaxTxConfirmCb(void *arg, u8 status)
{
	zb_buf_t *buf = (zb_buf_t *)arg;
	u8 handle;

	if (buf == (zb_buf_t *)g_zbMacCtx.txRawDataBuf) {
		buf->hdr.active = 0;
	}

	handle = buf->hdr.handle;
	if (handle == MAC_INTERNAL_ASSOCIATE_REQUEST_HANDLE) {
		tl_zbMacAssociateRequestStatusCheck(arg, status);
		return;
	}

#if !defined(ZB_ED_ROLE)
	if (handle == MAC_INTERNAL_ASSOCIATE_RESPONSE_HANDLE) {
		tl_zbMacCommStatusSend(arg, status);
		return;
	}
#endif

	if (handle == MAC_INTERNAL_DISASSOCIATE_NOTIFICATION_HANDLE) {
		tl_zbMacDisassociateNotifyCmdConfirm(arg, status);
		return;
	}

	if (handle == MAC_INTERNAL_DATA_REQUEST_HANDLE ||
	    handle == MAC_INTERNAL_MLME_DATA_REQUEST_HANDLE) {
		tl_zbMacDataRequestStatusCheck(buf, status);
		return;
	}

	if (handle == MAC_INTERNAL_START_REQUEST_HANDLE) {
		tl_zbMacStartReqConfirm(arg, status);
		return;
	}

#if defined(ZB_ROUTER_ROLE) || defined(ZB_COORDINATOR_ROLE)
	if (handle == MAC_INTERNAL_BEACON_SEND_HANDLE) {
		tl_zbMacMlmeBeaconSendConfirm(arg, status);
		return;
	}

	if (handle == MAC_INTERNAL_ORPHAN_RESPONSE_HANDLE) {
		tl_zbMacOrphanResponseStatusCheck(arg, status);
		return;
	}
#endif

#if defined(ZB_COORDINATOR_ROLE)
	if (handle == MAC_INTERNAL_PANID_CONFLICT_HANDLE) {
		tl_zbMacCmdPanIdConflictNotifySendCheck(arg, status);
		return;
	}
#endif

	/* Scan and beacon-request handles return early in every role. */
	if (handle == MAC_INTERNAL_SCAN_HANDLE || handle == MAC_INTERNAL_BEACON_REQUEST_HANDLE) {
		return;
	}

	tl_zbMacMcpsDataRequestSendConfirm(buf, status);
}

void tl_zbMacTaskProc(void)
{
	tl_zb_task_t taskInfo;

	if ((tl_zbTaskQPop(TL_Q_NWK2MAC, &taskInfo) != NULL) && (taskInfo.data != NULL)) {
		u8 primitive = ((zb_buf_t *)taskInfo.data)->hdr.id;

		for (u8 i = 0; i < ARRAY_SIZE(g_zbMacEventFromNwkTbl); i++) {
			if ((g_zbMacEventFromNwkTbl[i].primitive == primitive) &&
			    (g_zbMacEventFromNwkTbl[i].handler != NULL)) {
				g_zbMacEventFromNwkTbl[i].handler(taskInfo.data);
				break;
			}
		}
	}

	zb_macTimerEventProc(NULL);
}

void mac_appIndCbRegister(mac_appIndCb_t *cb)
{
	macAppIndCb = cb;
}
