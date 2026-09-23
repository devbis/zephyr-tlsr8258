/*
 * Copyright 2026 libzigbee
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "zb_common.h"
#include "nwk_brc.h"
#include "nwk_data.h"
#include "zb_nwk_core.h"
#include "nwk_pend.h"
#include "nwk_route_disc.h"
#include "nwk_routing.h"
#include "zdo_nwk_manager.h"
#include "ss_security_flags.h"
#include "ev_timer.h"
#include <stdint.h>

#if defined(ZB_ROUTER_ROLE)

enum {
	NWK_ROUTE_DISC_TABLE_SIZE = 10,
};

void nwkRouteDiscStatusCodeSend(zb_buf_t *buf, u16 srcAddr, u16 dstAddr, u8 status);
u8 nwkRouteDiscStart(nlme_routeDisc_req_t *req);
void nwkRouteReqCmdSend(zb_buf_t *buf, nwk_hdr_t *pNwkHdr, nwkCmd_t *cmd, u8 handle);
zb_buf_t *nwkRouteDiscCachePacketCopy(zb_buf_t *src, nwk_hdr_t *pNwkHdr,
				      const nwkCmd_routeReq_t *cmdTail);
int nwkConcentDiscTimerCb(void *arg);
int nwkRouteReqRetry(void *arg);
int nwkRouteReplySendDelay(void *arg);

nwk_routeDiscEntry_t g_routeDiscTab[NWK_ROUTE_DISC_TABLE_SIZE];
ev_timer_event_t *concentratorDiscoveryTimer = NULL;
u8 g_routeReqId = 0;
u8 g_routeDiscTabCnt = 0;

static inline bool nwk_route_disc_used(const nwk_routeDiscEntry_t *entry)
{
	return entry != NULL && entry->used != 0U;
}

enum {
	NWK_ROUTE_REQ_RETRY_DELAY = 0xfe,
};

typedef struct _attribute_packed_ {
	u8 radius;
	u8 routeReqId;
	u16 originatorAddr;
	u16 responderAddr;
	u16 prevHop;
	u8 linkCost;
} nwk_route_reply_delay_t;

STATIC_ASSERT(sizeof(nwk_route_reply_delay_t) == 9);

static u8 nwkDiscDataPendSend(u16 dstAddr, u8 routeReqId)
{
	nwk_txDataPendEntry_t *pend;

	while ((pend = nwkTxDataPendTabEntryRtDiscFind(dstAddr, routeReqId)) != NULL) {
		nwk_route_disc_cache_buf_t *cache = NULL;

		if (pend->srcBuf != NULL) {
			cache = (nwk_route_disc_cache_buf_t *)pend->srcBuf;
			cache->savedHandle = pend->handle;
			pend->needRouteDisc = 0;
			pend->srcBuf = NULL;
		}

		nwkTxDataPendTabEntryClear(pend);

		if (cache != NULL) {
			tl_zbTaskPost(nwkMsgSendCb, cache);
		}
	}

	return 0;
}

static void nwkDiscDataPendClear(u16 dstAddr, u8 routeReqId)
{
	nwk_txDataPendEntry_t *pend;

	while ((pend = nwkTxDataPendTabEntryRtDiscFind(dstAddr, routeReqId)) != NULL) {
		void *srcBuf = pend->srcBuf;

		if (pend->handle < NWK_INTERNAL_NSDU_HANDLE) {
			nwkNldeDataCnf(srcBuf, NWK_STATUS_ROUTE_DISCOVERY_FAILED, pend->handle);
		} else {
			zb_buf_free((zb_buf_t *)srcBuf);
		}

		pend->srcBuf = NULL;
		nwkTxDataPendTabEntryClear(pend);
	}
}

void nwkRouteDiscTabEntryRst(nwk_routeDiscEntry_t *entry)
{
	if (entry == NULL) {
		return;
	}

	memset(entry, 0, sizeof(*entry));
	entry->srcAddr = NWK_BROADCAST_RESERVED;
	entry->senderAddr = NWK_BROADCAST_RESERVED;
	entry->dstAddr = NWK_BROADCAST_RESERVED;
}

void nwkRouteDiscTabEntryClear(nwk_routeDiscEntry_t *entry)
{
	if (!nwk_route_disc_used(entry)) {
		return;
	}

	if (entry->retryTimer != NULL) {
		ev_timer_taskCancel(&entry->retryTimer);
	}

	if (entry->buf != NULL) {
		ZB_EXCEPTION_POST(SYS_EXCEPTTION_ZB_NWK_ROUTE_TABLE);
	}

	nwkRouteDiscTabEntryRst(entry);
	g_routeDiscTabCnt--;
}

void nwkRouteDiscTabInit(void)
{
	for (u8 i = 0; i < NWK_ROUTE_DISC_TABLE_SIZE; i++) {
		nwkRouteDiscTabEntryRst(&g_routeDiscTab[i]);
	}
}

nwk_routeDiscEntry_t *nwkRouteDiscEntryFind(u16 srcAddr, u8 routeReqId)
{
	for (u8 i = 0; i < NWK_ROUTE_DISC_TABLE_SIZE; i++) {
		nwk_routeDiscEntry_t *entry = &g_routeDiscTab[i];

		if (nwk_route_disc_used(entry) && entry->srcAddr == srcAddr &&
		    entry->routeReqId == routeReqId) {
			return entry;
		}
	}

	return NULL;
}

nwk_routeDiscEntry_t *nwkRouteDiscEntryDstFind(u16 dstAddr)
{
	for (u8 i = 0; i < NWK_ROUTE_DISC_TABLE_SIZE; i++) {
		nwk_routeDiscEntry_t *entry = &g_routeDiscTab[i];

		if (nwk_route_disc_used(entry) && entry->dstAddr == dstAddr) {
			return entry;
		}
	}

	return NULL;
}

nwk_routeDiscEntry_t *nwkManyToOneRouteDiscEntryInitFind(void)
{
	/* Not nwkRouteDiscEntryDstFind(): the vendor library does not test the
	 * used flag and additionally requires the entry to have been initiated
	 * locally ("1e: tloadrh r2,[r0,#12]" against 0xfffc, then
	 * "24: tloadrh r2,[r0,#8]" against g_zbInfo+102/103 = g_zbNIB.nwkAddr). */
	for (u8 i = 0; i < NWK_ROUTE_DISC_TABLE_SIZE; i++) {
		nwk_routeDiscEntry_t *entry = &g_routeDiscTab[i];

		if ((entry->dstAddr == NWK_BROADCAST_ROUTER_COORDINATOR) &&
		    (entry->srcAddr == g_zbNIB.nwkAddr)) {
			return entry;
		}
	}

	return NULL;
}

