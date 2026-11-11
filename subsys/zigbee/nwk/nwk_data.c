/*
 * Copyright 2026 libzigbee
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "zb_common.h"
#include "zb_nwk_core.h"
#include "nwk_brc.h"
#include "nwk_data.h"
#include "nwk_endDev_timeout.h"
#include "nwk_join.h"
#include "nwk_leave.h"
#include "zb_nwk_neighbor.h"
#include "nwk_panid_conflict.h"
#include "nwk_pend.h"
#include "nwk_route_disc.h"
#include "nwk_routing.h"
#include "aps_data.h"
#include "mac.h"
#include "mac_associate.h"
#include "mac_data.h"
#include "ss_nwkEnDecrypt.h"
#include "ss_zdoSecurityME.h"
#include <stdint.h>
#include "ev_timer.h"

uint8_t g_edBrcSkipParent = 0;
nwkDataIndCb_t g_nwkDataIndCb = NULL;

#if !defined(ZB_ROUTER_ROLE)
uint8_t quickDataPollCnt = 0;
ev_timer_event_t *quickDataPollTimerEvt = NULL;
#endif

#if defined(ZB_ROUTER_ROLE)
static void nwk_tx_data_pend_route_disc_fail(nwk_txDataPendEntry_t *pend)
{
	if (pend == NULL) {
		return;
	}

	if (pend->srcBuf != NULL) {
		void *srcBuf = pend->srcBuf;

		pend->srcBuf = NULL;
		if (pend->handle < NWK_INTERNAL_NSDU_HANDLE) {
			nwkNldeDataCnf(srcBuf, NWK_STATUS_ROUTE_DISCOVERY_FAILED, pend->handle);
		} else {
			zb_buf_free((zb_buf_t *)srcBuf);
		}
	}

	nwkTxDataPendTabEntryClear(pend);
}

#endif

#if UINTPTR_MAX == UINT32_MAX
STATIC_ASSERT(OFFSETOF(zb_mscp_data_ind_t, timestamp) == 0);
STATIC_ASSERT(OFFSETOF(zb_mscp_data_ind_t, msdu) == 4);
STATIC_ASSERT(OFFSETOF(zb_mscp_data_ind_t, srcAddr) == 12);
STATIC_ASSERT(OFFSETOF(zb_mscp_data_ind_t, dstAddr) == 21);
STATIC_ASSERT(OFFSETOF(zb_mscp_data_ind_t, msduLength) == 30);
STATIC_ASSERT(OFFSETOF(zb_mscp_data_ind_t, mpduLinkQuality) == 31);
STATIC_ASSERT(OFFSETOF(zb_mscp_data_req_t, dstPanId) == 0);
STATIC_ASSERT(OFFSETOF(zb_mscp_data_req_t, srcAddr) == 2);
STATIC_ASSERT(OFFSETOF(zb_mscp_data_req_t, dstAddr) == 11);
STATIC_ASSERT(OFFSETOF(zb_mscp_data_req_t, msduLength) == 20);
STATIC_ASSERT(OFFSETOF(zb_mscp_data_req_t, msduHandle) == 21);
STATIC_ASSERT(OFFSETOF(zb_mscp_data_req_t, msdu) == 22);
STATIC_ASSERT(OFFSETOF(zb_mscp_data_req_t, txOptions) == 26);
STATIC_ASSERT(OFFSETOF(nlde_data_ind_t, nsdu) == 0);
STATIC_ASSERT(OFFSETOF(nlde_data_ind_t, nsduLen) == 4);
STATIC_ASSERT(OFFSETOF(nlde_data_ind_t, dstAddrMode) == 5);
STATIC_ASSERT(OFFSETOF(nlde_data_ind_t, securityUse) == 6);
STATIC_ASSERT(OFFSETOF(nlde_data_ind_t, lqi) == 7);
STATIC_ASSERT(OFFSETOF(nlde_data_ind_t, dstAddr) == 8);
STATIC_ASSERT(OFFSETOF(nlde_data_ind_t, srcAddr) == 10);
STATIC_ASSERT(OFFSETOF(nlde_data_ind_t, rxTime) == 12);
STATIC_ASSERT(OFFSETOF(nlde_data_ind_t, srcMacAddr) == 16);
STATIC_ASSERT(sizeof(zb_mscp_data_conf_t) == 14);
#endif
STATIC_ASSERT(OFFSETOF(nlde_data_cnf_t, status) == 4);
STATIC_ASSERT(OFFSETOF(nlde_data_cnf_t, nsduHandle) == 5);
STATIC_ASSERT(OFFSETOF(nwk_hdr_t, dstAddr) == 0);
STATIC_ASSERT(OFFSETOF(nwk_hdr_t, srcAddr) == 2);
STATIC_ASSERT(OFFSETOF(nwk_hdr_t, frameControl) == 4);
STATIC_ASSERT(OFFSETOF(nwk_hdr_t, radius) == 6);
STATIC_ASSERT(OFFSETOF(nwk_hdr_t, seqNum) == 7);
STATIC_ASSERT(OFFSETOF(nwk_hdr_t, dstIeeeAddr) == 8);
STATIC_ASSERT(OFFSETOF(nwk_hdr_t, srcIeeeAddr) == 16);
STATIC_ASSERT(OFFSETOF(nwk_hdr_t, mcastControl) == 24);
STATIC_ASSERT(OFFSETOF(nwk_hdr_t, frameHdrLen) == 25);
STATIC_ASSERT(OFFSETOF(nwkCmd_t, cmdId) == 0);
STATIC_ASSERT(OFFSETOF(nwkCmd_t, nwkStatus) == 4);
STATIC_ASSERT(OFFSETOF(nwkCmd_t, leave) == 4);
STATIC_ASSERT(OFFSETOF(nwkCmd_t, rejoinReq) == 4);
STATIC_ASSERT(OFFSETOF(nwkCmd_t, endDevTimeoutReq) == 4);
STATIC_ASSERT(OFFSETOF(nwkCmd_t, rejoinRsp) == 4);
STATIC_ASSERT(OFFSETOF(nwkCmd_t, nwkUpdate) == 4);
STATIC_ASSERT(OFFSETOF(nwkCmd_t, endDevTimeoutRsp) == 4);

static inline bool nwk_joined(void)
{
	return g_zbNwkCtx.joined != 0U;
}

#if !defined(ZB_ROUTER_ROLE)
static int zb_NwkQuickDataPollCb(void *arg)
{
	(void)arg;

	if (quickDataPollCnt++ < AUTO_QUICK_DATA_POLL_TIMES &&
	    AUTO_QUICK_DATA_POLL_INTERVAL != 0U) {
		endDevMacDataPoll();
		return (int)AUTO_QUICK_DATA_POLL_INTERVAL;
	}

	quickDataPollTimerEvt = NULL;
	return -1;
}
#endif

void zb_nwkDataIndRegister(nwkDataIndCb_t cb)
{
	g_nwkDataIndCb = cb;
}

void zb_edBrcDataSkipParentSet(bool skip)
{
	g_edBrcSkipParent = skip;
}

void nwkNldeDataCnf(void *arg, uint8_t status, uint8_t nsduHandle)
{
	nlde_data_cnf_t *cnf = (nlde_data_cnf_t *)arg;

	cnf->status = status;
	cnf->nsduHandle = nsduHandle;
	zb_TaskPost(aps_nwk_data_confirm_cb, arg);
}

void nwkNldeDataInd(void *arg, nwk_hdr_t *pNwkHdr)
{
	zb_mscp_data_ind_t *macInd = (zb_mscp_data_ind_t *)arg;
	nlde_data_ind_t ind;

	memset(&ind, 0, sizeof(ind));
	/*
	 * The NLDE indication carries an APS destination addressing mode, not a
	 * copy of the NWK multicast flag: a multicast frame is delivered to a
	 * group (APS_SHORT_GROUPADDR_NOEP), a normal one to an endpoint
	 * (APS_SHORT_DSTADDR_WITHEP).  Numbering them the other way round made
	 * aps_nwk_data_indication_cb() rewrite every unicast as a group frame,
	 * overwriting the parsed destination endpoint with the NWK address and
	 * skipping the profile match, so ZDO requests were answered as group
	 * traffic and then discarded.
	 */
	ind.dstAddrMode = pNwkHdr->frameControl.multicastFlg ? APS_SHORT_GROUPADDR_NOEP
							     : APS_SHORT_DSTADDR_WITHEP;
	ind.dstAddr = pNwkHdr->dstAddr;
	ind.srcAddr = pNwkHdr->srcAddr;
	ind.nsduLen = (uint8_t)(macInd->msduLength - pNwkHdr->frameHdrLen);
	ind.nsdu = macInd->msdu + pNwkHdr->frameHdrLen;
	ind.lqi = macInd->mpduLinkQuality;
	ind.srcMacAddr = macInd->srcAddr.addr.shortAddr;
	ind.securityUse = pNwkHdr->frameControl.security ? 1U : 0U;

	if (g_zbInfo.nwkNib.timeStamp) {
		ind.rxTime = macInd->timestamp;
	}

	memcpy(arg, &ind, sizeof(ind));

	if (g_nwkDataIndCb != NULL) {
		g_nwkDataIndCb(arg);
	}
	zb_TaskPost(aps_nwk_data_indication_cb, arg);
}

