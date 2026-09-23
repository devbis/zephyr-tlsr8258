/*
 * Copyright 2026 libzigbee
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "zb_common.h"
#include "nwk_pend.h"
#include "nwk_brc.h"
#include "nwk_data.h"
#include "nwk_routing.h"
#include "zb_nwk_neighbor.h"
#include "ev_timer.h"
#include <stdint.h>

#if defined(ZB_ROUTER_ROLE)

static u32 g_brcTransJitter = NWK_MAX_BROADCAST_JITTER;
u8 g_brcTransTabCnt = 0;

typedef struct _attribute_packed_ {
	u8 payloadLen;
	u8 *payload;
	nwk_hdr_t hdr;
} nwk_brc_delay_ctx_t;

static inline bool nwk_brc_entry_used(const nwk_brcTransRecordEntry_t *entry)
{
	return entry != NULL && entry->used != 0U;
}

int nwkBrcMsgAllDevicesCb(void *arg)
{
	nwk_brcTransRecordEntry_t *entry = (nwk_brcTransRecordEntry_t *)arg;
	tl_zb_normal_neighbor_entry_t *child;
	nwk_txDataPendEntry_t *pend;
	nwk_route_disc_cache_buf_t *cache;
	nwk_hdr_t hdr;
	u8 *payload;
	u16 childAddr;

	if (entry == NULL || entry->entry == NULL) {
		return -1;
	}

	child = tl_zbNeighborTabSearchForChildEndDev(entry->edEntry);
	entry->edEntry = child;
	if (child == NULL) {
		return -1;
	}

	childAddr = tl_zbshortAddrByIdx(child->addrmapIdx);
	if (childAddr == entry->srcAddr || child->rxOnWhileIdle == 0U) {
		return -1;
	}

	pend = entry->entry;
	cache = (nwk_route_disc_cache_buf_t *)nwkTxDataCachePacketCopy((zb_buf_t *)pend->srcBuf);
	if (cache == NULL) {
		return -1;
	}

	payload = nwk_route_disc_cache_payload_get(cache);
	memcpy(&hdr, &cache->hdr, sizeof(hdr));
	nwk_tx((zb_buf_t *)cache, &hdr, childAddr, child->rxOnWhileIdle, payload,
	       cache->payloadLen);

	return -1;
}

/*
 * Replay of a deferred broadcast.  nwkBrcTimerStart packed everything into the
 * head of this buffer: the payload length at byte 0, the payload pointer at
 * byte 1 (unaligned, hence the memcpy) and the network header immediately
 * after it.  Unpack and hand it back to nwk_fwdPacket.
 */
void nwkMsgSendCb(void *arg)
{
	nwk_brc_delay_ctx_t *ctx = (nwk_brc_delay_ctx_t *)arg;
	nwk_hdr_t hdr;
	u8 *payload;

	payload = ctx->payload;
	memcpy(&hdr, &ctx->hdr, sizeof(hdr));
	nwk_fwdPacket((zb_buf_t *)arg, &hdr, payload, ctx->payloadLen);
}

int nwkMsgSendCbDelay(void *arg)
{
	nwkMsgSendCb(arg);
	return -1;
}

/* The vendor takes the jitter as an argument; callers pass NWK_BRC_JITTER, or
 * 1 when the broadcast must go out without additional delay. */
void nwkBrcTransJitterSet(u32 jitter)
{
	g_brcTransJitter = jitter;
}

void nwkBrcTransTabEntryRst(nwk_brcTransRecordEntry_t *entry)
{
	if (entry == NULL) {
		return;
	}

	if (entry->passiveAckAddr != NULL) {
		ev_buf_free((u8 *)entry->passiveAckAddr);
	}

	memset(entry, 0, sizeof(*entry));
	entry->srcAddr = ZB_MAC_SHORT_ADDR_NOT_ALLOCATED;
}

void nwkBrcTransTabEntryClear(nwk_brcTransRecordEntry_t *entry)
{
	/*
	 * The record goes back to the table once the transmission it tracks is
	 * over, which is what a cleared `entry` means. The vendor reads the used
	 * flag at "6: tloadrb r3,[r0,#23]", assembles the pending-transmission
	 * pointer from offset 4 at "c:".."1e:", and branches to the reset at
	 * "20: tjeq 24": it frees when that pointer is NULL and keeps the record
	 * while it is still set.
	 */
	if (!nwk_brc_entry_used(entry) || entry->entry != NULL) {
		return;
	}

	nwkBrcTransTabEntryRst(entry);
	g_brcTransTabCnt--;
}

void nwkBrcTransTabInit(void)
{
	for (u8 i = 0; i < NWK_BRC_TRANSTBL_SIZE; i++) {
		nwkBrcTransTabEntryRst(brcTransRecordEntryGet(i));
	}
}

nwk_brcTransRecordEntry_t *nwkBrcTransEntryFind(u16 srcAddr, u8 seqNum)
{
	for (u8 i = 0; i < NWK_BRC_TRANSTBL_SIZE; i++) {
		nwk_brcTransRecordEntry_t *entry = brcTransRecordEntryGet(i);

		if (nwk_brc_entry_used(entry) && entry->srcAddr == srcAddr &&
		    entry->seqNum == seqNum) {
			return entry;
		}
	}

	return NULL;
}

