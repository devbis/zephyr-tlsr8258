/*
 * Copyright 2026 libzigbee
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "zb_common.h"
#include "zb_af.h"
#include "nwk_data.h"
#include "nwk_join.h"
#include "zb_nwk_neighbor.h"
#include "nwk_nlme.h"
#include "nwk_route_disc.h"
#include "zdo_nwk_manager.h"
#include "zdp_services.h"
#include "ev_timer.h"
#include "ss_security_flags.h"

static u8 rejoinRespPollCnt;
static ev_timer_event_t *rejoinRespTimeoutEvt;

static void nwk_rejoinReq(void *arg);

static inline u32 rejoin_resp_timeout_ms(void)
{
	return (((u32)g_zbInfo.macPib.respWaitTime * 5U) << 10) / 1000U;
}

static inline u8 nwk_end_device_initiator_bit(void)
{
	return (g_zbInfo.nwkNib.parentInfo > 0U) ? (u8)(g_zbInfo.nwkNib.parentInfo - 1U) : 0U;
}

static inline void nwk_copy_scan_req(zb_mac_mlme_scan_req_t *scanReq, nlme_join_req_t *joinReq,
				     u8 scanType)
{
	scanReq->scanChannels = joinReq->scanChannels;
	scanReq->scanType = scanType;
	scanReq->scanDuration = joinReq->scanDuration;
	scanReq->channelPage = 0;
}

void nwk_nlmeJoinCnf(void *arg, u8 status)
{
	nlme_join_cnf_t *cnf = (nlme_join_cnf_t *)arg;

	cnf->status = status;

	if (status == NWK_STATUS_SUCCESS) {
		cnf->activeChannel = g_zbInfo.macPib.phyChannelCur;
		cnf->nwkAddr = g_zbInfo.nwkNib.nwkAddr;
		ZB_EXTPANID_COPY(cnf->extPANId, g_zbInfo.nwkNib.extPANId);
	}

	/* "34: tmovs r3,#47; 36: tloadrb r1,[r2,r3]; 38: tmovs r0,#15; 3a: tands
	 * r1,r0" - the vendor clears the high nibble of g_zbNwkCtx+47, which is
	 * the NLME state, and keeps the low nibble, the user state.  The device
	 * stays NLME_JOINING until the trust center authenticates it. */
	g_zbNwkCtx.state = NLME_STATE_IDLE;
	tl_zbTaskPost(zdo_nlme_join_confirm, arg);
}

static tl_zb_addition_neighbor_entry_t *tl_zbNwkParentChoose(extPANId_t extPanId, bool rejoin)
{
	tl_zb_addition_neighbor_entry_t *best = NULL;
	u8 entryNum = tl_zbAdditionNeighborTableNumGet();

	for (u8 i = 0; i < entryNum; i++) {
		tl_zb_addition_neighbor_entry_t *entry = tl_zbAdditionNeighborEntryGetFromIdx(i);

		if (entry == NULL) {
			continue;
		}

		if (entry->lqi < NWK_NEIGHBORTBL_ADD_LQITHRESHOLD) {
			continue;
		}

		if (!ZB_EXTPANID_IS_ZERO(extPanId) && !ZB_EXTPANID_CMP(extPanId, entry->extPanId)) {
			continue;
		}

		if (rejoin) {
			if (PRE_PARENT_FIRST_WHEN_REJOIN &&
			    entry->shortAddr == g_zbInfo.macPib.coordShortAddress &&
			    entry->panId == g_zbInfo.macPib.panId && entry->potentialParent) {
				best = entry;
				break;
			}

			if (!entry->permitJoining || !entry->potentialParent) {
				continue;
			}
		} else {
			if (!entry->permitJoining || !entry->potentialParent) {
				continue;
			}
		}

		if (best == NULL || best->lqi < entry->lqi) {
			best = entry;
		}
	}

	if (best != NULL) {
#if defined(ZB_ROUTER_ROLE) || defined(ZB_COORDINATOR_ROLE)
		if (best->stackProfile == 1U) {
			g_zbNwkCtx.joined_pro = 1;
			g_zbInfo.nwkNib.capabilityInfo.devType = 0;
		}
#endif
		best->potentialParent = 0;
	}

	return best;
}

static int tl_zbNlmeRejoinRespTimeoutCb(void *arg)
{
	(void)arg;

	if (rejoinRespPollCnt++ <= 2U) {
		tl_zb_addition_neighbor_entry_t *parent = g_zbNwkCtx.join.pRejoinParent;

		if (g_zbInfo.macPib.rxOnWhenIdle == 0U && parent != NULL) {
			zb_buf_t *buf = zb_buf_allocate();

			if (buf != NULL) {
				mac_mlme_poll_req_t *pollReq = (mac_mlme_poll_req_t *)buf->buf;
				u16 dst = parent->shortAddr;

				pollReq->coordAddrMode = ADDR_MODE_SHORT;
				pollReq->coordAddr.shortAddr = dst;
				pollReq->coordPanId = g_zbInfo.macPib.panId;
				tl_zbPrimitivePost(TL_Q_NWK2MAC, MAC_MLME_POLL_REQ, buf);
			}
		}

		return 0;
	}

	{
		zb_buf_t *buf = zb_buf_allocate();

		if (buf != NULL) {
			tl_zbTaskPost((tl_zb_callback_t)nwk_rejoinReq, buf);
		}
	}

	rejoinRespPollCnt = 0;
	rejoinRespTimeoutEvt = NULL;
	return -1;
}