uint8_t zb_NwkInterPanDataReq(void *arg)
{
	nlde_data_req_t *req = (nlde_data_req_t *)arg;
	zb_mscp_data_req_t macReq;
	uint8_t nwkStubHdr[2] = {
		FRAME_TYPE_INTERPAN | (2U << 2),
		0,
	};

	memset(&macReq, 0, sizeof(macReq));
	memcpy(req->nsdu - sizeof(nwkStubHdr), nwkStubHdr, sizeof(nwkStubHdr));

	macReq.srcAddr.addrMode = ADDR_MODE_EXT;
	macReq.dstAddr.addrMode = req->addrMode;
	macReq.msdu = req->nsdu - sizeof(nwkStubHdr);
	macReq.msduLength = (uint8_t)(req->nsduLen + sizeof(nwkStubHdr));
	macReq.msduHandle = req->ndsuHandle;

	if (dstPanID == 0U || deviceInfoRsp == 0U) {
		macReq.dstPanId = MAC_PAN_ID_BROADCAST;
	} else {
		deviceInfoRsp = 0;
		macReq.dstPanId = dstPanID;
	}

	if (req->addrMode == ADDR_MODE_EXT) {
		macReq.txOptions = 1;
		ZB_IEEE_ADDR_COPY(macReq.dstAddr.addr.extAddr, req->ieeAddr);
	} else {
		macReq.dstAddr.addr.shortAddr = MAC_SHORT_ADDR_BROADCAST;
	}

	memcpy(arg, &macReq, sizeof(macReq));
	zb_MacMcpsDataRequestProc(arg);
	return RET_OK;
}

void zb_MacInterPanDataHandle(void *arg)
{
	zb_TaskPost(aps_interPanDataIndCb, arg);
}