nwk_routeDiscEntry_t *nwkRouteDiscEntryCreate(u16 srcAddr, u16 dstAddr, u16 senderAddr,
					      u8 forwardCost, u8 lastCost, u8 routeReqId)
{
	nwk_routeDiscEntry_t *entry = NULL;

	for (u8 i = 0; i < NWK_ROUTE_DISC_TABLE_SIZE; i++) {
		if (!g_routeDiscTab[i].used) {
			entry = &g_routeDiscTab[i];
			break;
		}
	}

	if (entry == NULL) {
		return NULL;
	}

	memset(entry, 0, sizeof(*entry));
	entry->srcAddr = srcAddr;
	entry->dstAddr = dstAddr;
	entry->senderAddr = senderAddr;
	entry->expiry = 10U;
	entry->forwardCost = forwardCost;
	entry->residCost = 0xffU;
	entry->lastCost = lastCost;
	entry->routeReqId = routeReqId;
	entry->retries = 0;
	entry->used = 1;
	g_routeDiscTabCnt++;

	return entry;
}

/*
 * Reconstructed from _router/nwk_route_disc.s.  The caller owns the buffer, the
 * NWK header and the command; this only serialises the route-request payload
 * and forwards it.  The reconstruction used to allocate and build all three
 * itself from a discovery-table entry.
 */
void nwkRouteReqCmdSend(zb_buf_t *buf, nwk_hdr_t *pNwkHdr, nwkCmd_t *cmd, u8 handle)
{
	u8 len = cmd->rreq.options.dstIeeeAddr ? 14U : 6U;
	u8 *payload;

	TL_BUF_INITIAL_ALLOC(buf, len, payload, u8 *);

	payload[0] = cmd->cmdId;
	payload[1] = *(u8 *)&cmd->rreq.options;
	payload[2] = cmd->rreq.routeReqId;
	memcpy(payload + 3, &cmd->rreq.dstAddr, sizeof(cmd->rreq.dstAddr));
	payload[5] = cmd->rreq.pathCost;

	if (cmd->rreq.options.dstIeeeAddr) {
		ZB_IEEE_ADDR_COPY(payload + 6, cmd->rreq.dstIeeeAddr);
	}

	buf->hdr.handle = handle;
	nwk_fwdPacket(buf, pNwkHdr, payload, len);
}

/* The retries replay the cached NWK header and route-request command that
 * nwkRouteDiscCachePacketCopy laid out in the buffer. */
static inline void nwk_route_disc_cache_load(const zb_buf_t *cached, nwk_hdr_t *hdr, nwkCmd_t *cmd)
{
	memcpy(hdr, cached, sizeof(*hdr));
	memcpy(&cmd->rreq, (const u8 *)cached + sizeof(*hdr), sizeof(cmd->rreq));
}

static void nwk_route_req_cache_update(zb_buf_t *cache, nwk_hdr_t *pNwkHdr, nwkCmd_t *cmd)
{
	if (cache == NULL) {
		return;
	}

	memcpy(cache, pNwkHdr, sizeof(*pNwkHdr));
	memcpy((u8 *)cache + sizeof(*pNwkHdr), &cmd->rreq, sizeof(cmd->rreq));
}

static void nwk_route_req_cache_forward(void *arg, nwk_hdr_t *pNwkHdr, nwkCmd_t *cmd,
					nwk_routeDiscEntry_t *entry)
{
	zb_buf_t *cache;
	u32 delay;

	if ((entry == NULL) || (pNwkHdr->radius == 0U) || !ev_timer_enough()) {
		return;
	}

	cache = nwkRouteDiscCachePacketCopy((zb_buf_t *)arg, pNwkHdr, &cmd->rreq);
	if (cache == NULL) {
		return;
	}

	entry->buf = cache;

	delay = drv_u32Rand() & 0x7fU;
	if (delay <= 1U) {
		delay = 2U;
	}
	entry->retryTimer = ev_timer_taskPost(nwkRouteReqRetry, entry, (u16)(delay << 1));
}

static void nwk_route_req_reply_schedule(void *arg, nwk_hdr_t *pNwkHdr, nwkCmd_routeReq_t *rreq,
					 u16 prevHop, u8 linkCost)
{
	nwk_route_reply_delay_t *delay = (nwk_route_reply_delay_t *)arg;

	delay->radius = (u8)(g_zbNIB.maxDepth << 1);
	delay->routeReqId = rreq->routeReqId;
	delay->originatorAddr = pNwkHdr->srcAddr;
	delay->responderAddr = rreq->dstAddr;
	delay->prevHop = prevHop;
	delay->linkCost = linkCost;
	tl_zbTaskPost((tl_zb_callback_t)nwkRouteReplySendDelay, arg);
}

int nwkRouteReqRetry(void *arg)
{
	nwk_routeDiscEntry_t *entry = (nwk_routeDiscEntry_t *)arg;
	nwk_hdr_t hdr;
	nwkCmd_t cmd;
	u8 retries;
	bool replay = FALSE;

	memset(&cmd, 0, sizeof(cmd));

	if (entry->buf != NULL) {
		nwk_route_disc_cache_load((const zb_buf_t *)entry->buf, &hdr, &cmd);
		replay = (hdr.radius != 0U);
	}

	retries = entry->retries;
	entry->retries = (u8)(retries + 1U);

	/* "5c: tcmp r3,r1; 5e: tjhi 88" - strictly greater. */
	if (retries > NWKC_RREQ_RETRIES) {
		if (entry->buf != NULL) {
			zb_buf_free((zb_buf_t *)entry->buf);
			entry->buf = NULL;
		}

		entry->retryTimer = NULL;
		return -1;
	}

	if (replay) {
		zb_buf_t *buf = zb_buf_allocate();

		if (buf != NULL) {
			cmd.cmdId = NWK_CMD_ROUTE_REQUEST;
			cmd.rreq.pathCost = entry->forwardCost;
			nwkRouteReqCmdSend(buf, &hdr, &cmd, NWK_INTERNAL_ROUTE_REQ_CMD_HANDLE);
		}
	}

	return NWK_ROUTE_REQ_RETRY_DELAY;
}