void tl_zbNwkSendRejoinReqCmd(zb_buf_t *buf, nwk_hdr_t *pNwkHdr, nwkCmd_t *cmd, u8 handle)
{
	u8 *payload;

	TL_BUF_INITIAL_ALLOC(buf, 2, payload, u8 *);

	payload[0] = cmd->cmdId;
	payload[1] = *(u8 *)&cmd->rejoinReq.capabilityInfo;
	buf->hdr.handle = handle;

	nwk_tx(buf, pNwkHdr, pNwkHdr->dstAddr, 0, payload, 2);
}

void tl_zbNwkSendRejoinRespCmd(zb_buf_t *buf, nwk_hdr_t *pNwkHdr, nwkCmd_t *cmd, u8 ack, u8 handle)
{
	u8 *payload;

	TL_BUF_INITIAL_ALLOC(buf, 4, payload, u8 *);

	payload[0] = cmd->cmdId;
	memcpy(payload + 1, &cmd->rejoinRsp.nwkAddr, sizeof(cmd->rejoinRsp.nwkAddr));
	payload[3] = cmd->rejoinRsp.rejoinStatus;
	buf->hdr.handle = handle;

	nwk_tx(buf, pNwkHdr, pNwkHdr->dstAddr, ack, payload, 4);
}

static void nwk_associateJoin(void *arg)
{
	nlme_join_req_t *req = (nlme_join_req_t *)arg;
	tl_zb_addition_neighbor_entry_t *parent;
	zb_mlme_associate_req_t *macReq;

	if (g_zbNwkCtx.state != NLME_STATE_IDLE) {
		nwk_nlmeJoinCnf(arg, NWK_STATUS_INVALID_REQUEST);
		return;
	}

	parent = tl_zbNwkParentChoose(req->extPANId, FALSE);
	if (parent == NULL) {
		nwk_nlmeJoinCnf(arg, NWK_STATUS_NOT_PERMITTED);
		return;
	}
	if ((*(u8 *)&req->capabilityInfo & MAC_CAP_ALLOCATE_ADDRESS) == 0U) {
		/* ZDO normally fills this from the AF node descriptor. Keep the
		 * association request valid if a platform join shim omitted the
		 * mandatory address-allocation capability. */
		*(u8 *)&req->capabilityInfo = af_nodeMacCapabilityGet();
	}

	g_zbNwkCtx.join.pAssocJoinParent = parent;
	g_zbInfo.nwkNib.panId = parent->panId;
	/* "3c: tstorerb r3,[r6,#24]" - the MAC PIB takes the selected parent's
	 * PAN before the association request is posted, because the poll that
	 * releases the indirect response is built from the PIB. */
	g_zbInfo.macPib.panId = parent->panId;
	/* The poll is also built from the coordinator tuple, which the vendor
	 * fills from the association confirm. Keep it in sync while the join is
	 * in flight, because the native-socket medium delivers the indirect
	 * response before that confirm reaches the MAC. */
	g_zbInfo.macPib.coordShortAddress = parent->shortAddr;
	if (parent->addrMode == ZB_ADDR_64BIT_DEV) {
		ZB_IEEE_ADDR_COPY(g_zbInfo.macPib.coordExtAddress, parent->extAddr);
	}
	g_zbInfo.nwkNib.parentInfo = 0;
	ZB_EXTPANID_COPY(g_zbInfo.nwkNib.extPANId, req->extPANId);
	g_zbInfo.nwkNib.capabilityInfo = req->capabilityInfo;
	g_zbInfo.macPib.rxOnWhenIdle = req->capabilityInfo.rcvOnWhenIdle;

	memset(arg, 0, sizeof(zb_mlme_associate_req_t));
	macReq = (zb_mlme_associate_req_t *)arg;
	macReq->logicalChannel = parent->logicChannel;
	macReq->coordPanId = parent->panId;
	macReq->capbilityInfo = g_zbInfo.nwkNib.capabilityInfo;

	/* "84: tstorerb r3,[r4,#12]" - the coordinator address mode comes from
	 * the chosen parent and selects the addressing the request is built
	 * with; without it the MAC cannot tell which of the two it holds. */
	macReq->coordAddress.addrMode = parent->addrMode;
	if (parent->addrMode == ZB_ADDR_16BIT_DEV_OR_BROADCAST) {
		memcpy(&macReq->coordAddress.addr, &parent->shortAddr, sizeof(parent->shortAddr));
	} else if (parent->addrMode == ZB_ADDR_64BIT_DEV) {
		ZB_IEEE_ADDR_COPY(macReq->coordAddress.addr.extAddr, parent->extAddr);
	}

	g_zbNwkCtx.user_state = NLME_JOINING;
	tl_zbPrimitivePost(TL_Q_NWK2MAC, MAC_MLME_ASSOCIATE_REQ, arg);
}

