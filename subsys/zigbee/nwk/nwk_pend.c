/*
 * Copyright 2026 libzigbee
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "zb_common.h"
#include "nwk_pend.h"
#include <stdint.h>

#if defined(ZB_ROUTER_ROLE)

enum {
	NWK_TX_DATA_PEND_TABLE_SIZE = 16,
};

STATIC_ASSERT(OFFSETOF(nwk_txDataPendEntry_t, srcBuf) == 0);
#if UINTPTR_MAX == UINT32_MAX
STATIC_ASSERT(sizeof(nwk_txDataPendEntry_t) == 12);
STATIC_ASSERT(OFFSETOF(nwk_txDataPendEntry_t, srcAddr) == 4);
STATIC_ASSERT(OFFSETOF(nwk_txDataPendEntry_t, dstAddr) == 6);
STATIC_ASSERT(OFFSETOF(nwk_txDataPendEntry_t, handle) == 8);
STATIC_ASSERT(OFFSETOF(nwk_txDataPendEntry_t, seqNum) == 9);
STATIC_ASSERT(OFFSETOF(nwk_txDataPendEntry_t, routeReqId) == 10);
#else
STATIC_ASSERT(sizeof(nwk_txDataPendEntry_t) == 16);
STATIC_ASSERT(OFFSETOF(nwk_txDataPendEntry_t, srcAddr) == 8);
STATIC_ASSERT(OFFSETOF(nwk_txDataPendEntry_t, dstAddr) == 10);
STATIC_ASSERT(OFFSETOF(nwk_txDataPendEntry_t, handle) == 12);
STATIC_ASSERT(OFFSETOF(nwk_txDataPendEntry_t, seqNum) == 13);
STATIC_ASSERT(OFFSETOF(nwk_txDataPendEntry_t, routeReqId) == 14);
#endif

STATIC_ASSERT(sizeof(nwk_route_disc_cache_buf_t) == NWK_ROUTE_DISC_CACHE_SIZE);
STATIC_ASSERT(OFFSETOF(nwk_route_disc_cache_buf_t, payloadLen) == 0);
STATIC_ASSERT(OFFSETOF(nwk_route_disc_cache_buf_t, payload) == 1);
STATIC_ASSERT(OFFSETOF(nwk_route_disc_cache_buf_t, hdr) == NWK_ROUTE_DISC_CACHE_HDR_OFFSET);
STATIC_ASSERT(OFFSETOF(nwk_route_disc_cache_buf_t, bufferHeaderId) == ZB_BUF_SIZE);
STATIC_ASSERT(OFFSETOF(nwk_route_disc_cache_buf_t, savedHandle) ==
	      OFFSETOF(zb_buf_t, hdr) + OFFSETOF(zb_buf_hdr_t, handle));

nwk_txDataPendEntry_t g_txDataPendTab[NWK_TX_DATA_PEND_TABLE_SIZE];
u8 g_txDataPendTabCnt = 0;

static inline bool nwk_tx_data_pend_used(const nwk_txDataPendEntry_t *entry)
{
	return entry != NULL && entry->used != 0U;
}

void nwkTxDataPendTabEntryRst(nwk_txDataPendEntry_t *entry)
{
	memset(entry, 0, sizeof(*entry));
	entry->srcAddr = MAC_SHORT_ADDR_NONE;
	entry->dstAddr = ZB_MAC_SHORT_ADDR_NOT_ALLOCATED;
}

void nwkTxDataPendTabEntryClear(nwk_txDataPendEntry_t *entry)
{
	void *srcBuf;

	if (entry == NULL || entry->used == 0U) {
		return;
	}

	srcBuf = entry->srcBuf;
	nwkTxDataPendTabEntryRst(entry);
	if (srcBuf != NULL) {
		zb_buf_free((zb_buf_t *)srcBuf);
	}
	g_txDataPendTabCnt--;
}

void nwkTxDataPendTabInit(void)
{
	for (u8 i = 0; i < NWK_TX_DATA_PEND_TABLE_SIZE; i++) {
		nwkTxDataPendTabEntryRst(&g_txDataPendTab[i]);
	}
}

nwk_txDataPendEntry_t *nwkTxDataPendTabEntryFind(u16 srcAddr, u16 dstAddr, u8 handle, u8 seqNum)
{
	for (u8 i = 0; i < NWK_TX_DATA_PEND_TABLE_SIZE; i++) {
		nwk_txDataPendEntry_t *entry = &g_txDataPendTab[i];

		if (nwk_tx_data_pend_used(entry) && entry->srcAddr == srcAddr &&
		    entry->dstAddr == dstAddr && entry->handle == handle &&
		    entry->seqNum == seqNum) {
			return entry;
		}
	}

	return NULL;
}

nwk_txDataPendEntry_t *nwkTxDataPendTabEntryRtDiscFind(u16 dstAddr, u8 routeReqId)
{
	for (u8 i = 0; i < NWK_TX_DATA_PEND_TABLE_SIZE; i++) {
		nwk_txDataPendEntry_t *entry = &g_txDataPendTab[i];

		if (nwk_tx_data_pend_used(entry) && entry->needRouteDisc != 0U &&
		    entry->dstAddr == dstAddr && entry->routeReqId == routeReqId) {
			return entry;
		}
	}

	return NULL;
}

nwk_txDataPendEntry_t *nwkTxDataPendTabEntryCreate(void *srcBuf, u16 srcAddr, u16 dstAddr,
						   u8 handle, u8 seqNum)
{
	nwk_txDataPendEntry_t *entry = NULL;

	for (u8 i = 0; i < NWK_TX_DATA_PEND_TABLE_SIZE; i++) {
		if (!g_txDataPendTab[i].used) {
			entry = &g_txDataPendTab[i];
			break;
		}
	}

	if (entry == NULL) {
		return NULL;
	}

	entry->srcBuf = srcBuf;
	entry->srcAddr = srcAddr;
	entry->dstAddr = dstAddr;
	entry->handle = handle;
	entry->seqNum = seqNum;
	entry->routeReqId = 0;
	entry->expirationTime = 60;
	entry->needRouteDisc = 0;
	entry->used = 1;
	g_txDataPendTabCnt++;

	return entry;
}

u8 *nwkTxDataCachePacketCopy(zb_buf_t *buf)
{
	zb_buf_t *copy;
	nwk_route_disc_cache_buf_t *srcCache;
	nwk_route_disc_cache_buf_t *dstCache;
	u8 *payload;
	u8 *relayList;

	if (buf == NULL) {
		return NULL;
	}

	copy = zb_buf_allocate();
	if (copy == NULL) {
		return NULL;
	}

	memcpy(copy, buf, NWK_ROUTE_DISC_CACHE_SIZE);

	srcCache = (nwk_route_disc_cache_buf_t *)buf;
	dstCache = (nwk_route_disc_cache_buf_t *)copy;

	/* preserve saved handle contract used by vendor code */
	dstCache->savedHandle = NWK_INTERNAL_NSDU_HANDLE;

	payload = nwk_route_disc_cache_payload_get(srcCache);
	nwk_route_disc_cache_payload_set(dstCache,
					 payload == NULL ? NULL : copy->buf + (payload - buf->buf));

	relayList = nwk_route_disc_cache_relay_list_get(srcCache);
	if (dstCache->hdr.frameControl.srcRoute != 0U && relayList != NULL) {
		nwk_route_disc_cache_relay_list_set(dstCache, copy->buf + (relayList - buf->buf));
	}

	return (u8 *)copy;
}