int nwkRouteReqInitialRetry(void *arg)
{
	nwk_routeDiscEntry_t *entry = (nwk_routeDiscEntry_t *)arg;
	nwk_hdr_t hdr;
	nwkCmd_t cmd;
	zb_buf_t *buf;
	u8 retries = entry->retries;

	memset(&cmd, 0, sizeof(cmd));
	entry->retries = (u8)(retries + 1U);

	if (retries >= NWKC_INITIAL_RREQ_RETRIES) {
		if (entry->buf != NULL) {
			zb_buf_free((zb_buf_t *)entry->buf);
			entry->buf = NULL;
		}

		entry->retryTimer = NULL;
		return -1;
	}

	if (entry->buf == NULL) {
		return 0;
	}

	buf = zb_buf_allocate();
	if (buf == NULL) {
		return 0;
	}

	/* The cached frame is copied into the fresh buffer first, then read back
	 * out of it ("52:".."80:"). */
	memcpy(buf, entry->buf, NWK_ROUTE_DISC_CACHE_SIZE);
	nwk_route_disc_cache_load(buf, &hdr, &cmd);

	cmd.cmdId = NWK_CMD_ROUTE_REQUEST;
	cmd.rreq.pathCost = entry->forwardCost;
	nwkRouteReqCmdSend(buf, &hdr, &cmd, NWK_INTERNAL_ROUTE_REQ_CMD_HANDLE);

	return 0;
}

void nwkRouteReplyCmdSend(zb_buf_t *buf, nwk_hdr_t *pNwkHdr, nwkCmd_t *cmd, u8 handle)
{
	u8 *payload;
	u8 payloadLen;

	payloadLen = ((*(u8 *)&cmd->rrep.options & BIT(4)) != 0U) ? 16U : 8U;
	if ((*(u8 *)&cmd->rrep.options & BIT(5)) != 0U) {
		payloadLen = (u8)(payloadLen + 8U);
	}
	TL_BUF_INITIAL_ALLOC(buf, payloadLen, payload, u8 *);

	payload[0] = cmd->cmdId;
	payload[1] = *(u8 *)&cmd->rrep.options;
	payload[2] = cmd->rrep.routeReqId;
	memcpy(payload + 3, &cmd->rrep.originatorAddr, sizeof(cmd->rrep.originatorAddr));
	memcpy(payload + 5, &cmd->rrep.responderAddr, sizeof(cmd->rrep.responderAddr));
	payload[7] = cmd->rrep.pathCost;

	if ((*(u8 *)&cmd->rrep.options & BIT(4)) != 0U) {
		ZB_IEEE_ADDR_COPY(payload + 8, cmd->rrep.originatorIeeeAddr);
	} else if ((*(u8 *)&cmd->rrep.options & BIT(5)) != 0U) {
		ZB_IEEE_ADDR_COPY(payload + 8, cmd->rrep.responderIeeeAddr);
	}

	buf->hdr.handle = handle;
	nwk_tx(buf, pNwkHdr, pNwkHdr->dstAddr, 0, payload, payloadLen);
}

void nwkRouteRecordCmdSend(zb_buf_t *buf, nwk_hdr_t *pNwkHdr, nwkCmd_t *cmd, u8 handle)
{
	u8 payloadLen;
	u8 *payload;

	if (buf == NULL || pNwkHdr == NULL || cmd == NULL) {
		return;
	}

	payloadLen = (u8)(2U + ((u16)cmd->rrec.relayCnt * sizeof(u16)));
	TL_BUF_INITIAL_ALLOC(buf, payloadLen, payload, u8 *);
	if (payload == NULL) {
		return;
	}

	payload[0] = cmd->cmdId;
	payload[1] = cmd->rrec.relayCnt;
	if (cmd->rrec.relayCnt != 0U && cmd->rrec.relayList != NULL) {
		memcpy(payload + 2, cmd->rrec.relayList, (size_t)cmd->rrec.relayCnt * sizeof(u16));
	}

	buf->hdr.handle = handle;
	nwk_fwdPacket(buf, pNwkHdr, payload, payloadLen);
}

/* Caches the frame that started a discovery so the retries can replay it: the
 * whole buffer, then the header and the command payload written over its first
 * 45 bytes ("10:".."34:"). */
zb_buf_t *nwkRouteDiscCachePacketCopy(zb_buf_t *src, nwk_hdr_t *pNwkHdr,
				      const nwkCmd_routeReq_t *cmdTail)
{
	zb_buf_t *buf = zb_buf_allocate();

	if (buf != NULL) {
		memcpy(buf, src, NWK_ROUTE_DISC_CACHE_SIZE);
		memcpy(buf, pNwkHdr, sizeof(*pNwkHdr));
		memcpy((u8 *)buf + sizeof(*pNwkHdr), cmdTail, sizeof(*cmdTail));
		buf->hdr.handle = NWK_INTERNAL_ROUTE_REQ_CMD_HANDLE;
	}

	return buf;
}

/*
 * Sends the route request for a discovery entry that is already set up.
 * Reconstructed from _router/nwk_route_disc.s - it builds the same header as
 * nwkRouteDiscStart and takes the buffer, the request and the entry.
 */
void nwkRouteReqConcentInitiation(zb_buf_t *buf, nlme_routeDisc_req_t *req,
				  nwk_routeDiscEntry_t *entry)
{
	nwk_hdr_t hdr;
	nwkCmd_t cmd;
	u8 security = 0;
	u8 radius;

	memset(&hdr, 0, sizeof(hdr));
	memset(&cmd, 0, sizeof(cmd));

	if (ss_ib_secure_all_fresh() &&
	    ((ss_ib_security_level_get() & SS_IB_SECURITY_LEVEL_ENCRYPTION_MASK) != 0U)) {
		security = ss_keyPreconfigured() ? 1U : 0U;
	}

	hdr.frameControl.protocolVer = ZB_PROTOCOL_VERSION;
	hdr.frameControl.frameType = FRAME_TYPE_COMMAND;
	hdr.frameControl.security = security;
	hdr.frameControl.srcIEEEAddr = 1;
	hdr.dstAddr = NWK_BROADCAST_ROUTER_COORDINATOR;
	hdr.srcAddr = g_zbNIB.nwkAddr;
	ZB_IEEE_ADDR_COPY(hdr.srcIeeeAddr, g_zbInfo.macPib.extAddress);
	hdr.seqNum = g_zbNIB.seqNum++;

	radius = (req->dstAddrMode != ADDR_MODE_NONE) ? req->radius : g_zbNIB.concentratorRadius;
	hdr.radius = (radius != 0U) ? radius : (u8)(g_zbNIB.maxDepth << 1);
	hdr.frameHdrLen = getNwkHdrSize(&hdr);

	cmd.cmdId = NWK_CMD_ROUTE_REQUEST;

	if (req->dstAddrMode == ADDR_MODE_NONE) {
		cmd.rreq.options.manyToOne = req->noRouteCache ? 2U : 1U;
	} else if (req->dstAddrMode == ZB_ADDR_16BIT_DEV_OR_BROADCAST) {
		if ((zb_address_ieee_by_short(req->dstAddr, cmd.rreq.dstIeeeAddr) == 0U) &&
		    !ZB_IEEE_ADDR_IS_ZERO(cmd.rreq.dstIeeeAddr) &&
		    !ZB_IEEE_ADDR_IS_INVALID(cmd.rreq.dstIeeeAddr)) {
			cmd.rreq.options.dstIeeeAddr = 1;
		}
	}

	cmd.rreq.options.multicast = (req->dstAddrMode == ZB_ADDR_16BIT_DEV_OR_BROADCAST);
	cmd.rreq.routeReqId = entry->routeReqId;
	cmd.rreq.dstAddr = entry->dstAddr;
	cmd.rreq.pathCost = 0;

	nwkRouteReqCmdSend(buf, &hdr, &cmd, NWK_INTERNAL_ROUTE_REQ_CMD_HANDLE);
}