nwk_brcTransRecordEntry_t *nwkBrcTransEntryCreate(nwk_txDataPendEntry_t *pend, u16 srcAddr,
						  u8 seqNum)
{
	nwk_brcTransRecordEntry_t *entry = NULL;
	u16 *passiveAckAddr = NULL;

	if (NWK_BRC_PASSIVE_ACK_ENABLE) {
		passiveAckAddr =
			(u16 *)ev_buf_allocate((u16)(sizeof(u16) * TL_ZB_NEIGHBOR_TABLE_SIZE));
		if (passiveAckAddr == NULL) {
			return NULL;
		}
	}

	for (u8 i = 0; i < NWK_BRC_TRANSTBL_SIZE; i++) {
		nwk_brcTransRecordEntry_t *candidate = brcTransRecordEntryGet(i);

		if (!candidate->used) {
			entry = candidate;
			break;
		}
	}

	if (entry == NULL) {
		if (passiveAckAddr != NULL) {
			ev_buf_free((u8 *)passiveAckAddr);
		}
		return NULL;
	}

	entry->passiveAckAddr = passiveAckAddr;
	entry->entry = pend;
	entry->srcAddr = srcAddr;
	entry->seqNum = seqNum;
	entry->retries = 0;
	entry->expirationTime = (u8)(g_zbNIB.nwkBroadcastDeliveryTime / 1000U + 1U);
	entry->status = 1;
	entry->used = 1;
	g_brcTransTabCnt++;

	return entry;
}

u8 nwkBrcMsgAddSender(nwk_brcTransRecordEntry_t *entry, u16 shortAddr)
{
	tl_zb_normal_neighbor_entry_t *neighbor;

	neighbor = nwk_neTblGetByShortAddr(shortAddr);
	if (neighbor == NULL || (neighbor->rxOnWhileIdle + (neighbor->deviceType << 1)) == 4U ||
	    entry->passiveAckAddr == NULL) {
		return 0;
	}

	for (u8 i = 0; i < entry->activeNum; i++) {
		if (entry->passiveAckAddr[i] == shortAddr) {
			return 0;
		}
	}

	if (entry->activeNum >= TL_ZB_NEIGHBOR_TABLE_SIZE) {
		return 0;
	}

	entry->passiveAckAddr[entry->activeNum++] = shortAddr;
	return 1;
}

u8 nwkBrcAckFind(u16 shortAddr, nwk_brcTransRecordEntry_t *entry)
{
	if (entry == NULL || entry->passiveAckAddr == NULL) {
		return 0;
	}

	for (u8 i = 0;; i++) {
		if (entry->passiveAckAddr[i] == shortAddr) {
			return 1;
		}

		if (i + 1U >= entry->activeNum) {
			break;
		}
	}

	return 0;
}

u8 nwkBrcAllRelayed(nwk_brcTransRecordEntry_t *entry)
{
	u8 neighborCount = tl_zbNeighborTableNumGet();

	if (neighborCount == 0U) {
		return 1;
	}

	u16 dstAddr = entry->entry->dstAddr;

	for (u8 i = 0; i < neighborCount; i++) {
		tl_zb_normal_neighbor_entry_t *neighbor = tl_zbNeighborEntryGetFromIdx(i);
		bool eligible;

		/* Vendor ASM treats only 0xffff as the all-devices broadcast.  Every
		 * other destination follows the end-device relay path. */
		if (dstAddr == NWK_BROADCAST_ALL_DEVICES) {
			eligible = neighbor->deviceType != NWK_DEVICE_TYPE_ED;
		} else {
			eligible = neighbor->deviceType == NWK_DEVICE_TYPE_ED;
		}

		if (!eligible) {
			continue;
		}

		u16 shortAddr = tl_zbshortAddrByIdx(neighbor->addrmapIdx);
		if (entry->passiveAckAddr == NULL || entry->activeNum == 0U) {
			return 0;
		}

		bool found = false;
		for (u8 j = 0; j < entry->activeNum; j++) {
			if (entry->passiveAckAddr[j] == shortAddr) {
				found = true;
				break;
			}
		}

		if (!found) {
			return 0;
		}
	}

	return 1;
}

int nwkBrcMsgPassiveAckTimeoutCb(void *arg)
{
	nwk_brcTransRecordEntry_t *entry = (nwk_brcTransRecordEntry_t *)arg;
	nwk_txDataPendEntry_t *pend;
	zb_buf_t *cache;

	if (entry == NULL || entry->used == 0U) {
		return -1;
	}

	pend = entry->entry;
	if (pend == NULL) {
		goto cleanup;
	}

	entry->retries++;
	if (entry->retries >= g_zbNIB.maxBroadcastRetries || nwkBrcAllRelayed(entry)) {
		goto cleanup;
	}

	if (pend->used == 0U || pend->srcBuf == NULL) {
		goto cleanup;
	}

	cache = (zb_buf_t *)nwkTxDataCachePacketCopy((zb_buf_t *)pend->srcBuf);
	if (cache == NULL) {
		return 0;
	}

	tl_zbTaskPost(nwkMsgSendCb, cache);
	entry->retryTimer = NULL;
	return -1;

cleanup:
	if (entry->passiveAckAddr != NULL) {
		ev_buf_free((u8 *)entry->passiveAckAddr);
		entry->passiveAckAddr = NULL;
	}

	pend = entry->entry;
	if (pend != NULL) {
		nwkTxDataPendTabEntryClear(pend);
		entry->entry = NULL;
	}

	entry->retryTimer = NULL;
	return -1;
}