void zb_MacMcpsDataConfirmHandler(void *arg)
{
	/*
	 * The confirm is copied off the buffer first because the buffer itself is
	 * reused further down.  The msdu *pointer* is loaded out of that
	 * copy and the frame it points at is parsed, not the address of the
	 * pointer field itself, which would parse the pointer's own four
	 * bytes as a header.
	 */
	zb_mscp_data_conf_t conf;
	nwk_hdr_t nwkHdr;
	uint8_t handle;
	uint8_t status;

	memset(&conf, 0, sizeof(conf));
	memcpy(&conf, arg, sizeof(conf));
	nwkHdrParse(&nwkHdr, conf.msdu);

	status = conf.status;

#if defined(ZB_ROUTER_ROLE)
	/*
	 * The router and coordinator objects share one body that has nothing in
	 * common with the end-device one: link accounting for the neighbour the
	 * frame went to, transmit-failure accounting for the route it used, and a
	 * route repair when either of them gives up.  Broadcasts skip all of it
	 * ("32:".."3c:" tests the 0xfff8 mask).
	 */
	if (!ZB_NWK_IS_ADDRESS_BROADCAST(nwkHdr.dstAddr)) {
		uint16_t macDstAddr = conf.macDstAddr;
		nwk_txDataPendEntry_t *pend;
		zb_normal_neighbor_entry_t *nbr;

		pend = nwkTxDataPendTabEntryFind(nwkHdr.srcAddr, nwkHdr.dstAddr, conf.msduHandle,
						 nwkHdr.seqNum);
		if (pend != NULL) {
			nwkTxDataPendTabEntryClear(pend);
		}

		nbr = nwk_neTblGetByShortAddr(macDstAddr);

		/* "6a:".."74:" - an end-device neighbour skips straight to the
		 * handle dispatch. */
		if ((nbr == NULL) || (nbr->deviceType != NWK_DEVICE_TYPE_ED)) {
			nwk_routingTabEntry_t *route;
			bool repair = FALSE;

			if (nbr != NULL) {
				if (status == MAC_SUCCESS) {
					nbr->transFailure = 0;
					nbr->age = 0;
				} else if (status == MAC_STA_NO_ACK) {
					uint8_t prev = nbr->transFailure;

					nbr->transFailure = (uint8_t)(prev + 1U);

					if (prev >= NWKC_TRANSFAILURE_CNT_THRESHOLD) {
						nbr->outgoingCost = 0;

						if (nwkHdr.frameControl.srcRoute) {
							nbr->transFailure = 0;
							repair = TRUE;
						}
					}
				}
			}

			route = nwkRoutingTabEntryDstActiveGet(nwkHdr.dstAddr);

			if (route == NULL) {
				/* Nothing knows this destination any more. */
				if ((nbr == NULL) && nwkHdr.frameControl.srcRoute) {
					repair = TRUE;
				}
			} else if (status == MAC_SUCCESS) {
				route->transFail = 0;
			} else if (status == MAC_STA_NO_ACK) {
				uint8_t prev = route->transFail;

				route->transFail = (uint8_t)(prev + 1U);

				if (prev >= NWKC_TRANSFAILURE_CNT_THRESHOLD) {
					route->transFail = 0;
					repair = TRUE;
				}
			}

			if (repair && (status == MAC_STA_NO_ACK)) {
				nwkRouteMaintenance(&nwkHdr, macDstAddr);
			}
		}
	}
#else
	if (g_zbInfo.macPib.rxOnWhenIdle == 0U) {
		if (AUTO_QUICK_DATA_POLL_ENABLE && status == MAC_STA_FRAME_PENDING) {
			endDevMacDataPoll();
		}
	} else {
		uint16_t shortAddr = conf.macDstAddr;
		zb_normal_neighbor_entry_t *entry = nwk_neTblGetByShortAddr(shortAddr);

		if (entry != NULL && entry->relationship == NEIGHBOR_IS_PARENT &&
		    status == MAC_STA_NO_ACK) {
			zb_buf_t *buf = zb_buf_allocate();

			if (buf != NULL) {
				zb_NwkNlmeNwkStatusInd(buf, shortAddr,
							 NWK_COMMAND_STATUS_PARENT_LINK_FAILURE);
			}
		}
	}
#endif

	handle = conf.msduHandle;
	if (handle < NWK_INTERNAL_NSDU_HANDLE) {
		nwkNldeDataCnf(arg, status, handle);
		return;
	}

	/* The dispatch tables differ per role as well. */
	switch (handle) {
	case NWK_INTERNAL_NSDU_HANDLE:
	case NWK_INTERNAL_DATA_RECEIVED_HANDLE:
	case NWK_INTERNAL_LEAVE_REQ_CMD_INDIRECT_HANDLE:
		zb_buf_free((zb_buf_t *)arg);
		return;
	case NWK_INTERNAL_REJOIN_REQ_CMD_HANDLE:
		nwk_rejoinCmdSendCnf(arg);
		return;
	case NWK_INTERNAL_LEAVE_REQ_CMD_HANDLE:
		nwk_leaveCmdSendCnf(arg, nwkHdr.dstAddr);
		return;
#if defined(ZB_ROUTER_ROLE)
	case NWK_INTERNAL_REJOIN_RESP_CMD_HANDLE:
		zb_McpsRejoinRespCnfHandler(arg, status, nwkHdr.dstAddr);
		return;
	case NWK_INTERNAL_NETWORK_UPDATE_CMD_PAN_ID_UPDATE_HANDLE:
		zb_buf_free((zb_buf_t *)arg);
		if (status == MAC_SUCCESS) {
			zb_NwkPanidConflictSetPanidStart();
		}
		return;
	case NWK_INTERNAL_NETWORK_REPORT_CMD_HANDLE:
	case NWK_INTERNAL_ROUTE_REPLY_CMD_HANDLE:
		zb_buf_free((zb_buf_t *)arg);
		return;
	case NWK_INTERNAL_ENDDEVTIMEOUT_RSP_CMD_HANDLE:
		nwkEndDevTimeoutRspCnfHandler(arg);
		return;
#else
	case NWK_INTERNAL_ENDDEVTIMEOUT_REQ_CMD_HANDLE:
		nwkEndDevTimeoutReqCnfHandler(arg);
		return;
#endif
	default:
		zb_buf_free((zb_buf_t *)arg);
		return;
	}
}

void nwk_tx(zb_buf_t *buf, nwk_hdr_t *pNwkHdr, uint16_t nextHop, uint8_t indirect, uint8_t *payload, uint8_t payloadLen)
{
	zb_mscp_data_req_t *req;
	uint8_t nwkHdrLen;
	uint8_t *frameStart;
	uint8_t savedHandle;
	if (buf == NULL || pNwkHdr == NULL || payload == NULL) {
		return;
	}

	savedHandle = buf->hdr.handle;
	nwkHdrLen = getNwkHdrSize(pNwkHdr);
	pNwkHdr->frameHdrLen = nwkHdrLen;
	frameStart = payload - nwkHdrLen;
	nwkHdrBuilder(frameStart, pNwkHdr);

	req = (zb_mscp_data_req_t *)buf;
	memset(req, 0, sizeof(*req));
	req->srcAddr.addrMode = ADDR_MODE_SHORT;
	req->dstAddr.addrMode = ADDR_MODE_SHORT;
	req->dstAddr.addr.shortAddr = nextHop;
	req->dstPanId = g_zbInfo.macPib.panId;
	req->msduLength = (uint8_t)(nwkHdrLen + payloadLen);
	req->msdu = frameStart;
	req->msduHandle = savedHandle;

	if (nextHop != MAC_SHORT_ADDR_BROADCAST) {
		req->txOptions = MAC_TX_OPTION_ACKNOWLEDGED_BIT;
	}

	/* Bit 2 of the transmit options is
	 * MAC_TX_OPTION_INDIRECT_TRANSMISSION_BIT, so the caller is asking the MAC
	 * to hold the frame until the destination polls, not to request an
	 * acknowledgement. */
	if (indirect != 0U) {
		req->txOptions |= MAC_TX_OPTION_INDIRECT_TRANSMISSION_BIT;
	}

	if (!nwk_joined() && savedHandle != NWK_INTERNAL_REJOIN_REQ_CMD_HANDLE &&
	    savedHandle != NWK_INTERNAL_LEAVE_REQ_CMD_HANDLE) {
		zb_MacMcpsDataRequestSendConfirm(buf, MAC_STA_BAD_STATE);
		return;
	}

	if (pNwkHdr->frameControl.security) {
		if (ss_nwkSecureFrame(buf, pNwkHdr->frameHdrLen) != RET_OK) {
			g_sysDiags.nwkTxEnDecryptFail++;
			zb_MacMcpsDataRequestSendConfirm(buf, NWK_STATUS_DECRYPT_ERROR);
			return;
		}
	}

	g_sysDiags.nwkTxCnt++;
	zb_MacMcpsDataRequestProc(buf);
}