int nwkConcentDiscTimerCb(void *arg)
{
	nlme_routeDisc_req_t req;
	nwk_routeDiscEntry_t *entry;
	zb_buf_t *buf;

	if (g_zbNIB.concentratorDiscoveryTime == 0U) {
		concentratorDiscoveryTimer = NULL;
		return -1;
	}

	buf = zb_buf_allocate();
	if (buf == NULL) {
		return 0;
	}

	entry = nwkManyToOneRouteDiscEntryInitFind();

	if (entry == NULL) {
		entry = nwkRouteDiscEntryCreate(g_zbNIB.nwkAddr, NWK_BROADCAST_ROUTER_COORDINATOR,
						g_zbNIB.nwkAddr, 0, 0, g_routeReqId++);
		if (entry == NULL) {
			zb_buf_free(buf);
			return 0;
		}
	} else {
		entry->routeReqId = g_routeReqId++;
		entry->expiry = 10U;
	}

	/* The timer argument carries the noRouteCache flag the discovery was
	 * started with ("4e: tstorerb r7,[r1,#4]"). */
	req.dstAddrMode = 0;
	req.dstAddr = entry->dstAddr;
	req.radius = g_zbNIB.concentratorRadius ? g_zbNIB.concentratorRadius
						: (u8)(g_zbNIB.maxDepth << 1);
	req.noRouteCache = (u8)(uintptr_t)arg;

	nwkRouteReqConcentInitiation(buf, &req, entry);

	return 0;
}

void nwkRouteReplySend(void *arg, u8 radius, u8 routeReqId, u16 originatorAddr, u16 responderAddr,
		       u16 dstAddr, u8 pathCost)
{
	zb_buf_t *buf = (zb_buf_t *)arg;
	nwk_hdr_t hdr;
	nwkCmd_t cmd;
	u8 security = 0;

	memset(&hdr, 0, sizeof(hdr));
	memset(&cmd, 0, sizeof(cmd));

	if (ss_ib_secure_all_fresh() && (ss_ib_security_level_get() != 0U)) {
		security = ss_keyPreconfigured() ? 1U : 0U;
	}

	hdr.dstAddr = dstAddr;
	hdr.srcAddr = g_zbNIB.nwkAddr;
	hdr.frameControl.frameType = FRAME_TYPE_COMMAND;
	hdr.frameControl.protocolVer = ZB_PROTOCOL_VERSION;
	hdr.frameControl.security = security;
	hdr.frameControl.srcIEEEAddr = 1;
	ZB_IEEE_ADDR_COPY(hdr.srcIeeeAddr, g_zbInfo.macPib.extAddress);
	hdr.radius = (radius != 0U) ? radius : 1U;
	hdr.seqNum = g_zbNIB.seqNum++;
	hdr.frameHdrLen = getNwkHdrSize(&hdr);

	cmd.cmdId = NWK_CMD_ROUTE_REPLY;
	cmd.rrep.routeReqId = routeReqId;
	cmd.rrep.originatorAddr = originatorAddr;
	cmd.rrep.responderAddr = responderAddr;
	cmd.rrep.pathCost = pathCost;

	nwkRouteReplyCmdSend(buf, &hdr, &cmd, NWK_INTERNAL_ROUTE_REPLY_CMD_HANDLE);
}

int nwkRouteReplySendDelay(void *arg)
{
	const nwk_route_reply_delay_t *delay = (const nwk_route_reply_delay_t *)arg;

	if (delay != NULL) {
		nwkRouteReplySend(arg, delay->radius, delay->routeReqId, delay->originatorAddr,
				  delay->responderAddr, delay->prevHop, delay->linkCost);
	}

	return -1;
}

/*
 * Reconstructed from _router/nwk_route_disc.s:.text.nwkRouteDiscStart.  Takes
 * the NLME request, builds the NWK header and the route-request command itself
 * and returns a status byte; the reconstruction used to take a buffer and a
 * caller-built header and return a discovery-table entry.
 */
