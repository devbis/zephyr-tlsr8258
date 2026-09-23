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

void tl_zbMacMcpsDataRequestSendConfirm(zb_buf_t *buf, u8 status)
{
	zb_mscp_data_req_t *req = (zb_mscp_data_req_t *)buf;
	zb_mscp_data_conf_t *cnf = (zb_mscp_data_conf_t *)buf;
	u16 macDstAddr = MAC_ADDR_USE_EXT;
	u8 handle = req->msduHandle;

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
		tl_zbTaskPost(cGp_dataCnf, buf);
		return;
	}
#endif

	cnf->msdu = req->msdu;
	cnf->msduHandle = handle;
	cnf->status = status;
	cnf->macDstAddr = macDstAddr;
	cnf->rssi = buf->hdr.rssi;
	cnf->lqi = rf_getLqi(cnf->rssi);

	tl_zbPrimitivePost(TL_Q_MAC2NWK, MAC_MCPS_DATA_CNF, buf);
}

void tl_zbMacMcpsDataRequestProc(void *arg)
{
	zb_buf_t *buf = (zb_buf_t *)arg;
	zb_mscp_data_req_t *req;
	tl_zb_mac_mhr_t mhr;
	u8 hdrSize;
	u8 psduLen;
	u8 *txData;
	u8 txStatus;

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
			((u16)(req->txOptions & MAC_TX_OPTION_ACKNOWLEDGED_BIT)
			 << MAC_FCF_ACK_REQUEST_POS) |
			((u16)req->dstAddr.addrMode << MAC_FCF_DST_ADDR_MODE_POS) |
			((u16)req->srcAddr.addrMode << MAC_FCF_SRC_ADDR_MODE_POS);

	if ((req->srcAddr.addrMode != ADDR_MODE_NONE) &&
	    (req->dstAddr.addrMode != ADDR_MODE_NONE) && (req->dstPanId == g_zbMacPib.panId)) {
		mhr.frameCtrl |= MAC_FCF_INTRA_PAN_MASK;
	}

	hdrSize = tl_zbMacHdrSize(mhr.frameCtrl);
	buf->hdr.handle = req->msduHandle;
	psduLen = (u8)(hdrSize + req->msduLength);
	txData = req->msdu - hdrSize;
	/* "b2: tsubs r6,r6,r0" keeps the frame start in r6 and the builder's
	 * return value is discarded: the pending context and tl_zbMacTx() both
	 * want the complete frame, header first. */
	(void)tl_zbMacHdrBuilder(txData, &mhr);

	buf->hdr.macTxFifo = 1;
#if defined(ZB_ROUTER_ROLE)
	if ((req->txOptions & MAC_TX_OPTION_INDIRECT_TRANSMISSION_BIT) != 0U) {
		u32 pendingAddr[2];
		mac_pending_tx_ctx_t *ctx = (mac_pending_tx_ctx_t *)buf;

		ctx->txData = txData;
		ctx->psduLen = psduLen;
		ZB_IEEE_ADDR_COPY(pendingAddr, &req->dstAddr.addr);

		txStatus =
			macDataPending(buf, pendingAddr[0], pendingAddr[1], req->dstAddr.addrMode);
	} else
#endif
	{
		txStatus = tl_zbMacTx(buf, txData, psduLen,
				      (mhr.frameCtrl & MAC_FCF_ACK_REQ_BIT) ? 1U : 0U, NULL);
	}

	if (txStatus != MAC_SUCCESS) {
		tl_zbMacMcpsDataRequestSendConfirm(buf, txStatus);
	}
}

void tl_zbPhyMldeIndication(zb_buf_t *buf, u8 *raw, u8 len)
{
	mac_phy_ind_meta_t meta = *(mac_phy_ind_meta_t *)buf;
	zb_mscp_data_ind_t *ind = (zb_mscp_data_ind_t *)buf;
	tl_zb_mac_mhr_t *mhr = (tl_zb_mac_mhr_t *)raw;

	ind->msdu = meta.payload;
	ind->msduLength = (u8)(meta.payloadLen - len);
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

	tl_zbPrimitivePost(TL_Q_MAC2NWK, MAC_MCPS_DATA_IND, buf);
}
