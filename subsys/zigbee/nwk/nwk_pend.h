/* Copyright 2026 libzigbee
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef _NWK_PEND_H
#define _NWK_PEND_H

#include "zb_common.h"

enum {
	/* The cache starts with private metadata before the copied NWK header. */
	NWK_ROUTE_DISC_CACHE_HDR_OFFSET = sizeof(uint8_t) + sizeof(uint8_t *),
	/* Match the SDK's ZB_COPY_BUF copy range, excluding the final header byte. */
	NWK_ROUTE_DISC_CACHE_SIZE = ZB_BUF_SIZE + sizeof(zb_buf_hdr_t) - 1,
	/* The remaining copied buffer bytes hold payload/route-list storage. */
	NWK_ROUTE_DISC_CACHE_DATA_SIZE = ZB_BUF_SIZE - NWK_ROUTE_DISC_CACHE_HDR_OFFSET -
		(int)sizeof(nwk_hdr_t),
};

typedef struct _attribute_packed_ {
	uint8_t payloadLen;
	uint8_t *payload;
	nwk_hdr_t hdr;
	uint8_t bufferData[NWK_ROUTE_DISC_CACHE_DATA_SIZE];
	uint8_t bufferHeaderId;
	uint8_t savedHandle;
	uint8_t bufferHeaderRssi;
} nwk_route_disc_cache_buf_t;

enum {
	NWK_ROUTE_DISC_CACHE_PAYLOAD_OFFSET = OFFSETOF(nwk_route_disc_cache_buf_t, payload),
};

static inline uint8_t *nwk_route_disc_cache_payload_get(const nwk_route_disc_cache_buf_t *cache)
{
	uint8_t *payload;

	memcpy(&payload, (const uint8_t *)cache + NWK_ROUTE_DISC_CACHE_PAYLOAD_OFFSET, sizeof(payload));
	return payload;
}

static inline void nwk_route_disc_cache_payload_set(nwk_route_disc_cache_buf_t *cache, uint8_t *payload)
{
	memcpy((uint8_t *)cache + NWK_ROUTE_DISC_CACHE_PAYLOAD_OFFSET, &payload, sizeof(payload));
}

static inline uint8_t *nwk_route_disc_cache_relay_list_get(const nwk_route_disc_cache_buf_t *cache)
{
	uint8_t *relayList;
	size_t offset = OFFSETOF(nwk_route_disc_cache_buf_t, hdr) +
			OFFSETOF(nwk_hdr_t, srcRouteSubframe) +
			OFFSETOF(srcRouteSubframe_t, relayList);

	memcpy(&relayList, (const uint8_t *)cache + offset, sizeof(relayList));
	return relayList;
}

static inline void nwk_route_disc_cache_relay_list_set(nwk_route_disc_cache_buf_t *cache,
						       uint8_t *relayList)
{
	size_t offset = OFFSETOF(nwk_route_disc_cache_buf_t, hdr) +
			OFFSETOF(nwk_hdr_t, srcRouteSubframe) +
			OFFSETOF(srcRouteSubframe_t, relayList);

	memcpy((uint8_t *)cache + offset, &relayList, sizeof(relayList));
}

void nwkTxDataPendTabInit(void);
nwk_txDataPendEntry_t *nwkTxDataPendTabEntryFind(uint16_t srcAddr, uint16_t dstAddr, uint8_t handle, uint8_t seqNum);
nwk_txDataPendEntry_t *nwkTxDataPendTabEntryRtDiscFind(uint16_t dstAddr, uint8_t routeReqId);
nwk_txDataPendEntry_t *nwkTxDataPendTabEntryAdd(zb_buf_t *buf, nwk_hdr_t *pNwkHdr, uint8_t *payload,
						uint8_t payloadLen, uint8_t handle);
uint8_t *nwkTxDataCachePacketCopy(zb_buf_t *buf);
void nwkTxDataPendTabEntryClear(nwk_txDataPendEntry_t *entry);

#endif