u8 nwkRouteDiscStart(nlme_routeDisc_req_t *req)
{
	zb_buf_t *buf;
	nwk_hdr_t hdr;
	nwkCmd_t cmd;
	nwk_routeDiscEntry_t *entry = NULL;
	nwk_routingTabEntry_t *route;
	u8 radius;
	u8 security = 0;

	g_sysDiags.routeDiscInitiated++;

	buf = zb_buf_allocate();
	if (buf == NULL) {
		return NWK_STATUS_ROUTE_ERROR;
	}

	memset(&hdr, 0, sizeof(hdr));
	memset(&cmd, 0, sizeof(cmd));

	radius = req->radius;
	if ((radius == 0U) && (req->dstAddrMode == ADDR_MODE_NONE)) {
		radius = g_zbNIB.concentratorRadius;
	}

	if (req->dstAddrMode != ADDR_MODE_NONE && ss_ib_secure_all_fresh() &&
	    ((ss_ib_security_level_get() & SS_IB_SECURITY_LEVEL_ENCRYPTION_MASK) != 0U)) {
		security = ss_keyPreconfigured() ? 1U : 0U;
	}

	hdr.frameControl.protocolVer = ZB_PROTOCOL_VERSION;
	hdr.frameControl.frameType = FRAME_TYPE_COMMAND;
	hdr.frameControl.security = security;
	hdr.frameControl.srcIEEEAddr = 1;
	hdr.dstAddr = NWK_BROADCAST_ROUTER_COORDINATOR;
	hdr.srcAddr = g_zbNIB.nwkAddr;
	ZB_IEEE_ADDR_COPY(hdr.srcIeeeAddr, g_zbInfo.macPib.extAddress);
	hdr.seqNum = g_zbNIB.seqNum++;
	hdr.radius = (radius != 0U) ? radius : (u8)(g_zbNIB.maxDepth << 1);
	hdr.frameHdrLen = getNwkHdrSize(&hdr);

	cmd.cmdId = NWK_CMD_ROUTE_REQUEST;

	if (req->dstAddrMode == ADDR_MODE_NONE) {
		/* Many-to-one: one standing discovery, refreshed on every call. */
		cmd.rreq.options.manyToOne = req->noRouteCache ? 2U : 1U;
		cmd.rreq.options.multicast = 0;
		cmd.rreq.pathCost = 0;

		entry = nwkManyToOneRouteDiscEntryInitFind();

		if (entry == NULL) {
			entry = nwkRouteDiscEntryCreate(g_zbNIB.nwkAddr,
							NWK_BROADCAST_ROUTER_COORDINATOR,
							g_zbNIB.nwkAddr, 0, 0, g_routeReqId++);
			if (entry == NULL) {
				zb_buf_free(buf);
				return NWK_STATUS_ROUTE_ERROR;
			}
		} else {
			entry->routeReqId = g_routeReqId++;
			entry->expiry = 10U;
		}

		cmd.rreq.routeReqId = entry->routeReqId;
		cmd.rreq.dstAddr = entry->dstAddr;
		nwkRouteReqCmdSend(buf, &hdr, &cmd, NWK_INTERNAL_ROUTE_REQ_CMD_HANDLE);

		if (concentratorDiscoveryTimer != NULL) {
			ev_timer_taskCancel(&concentratorDiscoveryTimer);
		}

		if (g_zbNIB.concentratorDiscoveryTime != 0U) {
			concentratorDiscoveryTimer = ev_timer_taskPost(
				nwkConcentDiscTimerCb, (void *)(uintptr_t)req->noRouteCache,
				(u32)g_zbNIB.concentratorDiscoveryTime * 1000UL);
		}

		return NWK_STATUS_SUCCESS;
	}

	if (req->dstAddrMode == ZB_ADDR_16BIT_DEV_OR_BROADCAST) {
		/* The destination's IEEE address goes into the command when it is
		 * known and neither all-zero nor all-ones ("27c:".."2b4:"). */
		if ((zb_address_ieee_by_short(req->dstAddr, cmd.rreq.dstIeeeAddr) == 0U) &&
		    !ZB_IEEE_ADDR_IS_ZERO(cmd.rreq.dstIeeeAddr) &&
		    !ZB_IEEE_ADDR_IS_INVALID(cmd.rreq.dstIeeeAddr)) {
			cmd.rreq.options.dstIeeeAddr = 1;
		}
	}

	cmd.rreq.options.multicast = (req->dstAddrMode == ZB_ADDR_16BIT_GROUP);
	cmd.rreq.pathCost = 0;

	route = nwkRoutingTabEntryDstFind(req->dstAddr);

	/* The entry is reusable only when its group flag agrees with the request's
	 * address mode ("1a0:".."24a:"). */
	if ((route != NULL) &&
	    ((route->groupIdFlag != 0U) == (req->dstAddrMode == ZB_ADDR_16BIT_GROUP)) &&
	    (req->dstAddrMode != ADDR_MODE_NONE)) {
		if ((route->status != NWK_ROUTE_STATE_VALIDATION_UNDERWAY) &&
		    (route->status != NWK_ROUTE_STATE_ACTIVE)) {
			route->status = NWK_ROUTE_STATE_DISCOVERY_UNDERWAY;
		}

		route->transFail = 0;
	} else {
		route = nwkRoutingTabEntryCreate(req->dstAddr);

		if (route == NULL) {
			zb_buf_free(buf);
			return NWK_STATUS_ROUTE_ERROR;
		}

		route->groupIdFlag = (req->dstAddrMode == ZB_ADDR_16BIT_GROUP);
	}

	for (u8 i = 0; i < NWK_ROUTE_DISC_TABLE_SIZE; i++) {
		if (g_routeDiscTab[i].dstAddr != req->dstAddr) {
			continue;
		}

		if (!g_routeDiscTab[i].used) {
			continue;
		}

		entry = &g_routeDiscTab[i];
		break;
	}

	if (entry != NULL) {
		if (entry->retryTimer != NULL) {
			/* A discovery for this destination is already running. */
			zb_buf_free(buf);
			return NWK_STATUS_SUCCESS;
		}

		nwkDiscDataPendClear(req->dstAddr, entry->routeReqId);
		entry->routeReqId = g_routeReqId++;
		entry->expiry = 10U;
		entry->retries = 0;
		entry->forwardCost = 0;
		entry->residCost = 0xffU;
	} else {
		entry = nwkRouteDiscEntryCreate(g_zbNIB.nwkAddr, req->dstAddr, g_zbNIB.nwkAddr, 0,
						0, g_routeReqId++);

		if (entry == NULL) {
			if (route->status == NWK_ROUTE_STATE_DISCOVERY_UNDERWAY) {
				route->status = NWK_ROUTE_STATE_DISCOVERY_INACTIVE;
			}

			zb_buf_free(buf);
			return NWK_STATUS_ROUTE_ERROR;
		}
	}

	cmd.rreq.routeReqId = entry->routeReqId;
	cmd.rreq.dstAddr = entry->dstAddr;
	nwkRouteReqCmdSend(buf, &hdr, &cmd, NWK_INTERNAL_ROUTE_REQ_CMD_HANDLE);

	if ((NWKC_INITIAL_RREQ_RETRIES == 0U) || !ev_timer_enough()) {
		return NWK_STATUS_SUCCESS;
	}

	entry->buf = nwkRouteDiscCachePacketCopy(buf, &hdr, &cmd.rreq);

	if (entry->buf != NULL) {
		entry->retryTimer = ev_timer_taskPost(nwkRouteReqInitialRetry, entry,
						      NWK_ROUTE_REQ_RETRY_DELAY);
	}

	return NWK_STATUS_SUCCESS;
}

