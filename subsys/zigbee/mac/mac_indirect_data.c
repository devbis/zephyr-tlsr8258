/*
 * Copyright 2026 libzigbee
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "zb_common.h"
#include "mac.h"
#include "mac_associate.h"
#include "mac_common.h"
#include "mac_data.h"
#include "mac_indirect_data.h"
#include "mac_trx.h"

#if defined(ZB_ROUTER_ROLE)
LIST(macPendingQueue)

enum {
	MAC_PENDING_STATE_QUEUED = 1,
	MAC_PENDING_STATE_READY,
	MAC_PENDING_STATE_SENT,
	MAC_PENDING_STATE_EXPIRED,
};

static inline int mac_pending_addr_matches(const mac_pending_entry_t *entry, u8 addrMode,
					   const u8 *addr)
{
	u8 len = (addrMode == ADDR_MODE_EXT) ? EXT_ADDR_LEN : 2;

	if (entry->buf == NULL) {
		return 0;
	}

	return entry->addrMode == addrMode && memcmp(addr, entry->addr, len) == 0;
}

void macDataPendingListProc(void *arg)
{
	mac_pending_entry_t *entry = (mac_pending_entry_t *)arg;
	void *buf;
	u8 status;

	if (entry == NULL) {
		return;
	}

	if (entry->state != MAC_PENDING_STATE_EXPIRED) {
		status = entry->status;
		/* "3a: tcmp r1,#0; 3c: tjeq 12" - a transaction that already
		 * succeeded is confirmed here as well, not put back on the queue. */
		if (status != MAC_SUCCESS && status != MAC_STA_FRAME_PENDING) {
			entry->state = MAC_PENDING_STATE_QUEUED;
			return;
		}
	} else {
		status = entry->status;
	}

	buf = entry->buf;
	tl_zbMaxTxConfirmCb(buf, status);
	listRemove(macPendingQueue, entry);
	ev_buf_free((u8 *)entry);
}

u8 macDataPending(void *buf, u32 dstAddrLo, u32 dstAddrHi, u8 dstAddrMode)
{
	mac_pending_entry_t *entry;
	u8 status = MAC_STA_TRANSACTION_OVERFLOW;
	u8 match[9];

	if (listLength(macPendingQueue) >= ZB_MAC_PENDING_TRANS_QUEUE_SIZE) {
		return status;
	}

	entry = (mac_pending_entry_t *)ev_buf_allocate(sizeof(*entry));
	if (entry == NULL) {
		return status;
	}

	memset(entry, 0, sizeof(*entry));
	listAdd(macPendingQueue, entry);
	entry->buf = buf;

	match[0] = (u8)dstAddrLo;
	match[1] = (u8)(dstAddrLo >> 8);
	match[2] = (u8)(dstAddrLo >> 16);
	match[3] = (u8)(dstAddrLo >> 24);
	match[4] = (u8)dstAddrHi;
	match[5] = (u8)(dstAddrHi >> 8);
	match[6] = (u8)(dstAddrHi >> 16);
	match[7] = (u8)(dstAddrHi >> 24);
	match[8] = dstAddrMode;
	memcpy(entry->addr, match, sizeof(entry->addr));
	entry->addrMode = match[8];

	entry->timeout = (u8)(((((u32)g_zbInfo.macPib.transactionPersistenceTime << 4) -
				g_zbInfo.macPib.transactionPersistenceTime)
			       << 10) /
			      1000000U);
	entry->expiry = ZB_MAC_EXT_EXPEIRY_CNT;
	entry->state = MAC_PENDING_STATE_QUEUED;
	status = MAC_SUCCESS;

	return status;
}

void macDataPendingListManage(void *arg, u8 status)
{
	mac_pending_entry_t *entry = (mac_pending_entry_t *)arg;

	entry->status = status;
	tl_zbTaskPost(macDataPendingListProc, entry);
}

_attribute_ram_code_ u8 tl_zbMacPendingDataCheck(u8 addrMode, u8 *addr, u8 update)
{
	mac_pending_entry_t *entry;

	if (listLength(macPendingQueue) == 0) {
		return MAC_STA_OUT_OF_CAP;
	}

	for (entry = (mac_pending_entry_t *)listHead(macPendingQueue); entry != NULL;
	     entry = entry->next) {
		if (!mac_pending_addr_matches(entry, addrMode, addr)) {
			continue;
		}

		if (!update) {
			return MAC_STA_OUT_OF_CAP;
		}

		if (entry->state != MAC_PENDING_STATE_QUEUED &&
		    entry->state != MAC_PENDING_STATE_READY) {
			return MAC_STA_OUT_OF_CAP;
		}

		entry->state = MAC_PENDING_STATE_READY;
		return MAC_SUCCESS;
	}

	return MAC_STA_OUT_OF_CAP;
}

_attribute_no_inline_ int tl_zbMacPendingDataSearch(u8 addrMode, u8 *addr)
{
	mac_pending_entry_t *entry;
	u8 count = 0;

	if (listLength(macPendingQueue) == 0) {
		return (s8)count; /* Vendor returns the signed 8-bit count through the int ABI. */
	}

	for (entry = (mac_pending_entry_t *)listHead(macPendingQueue); entry != NULL;
	     entry = entry->next) {
		if (!mac_pending_addr_matches(entry, addrMode, addr)) {
			continue;
		}

		count++;
	}

	return (s8)count;
}

