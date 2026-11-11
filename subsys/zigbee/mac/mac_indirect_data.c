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

static inline int mac_pending_addr_matches(const mac_pending_entry_t *entry, uint8_t addrMode,
					   const uint8_t *addr)
{
	uint8_t len = (addrMode == ADDR_MODE_EXT) ? EXT_ADDR_LEN : 2;

	if (entry->buf == NULL) {
		return 0;
	}

	return entry->addrMode == addrMode && memcmp(addr, entry->addr, len) == 0;
}

void macDataPendingListProc(void *arg)
{
	mac_pending_entry_t *entry = (mac_pending_entry_t *)arg;
	void *buf;
	uint8_t status;

	if (entry == NULL) {
		return;
	}

	if (entry->state != MAC_PENDING_STATE_EXPIRED) {
		status = entry->status;
		/* A transaction that already
		 * succeeded is confirmed here as well, not put back on the queue. */
		if (status != MAC_SUCCESS && status != MAC_STA_FRAME_PENDING) {
			entry->state = MAC_PENDING_STATE_QUEUED;
			return;
		}
	} else {
		status = entry->status;
	}

	buf = entry->buf;
	zb_MaxTxConfirmCb(buf, status);
	listRemove(macPendingQueue, entry);
	ev_buf_free((uint8_t *)entry);
}

uint8_t macDataPending(void *buf, uint32_t dstAddrLo, uint32_t dstAddrHi, uint8_t dstAddrMode)
{
	mac_pending_entry_t *entry;
	uint8_t status = MAC_STA_TRANSACTION_OVERFLOW;
	uint8_t match[9]; /* extended address bytes 0-7, then the address mode */

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

	match[0] = (uint8_t)dstAddrLo;
	match[1] = (uint8_t)(dstAddrLo >> 8);
	match[2] = (uint8_t)(dstAddrLo >> 16);
	match[3] = (uint8_t)(dstAddrLo >> 24);
	match[4] = (uint8_t)dstAddrHi;
	match[5] = (uint8_t)(dstAddrHi >> 8);
	match[6] = (uint8_t)(dstAddrHi >> 16);
	match[7] = (uint8_t)(dstAddrHi >> 24);
	match[8] = dstAddrMode;
	memcpy(entry->addr, match, sizeof(entry->addr));
	entry->addrMode = match[8];

	entry->timeout = (uint8_t)(((((uint32_t)g_zbInfo.macPib.transactionPersistenceTime << 4) -
				g_zbInfo.macPib.transactionPersistenceTime)
			       << 10) /
			      1000000U);
	entry->expiry = ZB_MAC_EXT_EXPEIRY_CNT;
	entry->state = MAC_PENDING_STATE_QUEUED;
	status = MAC_SUCCESS;

	return status;
}

void macDataPendingListManage(void *arg, uint8_t status)
{
	mac_pending_entry_t *entry = (mac_pending_entry_t *)arg;

	entry->status = status;
	zb_TaskPost(macDataPendingListProc, entry);
}

_attribute_ram_code_ uint8_t zb_MacPendingDataCheck(uint8_t addrMode, uint8_t *addr, uint8_t update)
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

_attribute_no_inline_ int zb_MacPendingDataSearch(uint8_t addrMode, uint8_t *addr)
{
	mac_pending_entry_t *entry;
	uint8_t count = 0;

	if (listLength(macPendingQueue) == 0) {
		return (int8_t)count; /* Vendor returns the signed 8-bit count through the int ABI. */
	}

	for (entry = (mac_pending_entry_t *)listHead(macPendingQueue); entry != NULL;
	     entry = entry->next) {
		if (!mac_pending_addr_matches(entry, addrMode, addr)) {
			continue;
		}

		count++;
	}

	return (int8_t)count;
}