void tl_zbNwkNlmeRouteDiscRequestHandler(void *arg)
{
	nlme_routeDisc_req_t req;
	nlme_routeDisc_cnf_t *cnf = (nlme_routeDisc_cnf_t *)arg;
	u8 status;
	u8 nwkStatusCode = 0;

	memcpy(&req, arg, sizeof(req));

	/* "12:" - only a router may start a discovery. */
	if (!g_zbNIB.capabilityInfo.devType) {
		status = NWK_STATUS_INVALID_REQUEST;
		nwkStatusCode = NWK_COMMAND_STATUS_NO_ROUTING_CAPACITY;
	} else if (req.dstAddrMode == ADDR_MODE_NONE) {
		/* Many-to-one, which only a concentrator may ask for ("44:"). */
		if (!g_zbNIB.isConcentrator) {
			status = NWK_STATUS_INVALID_REQUEST;
			nwkStatusCode = NWK_COMMAND_STATUS_NO_ROUTING_CAPACITY;
		} else {
			status = nwkRouteDiscStart(&req);
		}
	} else if (ZB_NWK_IS_ADDRESS_BROADCAST(req.dstAddr)) {
		status = NWK_STATUS_INVALID_REQUEST;
		nwkStatusCode = NWK_COMMAND_STATUS_MANY_TO_ONE_ROUTE_FAILURE;
	} else if ((req.dstAddrMode == ZB_ADDR_16BIT_DEV_OR_BROADCAST) &&
		   (req.dstAddr == g_zbNIB.nwkAddr)) {
		status = NWK_STATUS_SUCCESS;
	} else if ((req.dstAddrMode == ZB_ADDR_16BIT_GROUP) &&
		   (aps_group_search_by_addr(req.dstAddr) != NULL)) {
		status = NWK_STATUS_SUCCESS;
	} else {
		status = nwkRouteDiscStart(&req);
	}

	cnf->status = status;
	cnf->nwkStatusCode = nwkStatusCode;
	tl_zbTaskPost(zdo_routeDiscCnf, arg);
}

u8 nwkRouteReqDstChk(u16 dstAddr, u8 pathCost)
{
	tl_zb_normal_neighbor_entry_t *entry;

	(void)pathCost;

	if (dstAddr == g_zbNIB.nwkAddr) {
		return 1U;
	}

	entry = nwk_neTblGetByShortAddr(dstAddr);
	if (entry == NULL) {
		return 0U;
	}

	/* The vendor reads the packed capability byte at offset 0x26 and tests
	 * bits 1..6 against 0x14.  NEG+ADC is TC32's logical-not idiom here, not
	 * an absolute-difference calculation. */
	return (u8)(entry->rxOnWhileIdle == 0U && entry->deviceType == NWK_DEVICE_TYPE_ED &&
		    entry->relationship == NEIGHBOR_IS_CHILD);
}

void nwkRouteReqCmdHandler(void *arg, nwk_hdr_t *pNwkHdr, nwkCmd_t *cmd)
{
	nwkCmd_routeReq_t *rreq = &cmd->rreq;
	u16 prevHop = ((zb_mscp_data_ind_t *)arg)->srcAddr.addr.shortAddr;
	tl_zb_normal_neighbor_entry_t *neighbor = nwk_neTblGetByShortAddr(prevHop);
	nwk_routingTabEntry_t *route;
	nwk_routingTabEntry_t *originRoute = NULL;
	nwk_routeDiscEntry_t *entry;
	u8 linkLqi = ((zb_mscp_data_ind_t *)arg)->mpduLinkQuality;
	u8 linkCost;
	u8 totalCost;

	if (neighbor != NULL) {
		linkLqi = neighbor->lqi;
	}

	linkCost = rf_lqi2cost(linkLqi);
	if (linkCost > NWK_COST_THRESHOLD_ONEHOP) {
		zb_buf_free((zb_buf_t *)arg);
		return;
	}

	/* With symmetric links the vendor uses the neighbor's outgoing cost,
	 * while retaining the one-hop threshold when the received cost is at
	 * least as good. */
	if (g_zbNIB.symLink != 0U && rreq->options.multicast == 0U && neighbor != NULL) {
		if (neighbor->outgoingCost == 0U) {
			linkCost = NWK_COST_THRESHOLD_ONEHOP;
		} else if (linkCost < neighbor->outgoingCost) {
			linkCost = neighbor->outgoingCost;
		}
	}

	totalCost = (u8)(rreq->pathCost + linkCost);
	route = NULL;

	/* Many-to-one requests do not create/update the destination route here.
	 * For ordinary requests the vendor accepts an existing route only when
	 * its multicast flag matches the request. */
	if (rreq->options.manyToOne == 0U &&
	    nwkRouteReqDstChk(rreq->dstAddr, rreq->pathCost) == 0U) {
		route = nwkRoutingTabEntryDstFind(rreq->dstAddr);
		if (route != NULL &&
		    ((route->groupIdFlag != 0U) == (rreq->options.multicast != 0U))) {
			if (route->status != NWK_ROUTE_STATE_VALIDATION_UNDERWAY &&
			    route->status != NWK_ROUTE_STATE_ACTIVE) {
				route->status = NWK_ROUTE_STATE_DISCOVERY_UNDERWAY;
			}
			route->transFail = 0;
		} else {
			if (g_zbNwkCtx.joinAccept == 0U) {
				zb_buf_free((zb_buf_t *)arg);
				return;
			}

			route = nwkRoutingTabEntryCreate(rreq->dstAddr);
			if (route == NULL) {
				zb_buf_free((zb_buf_t *)arg);
				return;
			}
			route->groupIdFlag = rreq->options.multicast != 0U;
		}
	}

	if (g_zbNIB.symLink != 0U && rreq->options.multicast == 0U) {
		originRoute = nwkRoutingTabEntryDstFind(pNwkHdr->srcAddr);
		if (originRoute != NULL) {
			originRoute->transFail &= 0x0fU;
		}
	}

	entry = nwkRouteDiscEntryFind(pNwkHdr->srcAddr, rreq->routeReqId);
	if (entry != NULL) {
		if (entry->forwardCost <= totalCost) {
			zb_buf_free((zb_buf_t *)arg);
			return;
		}

		entry->senderAddr = prevHop;
		entry->lastCost = linkCost;
		entry->forwardCost = totalCost;
		nwk_route_req_cache_update((zb_buf_t *)entry->buf, pNwkHdr, cmd);
	} else {
		entry = nwkRouteDiscEntryCreate(pNwkHdr->srcAddr, rreq->dstAddr, prevHop, totalCost,
						linkCost, rreq->routeReqId);
		if (entry == NULL) {
			if (nwkRouteReqDstChk(rreq->dstAddr, rreq->pathCost) == 0U) {
				if (route != NULL &&
				    route->status == NWK_ROUTE_STATE_DISCOVERY_UNDERWAY) {
					route->status = NWK_ROUTE_STATE_DISCOVERY_INACTIVE;
				}
				if (originRoute != NULL &&
				    originRoute->status == NWK_ROUTE_STATE_DISCOVERY_UNDERWAY) {
					originRoute->status = NWK_ROUTE_STATE_DISCOVERY_INACTIVE;
				}
				zb_buf_free((zb_buf_t *)arg);
				return;
			}
		} else if (nwkRouteReqDstChk(rreq->dstAddr, rreq->pathCost) == 0U) {
			nwk_route_req_cache_forward(arg, pNwkHdr, cmd, entry);
		}
	}

	if (nwkRouteReqDstChk(rreq->dstAddr, rreq->pathCost) != 0U) {
		nwk_route_req_reply_schedule(arg, pNwkHdr, rreq, prevHop, linkCost);
		return;
	}

	zb_buf_free((zb_buf_t *)arg);
}

