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
 * "ba:".."d8:" advances only the payload pointer.  The recorded length still
 * covers the MAC header, because tl_zbPhyMldeIndication() and
 * tl_zbPhyMlmeIndicate() subtract the header length themselves ("2e: tloadrb
 * r3,[r0,#19]; 30: tsubs r2,r3,r2").  Only the beacon path shortens the stored
 * length, at "84:".
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
	enum {
		BEACON_NOTIFY_SIZE = sizeof(zb_mlme_beacon_notify_ind_t),
		BEACON_FIXED_OVERHEAD = 4,
		ZIGBEE_BEACON_PAYLOAD_LEN = 11,
	};

	u8 *safe;
	u8 shortPendingNum;
	u8 extPendingNum;
	u8 addrListLen;
	u8 beaconPayloadOffset;
	zb_mlme_beacon_notify_ind_t ind;

	if (payloadLen <= BEACON_FIXED_OVERHEAD || payload[0] != 0xffU ||
	    (payload[1] & 0x0fU) != 0x0fU || payload[2] != 0U) {
		return FALSE;
	}

	shortPendingNum = payload[3] & 0x07U;
	extPendingNum = (payload[3] >> 4) & 0x07U;
	addrListLen = (u8)(shortPendingNum * 2U + extPendingNum * 8U);
	beaconPayloadOffset = (u8)(BEACON_FIXED_OVERHEAD + addrListLen);

	if (payloadLen < (u8)(beaconPayloadOffset + ZIGBEE_BEACON_PAYLOAD_LEN)) {
		return FALSE;
	}

	safe = (u8 *)buf + BEACON_NOTIFY_SIZE;
	memcpy(safe, payload, payloadLen);

	memset(&ind, 0, sizeof(ind));
	memcpy(&ind.panDesc.timestamp, buf, sizeof(ind.panDesc.timestamp));
	ind.panDesc.coordPanId = mhr->srcPanId;
	ind.panDesc.superframeSpec = (u16)safe[0] | ((u16)safe[1] << 8);
	ind.panDesc.coordAddr.addrMode = mhr->srcAddrMode;
	if (mhr->srcAddrMode == ADDR_MODE_EXT) {
		ZB_IEEE_ADDR_COPY(ind.panDesc.coordAddr.addr.extAddr, mhr->srcAddr.extAddr);
	} else {
		ind.panDesc.coordAddr.addr.shortAddr = mhr->srcAddr.shortAddr;
	}
	ind.panDesc.logicalChannel = ((mac_phy_ind_meta_t *)buf)->curChannel;
	ind.panDesc.gtsPermit = safe[2];
	ind.panDesc.linkQuality = ((mac_phy_ind_meta_t *)buf)->linkQuality;
	ind.pAddrList = (addrListLen != 0U) ? (safe + BEACON_FIXED_OVERHEAD) : NULL;
	ind.psdu = safe + beaconPayloadOffset;
	ind.bsn = mhr->seqNum;
	ind.pendAddrSpec = safe[3];
	ind.psduLength = (u8)(payloadLen - beaconPayloadOffset);

	memcpy(buf, &ind, sizeof(ind));
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

	if (arg == NULL) {
		return;
	}

	if (raw == NULL || len == 0U) {
		zb_buf_free(buf);
		return;
	}

	macPld = meta->payload;
	frameType = macPld[0] & 0x07U;

	if (mhr->dstAddrMode == ADDR_MODE_SHORT &&
	    mhr->dstAddr.shortAddr == MAC_SHORT_ADDR_BROADCAST) {
		g_sysDiags.macRxBcast++;
	} else if (mhr->dstAddrMode == ADDR_MODE_SHORT) {
		g_sysDiags.macRxUcast++;
	}

	if (macAppIndCb != NULL) {
		if (frameType == MAC_FRAME_TYPE_BEACON && macAppIndCb->macBeaconRcvCb != NULL &&
		    !macAppIndCb->macBeaconRcvCb(arg)) {
			zb_buf_free(buf);
			return;
		}

		if (frameType == MAC_FRAME_TYPE_COMMAND && macAppIndCb->macBeaconReqRcvCb != NULL &&
		    macPld[len] == MAC_CMD_BEACON_REQUEST && !macAppIndCb->macBeaconReqRcvCb(arg)) {
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
		g_zbMacPib.frameTotalWaitTime = (u16)(totalWait * 296UL);
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

#if defined(ZB_ROUTER_ROLE)
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

	/* Both handles return here in every role: the scan test at
	 * "2e: tcmp r3,#234" and the beacon-request test at "32: tcmp r3,#228"
	 * branch to the same early return at 0x42 in _router/mac.s, and
	 * _coordinator/mac.s and _ed/mac.s do the same.
	 */
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