void nwk_fwdPacket(zb_buf_t *buf, nwk_hdr_t *pNwkHdr, uint8_t *payload, uint8_t payloadLen)
{
	uint16_t nextHop;
	uint8_t handle;

	if (buf == NULL) {
		ZB_EXCEPTION_POST(SYS_EXCEPTTION_ZB_BUFFER_EXCEPTION);
		return;
	}

	if (buf->hdr.used == 0U) {
		ZB_EXCEPTION_POST(SYS_EXCEPTTION_ZB_BUFFER_EXCEPTION);
		return;
	}

	handle = buf->hdr.handle;

	if (pNwkHdr->radius == 0U) {
		ZB_EXCEPTION_POST(SYS_EXCEPTTION_ZB_NWK_ROUTE_TABLE);
		return;
	}

#if defined(ZB_ROUTER_ROLE)
	nwk_txDataPendEntry_t *pending = NULL;

	if (pNwkHdr->frameControl.frameType == FRAME_TYPE_DATA &&
	    handle < NWK_INTERNAL_NSDU_HANDLE) {
		pending = nwkTxDataPendTabEntryAdd(buf, pNwkHdr, payload, payloadLen, handle);
		if (pending != NULL && pending->srcBuf == buf) {
			ZB_EXCEPTION_POST(SYS_EXCEPTTION_ZB_NWK_ROUTE_TABLE);
		}
	}

	if (ZB_NWK_IS_ADDRESS_BROADCAST(pNwkHdr->dstAddr)) {
		nwk_brcTransRecordEntry_t *trans;

		/* "166:".."1a2:" - link status and route request commands, and a
		 * one-hop broadcast of our own, go out without a broadcast record.
		 */
		if (pNwkHdr->frameControl.frameType == FRAME_TYPE_COMMAND &&
		    (payload[0] == NWK_CMD_LINK_STATUS || payload[0] == NWK_CMD_ROUTE_REQUEST)) {
			goto brc_tx;
		}
		if (pNwkHdr->srcAddr == g_zbInfo.nwkNib.nwkAddr && pNwkHdr->radius == 1U) {
			nwkTxDataPendTabEntryClear(pending);
			goto brc_tx;
		}

		/* "194:".."1b8:", "2c4:".."2fa:" - the receive path records every
		 * broadcast it relays, so an existing record is the normal case and
		 * the frame still goes out.  Only an internal frame without a record
		 * is dropped.  An internal frame takes the pending entry of the
		 * record, any other one hands its own entry to the record.
		 */
		trans = nwkBrcTransEntryFind(pNwkHdr->srcAddr, pNwkHdr->seqNum);
		if (trans == NULL) {
			if (handle == NWK_INTERNAL_NSDU_HANDLE) {
				zb_buf_free(buf);
				return;
			}
			trans = nwkBrcTransEntryCreate(pending, pNwkHdr->srcAddr, pNwkHdr->seqNum);
			if (trans == NULL) {
				/* "3e0:".."3f6:" */
				nwkTxDataPendTabEntryClear(pending);
				if (handle < NWK_INTERNAL_NSDU_HANDLE) {
					nwkNldeDataCnf(buf, NWK_STATUS_BT_TABLE_FULL, handle);
				} else {
					zb_buf_free(buf);
				}
				return;
			}
			pending = trans->entry;
		} else if (handle == NWK_INTERNAL_NSDU_HANDLE) {
			pending = trans->entry;
		} else {
			trans->entry = pending;
		}

		/* "1ba:".."1d8:", "350:".."3de:" - a record whose retry timer already
		 * runs, or one without a pending entry, needs no new timers.  The
		 * timers are started before the frame is sent; without timer room
		 * the record drops its passive-ACK buffer and pending entry.
		 */
		if (pending != NULL && trans->retryTimer == NULL) {
			if (ev_timer_enough()) {
				if (pNwkHdr->dstAddr == NWK_BROADCAST_ALL_DEVICES &&
				    trans->retries == 0U) {
					nwkBrcMsgAllEndDevStart(trans);
				}
				nwkBrcMsgPassiveAckTimeoutStart(trans);
			} else {
				if (trans->passiveAckAddr != NULL) {
					ev_buf_free((uint8_t *)trans->passiveAckAddr);
					trans->passiveAckAddr = NULL;
				}
				nwkTxDataPendTabEntryClear(trans->entry);
				trans->entry = NULL;
			}
		}

brc_tx:
		nwk_tx(buf, pNwkHdr, MAC_SHORT_ADDR_BROADCAST, 0, payload, payloadLen);
		return;
	}

	{
		zb_normal_neighbor_entry_t *neighbor;
		zb_normal_neighbor_entry_t *srcNeighbor;
		nwk_routingTabEntry_t *route;
		nwk_routeDiscEntry_t *entry;
		nwk_txDataPendEntry_t *pend;

#if defined(ZB_COORDINATOR_ROLE)
		/* A concentrator keeps the reverse path learned by route-record
		 * commands.  The vendor turns that path into a source-route subframe
		 * before selecting the first relay. */
		if (pNwkHdr->frameControl.srcRoute == 0U && pNwkHdr->radius > 1U) {
			nwk_routeRecordTabEntry_t *record =
				nwkRouteRecTabEntryFind(pNwkHdr->dstAddr);

			if (record != NULL && record->relayCnt > 1U) {
				pNwkHdr->frameControl.srcRoute = 1;
				pNwkHdr->srcRouteSubframe.relayCnt = record->relayCnt;
				pNwkHdr->srcRouteSubframe.relayIdx = (uint8_t)(record->relayCnt - 1U);
				pNwkHdr->srcRouteSubframe.relayList = (uint8_t *)record->path;
			}
		}
#endif

		if (pNwkHdr->frameControl.srcRoute != 0U) {
			uint8_t indirect = 0;

			nextHop = nwkSrcRouteReplayNextHop(pNwkHdr);
			if (pNwkHdr->srcRouteSubframe.relayIdx == 0U) {
				neighbor = nwk_neTblGetByShortAddr(nextHop);
				if (neighbor != NULL) {
					indirect = neighbor->rxOnWhileIdle ? 0U : 1U;
				}
			}

			if (nextHop == MAC_ADDR_USE_EXT &&
			    pNwkHdr->srcRouteSubframe.relayIdx != 0U) {
				if (handle < NWK_INTERNAL_NSDU_HANDLE) {
					nwkNldeDataCnf(buf, NWK_STATUS_ROUTE_ERROR, handle);
				} else {
					zb_buf_free(buf);
				}
				return;
			}

			if (nextHop == 0U || nextHop == MAC_ADDR_USE_EXT) {
				if (handle < NWK_INTERNAL_NSDU_HANDLE) {
					nwkNldeDataCnf(buf, NWK_STATUS_ROUTE_ERROR, handle);
				} else {
					zb_buf_free(buf);
				}
				return;
			}

			nwk_tx(buf, pNwkHdr, nextHop, indirect, payload, payloadLen);
			return;
		}

		neighbor = nwkValidNeighborToFwd(pNwkHdr->dstAddr);
		route = nwkRoutingTabGetNextHop(pNwkHdr);

		/* The decision is made on the neighbor and route pointers.  Marking "no next hop"
		 * with the address 0 makes the coordinator's own short address look like a missing
		 * route, so every frame a router sends to the trust center went to route discovery
		 * instead of the parent. */
		if (neighbor != NULL) {
			nextHop = pNwkHdr->dstAddr;
		} else if (route != NULL) {
			nextHop = route->nextHopAddr;
		} else {
			nextHop = MAC_ADDR_USE_EXT;
		}

		if (route != NULL && route->manyToOne != 0U && route->routeRecordRequired != 0U) {
			srcNeighbor = nwk_neTblGetByShortAddr(pNwkHdr->srcAddr);
			if (pNwkHdr->srcAddr == g_zbInfo.nwkNib.nwkAddr ||
			    (srcNeighbor != NULL &&
			     (((uint8_t)(srcNeighbor->rxOnWhileIdle | (srcNeighbor->deviceType << 1) |
				    (srcNeighbor->relationship << 4)) &
			       0x7eU) == 0x14U))) {
				nwkRouteRecordInitiation(pNwkHdr->srcAddr, pNwkHdr->dstAddr);
				if (route->transFail == 0U || (route->transFail & 0x08U) == 0U) {
					route->routeRecordRequired = 0;
				}
			}
		}

		if ((neighbor == NULL && route == NULL) || nextHop == MAC_ADDR_USE_EXT) {
			if (route == NULL && pNwkHdr->frameControl.discRoute != 0U) {
				pend = pending;
				if (pend != NULL &&
				    nwkTxDataRouteDiscStart(pNwkHdr) == NWK_STATUS_SUCCESS) {
					entry = nwkRouteDiscEntryDstFind(pNwkHdr->dstAddr);
					if (entry != NULL) {
						pend->needRouteDisc = 1;
						pend->routeReqId = entry->routeReqId;
						zb_buf_free(buf);
						return;
					}
				}

				if (pend != NULL) {
					zb_buf_free(buf);
					nwk_tx_data_pend_route_disc_fail(pend);
				} else if (handle < NWK_INTERNAL_NSDU_HANDLE) {
					nwkNldeDataCnf(buf, NWK_STATUS_ROUTE_ERROR, handle);
				} else {
					zb_buf_free(buf);
				}
				return;
			}

			if (handle < NWK_INTERNAL_NSDU_HANDLE) {
				nwkNldeDataCnf(buf, NWK_STATUS_ROUTE_ERROR, handle);
			} else {
				zb_buf_free(buf);
			}
			return;
		}

		/* Ask for an indirect transmission when the neighbour this frame goes
		 * to sleeps between polls, and for a direct one when the route decides
		 * the next hop.  Transmitting to a
		 * sleeping child immediately means nobody is listening: its receiver
		 * only opens for a moment after each data request. */
		nwk_tx(buf, pNwkHdr, nextHop,
		       (neighbor != NULL && neighbor->rxOnWhileIdle == 0U) ? 1U : 0U, payload,
		       payloadLen);
		return;
	}
#else
	if (ZB_NWK_IS_ADDRESS_BROADCAST(pNwkHdr->dstAddr)) {
		if (g_edBrcSkipParent != 0U ||
		    (!nwk_joined() && handle == NWK_INTERNAL_LEAVE_REQ_CMD_HANDLE)) {
			nextHop = MAC_SHORT_ADDR_BROADCAST;
		} else {
			nextHop = zb_NeighborParentShortAddrGet();
		}
	} else {
		nextHop = zb_NeighborParentShortAddrGet();
	}

	if (nextHop == MAC_ADDR_USE_EXT) {
		if (handle < NWK_INTERNAL_NSDU_HANDLE) {
			nwkNldeDataCnf(buf, NWK_STATUS_ROUTE_ERROR, handle);
		} else {
			zb_buf_free(buf);
		}
		return;
	}

	nwk_tx(buf, pNwkHdr, nextHop, 0, payload, payloadLen);
#endif
}