ev_timer_event_t *nwkBrcMsgPassiveAckTimeoutStart(nwk_brcTransRecordEntry_t *entry)
{
	if (entry == NULL || entry->used == 0U || entry->retryTimer != NULL) {
		return NULL;
	}

	entry->retryTimer = ev_timer_taskPost(nwkBrcMsgPassiveAckTimeoutCb, entry,
					      g_zbNIB.passiveAckTimeout / 1000U);
	return entry->retryTimer;
}

void nwkBrcMsgAllEndDevStart(nwk_brcTransRecordEntry_t *entry)
{
	if (entry == NULL) {
		return;
	}

	entry->edEntry = tl_zbNeighborTabSearchForChildEndDev(NULL);
	if (entry->edEntry != NULL) {
		ev_timer_taskPost(nwkBrcMsgAllDevicesCb, entry,
				  g_zbNIB.passiveAckTimeout / TL_ZB_NEIGHBOR_TABLE_SIZE / 1000U);
	}
}

/*
 * Schedule a broadcast for retransmission after a random jitter.  The frame is
 * copied into a buffer of its own, because the caller's buffer is handed back
 * to the receive path as soon as this returns; the payload pointer is re-based
 * onto the copy so that nwkMsgSendCb can replay it unchanged.
 */
u8 nwkBrcTimerStart(zb_buf_t *buf, nwk_hdr_t *pNwkHdr, u8 *payload, u8 payloadLen)
{
	zb_buf_t *copy;
	u8 *copyPayload;
	nwk_brc_delay_ctx_t *ctx;
	uintptr_t payloadOffset;

	if (!ev_timer_enough()) {
		/* 20: no named constant in the SDK headers; the vendor returns it
		 * whenever the timer pool cannot take another entry. */
		return 20;
	}

	copy = zb_buf_allocate();
	if (copy == NULL) {
		return NWK_STATUS_FRAME_NOT_BUFFERED;
	}

	/* ZB_BUF_SIZE payload bytes plus the id/handle/rssi of the buffer header. */
	TL_COPY_BUF(copy, buf);

	payloadOffset = (uintptr_t)(payload - buf->buf);
	copyPayload = copy->buf + payloadOffset;
	ctx = (nwk_brc_delay_ctx_t *)copy;

	ctx->payloadLen = payloadLen;
	ctx->payload = copyPayload;
	memcpy(&ctx->hdr, pNwkHdr, sizeof(ctx->hdr));

	ev_timer_taskPost(nwkMsgSendCbDelay, copy, (drv_u32Rand() % g_brcTransJitter) + 1);

	return NWK_STATUS_SUCCESS;
}

int nwkBrcPeriodic(void *arg)
{
	(void)arg;

	if (g_brcTransTabCnt == 0U) {
		return 0;
	}

	for (u8 i = 0; i < NWK_BRC_TRANSTBL_SIZE; i++) {
		nwk_brcTransRecordEntry_t *entry = brcTransRecordEntryGet(i);

		if (!nwk_brc_entry_used(entry)) {
			continue;
		}

		if (entry->expirationTime != 0U) {
			entry->expirationTime--;
		}

		if (entry->expirationTime == 0U) {
			nwkBrcTransTabEntryClear(entry);
		}
	}

	return 0;
}

u8 nwkBrcCheckDevMatch(u16 dstAddr)
{
	if (dstAddr == NWK_BROADCAST_RX_ON_WHEN_IDLE) {
		return ZB_PIB_RX_ON_WHEN_IDLE() ? 1U : 0U;
	}

	if (dstAddr == NWK_BROADCAST_ALL_DEVICES) {
		return 1U;
	}

	if (dstAddr == NWK_BROADCAST_ROUTER_COORDINATOR) {
		return 0U;
	}

	return g_zbNIB.capabilityInfo.devType ? 0xffU : 0U;
}

#else

u8 nwkBrcCheckDevMatch(u16 dstAddr)
{
	if (dstAddr == NWK_BROADCAST_RX_ON_WHEN_IDLE) {
		return ZB_PIB_RX_ON_WHEN_IDLE() ? 1U : 0U;
	}

	if (dstAddr == NWK_BROADCAST_ALL_DEVICES) {
		return 1U;
	}

	if (dstAddr == NWK_BROADCAST_ROUTER_COORDINATOR) {
		return 0U;
	}

	return g_zbNIB.capabilityInfo.devType ? 0xffU : 0U;
}

#endif