nwk_txDataPendEntry_t *nwkTxDataPendTabEntryAdd(zb_buf_t *buf, nwk_hdr_t *pNwkHdr, u8 *payload,
						u8 payloadLen, u8 handle)
{
	nwk_txDataPendEntry_t *entry;
	zb_buf_t *cachedBuf;
	nwk_route_disc_cache_buf_t *cache;

	entry = nwkTxDataPendTabEntryFind(pNwkHdr->srcAddr, pNwkHdr->dstAddr, handle,
					  pNwkHdr->seqNum);
	if (entry != NULL) {
		return entry;
	}

	cache = (nwk_route_disc_cache_buf_t *)buf;

	cache->payloadLen = payloadLen;
	nwk_route_disc_cache_payload_set(cache, payload);
	memcpy(&cache->hdr, pNwkHdr, sizeof(cache->hdr));

	cachedBuf = (zb_buf_t *)nwkTxDataCachePacketCopy(buf);
	if (cachedBuf == NULL) {
		return NULL;
	}

	entry = nwkTxDataPendTabEntryCreate(cachedBuf, pNwkHdr->srcAddr, pNwkHdr->dstAddr, handle,
					    pNwkHdr->seqNum);
	if (entry == NULL) {
		zb_buf_free(cachedBuf);
		return NULL;
	}

	return entry;
}

void nwkDataPendPeriodic(void)
{
	if (g_txDataPendTabCnt == 0U) {
		return;
	}

	for (u8 i = 0; i < NWK_TX_DATA_PEND_TABLE_SIZE; i++) {
		nwk_txDataPendEntry_t *entry = &g_txDataPendTab[i];

		if (!nwk_tx_data_pend_used(entry)) {
			continue;
		}

		if (entry->expirationTime != 0U) {
			entry->expirationTime--;
		}
		if (entry->expirationTime == 0U) {
			nwkTxDataPendTabEntryClear(entry);
		}
	}
}

#else

#include "zb_common.h"

#endif