static void nwk_rejoinReq(void *arg)
{
	nwk_hdr_t nwkHdr;
	nwkCmd_t cmd;
	tl_zb_addition_neighbor_entry_t *parent;
	bool secure = FALSE;

	memset(&nwkHdr, 0, sizeof(nwkHdr));
	memset(&cmd, 0, sizeof(cmd));

	parent = tl_zbNwkParentChoose(g_zbInfo.nwkNib.extPANId, TRUE);
	if (parent == NULL) {
		nwk_nlmeJoinCnf(arg, NWK_STATUS_NOT_PERMITTED);
		return;
	}

	g_zbNwkCtx.join.pRejoinParent = parent;
	tl_zbMacChannelSet(parent->logicChannel);
	g_zbInfo.macPib.coordShortAddress = parent->shortAddr;
	g_zbInfo.nwkNib.panId = parent->panId;

	if (ss_ib_secure_all_fresh() && ss_ib_security_level_get() != 0U && ss_keyPreconfigured()) {
		secure = (aps_ib.aps_use_insecure_join == 0U);
	}

	cmd.cmdId = NWK_CMD_REJOIN_REQUEST;
	cmd.rejoinReq.capabilityInfo = g_zbInfo.nwkNib.capabilityInfo;
	if (cmd.rejoinReq.capabilityInfo.devType == 1U) {
		cmd.rejoinReq.capabilityInfo.allocAddr = 0;
	}

	nwkHdr.frameControl.frameType = FRAME_TYPE_COMMAND;
	nwkHdr.frameControl.protocolVer = ZB_PROTOCOL_VERSION;
	nwkHdr.frameControl.security = secure ? 1U : 0U;
	nwkHdr.frameControl.srcIEEEAddr = 1;
	nwkHdr.frameControl.endDevInitiator = nwk_end_device_initiator_bit();
	nwkHdr.dstAddr = parent->shortAddr;
	nwkHdr.srcAddr = g_zbInfo.nwkNib.nwkAddr;
	nwkHdr.radius = 1;
	nwkHdr.seqNum = g_zbInfo.nwkNib.seqNum++;
	ZB_IEEE_ADDR_COPY(nwkHdr.srcIeeeAddr, g_zbInfo.macPib.extAddress);
	nwkHdr.frameHdrLen = getNwkHdrSize(&nwkHdr);

	tl_zbNwkSendRejoinReqCmd((zb_buf_t *)arg, &nwkHdr, &cmd,
				 NWK_INTERNAL_REJOIN_REQ_CMD_HANDLE);
}

void tl_zbMacMlmeAssociateConfirmHandler(void *arg)
{
	zb_mlme_associate_conf_t *cnf = (zb_mlme_associate_conf_t *)arg;
	tl_zb_addition_neighbor_entry_t *parent = g_zbNwkCtx.join.pAssocJoinParent;
	u16 selfRef = 0;
	u16 parentRef = 0;
	zb_nwk_status_t selfStatus;
	zb_nwk_status_t parentStatus;

	if (cnf->status != MAC_SUCCESS) {
		nlme_join_req_t *req = (nlme_join_req_t *)arg;

		memset(req, 0, sizeof(nlme_join_req_t));
		req->capabilityInfo = g_zbInfo.nwkNib.capabilityInfo;
		ZB_EXTPANID_COPY(req->extPANId, g_zbInfo.nwkNib.extPANId);
		nwk_associateJoin(arg);
		return;
	}

	if (parent == NULL) {
		nwk_nlmeJoinCnf(arg, NWK_STATUS_NEIGHBOR_TABLE_FULL);
		return;
	}

	g_zbInfo.nwkNib.updateId = parent->nwkUpdateId;
	g_zbInfo.nwkNib.depth =
		parent->depth + ((parent->depth < g_zbInfo.nwkNib.maxDepth) ? 1U : 0U);
	g_zbInfo.nwkNib.panId = parent->panId;
	ZB_EXTPANID_COPY(g_zbInfo.nwkNib.extPANId, parent->extPanId);

	g_zbInfo.macPib.shortAddress = cnf->shortAddress;
	g_zbInfo.nwkNib.nwkAddr = cnf->shortAddress;
	ZB_IEEE_ADDR_COPY(g_zbInfo.nwkNib.ieeeAddr, g_zbInfo.macPib.extAddress);

	g_zbInfo.macPib.coordShortAddress = parent->shortAddr;
	ZB_IEEE_ADDR_COPY(g_zbInfo.macPib.coordExtAddress, cnf->parentAddress);

	selfStatus =
		tl_zbNwkAddrMapAdd(g_zbInfo.nwkNib.nwkAddr, g_zbInfo.nwkNib.ieeeAddr, &selfRef);
	parentStatus = tl_zbNwkAddrMapAdd(g_zbInfo.macPib.coordShortAddress,
					  g_zbInfo.macPib.coordExtAddress, &parentRef);
	if ((selfStatus | parentStatus) != NWK_STATUS_SUCCESS) {
		nwk_nlmeJoinCnf(arg, NWK_STATUS_NEIGHBOR_TABLE_FULL);
		return;
	}

	{
		tl_zb_normal_neighbor_entry_t neighbor;

		memset(&neighbor, 0, sizeof(neighbor));
		neighbor.addrmapIdx = parentRef;
		neighbor.rxOnWhileIdle = 1;
		neighbor.deviceType = parent->deviceType;
		neighbor.relationship = NEIGHBOR_IS_PARENT;
		neighbor.used = 1;
		neighbor.depth = parent->depth;
		neighbor.lqi = parent->lqi;
		neighbor.outgoingCost = 1;

		if (tl_zbNeighborTableUpdate(&neighbor, 1) == NULL) {
			nwk_nlmeJoinCnf(arg, NWK_STATUS_NEIGHBOR_TABLE_FULL);
			return;
		}
	}

	g_zbNwkCtx.user_state = NLME_JOINING;
	nwk_nlmeJoinCnf(arg, NWK_STATUS_SUCCESS);
}