void tl_zbMacMlmeDataRequestCb(void *arg)
{
	u8 *req = (u8 *)arg;
	u8 addrMode = req[18];
	u8 *addr = req + 10;
	s8 pendingCnt = (s8)tl_zbMacPendingDataSearch(addrMode, addr);
	mac_pending_entry_t *entry;

	if (listLength(macPendingQueue) == 0) {
		goto post_poll_ind;
	}

	for (entry = (mac_pending_entry_t *)listHead(macPendingQueue); entry != NULL;
	     entry = entry->next) {
		zb_buf_t *buf;
		u8 *txData;
		u8 txStatus;

		if (entry->state != MAC_PENDING_STATE_READY ||
		    !mac_pending_addr_matches(entry, addrMode, addr)) {
			continue;
		}

		buf = (zb_buf_t *)entry->buf;
		txData = ((mac_pending_tx_ctx_t *)buf)->txData;
		if (pendingCnt > 1) {
			txData[0] |= MAC_FCF_FRAME_PENDING_MASK;
		}

		txStatus = tl_zbMacTx(buf, txData, ((mac_pending_tx_ctx_t *)buf)->psduLen,
				      txData[0] & MAC_FCF_ACK_REQ_BIT, entry);
		entry->state = (txStatus == MAC_SUCCESS) ? MAC_PENDING_STATE_SENT
							 : MAC_PENDING_STATE_QUEUED;
		break;
	}

post_poll_ind:
	if (listLength(macPendingQueue) == 0) {
		u8 saved[9];

		/* Preserve the vendor stack/code shape even though req[0] does not alias addr. */
		memcpy(saved, addr, sizeof(saved));
		req[0] = addrMode;
		ZB_IEEE_ADDR_COPY(req + 1, saved);
		tl_zbPrimitivePost(TL_Q_MAC2NWK, MAC_MLME_POLL_IND, req);
		return;
	}

	req[0] = addrMode;
	ZB_IEEE_ADDR_COPY(req + 1, addr);
	tl_zbPrimitivePost(TL_Q_MAC2NWK, MAC_MLME_POLL_IND, req);
}

int macIndirPeriodic(void *arg)
{
	mac_pending_entry_t *entry;

	(void)arg;

	if (listLength(macPendingQueue) == 0) {
		return 0;
	}

	for (entry = (mac_pending_entry_t *)listHead(macPendingQueue); entry != NULL;
	     entry = entry->next) {
		if (entry->timeout != 0U) {
			entry->timeout--;
			continue;
		}

		if (entry->expiry != 0U) {
			entry->expiry--;
			continue;
		}

		if ((u8)(entry->state - MAC_PENDING_STATE_SENT) <= 1U) {
			entry->state = MAC_PENDING_STATE_EXPIRED;
			return 0;
		}

		if (entry->buf != NULL) {
			tl_zbMaxTxConfirmCb(entry->buf, MAC_STA_TRANSACTION_EXPIRED);
		}
		listRemove(macPendingQueue, entry);
		ev_buf_free((u8 *)entry);
		return 0;
	}

	return 0;
}
#endif

void tl_zbMacDataRequestStatusCheck(zb_buf_t *buf, u8 status)
{
	u8 savedStatus = buf->hdr.handle;

	if (savedStatus == MAC_STA_NO_ACK) {
		zb_buf_free(buf);
		tl_zbMacAssocPollConfirm(status);
		return;
	}

	if (savedStatus == MAC_STA_INVALID_PARAMETER) {
		if (status == MAC_SUCCESS) {
			status = MAC_STA_NO_DATA;
		}

		((mac_mlme_poll_conf_t *)buf)->status = status;
		tl_zbPrimitivePost(TL_Q_MAC2NWK, MAC_MLME_POLL_CNF, buf);
		return;
	}

	zb_buf_free(buf);
}

u8 tl_zbMacMlmeDataRequestCmdSend(zb_mlme_data_req_cmd_t *req, zb_buf_t *buf, u8 status)
{
	tl_zb_mac_mhr_t mhr;
	u8 hdrSize;
	u8 *payload;
	u8 *frameStart;
	u8 txStatus;

	memset(&mhr, 0, sizeof(mhr));
	mhr.dstPanId = g_zbMacPib.panId;
	memcpy(&mhr.dstAddr, &req->dstAddr, sizeof(mhr.dstAddr));
	memcpy(&mhr.srcAddr, &req->srcAddr, sizeof(mhr.srcAddr));
	mhr.frameCtrl = MAC_FRAME_TYPE_COMMAND | MAC_FCF_ACK_REQUEST_MASK | MAC_FCF_INTRA_PAN_MASK |
			((u16)req->dstAddrMode << MAC_FCF_DST_ADDR_MODE_POS) |
			((u16)req->srcAddrMode << MAC_FCF_SRC_ADDR_MODE_POS);

	hdrSize = (u8)(tl_zbMacHdrSize(mhr.frameCtrl) + 1U);
	TL_BUF_INITIAL_ALLOC(buf, hdrSize, payload, u8 *);
	/* "74: tadds r5,r0,#0 ... 8a: tadds r1,r5,#0" - the queued frame is the
	 * allocation, not the pointer the header builder returns. */
	frameStart = payload;
	payload = tl_zbMacHdrBuilder(payload, &mhr);
	payload[0] = MAC_CMD_DATA_REQUEST;

	buf->hdr.handle = status;

	txStatus = tl_zbMacTx(buf, frameStart, hdrSize, 1, NULL);
	if (txStatus != MAC_SUCCESS) {
		tl_zbMacDataRequestStatusCheck(buf, txStatus);
	}

	return txStatus;
}