void zb_MacMcpsDataIndicationHandler(void *arg)
{
	zb_buf_t *buf = (zb_buf_t *)arg;
	zb_mscp_data_ind_t *ind = (zb_mscp_data_ind_t *)arg;
	nwk_hdr_t nwkHdr;
	nwkCmd_t cmd;
	uint16_t macSrcAddr;
	uint16_t macDstAddr;
	uint8_t frameType;
	uint8_t payloadTotalLen;
	uint8_t *payload;
#if defined(ZB_ROUTER_ROLE)
	nwk_brcTransRecordEntry_t *brcTrans;
	nwk_routingTabEntry_t *activeRoute;
	zb_normal_neighbor_entry_t *neighbor;
#endif

	/* Routers and coordinators still accept frames tagged with the
	 * broadcast PAN ID while leaving.  The vendor takes this exception
	 * before checking the user state; end devices do not have it. */
	if (ZB_GET_USER_STATE == NLME_LEAVING
#if defined(ZB_ROUTER_ROLE)
	    && ind->dstPanId != MAC_PAN_ID_BROADCAST
#endif
	) {
		zb_buf_free(buf);
		return;
	}

#if defined(ZB_ROUTER_ROLE)
	/* A broadcast-PAN frame is also the MAC ingress for Green Power.  The
	 * vendor routes GP frames by the NWK protocol nibble before parsing a
	 * normal NWK header; protocol 2 is allowed through only for inter-PAN. */
	if (ind->dstPanId == MAC_PAN_ID_BROADCAST) {
		uint8_t frameControl = ind->msdu[0];
		uint8_t protocolVersion = (uint8_t)((frameControl >> 2) & 0x0fU);

		if (protocolVersion == ZB_PROTOCOL_VERSION_GP) {
			zb_TaskPost(cGp_mcpsDataInd, arg);
			return;
		}

		if (protocolVersion != ZB_PROTOCOL_VERSION ||
		    (frameControl & 0x03U) != FRAME_TYPE_INTERPAN) {
			zb_buf_free(buf);
			return;
		}
	}
#endif

	memset(&nwkHdr, 0, sizeof(nwkHdr));
	nwkHdr.frameHdrLen = nwkHdrParse(&nwkHdr, ind->msdu);

	if (ind->msduLength <= nwkHdr.frameHdrLen) {
		zb_buf_free(buf);
		return;
	}

	frameType = nwkHdr.frameControl.frameType;
	if (!nwk_joined() && ZB_GET_USER_STATE != NLME_JOINING) {
		if (frameType != FRAME_TYPE_INTERPAN) {
			zb_buf_free(buf);
			return;
		}

		zb_MacInterPanDataHandle(arg);
		return;
	}

	if (frameType == FRAME_TYPE_INTERPAN) {
		zb_MacInterPanDataHandle(arg);
		return;
	}

	if (nwkHdr.frameControl.protocolVer != ZB_PROTOCOL_VERSION) {
		g_sysDiags.packetValidateDropCount++;
		zb_buf_free(buf);
		return;
	}

	macSrcAddr = ind->srcAddr.addr.shortAddr;
	macDstAddr = ind->dstAddr.addr.shortAddr;
	if (ZB_NWK_IS_ADDRESS_BROADCAST(macDstAddr)) {
#if !defined(ZB_ROUTER_ROLE)
		if (g_zbInfo.macPib.rxOnWhenIdle != 0U) {
			if (macSrcAddr != zb_NeighborParentShortAddrGet()) {
				g_sysDiags.packetValidateDropCount++;
				zb_buf_free(buf);
				return;
			}
		} else {
			g_sysDiags.packetValidateDropCount++;
			zb_buf_free(buf);
			return;
		}
#endif
	}

	if (ZB_NWK_IS_ADDRESS_BROADCAST(nwkHdr.dstAddr)) {
		if (!nwkBrcCheckDevMatch(nwkHdr.dstAddr)) {
			g_sysDiags.packetValidateDropCount++;
			zb_buf_free(buf);
			return;
		}
	} else {
		/* Router and coordinator reject only a unicast we sent ourselves;
		 * one for another node is relayed by nwk_fwdPacket() further down.
		 * The ED object also rejects any other destination. */
		if (nwkHdr.srcAddr == g_zbInfo.nwkNib.nwkAddr ||
#if !defined(ZB_ROUTER_ROLE)
		    nwkHdr.dstAddr != g_zbInfo.nwkNib.nwkAddr ||
#endif
		    nwkHdr.frameControl.endDevInitiator != 0U) {
			g_sysDiags.packetValidateDropCount++;
			zb_buf_free(buf);
			return;
		}
	}

	if (nwkHdr.frameControl.security != 0U) {
		if (ss_nwkDecryptFrame(arg, nwkHdr.frameHdrLen, ind->msduLength, ind->msdu, &nwkHdr,
				       ind->mpduLinkQuality) != RET_OK) {
			return;
		}
		payloadTotalLen = (uint8_t)(ind->msduLength - 4U);
	} else {
		payloadTotalLen = ind->msduLength;
	}

	/* Store the plaintext length back into the indication.  Leaving the ciphertext
	 * length in place made nwkNldeDataInd() hand APS four extra bytes, so an
	 * APS-secured command failed its own integrity check. */
	ind->msduLength = payloadTotalLen;

	nwkHdr.radius--;
	payload = ind->msdu + nwkHdr.frameHdrLen;
	buf->hdr.handle = NWK_INTERNAL_DATA_RECEIVED_HANDLE;

	memset(&cmd, 0, sizeof(cmd));
	if (frameType == FRAME_TYPE_COMMAND) {
		cmd.cmdId = payload[0];

		switch (cmd.cmdId) {
		case NWK_CMD_ROUTE_REQUEST:
			*(uint8_t *)&cmd.rreq.options = payload[1];
			cmd.rreq.routeReqId = payload[2];
			cmd.rreq.dstAddr = (uint16_t)payload[3] | ((uint16_t)payload[4] << 8);
			cmd.rreq.pathCost = payload[5];
			break;
		case NWK_CMD_ROUTE_REPLY:
			*(uint8_t *)&cmd.rrep.options = payload[1];
			cmd.rrep.routeReqId = payload[2];
			cmd.rrep.originatorAddr = (uint16_t)payload[3] | ((uint16_t)payload[4] << 8);
			cmd.rrep.responderAddr = (uint16_t)payload[5] | ((uint16_t)payload[6] << 8);
			cmd.rrep.pathCost = payload[7];
			break;
		case NWK_CMD_NETWORK_STATUS:
			cmd.nwkStatus.dstAddr = (uint16_t)payload[1] | ((uint16_t)payload[2] << 8);
			cmd.nwkStatus.statusCode = payload[3];
			break;
		case NWK_CMD_LEAVE:
			*(uint8_t *)&cmd.leave = payload[1];
			break;
		case NWK_CMD_ROUTE_RECORD:
			cmd.rrec.relayCnt = payload[1];
			cmd.rrec.relayList = payload + 2;
			break;
		case NWK_CMD_REJOIN_RESPONSE:
			cmd.rejoinRsp.nwkAddr = (uint16_t)payload[1] | ((uint16_t)payload[2] << 8);
			cmd.rejoinRsp.rejoinStatus = payload[3];
			break;
		case NWK_CMD_REJOIN_REQUEST:
			*(uint8_t *)&cmd.rejoinReq.capabilityInfo = payload[1];
			break;
		case NWK_CMD_LINK_STATUS:
			*(uint8_t *)&cmd.linkSt.options = payload[1];
			cmd.linkSt.linkStatusList = (linkStatus_entry_t *)(payload + 2);
			break;
		case NWK_CMD_NETWORK_REPORT:
			*(uint8_t *)&cmd.nwkReport.options = payload[1];
			ZB_EXTPANID_COPY(cmd.nwkReport.epid, payload + 2);
			cmd.nwkReport.panIds = payload + 10;
			break;
		case NWK_CMD_NETWORK_UPDATE:
			*(uint8_t *)&cmd.nwkUpdate.options = payload[1];
			ZB_EXTPANID_COPY(cmd.nwkUpdate.epid, payload + 2);
			cmd.nwkUpdate.newPanId = (uint16_t)payload[11] | ((uint16_t)payload[12] << 8);
			cmd.nwkUpdate.updateId = payload[10];
			break;
		case NWK_CMD_ENDDEVTIMEOUT_RESPONSE:
			cmd.endDevTimeoutRsp.status = payload[1];
			cmd.endDevTimeoutRsp.parentInfo = payload[2];
			break;
#if defined(ZB_ROUTER_ROLE)
		case NWK_CMD_ENDDEVTIMEOUT_REQUEST:
			cmd.endDevTimeoutReq.reqTimeoutEnum = payload[1];
			cmd.endDevTimeoutReq.endDevCfg = payload[2];
			break;
#endif
		default:
			break;
		}
	}

	if (nwk_joined() && ss_ib_security_level_get() != 0U && !nwkHdr.frameControl.security) {
		if (frameType == FRAME_TYPE_DATA) {
			zb_buf_free(buf);
			return;
		}

		if (frameType == FRAME_TYPE_COMMAND && cmd.cmdId != NWK_CMD_REJOIN_REQUEST) {
			zb_buf_free(buf);
			return;
		}
	}

#if !defined(ZB_ROUTER_ROLE)
	mac_pendingWaitTimerCancel();

	if (buf->hdr.pending && AUTO_QUICK_DATA_POLL_ENABLE && g_zbInfo.macPib.rxOnWhenIdle == 0U) {
		buf->hdr.pending = 0;
		endDevMacDataPoll();
	}
#endif

	if (NWK_HEADER_SRC_IEEE_INCLUDE && nwkHdr.frameControl.security &&
	    nwkHdr.frameControl.srcIEEEAddr != 0U) {
		uint16_t addrRef = 0;

		(void)zb_NwkAddrMapAdd(nwkHdr.srcAddr, nwkHdr.srcIeeeAddr, &addrRef);
	}

#if defined(ZB_ROUTER_ROLE)
	neighbor = nwk_neTblGetByShortAddr(macSrcAddr);
	if (neighbor != NULL) {
		neighbor->lqi = zb_nwkGetAverageLqi(neighbor->lqi, ind->mpduLinkQuality);
	}
#endif

	if (frameType == FRAME_TYPE_DATA) {
#if defined(ZB_ROUTER_ROLE)
		if (ZB_NWK_IS_ADDRESS_BROADCAST(nwkHdr.dstAddr)) {
			if (!nwkBrcCheckDevMatch(nwkHdr.dstAddr)) {
				g_sysDiags.packetValidateDropCount++;
				zb_buf_free(buf);
				return;
			}

			brcTrans = nwkBrcTransEntryFind(nwkHdr.srcAddr, nwkHdr.seqNum);
			if (brcTrans != NULL) {
				nwkBrcMsgAddSender(brcTrans, macSrcAddr);
				zb_buf_free(buf);
				return;
			}

			brcTrans = nwkBrcTransEntryCreate(NULL, nwkHdr.srcAddr, nwkHdr.seqNum);
			if (brcTrans == NULL) {
				zb_buf_free(buf);
				return;
			}
			nwkBrcMsgAddSender(brcTrans, macSrcAddr);

			activeRoute = nwkRoutingTabEntryDstActiveGet(macSrcAddr);
			if (activeRoute != NULL) {
				nwkRoutingTabEntryClear(activeRoute);
			}

			if (nwkHdr.radius != 0U) {
				nwkBrcTransJitterSet(NWK_BRC_JITTER);
				if (nwkBrcTimerStart(buf, &nwkHdr, payload,
						     (uint8_t)(payloadTotalLen - nwkHdr.frameHdrLen)) !=
				    NWK_STATUS_SUCCESS) {
					nwkBrcTransTabEntryClear(brcTrans);
					zb_buf_free(buf);
					return;
				}
			}

			nwkNldeDataInd(arg, &nwkHdr);
			return;
		}
#endif

		if (nwkHdr.dstAddr == g_zbInfo.nwkNib.nwkAddr) {
			nwkNldeDataInd(arg, &nwkHdr);
			return;
		}

		if (nwkHdr.radius == 0U) {
			zb_buf_free(buf);
			return;
		}

#if defined(ZB_ROUTER_ROLE)
		if (nwkHdr.frameControl.srcRoute != 0U &&
		    nwkSourceRoutePacketRelayFilter(&nwkHdr) != 0U) {
			zb_buf_free(buf);
			return;
		}
#endif

		g_sysDiags.relayedUcast++;

#if defined(ZB_ROUTER_ROLE)
		activeRoute = nwkRoutingTabEntryDstActiveGet(nwkHdr.dstAddr);
		if (activeRoute != NULL && activeRoute->nextHopAddr == macSrcAddr) {
			nwkRoutingTabEntryClear(activeRoute);
		}
#endif

		nwk_fwdPacket(buf, &nwkHdr, payload, (uint8_t)(payloadTotalLen - nwkHdr.frameHdrLen));
		return;
	}

#if defined(ZB_ROUTER_ROLE)
	if (nwkHdr.frameControl.frameType == FRAME_TYPE_INTERPAN) {
		zb_TaskPost(cGp_mcpsDataInd, arg);
		return;
	}
#endif

	if (nwkHdr.frameControl.frameType != FRAME_TYPE_COMMAND) {
		zb_buf_free(buf);
		return;
	}

	switch (cmd.cmdId) {
#if !defined(ZB_ROUTER_ROLE)
	case NWK_CMD_ENDDEVTIMEOUT_RESPONSE:
		nwkEndDevTimeoutRspCmdHandler(arg, &nwkHdr, &cmd);
		return;
#endif
#if defined(ZB_ROUTER_ROLE)
	case NWK_CMD_ROUTE_REQUEST:
		nwkRouteReqCmdHandler(arg, &nwkHdr, &cmd);
		return;
	case NWK_CMD_ROUTE_REPLY:
		nwkRouteReplyCmdHandler(arg, &nwkHdr, &cmd);
		return;
	case NWK_CMD_ROUTE_RECORD:
		nwkRouteRecordCmdHandler(arg, &nwkHdr, &cmd);
		return;
	case NWK_CMD_LINK_STATUS:
		zb_NwkLinkStatusCmdHandler(arg, &nwkHdr, &cmd);
		return;
	case NWK_CMD_NETWORK_REPORT:
		zb_NwkReportCmdHandler(arg, &nwkHdr, &cmd);
		return;
#endif
#if defined(ZB_ROUTER_ROLE)
	case NWK_CMD_ENDDEVTIMEOUT_REQUEST:
		nwkEndDevTimeoutReqCmdHandler(arg, &nwkHdr, &cmd);
		return;
#endif
	case NWK_CMD_NETWORK_UPDATE:
		zb_NwkNetworkUpdateCmdHandler(arg, &nwkHdr, &cmd);
		return;
	case NWK_CMD_REJOIN_RESPONSE:
		zb_NwkRejoinRespCmdHandler(arg, &nwkHdr, &cmd);
		return;
#if defined(ZB_ROUTER_ROLE)
	case NWK_CMD_REJOIN_REQUEST:
		zb_NwkRejoinReqCmdHandler(arg, &nwkHdr, &cmd);
		return;
#endif
	case NWK_CMD_LEAVE:
		zb_NwkLeaveReqCmdHandler(arg, &nwkHdr, &cmd);
		return;
	case NWK_CMD_NETWORK_STATUS:
		zb_NwkStatusCmdHandler(arg, &nwkHdr, &cmd);
		return;
	default:
		zb_buf_free(buf);
		return;
	}
}