void nwk_rejoinScanCnfHandler(void *arg)
{
	zb_mac_mlme_scan_conf_t *cnf = (zb_mac_mlme_scan_conf_t *)arg;

	if (cnf->scanType != ACTIVE_SCAN) {
		zb_buf_free((zb_buf_t *)arg);
		return;
	}

	if (cnf->status != MAC_SUCCESS) {
		nwk_nlmeJoinCnf(arg, cnf->status);
		return;
	}

	while (g_zbInfo.nwkNib.nwkAddr == 0U ||
	       ZB_NWK_IS_ADDRESS_BROADCAST(g_zbInfo.nwkNib.nwkAddr)) {
		g_zbInfo.nwkNib.nwkAddr = (u16)drv_u32Rand();
		g_zbInfo.macPib.shortAddress = g_zbInfo.nwkNib.nwkAddr;
	}

	ZB_IEEE_ADDR_COPY(g_zbInfo.nwkNib.ieeeAddr, g_zbInfo.macPib.extAddress);
	nwk_rejoinReq(arg);
}

void nwk_rejoinCmdSendCnf(void *arg)
{
	u8 status = ((u8 *)arg)[9];

	if (g_zbNwkCtx.state != NLME_STATE_REJOIN) {
		zb_buf_free((zb_buf_t *)arg);
		return;
	}

	if (status == MAC_SUCCESS || status == MAC_STA_FRAME_PENDING) {
		g_zbNwkCtx.user_state = NLME_JOINING;

		if (rejoinRespTimeoutEvt == NULL) {
			rejoinRespTimeoutEvt = ev_timer_taskPost(tl_zbNlmeRejoinRespTimeoutCb, NULL,
								 rejoin_resp_timeout_ms());
		}

		zb_buf_free((zb_buf_t *)arg);
		return;
	}

	nwk_rejoinReq(arg);
}

void tl_zbNwkRejoinRespCmdHandler(void *arg, nwk_hdr_t *pNwkHdr, nwkCmd_t *cmd)
{
	tl_zb_normal_neighbor_entry_t *parent = tl_zbNeighborTableSearchForParent();

	if (g_zbNwkCtx.state != NLME_STATE_REJOIN) {
		zb_buf_free((zb_buf_t *)arg);

		if (parent != NULL && tl_zbshortAddrByIdx(parent->addrmapIdx) == pNwkHdr->srcAddr &&
		    cmd->rejoinRsp.rejoinStatus == MAC_SUCCESS &&
		    !ZB_NWK_IS_ADDRESS_BROADCAST(cmd->rejoinRsp.nwkAddr)) {
			u16 addrRef = 0;

			g_zbInfo.macPib.shortAddress = cmd->rejoinRsp.nwkAddr;
			g_zbInfo.nwkNib.nwkAddr = cmd->rejoinRsp.nwkAddr;
			(void)tl_zbNwkAddrMapAdd(g_zbInfo.nwkNib.nwkAddr, g_zbInfo.nwkNib.ieeeAddr,
						 &addrRef);
			zb_info_save(NULL);
			zdo_device_announce_send();
		}

		return;
	}

	if (g_zbNwkCtx.join.pRejoinParent == NULL || rejoinRespTimeoutEvt == NULL ||
	    g_zbNwkCtx.join.pRejoinParent->shortAddr != pNwkHdr->srcAddr) {
		zb_buf_free((zb_buf_t *)arg);
		return;
	}

	rejoinRespPollCnt = 0;
	ev_timer_taskCancel(&rejoinRespTimeoutEvt);

	if (cmd->rejoinRsp.rejoinStatus != MAC_SUCCESS ||
	    ZB_NWK_IS_ADDRESS_BROADCAST(cmd->rejoinRsp.nwkAddr)) {
		tl_zbTaskPost((tl_zb_callback_t)nwk_rejoinReq, arg);
		return;
	}

	ZB_EXTPANID_COPY(g_zbInfo.nwkNib.extPANId, g_zbNwkCtx.join.pRejoinParent->extPanId);
	g_zbInfo.nwkNib.updateId = g_zbNwkCtx.join.pRejoinParent->nwkUpdateId;
	g_zbInfo.nwkNib.depth =
		g_zbNwkCtx.join.pRejoinParent->depth +
		((g_zbNwkCtx.join.pRejoinParent->depth < g_zbInfo.nwkNib.maxDepth) ? 1U : 0U);

	g_zbInfo.macPib.shortAddress = cmd->rejoinRsp.nwkAddr;
	g_zbInfo.nwkNib.nwkAddr = cmd->rejoinRsp.nwkAddr;
	ZB_IEEE_ADDR_COPY(g_zbInfo.nwkNib.ieeeAddr, g_zbInfo.macPib.extAddress);
	g_zbInfo.macPib.coordShortAddress = pNwkHdr->srcAddr;

	if (pNwkHdr->frameControl.srcIEEEAddr) {
		ZB_IEEE_ADDR_COPY(g_zbInfo.macPib.coordExtAddress, pNwkHdr->srcIeeeAddr);
	}

	if (parent != NULL && pNwkHdr->frameControl.srcIEEEAddr) {
		addrExt_t oldExt;
		u16 oldShort = tl_zbshortAddrByIdx(parent->addrmapIdx);

		tl_zbExtAddrByIdx(parent->addrmapIdx, oldExt);
		if (!ZB_IEEE_ADDR_CMP(oldExt, pNwkHdr->srcIeeeAddr) ||
		    oldShort != pNwkHdr->srcAddr) {
			tl_zbNeighborTableDelete(parent);
			g_zbNwkCtx.parentIsChanged = 1;
		}
	}

	{
		u16 selfRef = 0;
		u16 parentRef = 0;

		if (tl_zbNwkAddrMapAdd(g_zbInfo.nwkNib.nwkAddr, g_zbInfo.nwkNib.ieeeAddr,
				       &selfRef) != NWK_STATUS_SUCCESS ||
		    tl_zbNwkAddrMapAdd(g_zbInfo.macPib.coordShortAddress,
				       g_zbInfo.macPib.coordExtAddress,
				       &parentRef) != NWK_STATUS_SUCCESS) {
			nwk_nlmeJoinCnf(arg, NWK_STATUS_NEIGHBOR_TABLE_FULL);
			return;
		}

		{
			tl_zb_normal_neighbor_entry_t neighbor;

			memset(&neighbor, 0, sizeof(neighbor));
			neighbor.addrmapIdx = parentRef;
			neighbor.rxOnWhileIdle = 1;
			neighbor.deviceType = g_zbNwkCtx.join.pRejoinParent->deviceType;
			neighbor.relationship = NEIGHBOR_IS_PARENT;
			neighbor.used = 1;
			neighbor.depth = g_zbNwkCtx.join.pRejoinParent->depth;
			neighbor.lqi = g_zbNwkCtx.join.pRejoinParent->lqi;
			neighbor.outgoingCost = 1;

			if (tl_zbNeighborTableUpdate(&neighbor, 1) == NULL) {
				nwk_nlmeJoinCnf(arg, NWK_STATUS_NEIGHBOR_TABLE_FULL);
				return;
			}
		}
	}

	nwk_nlmeJoinCnf(arg, NWK_STATUS_SUCCESS);
}

