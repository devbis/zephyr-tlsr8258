/* SPDX-License-Identifier: Apache-2.0 */
/*
 * NWK TX-data pending table + route-discovery cache.
 *
 * Adapted from libzigbee/src/nwk_pend.c (~210 LOC). One-line change
 * vs. the vendor file: vendor "zb_local.h" → zb_common_stub.h +
 * nwk_internal.h. The pending table (g_txDataPendTab) and the cache-
 * copy helper drive NWK forwarding while a route-disc round-trip is
 * outstanding; both rely on zb_buf_allocate / zb_buf_free, declared
 * in nwk_internal.h and waiting for the vendor zb_buffer.c port.
 */

#include "zb_common_stub.h"
#include "common/static_assert.h"
#include "nwk/includes/nwk.h"
#include "nwk/includes/nwk_internal.h"

#include <string.h>

#if defined(ZB_ROUTER_ROLE) && ZB_ROUTER_ROLE

enum {
	NWK_TX_DATA_PEND_TABLE_SIZE = 16,
};

STATIC_ASSERT(sizeof(nwk_txDataPendEntry_t) == 12);
STATIC_ASSERT(OFFSETOF(nwk_txDataPendEntry_t, srcBuf) == 0);
STATIC_ASSERT(OFFSETOF(nwk_txDataPendEntry_t, srcAddr) == 4);
STATIC_ASSERT(OFFSETOF(nwk_txDataPendEntry_t, dstAddr) == 6);
STATIC_ASSERT(OFFSETOF(nwk_txDataPendEntry_t, handle) == 8);
STATIC_ASSERT(OFFSETOF(nwk_txDataPendEntry_t, seqNum) == 9);
STATIC_ASSERT(OFFSETOF(nwk_txDataPendEntry_t, routeReqId) == 10);

STATIC_ASSERT(sizeof(nwk_route_disc_cache_buf_t) == NWK_ROUTE_DISC_CACHE_SIZE);
STATIC_ASSERT(OFFSETOF(nwk_route_disc_cache_buf_t, payloadLen) == 0);
STATIC_ASSERT(OFFSETOF(nwk_route_disc_cache_buf_t, payload) == 1);
STATIC_ASSERT(OFFSETOF(nwk_route_disc_cache_buf_t, hdr) == 5);
STATIC_ASSERT(OFFSETOF(nwk_route_disc_cache_buf_t, savedHandle) ==
	      NWK_ROUTE_DISC_CACHE_SAVED_HANDLE_OFFSET);

nwk_txDataPendEntry_t g_txDataPendTab[NWK_TX_DATA_PEND_TABLE_SIZE];
u8 g_txDataPendTabCnt;

static inline bool nwk_tx_data_pend_used(const nwk_txDataPendEntry_t *entry)
{
	return entry != NULL && entry->used != 0U;
}

void nwkTxDataPendTabEntryRst(nwk_txDataPendEntry_t *entry)
{
	if (entry == NULL) {
		return;
	}

	memset(entry, 0, sizeof(*entry));
	entry->srcAddr = ZB_MAC_SHORT_ADDR_NOT_ALLOCATED;
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

nwk_txDataPendEntry_t *nwkTxDataPendTabEntryFind(u16 srcAddr, u16 dstAddr,
						  u8 handle, u8 seqNum)
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

nwk_txDataPendEntry_t *nwkTxDataPendTabEntryCreate(void *srcBuf, u16 srcAddr,
						    u16 dstAddr, u8 handle, u8 seqNum)
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
	dstCache->savedHandle = 0xc0;

	dstCache->payload = (u8 *)copy + (srcCache->payload - (u8 *)buf);

	if (dstCache->hdr.frameControl.srcRoute != 0U &&
	    dstCache->hdr.srcRouteSubframe.relayList != NULL) {
		dstCache->hdr.srcRouteSubframe.relayList =
			(u8 *)copy + (srcCache->hdr.srcRouteSubframe.relayList - (u8 *)buf);
	}

	return (u8 *)copy;
}

nwk_txDataPendEntry_t *nwkTxDataPendTabEntryAdd(zb_buf_t *buf, nwk_hdr_t *pNwkHdr,
						 u8 *payload, u8 payloadLen, u8 handle)
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
	cache->payload = payload;
	memcpy(&cache->hdr, pNwkHdr, sizeof(cache->hdr));

	cachedBuf = (zb_buf_t *)nwkTxDataCachePacketCopy(buf);
	if (cachedBuf == NULL) {
		return NULL;
	}

	entry = nwkTxDataPendTabEntryCreate(cachedBuf, pNwkHdr->srcAddr, pNwkHdr->dstAddr,
					     handle, pNwkHdr->seqNum);
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

#endif /* ZB_ROUTER_ROLE */