void nwkRouteReplyCmdHandler(void *arg, nwk_hdr_t *pNwkHdr, nwkCmd_t *cmd)
{
	nwkCmd_routeReply_t *rrep = &cmd->rrep;
	u16 prevHop = ((zb_mscp_data_ind_t *)arg)->srcAddr.addr.shortAddr;
	nwk_routingTabEntry_t *route = nwkRoutingTabEntryDstFind(rrep->responderAddr);
	nwk_routingTabEntry_t *reverseRoute;
	nwk_routeDiscEntry_t *entry;
	bool routeChanged;

	if (route == NULL) {
		zb_buf_free((zb_buf_t *)arg);
		return;
	}

	if (rrep->originatorAddr == g_zbNIB.nwkAddr) {
		/* A reply for a locally originated request is consumed here.  The
		 * vendor keeps the validation state until a strictly better path is
		 * received and only releases pending data for an active route. */
		zb_buf_free((zb_buf_t *)arg);
		entry = nwkRouteDiscEntryFind(rrep->originatorAddr, rrep->routeReqId);
		if (entry == NULL) {
			if (route->status != NWK_ROUTE_STATE_ACTIVE &&
			    route->status != NWK_ROUTE_STATE_VALIDATION_UNDERWAY) {
				route->status = NWK_ROUTE_STATE_DISCOVERY_FAILED;
			}
			return;
		}

		if (route->status == NWK_ROUTE_STATE_DISCOVERY_UNDERWAY) {
			route->status = route->groupIdFlag ? NWK_ROUTE_STATE_VALIDATION_UNDERWAY
							   : NWK_ROUTE_STATE_ACTIVE;
			route->nextHopAddr = prevHop;
			route->residCost = rrep->pathCost;
			entry->residCost = rrep->pathCost;
		} else if (route->status == NWK_ROUTE_STATE_ACTIVE ||
			   route->status == NWK_ROUTE_STATE_VALIDATION_UNDERWAY) {
			if (entry->residCost > rrep->pathCost) {
				route->nextHopAddr = prevHop;
				route->residCost = rrep->pathCost;
				entry->residCost = rrep->pathCost;
			}
		} else {
			return;
		}

		if (route->status == NWK_ROUTE_STATE_ACTIVE) {
			nwkDiscDataPendSend(entry->dstAddr, entry->routeReqId);
		}
		return;
	}

	entry = nwkRouteDiscEntryFind(rrep->originatorAddr, rrep->routeReqId);
	if (entry == NULL) {
		zb_buf_free((zb_buf_t *)arg);
		return;
	}

	if (entry->residCost < rrep->pathCost) {
		zb_buf_free((zb_buf_t *)arg);
		return;
	}

	if (route->status == NWK_ROUTE_STATE_DISCOVERY_UNDERWAY) {
		route->status = route->groupIdFlag ? NWK_ROUTE_STATE_VALIDATION_UNDERWAY
						   : NWK_ROUTE_STATE_ACTIVE;
	}

	routeChanged = route->nextHopAddr != prevHop;
	route->nextHopAddr = prevHop;
	route->residCost = rrep->pathCost;
	entry->residCost = rrep->pathCost;

	u8 *raw = (u8 *)arg;

	if (g_zbNIB.symLink != 0U) {
		reverseRoute = nwkRoutingTabEntryFind(rrep->originatorAddr);
		/* The vendor only creates a reverse route when no existing
		 * route is found.  It uses the discovery entry sender as the
		 * next hop and leaves an existing route untouched. */
		if (reverseRoute == NULL &&
		    (reverseRoute = nwkRoutingTabEntryCreate(rrep->originatorAddr)) != NULL) {
			reverseRoute->nextHopAddr = entry->senderAddr;
			reverseRoute->status = NWK_ROUTE_STATE_ACTIVE;
		}
	}

	if (routeChanged && route->groupIdFlag != 0U) {
		entry->expiry = (rrep->responderAddr == g_zbNIB.nwkAddr) ? 1U : 10U;
	}

	raw[0] = pNwkHdr->radius;
	raw[1] = rrep->routeReqId;
	memcpy(raw + 2, &rrep->originatorAddr, sizeof(rrep->originatorAddr));
	memcpy(raw + 4, &rrep->responderAddr, sizeof(rrep->responderAddr));
	memcpy(raw + 6, &entry->senderAddr, sizeof(entry->senderAddr));
	raw[8] = (u8)(entry->lastCost + entry->residCost);
	tl_zbTaskPost((tl_zb_callback_t)nwkRouteReplySendDelay, arg);
	return;
}

void nwkRouteRecordInitiation(u16 srcAddr, u16 dstAddr)
{
	zb_buf_t *buf;
	nwk_hdr_t hdr;
	nwkCmd_t cmd;
	u16 addrRef = 0;
	u8 security = 0;

	buf = zb_buf_allocate();
	memset(&hdr, 0, sizeof(hdr));
	memset(&cmd, 0, sizeof(cmd));

	if (ss_ib_secure_all_fresh() && (ss_ib_security_level_get() != 0U)) {
		security = ss_keyPreconfigured() ? 1U : 0U;
	}

	hdr.frameControl.frameType = FRAME_TYPE_COMMAND;
	hdr.frameControl.protocolVer = ZB_PROTOCOL_VERSION;
	hdr.frameControl.security = security;
	hdr.dstAddr = dstAddr;
	hdr.srcAddr = srcAddr;
	hdr.radius = (u8)(g_zbNIB.maxDepth << 1);
	hdr.seqNum = g_zbNIB.seqNum++;

	if (tl_zbExtAddrByShortAddr(srcAddr, hdr.srcIeeeAddr, &addrRef) != RET_OK) {
		hdr.frameControl.srcIEEEAddr = 1;
	}

	if (tl_zbExtAddrByShortAddr(dstAddr, hdr.dstIeeeAddr, &addrRef) != RET_OK) {
		hdr.frameControl.dstIEEEAddr = 1;
	}

	hdr.frameHdrLen = getNwkHdrSize(&hdr);

	cmd.cmdId = NWK_CMD_ROUTE_RECORD;
	cmd.rrec.relayCnt = (srcAddr == g_zbNIB.nwkAddr) ? 0U : 1U;
	cmd.rrec.relayList = (u8 *)&g_zbNIB.nwkAddr;

	nwkRouteRecordCmdSend(buf, &hdr, &cmd, NWK_INTERNAL_ROUTE_RECORD_CMD_HANDLE);
}