void nwk_directJoinScanCnfHandler(void *arg)
{
	zb_mac_mlme_scan_conf_t *cnf = (zb_mac_mlme_scan_conf_t *)arg;

	if (cnf->scanType != ORPHAN_SCAN) {
		zb_buf_free((zb_buf_t *)arg);
		return;
	}

	if (cnf->status != MAC_SUCCESS) {
		nwk_nlmeJoinCnf(arg, cnf->status);
		return;
	}

	{
		tl_zb_normal_neighbor_entry_t *parent = tl_zbNeighborTableSearchForParent();
		u16 parentRef = 0;

		if (parent != NULL) {
			addrExt_t oldExt;
			u16 oldShort = tl_zbshortAddrByIdx(parent->addrmapIdx);

			tl_zbExtAddrByIdx(parent->addrmapIdx, oldExt);
			if (!ZB_IEEE_ADDR_CMP(oldExt, g_zbInfo.macPib.coordExtAddress) ||
			    oldShort != g_zbInfo.macPib.coordShortAddress) {
				tl_zbNeighborTableDelete(parent);
				g_zbNwkCtx.parentIsChanged = 1;
			}
		}

		if (tl_zbNwkAddrMapAdd(g_zbInfo.macPib.coordShortAddress,
				       g_zbInfo.macPib.coordExtAddress,
				       &parentRef) != NWK_STATUS_SUCCESS) {
			nwk_nlmeJoinCnf(arg, NWK_STATUS_NEIGHBOR_TABLE_FULL);
			return;
		}

		{
			tl_zb_normal_neighbor_entry_t neighbor;

			memset(&neighbor, 0, sizeof(neighbor));
			neighbor.addrmapIdx = parentRef;
			neighbor.rxOnWhileIdle = 1;
			neighbor.deviceType = NWK_DEVICE_TYPE_ROUTER;
			neighbor.relationship = NEIGHBOR_IS_PARENT;
			neighbor.used = 1;
			neighbor.lqi = 0xdeU;
			neighbor.outgoingCost = 1;

			if (tl_zbNeighborTableUpdate(&neighbor, 1) == NULL) {
				nwk_nlmeJoinCnf(arg, NWK_STATUS_NEIGHBOR_TABLE_FULL);
				return;
			}
		}
	}

	nwk_nlmeJoinCnf(arg, NWK_STATUS_SUCCESS);
}

void tl_zbMacMlmeOrphanIndicationHandler(void *arg)
{
	const mac_mlme_orphan_ind_t *ind = (const mac_mlme_orphan_ind_t *)arg;
	mac_mlme_orphan_resp_t *resp = (mac_mlme_orphan_resp_t *)arg;
	addrExt_t extAddr;
	u16 shortAddr;
	u16 addrMapIdx;
	tl_zb_normal_neighbor_entry_t *entry;

	ZB_IEEE_ADDR_COPY(extAddr, ind->orphanAddr);
	entry = tl_zbNeighborTableSearchFromExtAddr(&shortAddr, extAddr, &addrMapIdx);
	if (entry == NULL || entry->relationship != NEIGHBOR_IS_PARENT) {
		zb_buf_free((zb_buf_t *)arg);
		return;
	}

	ZB_IEEE_ADDR_COPY(resp->orphanAddr, extAddr);
	resp->shortAddr = shortAddr;
	resp->associated = TRUE;
	tl_zbPrimitivePost(TL_Q_NWK2MAC, MAC_MLME_ORPHAN_RES, arg);
}