void zb_NwkNldeDataRequestHandler(void *arg)
{
	zb_buf_t *buf = (zb_buf_t *)arg;
	nlde_data_req_t *req = (nlde_data_req_t *)arg;
	nwk_hdr_t nwkHdr;
	uint8_t *fc = (uint8_t *)&nwkHdr.frameControl;
	uint16_t srcAddr;
	uint8_t radius;
	bool multicast;

	/* The vendor library only reads g_zbNwkCtx here: the joined flag and the
	 * low nibble of the state byte (user_state).  It never
	 * touches g_zbNIB.secAllFrames in this function. */
	if ((!nwk_joined()) || (ZB_GET_USER_STATE != NLME_IDLE)) {
		if (req->ndsuHandle < NWK_INTERNAL_NSDU_HANDLE) {
			nwkNldeDataCnf(arg, NWK_STATUS_INVALID_REQUEST, req->ndsuHandle);
		} else {
			zb_buf_free(buf);
		}
		return;
	}

	if (req->dstAddr == g_zbInfo.nwkNib.nwkAddr) {
		nwkNldeDataCnf(arg, NWK_STATUS_INVALID_PARAMETER, req->ndsuHandle);
		return;
	}

	memset(&nwkHdr, 0, sizeof(nwkHdr));
	buf->hdr.handle = req->ndsuHandle;

	fc[0] = (FRAME_TYPE_DATA & 0x03U) | (2U << 2);
	fc[0] |= (uint8_t)(req->discoverRoute << 6);
	/* NLDE addrMode 1 is group delivery; 2 and 3 are short and extended
	 * unicast and must not set the NWK multicast bit.  The vendor library
	 * tests for equality with 1 here, not for a plain nonzero value. */
	multicast = req->addrMode == ZB_ADDR_16BIT_GROUP;
	fc[1] = multicast ? 1U : 0U;

	if (req->securityEnable && ss_ib_security_level_get() != 0U) {
		nwkHdr.frameControl.security = 1;
	}

	if (NWK_HEADER_SRC_IEEE_INCLUDE) {
		fc[1] |= 0x10U;
		/* The MAC PIB extended address is used here, not the NIB copy. */
		ZB_IEEE_ADDR_COPY(nwkHdr.srcIeeeAddr, g_zbInfo.macPib.extAddress);
	}

	if (g_zbInfo.nwkNib.parentInfo != 0U) {
		fc[1] |= 0x20U;
	}

	nwkHdr.dstAddr = req->dstAddr;
	srcAddr = req->useAlias ? req->aliasSrcAddr : g_zbInfo.nwkNib.nwkAddr;
	nwkHdr.srcAddr = srcAddr;

	radius = req->radius;
	if (radius == 0U) {
		radius = g_zbInfo.nwkNib.maxDepth;
	}
	nwkHdr.radius = radius;

	if (req->useAlias) {
		nwkHdr.seqNum = req->aliasSeqNum;
	} else {
		nwkHdr.seqNum = g_zbInfo.nwkNib.seqNum++;
	}

	if (multicast) {
		uint8_t nonmemberRadius = req->nonmemberRadius & 0x07U;

		if (aps_group_search_by_addr(req->dstAddr)) {
			nwkHdr.mcastControl.multicastMode = 1U;
		} else {
			nwkHdr.mcastControl.multicastMode = 0U;
		}

		nwkHdr.mcastControl.nonmemberRadius = nonmemberRadius;
		nwkHdr.mcastControl.maxNonmemberRadius = nonmemberRadius;
	}

	if (ZB_NWK_IS_ADDRESS_BROADCAST(req->dstAddr)) {
		fc[1] &= (uint8_t)~0x10U;
	}

	if (req->unicastSkipRouting) {
		nwk_tx(buf, &nwkHdr, req->dstAddr, 0, req->nsdu, req->nsduLen);
	} else {
		nwk_fwdPacket(buf, &nwkHdr, req->nsdu, req->nsduLen);
	}

#if !defined(ZB_ROUTER_ROLE)
	if (AUTO_QUICK_DATA_POLL_ENABLE && AUTO_QUICK_DATA_POLL_INTERVAL != 0U &&
	    AUTO_QUICK_DATA_POLL_TIMES != 0U && g_zbInfo.macPib.rxOnWhenIdle == 0U) {
		quickDataPollCnt = 0;
		if (quickDataPollTimerEvt != NULL) {
			ev_timer_taskCancel(&quickDataPollTimerEvt);
		}
		quickDataPollTimerEvt = ev_timer_taskPost(
			zb_NwkQuickDataPollCb, NULL,
			(((uint32_t)g_zbInfo.macPib.respWaitTime * 15U) << 10) / 1000U);
	}
#endif
}