void nwkRouteRecordCmdHandler(void *arg, nwk_hdr_t *pNwkHdr, nwkCmd_t *cmd)
{
	zb_buf_t *buf = (zb_buf_t *)arg;

	if (pNwkHdr->dstAddr == g_zbNIB.nwkAddr) {
#if defined(ZB_COORDINATOR_ROLE)
		if (g_zbNIB.isConcentrator != 0U) {
			(void)nwkRouteRecTabEntryCreat(pNwkHdr->srcAddr, cmd);
		}
#endif
		zb_buf_free(buf);
		return;
	}

	if (cmd->rrec.relayCnt >= g_zbNIB.maxSourceRoute || pNwkHdr->radius == 0U ||
	    cmd->rrec.relayList == NULL) {
		zb_buf_free(buf);
		return;
	}

	memcpy(cmd->rrec.relayList + ((u16)cmd->rrec.relayCnt * sizeof(u16)), &g_zbNIB.nwkAddr,
	       sizeof(g_zbNIB.nwkAddr));
	cmd->rrec.relayCnt++;
	nwkRouteRecordCmdSend(buf, pNwkHdr, cmd, NWK_INTERNAL_ROUTE_RECORD_CMD_HANDLE);
}

/* Turns a frame that has no route into an NLME route-discovery request
 * (_router/nwk_route_disc.s:.text.nwkTxDataRouteDiscStart). */
u8 nwkTxDataRouteDiscStart(nwk_hdr_t *pNwkHdr)
{
	nlme_routeDisc_req_t req;

	req.dstAddr = pNwkHdr->dstAddr;
	req.dstAddrMode = pNwkHdr->frameControl.multicastFlg ? ZB_ADDR_16BIT_DEV_OR_BROADCAST
							     : ZB_ADDR_16BIT_GROUP;
	req.radius = pNwkHdr->radius;
	req.noRouteCache = 1;

	return nwkRouteDiscStart(&req);
}

void nwkRouteDiscStatusCodeSend(zb_buf_t *buf, u16 srcAddr, u16 dstAddr, u8 status)
{
	nwk_hdr_t hdr;
	nwkCmd_t cmd;
	u8 security = 0;

	if (buf == NULL) {
		return;
	}

	memset(&hdr, 0, sizeof(hdr));
	memset(&cmd, 0, sizeof(cmd));

	if (ss_ib_secure_all_fresh() && (ss_ib_security_level_get() != 0U)) {
		security = ss_keyPreconfigured() ? 1U : 0U;
	}

	hdr.dstAddr = srcAddr;
	hdr.srcAddr = g_zbNIB.nwkAddr;
	hdr.frameControl.frameType = FRAME_TYPE_COMMAND;
	hdr.frameControl.protocolVer = ZB_PROTOCOL_VERSION;
	hdr.frameControl.security = security;
	hdr.frameControl.srcIEEEAddr = 1;
	ZB_IEEE_ADDR_COPY(hdr.srcIeeeAddr, g_zbInfo.macPib.extAddress);
	hdr.radius = (u8)(g_zbNIB.maxDepth << 1);
	hdr.seqNum = g_zbNIB.seqNum++;

	cmd.cmdId = NWK_CMD_NETWORK_STATUS;
	cmd.nwkStatus.dstAddr = dstAddr;
	cmd.nwkStatus.statusCode = status;
	hdr.frameHdrLen = getNwkHdrSize(&hdr);
	tl_zbNwkSendNwkStatusCmd(buf, &hdr, &cmd, NWK_INTERNAL_NETWORK_STATUS_CMD_HANDLE);
}

int nwkRouteDiscPeriodic(void *arg)
{
	(void)arg;

	if (g_routeDiscTabCnt == 0U) {
		return 0;
	}

	for (u8 i = 0; i < NWK_ROUTE_DISC_TABLE_SIZE; i++) {
		nwk_routeDiscEntry_t *entry = &g_routeDiscTab[i];

		if (!nwk_route_disc_used(entry)) {
			continue;
		}

		if (entry->expiry != 0U) {
			entry->expiry--;
		}

		if (entry->expiry == 0U) {
			nwk_routingTabEntry_t *route = NULL;

			if (!ZB_NWK_IS_ADDRESS_BROADCAST(entry->dstAddr)) {
				route = nwkRoutingTabEntryDstFind(entry->dstAddr);
			}

			if (route == NULL) {
				nwkDiscDataPendClear(entry->dstAddr, entry->routeReqId);
			} else {
				switch (route->status) {
				case NWK_ROUTE_STATE_ACTIVE:
					nwkDiscDataPendSend(entry->dstAddr, entry->routeReqId);
					break;
				case NWK_ROUTE_STATE_DISCOVERY_UNDERWAY:
					route->status = NWK_ROUTE_STATE_DISCOVERY_FAILED;
					nwkDiscDataPendClear(entry->dstAddr, entry->routeReqId);
					break;
				case NWK_ROUTE_STATE_VALIDATION_UNDERWAY:
					if (entry->srcAddr == g_zbNIB.nwkAddr) {
						if (nwkDiscDataPendSend(entry->dstAddr,
									entry->routeReqId) != 0U) {
							zb_buf_t *buf = zb_buf_allocate();

							if (buf != NULL) {
								nwkRouteDiscStatusCodeSend(
									buf, entry->dstAddr,
									entry->dstAddr,
									NWK_COMMAND_STATUS_VALIDATE_ROUTE);
							}
						}
					} else {
						nwkDiscDataPendClear(entry->dstAddr,
								     entry->routeReqId);
					}
					break;
				default:
					nwkDiscDataPendClear(entry->dstAddr, entry->routeReqId);
					break;
				}
			}
			nwkRouteDiscTabEntryClear(entry);
		}
	}

	return 0;
}

#else

#include "zb_common.h"

#endif