void tl_zbNwkNlmeJoinRequestHandler(void *arg)
{
	nlme_join_req_t *req = (nlme_join_req_t *)arg;

	switch (req->rejoinNwk) {
	case NLME_REJOIN_METHOD_ASSOCIATION:
		nwk_associateJoin(arg);
		break;
	case NLME_REJOIN_METHOD_DIRECT:
		if (g_zbNwkCtx.state != NLME_STATE_IDLE) {
			nwk_nlmeJoinCnf(arg, NWK_STATUS_INVALID_REQUEST);
			return;
		}

		g_zbInfo.nwkNib.parentInfo = 0;
		ZB_EXTPANID_COPY(g_zbInfo.nwkNib.extPANId, req->extPANId);
		g_zbInfo.nwkNib.capabilityInfo = req->capabilityInfo;
		g_zbInfo.macPib.rxOnWhenIdle = req->capabilityInfo.rcvOnWhenIdle;
		g_zbNwkCtx.state = NLME_STATE_DIRECT_JOIN;
		g_zbNwkCtx.scanChannels = req->scanChannels;
		g_zbNwkCtx.scanDuration = req->scanDuration;
		nwk_copy_scan_req((zb_mac_mlme_scan_req_t *)arg, req, ORPHAN_SCAN);
		tl_zbPrimitivePost(TL_Q_NWK2MAC, MAC_MLME_SCAN_REQ, arg);
		break;
	case NLME_REJOIN_METHOD_REJOIN:
		if (g_zbNwkCtx.state != NLME_STATE_IDLE) {
			nwk_nlmeJoinCnf(arg, NWK_STATUS_INVALID_REQUEST);
			return;
		}

		g_zbInfo.nwkNib.parentInfo = 0;
		ZB_EXTPANID_COPY(g_zbInfo.nwkNib.extPANId, req->extPANId);
		g_zbInfo.nwkNib.capabilityInfo = req->capabilityInfo;
		g_zbInfo.macPib.rxOnWhenIdle = req->capabilityInfo.rcvOnWhenIdle;
		g_zbNwkCtx.state = NLME_STATE_REJOIN;
		g_zbNwkCtx.scanChannels = req->scanChannels;
		g_zbNwkCtx.scanDuration = req->scanDuration;
		nwk_copy_scan_req((zb_mac_mlme_scan_req_t *)arg, req, ACTIVE_SCAN);
		tl_zbPrimitivePost(TL_Q_NWK2MAC, MAC_MLME_SCAN_REQ, arg);
		break;
	default:
		zb_buf_free((zb_buf_t *)arg);
		break;
	}
}

#if defined(ZB_ROUTER_ROLE)
static inline u8 nwk_join_accept_device_type(u8 capabilityRaw)
{
	capability_info_t capability;

	*(u8 *)&capability = capabilityRaw;
	return capability.devType ? NWK_DEVICE_TYPE_ROUTER : NWK_DEVICE_TYPE_ED;
}

static inline u8 nwk_join_accept_relationship(u8 deviceType, bool secureRejoin)
{
	if (!secureRejoin) {
		return NEIGHBOR_IS_UNAUTH_CHILD;
	}

	return (deviceType == NWK_DEVICE_TYPE_ROUTER) ? NEIGHBOR_IS_SIBLING : NEIGHBOR_IS_CHILD;
}

static inline u8 nwk_join_indication_capability(const tl_zb_normal_neighbor_entry_t *entry)
{
	capability_info_t capability;

	memset(&capability, 0, sizeof(capability));
	capability.rcvOnWhenIdle = entry->rxOnWhileIdle ? 1U : 0U;
	capability.devType = (entry->deviceType == NWK_DEVICE_TYPE_ROUTER) ? 1U : 0U;
	return *(u8 *)&capability;
}

static u8 nwk_join_accept(addrExt_t extAddr, u8 capabilityRaw, u16 *nwkAddr, bool rejoinNwk,
			  bool secureRejoin)
{
	tl_zb_normal_neighbor_entry_t *entry;
	tl_zb_normal_neighbor_entry_t neighbor;
	u16 shortAddr = 0;
	u16 addrMapIdx = 0;
	u8 deviceType = nwk_join_accept_device_type(capabilityRaw);
	u8 relationship = nwk_join_accept_relationship(deviceType, secureRejoin);
	bool secureRejoinReq = rejoinNwk && secureRejoin;

	if (secureRejoinReq && ZB_IEEE_ADDR_IS_INVALID(extAddr)) {
		return 1;
	}

	entry = tl_zbNeighborTableSearchFromExtAddr(&shortAddr, extAddr, &addrMapIdx);
	if (entry != NULL && secureRejoinReq) {
		capability_info_t capability;

		*(u8 *)&capability = capabilityRaw;
		if (entry->rxOnWhileIdle != capability.rcvOnWhenIdle ||
		    entry->deviceType != deviceType) {
			return 1;
		}
	}

	if (entry == NULL) {
		capability_info_t capability;

		if (g_zbInfo.nwkNib.depth > g_zbInfo.nwkNib.maxDepth) {
			return 1;
		}

		*(u8 *)&capability = capabilityRaw;
		shortAddr = *nwkAddr;
		if (capability.allocAddr && shortAddr == MAC_SHORT_ADDR_NONE) {
			shortAddr = tl_zbNwkStochasticAddrCal();
		}

		if (tl_zbNwkAddrMapAdd(shortAddr, extAddr, &addrMapIdx) != NWK_STATUS_SUCCESS) {
			return 1;
		}
	}

	memset(&neighbor, 0, sizeof(neighbor));
	neighbor.addrmapIdx = addrMapIdx;
	neighbor.rxOnWhileIdle = (capabilityRaw >> 3) & 1U;
	neighbor.deviceType = deviceType;
	neighbor.relationship = relationship;
	neighbor.used = 1;
	neighbor.depth =
		(entry != NULL)
			? entry->depth
			: (u8)(g_zbInfo.nwkNib.depth +
			       ((g_zbInfo.nwkNib.depth < g_zbInfo.nwkNib.maxDepth) ? 1U : 0U));
	neighbor.lqi = 0xdeU;
	neighbor.outgoingCost = 1;

	if (relationship == NEIGHBOR_IS_UNAUTH_CHILD) {
		/* "140: tstorer r0,[r4,#8]" - the vendor stores the authentication
		 * lifetime in authTimeout, which is what nwkNebManagePeriodic()
		 * ages for an unauthenticated child; timeoutCnt (offset 12) is the
		 * end-device keepalive timer and stays zero here. */
		neighbor.authTimeout = NWK_UNAUTH_CHILD_TABLE_LIFE_TIME / 1000U;
	}

	if (entry != NULL) {
		neighbor.age = 0;
	}

	if (tl_zbNeighborTableUpdate(&neighbor, 1) == NULL) {
		return 1;
	}

	*nwkAddr = shortAddr;
	return MAC_SUCCESS;
}

