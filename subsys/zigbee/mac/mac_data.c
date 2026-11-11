/*
 * Copyright 2026 libzigbee
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "zb_common.h"
#include "mac.h"
#include "mac_data.h"
#include "mac_indirect_data.h"
#include "mac_common.h"
#include "mac_trx.h"

void zb_MacMcpsDataRequestSendConfirm(zb_buf_t *buf, uint8_t status)
{
	zb_mscp_data_req_t *req = (zb_mscp_data_req_t *)buf;
	zb_mscp_data_conf_t *cnf = (zb_mscp_data_conf_t *)buf;
	uint16_t macDstAddr = MAC_ADDR_USE_EXT;
	uint8_t handle = req->msduHandle;

	if (req->dstAddr.addrMode == ADDR_MODE_SHORT) {
		macDstAddr = req->dstAddr.addr.shortAddr;
	}

	buf->hdr.macTxFifo = 0;

#if defined(ZB_ROUTER_ROLE)
	if (req->dstAddr.addrMode != ADDR_MODE_SHORT && handle >= GP_HANDLE_MIN &&
	    handle <= GP_HANDLE_TUNNELED_GPD_CMD) {
		cgp_data_cnf_t *gpCnf = (cgp_data_cnf_t *)buf;

		gpCnf->status = status;
		gpCnf->gpMpduHandle = handle;
		zb_TaskPost(cGp_dataCnf, buf);
		return;
	}
#endif

	cnf->msdu = req->msdu;
	cnf->msduHandle = handle;
	cnf->status = status;
	cnf->macDstAddr = macDstAddr;
	cnf->rssi = buf->hdr.rssi;
	cnf->lqi = rf_getLqi(cnf->rssi);

	zb_PrimitivePost(ZB_Q_MAC2NWK, MAC_MCPS_DATA_CNF, buf);
}

void zb_MacMcpsDataRequestProc(void *arg)
{
	zb_buf_t *buf = (zb_buf_t *)arg;
	zb_mscp_data_req_t *req;
	zb_mac_mhr_t mhr;
	uint8_t hdrSize;
	uint8_t psduLen;
	uint8_t *txData;
	uint8_t txStatus;

	/* Caller guarantees buf is valid; the vendor path omits a local null guard. */
	req = (zb_mscp_data_req_t *)buf;

	if (req->dstAddr.addrMode == ADDR_MODE_SHORT) {
		if (req->dstAddr.addr.shortAddr == MAC_SHORT_ADDR_BROADCAST) {
			g_sysDiags.macTxBcast++;
		} else {
			g_sysDiags.macTxUcast++;
		}
	}

	memset(&mhr, 0, sizeof(mhr));
	mhr.dstPanId = req->dstPanId;
	mhr.srcPanId = g_zbMacPib.panId;
	memcpy(&mhr.dstAddr, &req->dstAddr.addr, sizeof(mhr.dstAddr));
	/* The vendor MAC builder selects the local source from the MAC PIB. */
	if (req->srcAddr.addrMode == ADDR_MODE_SHORT) {
		mhr.srcAddr.shortAddr = g_zbMacPib.shortAddress;
	} else if (req->srcAddr.addrMode == ADDR_MODE_EXT) {
		ZB_IEEE_ADDR_COPY(mhr.srcAddr.extAddr, g_zbMacPib.extAddress);
	}

	mhr.frameCtrl = MAC_FRAME_TYPE_DATA |
			((uint16_t)(req->txOptions & MAC_TX_OPTION_ACKNOWLEDGED_BIT)
			 << MAC_FCF_ACK_REQUEST_POS) |
			((uint16_t)req->dstAddr.addrMode << MAC_FCF_DST_ADDR_MODE_POS) |
			((uint16_t)req->srcAddr.addrMode << MAC_FCF_SRC_ADDR_MODE_POS);

	if ((req->srcAddr.addrMode != ADDR_MODE_NONE) &&
	    (req->dstAddr.addrMode != ADDR_MODE_NONE) && (req->dstPanId == g_zbMacPib.panId)) {
		mhr.frameCtrl |= MAC_FCF_INTRA_PAN_MASK;
	}

	hdrSize = zb_MacHdrSize(mhr.frameCtrl);
	buf->hdr.handle = req->msduHandle;
	psduLen = (uint8_t)(hdrSize + req->msduLength);
	txData = req->msdu - hdrSize;
	/* The builder's return value is discarded: the pending context and
	 * zb_MacTx() both want the complete frame, header first. */
	(void)zb_MacHdrBuilder(txData, &mhr);

	buf->hdr.macTxFifo = 1;
#if defined(ZB_ROUTER_ROLE)
	if ((req->txOptions & MAC_TX_OPTION_INDIRECT_TRANSMISSION_BIT) != 0U) {
		uint32_t pendingAddr[2];
		mac_pending_tx_ctx_t *ctx = (mac_pending_tx_ctx_t *)buf;

		ctx->txData = txData;
		ctx->psduLen = psduLen;
		ZB_IEEE_ADDR_COPY(pendingAddr, &req->dstAddr.addr);

		txStatus =
			macDataPending(buf, pendingAddr[0], pendingAddr[1], req->dstAddr.addrMode);
	} else
#endif
	{
		txStatus = zb_MacTx(buf, txData, psduLen,
				      (mhr.frameCtrl & MAC_FCF_ACK_REQ_BIT) ? 1U : 0U, NULL);
	}

	if (txStatus != MAC_SUCCESS) {
		zb_MacMcpsDataRequestSendConfirm(buf, txStatus);
	}
}

void zb_PhyMldeIndication(zb_buf_t *buf, uint8_t *raw, uint8_t len)
{
	mac_phy_ind_meta_t meta = *(mac_phy_ind_meta_t *)buf;
	zb_mscp_data_ind_t *ind = (zb_mscp_data_ind_t *)buf;
	zb_mac_mhr_t *mhr = (zb_mac_mhr_t *)raw;

	ind->msdu = meta.payload;
	ind->msduLength = (uint8_t)(meta.payloadLen - len);
	ind->dstAddr.addrMode = mhr->dstAddrMode;
	memcpy(&ind->dstAddr.addr, &mhr->dstAddr, sizeof(ind->dstAddr.addr));
	ind->dstPanId = mhr->dstPanId;
	ind->srcAddr.addrMode = mhr->srcAddrMode;
	memcpy(&ind->srcAddr.addr, &mhr->srcAddr, sizeof(ind->srcAddr.addr));
	if (mhr->panIdMode == 0U) {
		ind->srcPanId = mhr->srcPanId;
	}
	ind->dsn = mhr->seqNum;
	ind->mpduLinkQuality = meta.linkQuality;

	if (g_zbMacPib.rxOnWhenIdle == 0U && (mhr->frameCtrl & MAC_FCF_FRAME_PENDING_MASK) != 0U) {
		buf->hdr.pending = 1;
	}

	zb_PrimitivePost(ZB_Q_MAC2NWK, MAC_MCPS_DATA_IND, buf);
}
