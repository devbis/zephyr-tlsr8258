/* Copyright 2026 libzigbee
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef _NWK_PEND_H
#define _NWK_PEND_H

#include "zb_common.h"

enum {
	/* The cache starts with private metadata before the copied NWK header. */
	NWK_ROUTE_DISC_CACHE_HDR_OFFSET = sizeof(u8) + sizeof(u8 *),
	/* Match the SDK's TL_COPY_BUF copy range, excluding the final header byte. */
	NWK_ROUTE_DISC_CACHE_SIZE = ZB_BUF_SIZE + sizeof(zb_buf_hdr_t) - 1,
	/* The remaining copied buffer bytes hold payload/route-list storage. */
	NWK_ROUTE_DISC_CACHE_DATA_SIZE = ZB_BUF_SIZE - NWK_ROUTE_DISC_CACHE_HDR_OFFSET -
		(int)sizeof(nwk_hdr_t),
};

typedef struct _attribute_packed_ {
	u8 payloadLen;
	u8 *payload;
	nwk_hdr_t hdr;
	u8 bufferData[NWK_ROUTE_DISC_CACHE_DATA_SIZE];
	u8 bufferHeaderId;
	u8 savedHandle;
	u8 bufferHeaderRssi;
} nwk_route_disc_cache_buf_t;

enum {
	NWK_ROUTE_DISC_CACHE_PAYLOAD_OFFSET = OFFSETOF(nwk_route_disc_cache_buf_t, payload),
};

static inline u8 *nwk_route_disc_cache_payload_get(const nwk_route_disc_cache_buf_t *cache)
{
	u8 *payload;

	memcpy(&payload, (const u8 *)cache + NWK_ROUTE_DISC_CACHE_PAYLOAD_OFFSET, sizeof(payload));
	return payload;
}

static inline void nwk_route_disc_cache_payload_set(nwk_route_disc_cache_buf_t *cache, u8 *payload)
{
	memcpy((u8 *)cache + NWK_ROUTE_DISC_CACHE_PAYLOAD_OFFSET, &payload, sizeof(payload));
}

static inline u8 *nwk_route_disc_cache_relay_list_get(const nwk_route_disc_cache_buf_t *cache)
{
	u8 *relayList;
	size_t offset = OFFSETOF(nwk_route_disc_cache_buf_t, hdr) +
			OFFSETOF(nwk_hdr_t, srcRouteSubframe) +
			OFFSETOF(srcRouteSubframe_t, relayList);

	memcpy(&relayList, (const u8 *)cache + offset, sizeof(relayList));
	return relayList;
}

static inline void nwk_route_disc_cache_relay_list_set(nwk_route_disc_cache_buf_t *cache,
						       u8 *relayList)
{
	size_t offset = OFFSETOF(nwk_route_disc_cache_buf_t, hdr) +
			OFFSETOF(nwk_hdr_t, srcRouteSubframe) +
			OFFSETOF(srcRouteSubframe_t, relayList);

	memcpy((u8 *)cache + offset, &relayList, sizeof(relayList));
}

void nwkTxDataPendTabInit(void);
nwk_txDataPendEntry_t *nwkTxDataPendTabEntryFind(u16 srcAddr, u16 dstAddr, u8 handle, u8 seqNum);
nwk_txDataPendEntry_t *nwkTxDataPendTabEntryRtDiscFind(u16 dstAddr, u8 routeReqId);
nwk_txDataPendEntry_t *nwkTxDataPendTabEntryAdd(zb_buf_t *buf, nwk_hdr_t *pNwkHdr, u8 *payload,
						u8 payloadLen, u8 handle);
u8 *nwkTxDataCachePacketCopy(zb_buf_t *buf);
void nwkTxDataPendTabEntryClear(nwk_txDataPendEntry_t *entry);

#endif