void tl_zbMacMlmeAssociateIndicationHandler(void *arg)
{
	zb_mlme_associate_ind_t *ind = (zb_mlme_associate_ind_t *)arg;
	zb_mlme_associate_resp_t *resp = (zb_mlme_associate_resp_t *)arg;
	addrExt_t extAddr;
	u16 shortAddr = MAC_SHORT_ADDR_NONE;
	u8 status;

	ZB_IEEE_ADDR_COPY(extAddr, ind->devAddress);
	status = nwk_join_accept(ind->devAddress, *(u8 *)&ind->capbilityInfo, &shortAddr, FALSE,
				 FALSE);
	if (status == MAC_STA_FRAME_PENDING) {
		zb_buf_free((zb_buf_t *)arg);
		return;
	}

	if (status == MAC_SUCCESS) {
		g_sysDiags.joinIndication++;
	}

	resp->shortAddress = shortAddr;
	ZB_IEEE_ADDR_COPY(resp->devAddress, extAddr);
	resp->status = status;
	tl_zbPrimitivePost(TL_Q_NWK2MAC, MAC_MLME_ASSOCIATE_RES, arg);
}

void tl_zbMacMlmeCommStatusIndicationHandler(void *arg)
{
	zb_mlme_comm_status_ind_t *ind = (zb_mlme_comm_status_ind_t *)arg;
	tl_zb_normal_neighbor_entry_t *entry;
	nlme_join_ind_t *joinInd = (nlme_join_ind_t *)arg;
	addrExt_t extAddr;
	u16 shortAddr;
	u16 addrMapIdx;

	if (ind->dstAddr.addrMode == ADDR_MODE_EXT) {
		entry = tl_zbNeighborTableSearchFromExtAddr(&shortAddr, ind->dstAddr.addr.extAddr,
							    &addrMapIdx);
	} else {
		entry = tl_zbNeighborTableSearchFromShortAddr(ind->dstAddr.addr.shortAddr, extAddr,
							      &addrMapIdx);
	}

	if (entry == NULL || (ind->status != MAC_SUCCESS && ind->status != MAC_STA_FRAME_PENDING)) {
		zb_buf_free((zb_buf_t *)arg);
		return;
	}

	tl_zbNwkBeaconPayloadUpdate();
	tl_zbExtAddrByIdx(entry->addrmapIdx, joinInd->extAddr);
	joinInd->nwkAddr = tl_zbshortAddrByIdx(entry->addrmapIdx);
	*(u8 *)&joinInd->capabilityInfo = nwk_join_indication_capability(entry);
	joinInd->rejoinNwk = ind->isAssoc ? 0U : 1U;
	joinInd->secureRejoin = ind->isAssoc ? FALSE : TRUE;
	tl_zbTaskPost(zdo_nlme_join_indication, arg);
}