void zb_MacMlmeDataRequestCb(void *arg)
{
	mac_phy_ind_meta_t *meta = (mac_phy_ind_meta_t *)arg;
	uint8_t *req = (uint8_t *)arg;
	uint8_t addrMode = meta->srcAddrMode;
	uint8_t *addr = meta->srcAddr;
	int8_t pendingCnt = (int8_t)zb_MacPendingDataSearch(addrMode, addr);
	mac_pending_entry_t *entry;

	if (listLength(macPendingQueue) == 0) {
		goto post_poll_ind;
	}

	for (entry = (mac_pending_entry_t *)listHead(macPendingQueue); entry != NULL;
	     entry = entry->next) {
		zb_buf_t *buf;
		uint8_t *txData;
		uint8_t txStatus;

		if (entry->state != MAC_PENDING_STATE_READY ||
		    !mac_pending_addr_matches(entry, addrMode, addr)) {
			continue;
		}

		buf = (zb_buf_t *)entry->buf;
		txData = ((mac_pending_tx_ctx_t *)buf)->txData;
		if (pendingCnt > 1) {
			txData[0] |= MAC_FCF_FRAME_PENDING_MASK;
		}

		txStatus = zb_MacTx(buf, txData, ((mac_pending_tx_ctx_t *)buf)->psduLen,
				      txData[0] & MAC_FCF_ACK_REQ_BIT, entry);
		entry->state = (txStatus == MAC_SUCCESS) ? MAC_PENDING_STATE_SENT
							 : MAC_PENDING_STATE_QUEUED;
		break;
	}

post_poll_ind:
	if (listLength(macPendingQueue) == 0) {
		uint8_t saved[9]; /* address bytes copied from addr, then reused in req */

		/* Preserve the vendor stack/code shape even though req[0] does not alias addr. */
		memcpy(saved, addr, sizeof(saved));
		req[0] = addrMode;
		ZB_IEEE_ADDR_COPY(req + 1, saved);
		zb_PrimitivePost(ZB_Q_MAC2NWK, MAC_MLME_POLL_IND, req);
		return;
	}

	req[0] = addrMode;
	ZB_IEEE_ADDR_COPY(req + 1, addr);
	zb_PrimitivePost(ZB_Q_MAC2NWK, MAC_MLME_POLL_IND, req);
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

		if ((uint8_t)(entry->state - MAC_PENDING_STATE_SENT) <= 1U) {
			entry->state = MAC_PENDING_STATE_EXPIRED;
			return 0;
		}

		if (entry->buf != NULL) {
			zb_MaxTxConfirmCb(entry->buf, MAC_STA_TRANSACTION_EXPIRED);
		}
		listRemove(macPendingQueue, entry);
		ev_buf_free((uint8_t *)entry);
		return 0;
	}

	return 0;
}
#endif

void zb_MacDataRequestStatusCheck(zb_buf_t *buf, uint8_t status)
{
	uint8_t savedStatus = buf->hdr.handle;

	if (savedStatus == MAC_STA_NO_ACK) {
		zb_buf_free(buf);
		zb_MacAssocPollConfirm(status);
		return;
	}

	if (savedStatus == MAC_STA_INVALID_PARAMETER) {
		if (status == MAC_SUCCESS) {
			status = MAC_STA_NO_DATA;
		}

		((mac_mlme_poll_conf_t *)buf)->status = status;
		zb_PrimitivePost(ZB_Q_MAC2NWK, MAC_MLME_POLL_CNF, buf);
		return;
	}

	zb_buf_free(buf);
}

uint8_t zb_MacMlmeDataRequestCmdSend(zb_mlme_data_req_cmd_t *req, zb_buf_t *buf, uint8_t status)
{
	zb_mac_mhr_t mhr;
	uint8_t hdrSize;
	uint8_t *payload;
	uint8_t *frameStart;
	uint8_t txStatus;

	memset(&mhr, 0, sizeof(mhr));
	mhr.dstPanId = g_zbMacPib.panId;
	memcpy(&mhr.dstAddr, &req->dstAddr, sizeof(mhr.dstAddr));
	memcpy(&mhr.srcAddr, &req->srcAddr, sizeof(mhr.srcAddr));
	mhr.frameCtrl = MAC_FRAME_TYPE_COMMAND | MAC_FCF_ACK_REQUEST_MASK | MAC_FCF_INTRA_PAN_MASK |
			((uint16_t)req->dstAddrMode << MAC_FCF_DST_ADDR_MODE_POS) |
			((uint16_t)req->srcAddrMode << MAC_FCF_SRC_ADDR_MODE_POS);

	hdrSize = (uint8_t)(zb_MacHdrSize(mhr.frameCtrl) + 1U);
	ZB_BUF_INITIAL_ALLOC(buf, hdrSize, payload, uint8_t *);
	/* The queued frame is the
	 * allocation, not the pointer the header builder returns. */
	frameStart = payload;
	payload = zb_MacHdrBuilder(payload, &mhr);
	payload[0] = MAC_CMD_DATA_REQUEST;

	buf->hdr.handle = status;

	txStatus = zb_MacTx(buf, frameStart, hdrSize, 1, NULL);
	if (txStatus != MAC_SUCCESS) {
		zb_MacDataRequestStatusCheck(buf, txStatus);
	}

	return txStatus;
}