void tl_zbNwkRejoinReqCmdHandler(void *arg, nwk_hdr_t *pNwkHdr, nwkCmd_t *cmd)
{
	nwk_hdr_t hdr;
	nwkCmd_t rsp;
	u16 shortAddr;
	u8 status;

	if (!pNwkHdr->frameControl.srcIEEEAddr) {
		zb_buf_free((zb_buf_t *)arg);
		return;
	}

	shortAddr = pNwkHdr->srcAddr;
	status = nwk_join_accept(pNwkHdr->srcIeeeAddr, *(u8 *)&cmd->rejoinReq.capabilityInfo,
				 &shortAddr, TRUE, pNwkHdr->frameControl.security != 0U);
	if (status == MAC_STA_FRAME_PENDING) {
		zb_buf_free((zb_buf_t *)arg);
		return;
	}

	if (status == MAC_SUCCESS) {
		g_sysDiags.joinIndication++;
	}

	memset(&hdr, 0, sizeof(hdr));
	memset(&rsp, 0, sizeof(rsp));

	hdr.frameControl.frameType = FRAME_TYPE_COMMAND;
	hdr.frameControl.protocolVer = ZB_PROTOCOL_VERSION;
	hdr.frameControl.security = pNwkHdr->frameControl.security;
	hdr.frameControl.dstIEEEAddr = 1;
	hdr.frameControl.srcIEEEAddr = 1;
	hdr.dstAddr = pNwkHdr->srcAddr;
	hdr.srcAddr = g_zbInfo.nwkNib.nwkAddr;
	hdr.radius = 1;
	hdr.seqNum = g_zbInfo.nwkNib.seqNum++;
	ZB_IEEE_ADDR_COPY(hdr.dstIeeeAddr, pNwkHdr->srcIeeeAddr);
	ZB_IEEE_ADDR_COPY(hdr.srcIeeeAddr, g_zbInfo.macPib.extAddress);
	hdr.frameHdrLen = getNwkHdrSize(&hdr);

	rsp.cmdId = NWK_CMD_REJOIN_RESPONSE;
	rsp.rejoinRsp.nwkAddr = shortAddr;
	rsp.rejoinRsp.rejoinStatus = status;
	((u8 *)arg)[40] = pNwkHdr->frameControl.security;

	tl_zbNwkSendRejoinRespCmd((zb_buf_t *)arg, &hdr, &rsp,
				  cmd->rejoinReq.capabilityInfo.rcvOnWhenIdle ? 1U : 0U,
				  NWK_INTERNAL_REJOIN_RESP_CMD_HANDLE);
}

void tl_zbMcpsRejoinRespCnfHandler(void *arg, u8 status, u16 shortAddr)
{
	tl_zb_normal_neighbor_entry_t *entry;
	addrExt_t extAddr;
	nlme_join_ind_t *joinInd = (nlme_join_ind_t *)arg;
	u16 addrMapIdx;

	if (status != MAC_SUCCESS) {
		zb_buf_free((zb_buf_t *)arg);
		return;
	}

	entry = tl_zbNeighborTableSearchFromShortAddr(shortAddr, extAddr, &addrMapIdx);
	if (entry == NULL) {
		zb_buf_free((zb_buf_t *)arg);
		return;
	}

	tl_zbNwkBeaconPayloadUpdate();
	*(u8 *)&joinInd->capabilityInfo = nwk_join_indication_capability(entry);
	joinInd->nwkAddr = shortAddr;
	joinInd->rejoinNwk = NLME_REJOIN_METHOD_REJOIN;
	joinInd->secureRejoin = ((u8 *)arg)[40];
	ZB_IEEE_ADDR_COPY(joinInd->extAddr, extAddr);
	tl_zbTaskPost(zdo_nlme_join_indication, arg);
}

void tl_zbNwkNlmeDirectJoinRequestHandler(void *arg)
{
	nlme_directJoin_req_t *req = (nlme_directJoin_req_t *)arg;
	nlme_directJoin_cnf_t *cnf = (nlme_directJoin_cnf_t *)arg;
	tl_zb_normal_neighbor_entry_t *entry;
	tl_zb_normal_neighbor_entry_t neighbor;
	addrExt_t extAddr;
	u16 addrMapIdx = 0;
	u8 status = NWK_STATUS_SUCCESS;
	u8 deviceType;

	ZB_IEEE_ADDR_COPY(extAddr, req->deviceAddr);

	if (ZB_IEEE_ADDR_CMP(extAddr, g_zbInfo.macPib.extAddress)) {
		status = NWK_STATUS_INVALID_PARAMETER;
		goto post_confirm;
	}

	if (tl_zbNwkAddrMapAdd(req->nwkAddr, extAddr, &addrMapIdx) != NWK_STATUS_SUCCESS) {
		status = NWK_STATUS_NEIGHBOR_TABLE_FULL;
		goto post_confirm;
	}

	entry = nwk_neTblGetByExtAddr(extAddr);
	if (entry != NULL) {
		status = NWK_STATUS_ALREADY_PRESENT;
		goto post_confirm;
	}

	memset(&neighbor, 0, sizeof(neighbor));
	deviceType = nwk_join_accept_device_type(*(u8 *)&req->capabilityInfo);
	neighbor.addrmapIdx = addrMapIdx;
	neighbor.rxOnWhileIdle = req->capabilityInfo.rcvOnWhenIdle ? 1U : 0U;
	neighbor.deviceType = deviceType;
	neighbor.relationship =
		(deviceType == NWK_DEVICE_TYPE_ROUTER) ? NEIGHBOR_IS_SIBLING : NEIGHBOR_IS_CHILD;
	neighbor.used = 1;
	neighbor.lqi = 0xdeU;
	neighbor.outgoingCost = 1;

	if (tl_zbNeighborTableUpdate(&neighbor, 1) == NULL) {
		status = NWK_STATUS_NEIGHBOR_TABLE_FULL;
		goto post_confirm;
	}

	memset(cnf, 0, sizeof(*cnf));
	ZB_IEEE_ADDR_COPY(cnf->deviceAddr, extAddr);
	cnf->status = NWK_STATUS_SUCCESS;
	tl_zbTaskPost(zdo_nlme_direct_join_confirm, arg);
	return;

post_confirm:
	memset(cnf, 0, sizeof(*cnf));
	cnf->status = status;
	tl_zbTaskPost(zdo_nlme_direct_join_confirm, arg);
}
#endif
