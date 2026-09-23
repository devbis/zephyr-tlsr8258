/*
 * Copyright 2026 libzigbee
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "zb_common.h"
#include "zdp_services.h"
#include "zdo_nwk_manager.h"
#include "zb_nwk_neighbor.h"
#include "nwk_routing.h"
#include "gp_internal.h"
#include <stdint.h>

enum {
	ZDP_CB_MAX = 4,
	ZDP_ADDR_REQ_DELAY_MS = 200,
	ZDP_ASSOC_LIST_LIMIT = 15,
	ZDO_ADDR_RSP_ALLOC_BASE = 26,
	ZDO_ADDR_RSP_ALLOC_MAX = 57,
	ED_BIND_CLUSTER_LIST_MAX = 16,
	ZDO_MGMT_LEAVE_UNKNOWN_DEVICE = 0xc8,
};

typedef struct _attribute_packed_ {
	zdo_callback cb;
	u16 seq;
	u8 used;
	u8 active;
} zdp_cb_info_t;

typedef struct _attribute_packed_ {
	u32 scanChannels;
	u8 scanDuration;
	u8 scanCount;
	u16 srcAddr;
	u8 seqNum;
} zdo_nwk_update_req_state_t;

typedef struct {
	u8 seqNum;
	u8 srcEndpoint;
	u16 profileId;
	u8 numInClusters;
	const u8 *inClusters;
	u8 numOutClusters;
	const u8 *outClusters;
} zdo_ed_bind_req_view_t;

typedef struct _attribute_packed_ {
	void *pendingBuf;
	u16 src1ShortAddr;
	u8 src1Endpoint;
	u8 src1MatchCount;
	u8 rsv[4];
	u16 src2ShortAddr;
	u8 src2Endpoint;
	u8 src2MatchCount;
	u16 clusterList[ED_BIND_CLUSTER_LIST_MAX];
	u8 clusterTotalCount;
	u8 rsv2;
} ed_binding_ctx_t;

static u8 remainChildListNum_8733 __asm__("remainChildListNum.8733") = 0;
static zdo_nwk_update_req_state_t zdo_nur;
static u8 zdp_txSeqNo = 0;
static u8 zdpCblWptr = 0;
ev_timer_event_t *zdo_bind_timer_event = NULL;
static void *ed_binding_state __asm__("ent.8732") = NULL;
zdp_cb_info_t zdp_cbl[ZDP_CB_MAX];
ed_binding_ctx_t ed_binding_ctx;

static bool cluster_list_contains(const u16 *list, u8 cnt, u16 clusterId)
{
	for (u8 i = 0; i < cnt; i++) {
		if (list[i] == clusterId) {
			return TRUE;
		}
	}

	return FALSE;
}

static inline ed_binding_ctx_t *ed_bind_ctx(void)
{
	return &ed_binding_ctx;
}

static inline void ed_bind_ctx_reset(void)
{
	memset(&ed_binding_ctx, 0, sizeof(ed_binding_ctx));
	zdo_bind_timer_event = NULL;
}

static _always_inline void zdo_ed_bind_req_parse(const u8 *payload, zdo_ed_bind_req_view_t *req)
{
	u16 outPos;

	req->seqNum = payload[0];
	req->srcEndpoint = payload[11];
	COPY_BUFFERTOU16(req->profileId, payload + 12);
	req->numInClusters = payload[14];

	req->inClusters = payload + 15;
	outPos = (u16)(15U + (u16)req->numInClusters * 2U);
	req->numOutClusters = payload[outPos];
	req->outClusters = payload + outPos + 1U;
}

static inline u8 zdo_assoc_child_list_fill(u8 startIndex, u8 maxCount, u8 *dst)
{
	tl_zb_normal_neighbor_entry_t *entry = NULL;
	u8 skipped = 0;
	u8 written = 0;

	while ((written < maxCount) &&
	       ((entry = tl_zbNeighborTabSearchForChildEndDev(entry)) != NULL)) {
		if (skipped < startIndex) {
			skipped++;
			continue;
		}

		{
			u16 addrmapIdx = entry->addrmapIdx;
			u16 shortAddr = tl_zbshortAddrByIdx(addrmapIdx);

			dst[written * 2U] = LO_UINT16(shortAddr);
			dst[written * 2U + 1U] = HI_UINT16(shortAddr);
			written++;
		}
	}

	return written;
}

static inline u8 zdo_assoc_child_rsp_alloc_size(u8 startIndex, u8 *count)
{
	u8 childNum = tl_zbNeighborTableChildEDNumGet();

	if ((childNum == 0U) || (startIndex >= childNum)) {
		*count = 0;
		return ZDO_ADDR_RSP_ALLOC_BASE;
	}

	childNum = (u8)(childNum - startIndex);
	if (childNum > ZDP_ASSOC_LIST_LIMIT) {
		*count = ZDP_ASSOC_LIST_LIMIT;
		return ZDO_ADDR_RSP_ALLOC_MAX;
	}

	*count = childNum;
	return (u8)(ZDO_ADDR_RSP_ALLOC_BASE + childNum * 2U);
}

_attribute_no_inline_ static int zdoMgmtLeaveCmdProcessCb(void *arg)
{
	tl_zbPrimitivePost(TL_Q_HIGH2NWK, NWK_NLME_LEAVE_REQ, arg);
	return -1;
}

_attribute_no_inline_ static int zdo_change_channel_cb(void *arg)
{
	u8 channel = (u8)(uintptr_t)arg;

	tl_zbMacChannelSet(channel);
	g_zbInfo.macPib.phyChannelCur = channel;
	g_zbInfo.nwkNib.txTotal = 0;
	g_zbInfo.nwkNib.txFail = 0;
	tl_zbTaskPost(zb_info_save, NULL);

	return -1;
}
_attribute_no_inline_ static void zdo_cluster_list_match_process_clone_2(
	u8 clusterNum1, const u8 *clusterList1, u8 clusterNum2,
	const u8 *clusterList2) __asm__("zdo_cluster_list_match_process.clone.2");
_attribute_no_inline_ static void zdo_cluster_list_match_process_clone_2(u8 clusterNum1,
									 const u8 *clusterList1,
									 u8 clusterNum2,
									 const u8 *clusterList2)
{
	for (u8 i = 0; i < clusterNum1; i++) {
		u16 clusterId;

		COPY_BUFFERTOU16(clusterId, clusterList1 + i * 2U);

		for (u8 j = 0; j < clusterNum2; j++) {
			u16 otherClusterId;

			COPY_BUFFERTOU16(otherClusterId, clusterList2 + j * 2U);
			if (clusterId != otherClusterId) {
				continue;
			}

			ed_bind_ctx()->clusterList[ed_bind_ctx()->clusterTotalCount] = clusterId;
			ed_bind_ctx()->clusterTotalCount++;
			break;
		}
	}
}

void zdp_cb_process(u16 tsn, void *buf)
{
	for (u8 i = 0; i < ZDP_CB_MAX; i++) {
		zdp_cb_info_t *entry = &zdp_cbl[i];

		if (!entry->active || entry->seq != tsn) {
			continue;
		}

		if (entry->cb != NULL) {
			entry->cb(buf);
			return;
		}

		entry->active = 0;
		return;
	}
}

zdp_cb_info_t *zdo_cb_exist(u16 seqNo)
{
	for (u8 i = 0; i < ZDP_CB_MAX; i++) {
		zdp_cb_info_t *entry = &zdp_cbl[i];

		if (entry->active && entry->seq == seqNo) {
			return entry;
		}
	}

	return NULL;
}

void zdo_send_req(zdo_zdp_req_t *req)
{
	epInfo_t dstEpInfo;
	u8 apsCnt = 0;

	memset(&dstEpInfo, 0, sizeof(dstEpInfo));
	dstEpInfo.txOptions = APS_TX_OPT_ACK_TX;
	dstEpInfo.dstEp = ZDO_EP;
	dstEpInfo.profileId = ZDO_PROFILE_ID;

	/* The vendor library tests the two modes separately ("2e: tcmp r3,#0" then
	 * "a6: tcmp r3,#1; a8: tjne 40") and leaves dstAddrMode at 0 with no
	 * address copied for anything else. */
	if (req->dst_addr_mode == SHORT_ADDR_MODE) {
		dstEpInfo.dstAddrMode = APS_SHORT_DSTADDR_WITHEP;
		memcpy(&dstEpInfo.dstAddr, &req->dst_nwk_addr, sizeof(req->dst_nwk_addr));
	} else if (req->dst_addr_mode == EXT_ADDR_MODE) {
		dstEpInfo.dstAddrMode = APS_LONG_DSTADDR_WITHEP;
		ZB_IEEE_ADDR_COPY(&dstEpInfo.dstAddr, req->st_ext_addr);
	}

	if (req->cluster_id == DEVICE_ANNCE_CLID) {
		u16 announceAddr = (u16)req->zdu[1] | ((u16)req->zdu[2] << 8);

		if (announceAddr != g_zbNIB.nwkAddr) {
			dstEpInfo.useAlias = TRUE;
			dstEpInfo.aliasSrcAddr = announceAddr;
			dstEpInfo.aliasSeqNum = 0;
		}
	}

	af_dataSend(ZDO_EP, &dstEpInfo, req->cluster_id, req->zduLen, req->zdu, &apsCnt);

	if (req->zdoRspReceivedIndCb != NULL) {
		zdp_cb_info_t *entry = &zdp_cbl[zdpCblWptr++ & (ZDP_CB_MAX - 1U)];

		entry->cb = req->zdoRspReceivedIndCb;
		entry->seq = req->zdpSeqNum;
		entry->used = 1;
		entry->active = 1;
	}
}
_attribute_no_inline_ static void zdo_end_device_bind_resp_send(void *arg, zdo_status_t status,
								u8 seqNum, u16 dstNwkAddr)
{
	zdo_zdp_req_t zzr;

	memset(&zzr, 0, sizeof(zzr));
	TL_BUF_INITIAL_ALLOC((zb_buf_t *)arg, 2, zzr.zdu, u8 *);

	zzr.zdu[0] = seqNum;
	zzr.zdu[1] = status;
	zzr.cluster_id = END_DEVICE_BIND_RSP_CLID;
	zzr.zduLen = 2;
	zzr.buff_addr = (u8 *)arg;
	zzr.dst_addr_mode = SHORT_ADDR_MODE;
	zzr.dst_nwk_addr = dstNwkAddr;
	zdo_send_req(&zzr);
	zb_buf_free((zb_buf_t *)arg);
}

_attribute_no_inline_ static int zdo_end_device_bind_timeout_cb(void *arg)
{
	aps_data_ind_t *ad = (aps_data_ind_t *)arg;

	zdo_end_device_bind_resp_send(arg, ZDO_TIMEOUT, ad->asdu[0], ad->src_short_addr);
	ed_bind_ctx_reset();

	return -1;
}

_attribute_no_inline_ static int zdo_ieeeAddrReqDelayCb(void *arg)
{
	aps_data_ind_t *ad = (aps_data_ind_t *)arg;
	const u8 *payload = ad->asdu;
	const zdo_ieee_addr_req_t *req = (const zdo_ieee_addr_req_t *)(payload + 1);
	addrExt_t ieeeAddr;
	tl_zb_normal_neighbor_entry_t *entry;
	u16 nwkAddrInterest;
	u8 reqType = req->req_type;
	u8 startIndex = req->start_index;
	u8 assocCount = 0;
	u8 allocSize = ZDO_ADDR_RSP_ALLOC_BASE;
	u8 *ptr;
	zdo_status_t status = ZDO_DEVICE_NOT_FOUND;
	bool includeAssocList;
	bool targetIsLocal;
	bool localLike;
	zdo_zdp_req_t zzr;
	u16 addrMapIdx;

	nwkAddrInterest = req->nwk_addr_interest;

	ZB_IEEE_ADDR_INVALID(ieeeAddr);
	entry = nwk_neTblGetByShortAddr(nwkAddrInterest);
	targetIsLocal = nwkAddrInterest == g_zbInfo.nwkNib.nwkAddr;
	localLike = targetIsLocal || ((entry != NULL) && (entry->deviceType == NWK_DEVICE_TYPE_ED));

	if ((!localLike || (reqType > ZDO_ADDR_REQ_EXTENDED_REQ)) &&
	    ZB_NWK_IS_ADDRESS_BROADCAST(ad->dst_addr)) {
		zb_buf_free((zb_buf_t *)arg);
		return -1;
	}

	if (reqType > ZDO_ADDR_REQ_EXTENDED_REQ) {
		status = localLike ? ZDO_INVALID_REQUEST : ZDO_DEVICE_NOT_FOUND;
		(void)tl_zbExtAddrByShortAddr(nwkAddrInterest, ieeeAddr, &addrMapIdx);
	} else if (localLike) {
		status = ZDO_SUCCESS;
		if (reqType == ZDO_ADDR_REQ_SINGLE_REQ) {
			(void)tl_zbExtAddrByShortAddr(nwkAddrInterest, ieeeAddr, &addrMapIdx);
		} else {
			ZB_IEEE_ADDR_COPY(ieeeAddr, ZB_PIB_EXTENDED_ADDRESS());
		}
	}

	includeAssocList = (status == ZDO_SUCCESS) && (reqType == ZDO_ADDR_REQ_EXTENDED_REQ) &&
			   (af_nodeDevTypeGet() != DEVICE_TYPE_END_DEVICE);
	if (includeAssocList) {
		allocSize = zdo_assoc_child_rsp_alloc_size(startIndex, &assocCount);
	}

	memset(&zzr, 0, sizeof(zzr));
	TL_BUF_INITIAL_ALLOC((zb_buf_t *)arg, allocSize, zzr.zdu, u8 *);

	ptr = zzr.zdu;
	*ptr++ = payload[0];
	*ptr++ = status;
	ZB_IEEE_ADDR_COPY(ptr, ieeeAddr);
	ptr += EXT_ADDR_LEN;
	*ptr++ = LO_UINT16(nwkAddrInterest);
	*ptr++ = HI_UINT16(nwkAddrInterest);

	if (includeAssocList) {
		*ptr++ = assocCount;
		*ptr++ = startIndex;
		ptr += zdo_assoc_child_list_fill(startIndex, assocCount, ptr) * 2U;
	}

	zzr.cluster_id = IEEE_ADDR_RSP_CLID;
	zzr.zduLen = (u8)(ptr - zzr.zdu);
	zzr.buff_addr = (u8 *)arg;
	zzr.dst_addr_mode = SHORT_ADDR_MODE;
	zzr.dst_nwk_addr = ad->src_short_addr;
	zdo_send_req(&zzr);
	zb_buf_free((zb_buf_t *)arg);

	return -1;
}

_attribute_no_inline_ static int zdo_nwkAddrReqDelayCb(void *arg)
{
	aps_data_ind_t *ad = (aps_data_ind_t *)arg;
	const u8 *payload = ad->asdu;
	const zdo_nwk_addr_req_t *req = (const zdo_nwk_addr_req_t *)(payload + 1);
	addrExt_t ieeeAddr;
	tl_zb_normal_neighbor_entry_t *entry;
	u16 nwkAddrInterest = ZB_UNKNOWN_SHORT_ADDR;
	u16 addrMapIdx;
	u8 reqType = req->req_type;
	u8 startIndex = req->start_index;
	u8 assocCount = 0;
	u8 allocSize = ZDO_ADDR_RSP_ALLOC_BASE;
	u8 *ptr;
	zdo_status_t status = ZDO_DEVICE_NOT_FOUND;
	bool localLike;
	zdo_zdp_req_t zzr;

	ZB_IEEE_ADDR_COPY(ieeeAddr, req->ieee_addr_interest);
	entry = nwk_neTblGetByExtAddr(ieeeAddr);
	localLike = ZB_IEEE_ADDR_CMP(ieeeAddr, ZB_PIB_EXTENDED_ADDRESS()) ||
		    ((entry != NULL) && (entry->deviceType == NWK_DEVICE_TYPE_ED));

	if (!localLike && ZB_NWK_IS_ADDRESS_BROADCAST(ad->dst_addr)) {
		zb_buf_free((zb_buf_t *)arg);
		return -1;
	}

	if (reqType > ZDO_ADDR_REQ_EXTENDED_REQ) {
		if (ZB_NWK_IS_ADDRESS_BROADCAST(ad->dst_addr)) {
			zb_buf_free((zb_buf_t *)arg);
			return -1;
		}
		status = ZDO_INVALID_REQUEST;
	} else if (localLike ||
		   (tl_zbShortAddrByExtAddr(&nwkAddrInterest, ieeeAddr, &addrMapIdx) == RET_OK)) {
		status = ZDO_SUCCESS;
	}

	if (status == ZDO_SUCCESS) {
		if (localLike && (reqType == ZDO_ADDR_REQ_EXTENDED_REQ) &&
		    (af_nodeDevTypeGet() != DEVICE_TYPE_END_DEVICE)) {
			ZB_IEEE_ADDR_COPY(ieeeAddr, ZB_PIB_EXTENDED_ADDRESS());
			nwkAddrInterest = ZB_PIB_SHORT_ADDRESS();
			allocSize = zdo_assoc_child_rsp_alloc_size(startIndex, &assocCount);
		}

		if ((reqType == ZDO_ADDR_REQ_EXTENDED_REQ) && (assocCount == 0U) &&
		    (af_nodeDevTypeGet() != DEVICE_TYPE_END_DEVICE) &&
		    tl_zbNeighborTableChildEDNumGet() != 0U &&
		    ZB_IEEE_ADDR_CMP(ieeeAddr, ZB_PIB_EXTENDED_ADDRESS())) {
			allocSize = zdo_assoc_child_rsp_alloc_size(startIndex, &assocCount);
		}
	}

	memset(&zzr, 0, sizeof(zzr));
	TL_BUF_INITIAL_ALLOC((zb_buf_t *)arg, allocSize, zzr.zdu, u8 *);

	ptr = zzr.zdu;
	*ptr++ = payload[0];
	*ptr++ = status;
	ZB_IEEE_ADDR_COPY(ptr, ieeeAddr);
	ptr += EXT_ADDR_LEN;
	*ptr++ = LO_UINT16(nwkAddrInterest);
	*ptr++ = HI_UINT16(nwkAddrInterest);

	if (reqType == ZDO_ADDR_REQ_EXTENDED_REQ) {
		*ptr++ = assocCount;
		*ptr++ = startIndex;
		ptr += zdo_assoc_child_list_fill(startIndex, assocCount, ptr) * 2U;
	}

	zzr.cluster_id = NWK_ADDR_RSP_CLID;
	zzr.zduLen = (u8)(ptr - zzr.zdu);
	zzr.buff_addr = (u8 *)arg;
	zzr.dst_addr_mode = SHORT_ADDR_MODE;
	zzr.dst_nwk_addr = ad->src_short_addr;
	zdo_send_req(&zzr);
	zb_buf_free((zb_buf_t *)arg);

	return -1;
}

#if defined(ZB_ROUTER_ROLE)
_attribute_no_inline_ static int zdo_parentAnnounceIndicateDelay(void *arg)
{
	aps_data_ind_t *ad = (aps_data_ind_t *)arg;
	u8 *payload = ad->asdu;
	s8 childNum = (s8)payload[1];
	u8 copiedNum = 0;
	u8 matchedNum = 0;
	u8 *childList;
	zb_buf_t *buf;
	zdo_zdp_req_t zzr;
	u8 *ptr;

	if (childNum > 10U) {
		zb_buf_free((zb_buf_t *)arg);
		return -1;
	}

	childList = ev_buf_allocate((u16)childNum * EXT_ADDR_LEN);
	if (childList == NULL) {
		zb_buf_free((zb_buf_t *)arg);
		return -1;
	}

	memcpy(childList, payload + 2, (u16)childNum * EXT_ADDR_LEN);

	buf = zb_buf_allocate();
	if (buf == NULL) {
		ev_buf_free(childList);
		zb_buf_free((zb_buf_t *)arg);
		return -1;
	}

	memset(&zzr, 0, sizeof(zzr));
	TL_BUF_INITIAL_ALLOC(buf, 75, zzr.zdu, u8 *);

	zzr.zdu[0] = payload[0];
	zzr.zdu[1] = 0;
	ptr = zzr.zdu + 3;

	for (s8 i = 0; i < childNum; i++) {
		tl_zb_normal_neighbor_entry_t *entry = NULL;

		while ((entry = tl_zbNeighborTabSearchForChildEndDev(entry)) != NULL) {
			addrExt_t extAddr;

			tl_zbExtAddrByIdx(entry->addrmapIdx, extAddr);
			if (!ZB_IEEE_ADDR_CMP(extAddr, childList + (u16)i * EXT_ADDR_LEN)) {
				continue;
			}

			matchedNum++;
			if (copiedNum < 9U) {
				ZB_IEEE_ADDR_COPY(ptr, childList + (u16)i * EXT_ADDR_LEN);
				ptr += EXT_ADDR_LEN;
				copiedNum++;
			} else {
				payload[1] = (u8)(matchedNum - copiedNum);
				ZB_IEEE_ADDR_COPY(
					payload + 2U + (u16)(matchedNum - copiedNum) * EXT_ADDR_LEN,
					childList + (u16)i * EXT_ADDR_LEN);
			}
			break;
		}
	}

	zzr.zdu[1] = copiedNum;
	ev_buf_free(childList);

	if (copiedNum != 0U) {
		zzr.cluster_id = (u16)(PARENT_ANNCE_CLID | 0x8000U);
		zzr.zduLen = (u8)(3U + copiedNum * EXT_ADDR_LEN);
		zzr.buff_addr = (u8 *)buf;
		zzr.dst_addr_mode = SHORT_ADDR_MODE;
		zzr.dst_nwk_addr = ad->src_short_addr;
		zdo_send_req(&zzr);
	}

	zb_buf_free(buf);

	if (matchedNum > copiedNum) {
		return 0;
	}

	zb_buf_free((zb_buf_t *)arg);
	return -1;
}
#endif

u8 zdp_data_send(u8 *payload, u8 payloadLen, zdo_zdp_req_t *req)
{
	u8 *buf = ev_buf_allocate(payloadLen);

	if (buf == NULL) {
		return ZDO_INSUFFICIENT_SPACE;
	}

	req->zdpSeqNum = zdp_txSeqNo++;
	req->buff_addr = buf;
	req->zdu = buf;
	req->zdu[0] = req->zdpSeqNum;
	req->zduLen = payloadLen;

	if (req->cluster_id == MGMT_NWK_UPDATE_REQ_CLID) {
		const zdo_mgmt_nwk_update_req_t *updateReq =
			(const zdo_mgmt_nwk_update_req_t *)payload;

		memcpy(req->zdu + 1, payload, 5);
		if (updateReq->scan_duration <= ZDO_NWK_MANAGER_MAX_SCAN_DURATION) {
			req->zdu[6] = updateReq->scan_cnt;
		} else if (updateReq->scan_duration == ZDO_NWK_MANAGER_CHANNEL_CHANGE) {
			req->zdu[6] = (u8)(g_zbInfo.nwkNib.updateId + 1U);
			ev_timer_taskPost(
				zdo_change_channel_cb,
				(void *)(uintptr_t)zdo_channel_page2num(updateReq->scan_ch), 460);
		} else if (updateReq->scan_duration == ZDO_NWK_MANAGER_ATTRIBUTES_CHANGE) {
			req->zdu[6] = (u8)(g_zbInfo.nwkNib.updateId + 1U);
			COPY_U16TOBUFFER(req->zdu + 7, updateReq->nwk_manager_addr);
		}
	} else if (req->cluster_id == MATCH_DESC_REQ_CLID) {
		const zdo_match_descriptor_req_t *matchReq =
			(const zdo_match_descriptor_req_t *)payload;
		const u8 *clusterList = (const u8 *)matchReq->cluster_list;

		memcpy(req->zdu + 1, matchReq, 4);
		req->zdu[5] = matchReq->num_in_clusters;
		memcpy(req->zdu + 6, clusterList, matchReq->num_in_clusters * 2U);
		req->zdu[6 + matchReq->num_in_clusters * 2U] = matchReq->num_out_clusters;
		memcpy(req->zdu + 7 + matchReq->num_in_clusters * 2U,
		       clusterList + matchReq->num_in_clusters * 2U,
		       matchReq->num_out_clusters * 2U);
	} else {
		memcpy(req->zdu + 1, payload, payloadLen - 1U);
	}

	zdo_send_req(req);
	ev_buf_free(buf);

	return ZDO_SUCCESS;
}

void zdo_devAnnce(u16 nwkAddr, const addrExt_t ieeeAddr, u8 capability)
{
	zb_buf_t *buf = zb_buf_allocate();
	zdo_zdp_req_t zzr;

	if (buf == NULL) {
		return;
	}

	memset(&zzr, 0, sizeof(zzr));
	TL_BUF_INITIAL_ALLOC(buf, sizeof(zdo_device_annce_req_t) + 1U, zzr.zdu, u8 *);

	zzr.zdu[0] = zdp_txSeqNo++;
	zzr.zdu[1] = LO_UINT16(nwkAddr);
	zzr.zdu[2] = HI_UINT16(nwkAddr);
	ZB_IEEE_ADDR_COPY(&zzr.zdu[3], ieeeAddr);
	zzr.zdu[11] = capability;

	zzr.cluster_id = DEVICE_ANNCE_CLID;
	zzr.zduLen = sizeof(zdo_device_annce_req_t) + 1U;
	zzr.buff_addr = (u8 *)buf;
	zzr.dst_addr_mode = SHORT_ADDR_MODE;
	zzr.dst_nwk_addr = NWK_BROADCAST_RX_ON_WHEN_IDLE;
	zdo_send_req(&zzr);
	zb_buf_free(buf);
}

void zdo_device_announce_send(void)
{
	zdo_devAnnce(ZB_PIB_SHORT_ADDRESS(), (const u8 *)ZB_PIB_EXTENDED_ADDRESS(),
		     af_nodeMacCapabilityGet());
}

void zdo_apsParentAnnceTimerStart(void)
{
	aps_ib.aps_parent_announce_timer = (u8)(APS_PARENT_ANNOUNCE_BASE_TIMER +
						(drv_u32Rand() % APS_PARENT_ANNOUNCE_JITTER_MAX));
}

void zdo_parent_announce_send(void)
{
	u8 childNum = remainChildListNum_8733;

	if (childNum == 0) {
		childNum = tl_zbNeighborTableChildEDNumGet();
	}

	if (childNum == 0) {
		return;
	}

	if (childNum > 9) {
		remainChildListNum_8733 = childNum - 10;
		childNum = 10;
	} else {
		remainChildListNum_8733 = 0;
	}

	zb_buf_t *buf = zb_buf_allocate();
	if (buf == NULL) {
		return;
	}

	zdo_zdp_req_t zzr;
	memset(&zzr, 0, sizeof(zzr));
	TL_BUF_INITIAL_ALLOC(buf, (u8)(2 + childNum * EXT_ADDR_LEN), zzr.zdu, u8 *);

	zzr.zdu[0] = zdp_txSeqNo++;
	zzr.zdu[1] = childNum;

	u8 actualChildNum = 0;
	u8 *ptr = &zzr.zdu[2];
	tl_zb_normal_neighbor_entry_t *state = (tl_zb_normal_neighbor_entry_t *)ed_binding_state;

	while (actualChildNum < childNum) {
		state = tl_zbNeighborTabSearchForChildEndDev(state);
		ed_binding_state = state;
		if (state == NULL) {
			remainChildListNum_8733 = 0;
			break;
		}

		tl_zbExtAddrByIdx(state->addrmapIdx, ptr);
		ptr += EXT_ADDR_LEN;
		actualChildNum++;
	}

	zzr.zdu[1] = actualChildNum;
	zzr.cluster_id = PARENT_ANNCE_CLID;
	zzr.zduLen = (u8)(2 + actualChildNum * EXT_ADDR_LEN);
	zzr.buff_addr = (u8 *)buf;
	zzr.dst_addr_mode = SHORT_ADDR_MODE;
	zzr.dst_nwk_addr = NWK_BROADCAST_ROUTER_COORDINATOR;
	zdo_send_req(&zzr);
	zb_buf_free(buf);

	if (remainChildListNum_8733 != 0) {
		zdo_apsParentAnnceTimerStart();
	}
}

int apsParentAnncePeriodic(void *arg)
{
	(void)arg;

	if (aps_ib.aps_parent_announce_timer != 0) {
		aps_ib.aps_parent_announce_timer--;
		if (aps_ib.aps_parent_announce_timer == 0) {
			zdo_parent_announce_send();
		}
	}

	return 0;
}

#if defined(ZB_ROUTER_ROLE) || defined(ZB_COORDINATOR_ROLE)
void zdo_deviceAnnounceIndicate(void *arg)
{
	aps_data_ind_t *ad = (aps_data_ind_t *)arg;
	zdo_device_annce_req_t req;

	memcpy(&req, ad->asdu + 1, sizeof(req));

	if (tl_zbNwkAddrConflictDetect(arg, req.nwk_addr_local, req.ieee_addr_local) == 1U) {
		return;
	}

	if ((g_gpDeviceAnnounceCheckCb != NULL) &&
	    g_gpDeviceAnnounceCheckCb(req.nwk_addr_local, req.ieee_addr_local)) {
		zb_buf_free((zb_buf_t *)arg);
		return;
	}

	{
		tl_zb_normal_neighbor_entry_t *entry = nwk_neTblGetByExtAddr(req.ieee_addr_local);

		if ((entry != NULL) && (entry->deviceType == NWK_DEVICE_TYPE_ED) &&
		    (ad->srcMacAddr != req.nwk_addr_local)) {
			zb_addrForNeighbor_t addrInfo;

			memset(&addrInfo, 0, sizeof(addrInfo));
			addrInfo.relationship = NEIGHBOR_IS_CHILD;
			addrInfo.shortAddr = tl_zbshortAddrByIdx(entry->addrmapIdx);
			tl_zbExtAddrByIdx(entry->addrmapIdx, addrInfo.extAddr);
			(void)nwk_nodeAddrInfoDelete(&addrInfo);
			tl_zbNeighborTableDelete(entry);
		}
	}

	{
		u16 addrRef = 0;

		(void)tl_zbNwkAddrMapAdd(req.nwk_addr_local, req.ieee_addr_local, &addrRef);
	}

	nwkRoutingTabEntryDstDel(req.nwk_addr_local);
#if defined(ZB_COORDINATOR_ROLE)
	nwkRouteRecTabEntryDstDel(req.nwk_addr_local);
	if (g_zbNIB.isConcentrator) {
		tl_zbTaskPost(zdo_manyToOneRouteDisc, arg);
	}
#endif
	zb_buf_free((zb_buf_t *)arg);

	if ((zdoAppIndCbLst != NULL) && (zdoAppIndCbLst->zdpDevAnnounceIndCb != NULL)) {
		zdoAppIndCbLst->zdpDevAnnounceIndCb(&req);
	}
}

void zdo_parentAnnounceIndicate(void *arg)
{
	ev_timer_taskPost(zdo_parentAnnounceIndicateDelay, arg,
			  1000U + (u16)((drv_u32Rand() & 0x1fU) * 50U));
}

void zdo_remoteAddrNotify(void *arg)
{
	aps_data_ind_t *ad = (aps_data_ind_t *)arg;
	const u8 *payload = ad->asdu;
	const zdo_nwk_addr_resp_t *resp = (const zdo_nwk_addr_resp_t *)payload;
	addrExt_t extAddr;
	u16 addrMapIdx;

	if (resp->status != ZDO_SUCCESS) {
		return;
	}

	ZB_IEEE_ADDR_COPY(extAddr, resp->ieee_addr_remote);
	(void)tl_zbNwkAddrMapAdd(resp->nwk_addr_remote, extAddr, &addrMapIdx);
}

void zdo_parentAnnounceNotify(void *arg)
{
	aps_data_ind_t *ad = (aps_data_ind_t *)arg;
	const u8 *payload = ad->asdu;
	u8 childNum = payload[2];
	const u8 *ptr = payload + 3;

	if ((payload[1] != ZDO_SUCCESS) || (childNum == 0U)) {
		return;
	}

	for (u8 i = 0; i < childNum; i++) {
		tl_zb_normal_neighbor_entry_t *entry = nwk_neTblGetByExtAddr((u8 *)ptr);

		if (entry != NULL) {
			zb_addrForNeighbor_t addrInfo;

			memset(&addrInfo, 0, sizeof(addrInfo));
			addrInfo.shortAddr = tl_zbshortAddrByIdx(entry->addrmapIdx);
			tl_zbExtAddrByIdx(entry->addrmapIdx, addrInfo.extAddr);
			addrInfo.relationship = NEIGHBOR_IS_CHILD;

			(void)nwk_nodeAddrInfoDelete(&addrInfo);
			tl_zbNeighborTableDelete(entry);
		}

		ptr += EXT_ADDR_LEN;
	}
}

void zdo_mgmtPermitJoinIndicate(void *arg)
{
	aps_data_ind_t *ad = (aps_data_ind_t *)arg;
	const u8 *payload = ad->asdu;
	nlme_permitJoining_req_t req = {
		.permitDuration = payload[1],
	};

	if (!ZB_NWK_IS_ADDRESS_BROADCAST(ad->dst_addr)) {
		zb_buf_t *buf = zb_buf_allocate();
		zdo_zdp_req_t zzr;

		if (buf == NULL) {
			zb_buf_free((zb_buf_t *)arg);
			return;
		}

		memset(&zzr, 0, sizeof(zzr));
		TL_BUF_INITIAL_ALLOC(buf, 2, zzr.zdu, u8 *);

		zzr.zdu[0] = payload[0];
#if defined(ZB_COORDINATOR_ROLE)
		zzr.zdu[1] = SS_ALLOW_REMOTE_TC_POLICY_CHANGE ? ZDO_SUCCESS : ZDO_INVALID_REQUEST;
#else
		zzr.zdu[1] = ZDO_SUCCESS;
#endif
		zzr.cluster_id = MGMT_PERMIT_JOINING_RSP_CLID;
		zzr.zduLen = 2;
		zzr.buff_addr = (u8 *)buf;
		zzr.dst_addr_mode = SHORT_ADDR_MODE;
		zzr.dst_nwk_addr = ad->src_short_addr;
		zdo_send_req(&zzr);
		zb_buf_free(buf);
	}

#if defined(ZB_COORDINATOR_ROLE)
	if (!SS_ALLOW_REMOTE_TC_POLICY_CHANGE) {
		zb_buf_free((zb_buf_t *)arg);
		return;
	}
#endif

	((nlme_permitJoining_req_t *)arg)->permitDuration = req.permitDuration;
	tl_zbPrimitivePost(TL_Q_HIGH2NWK, NWK_NLME_PERMIT_JOINING_REQ, arg);

	if ((zdoAppIndCbLst != NULL) && (zdoAppIndCbLst->zdpPermitJoinIndCb != NULL)) {
		zdoAppIndCbLst->zdpPermitJoinIndCb(&req);
	}
}
#endif

void zdo_nwkAddrIndicate(void *arg)
{
	ev_timer_taskPost(zdo_nwkAddrReqDelayCb, arg, ZDP_ADDR_REQ_DELAY_MS);
}
void zdo_ieeeAddrIndicate(void *arg)
{
	ev_timer_taskPost(zdo_ieeeAddrReqDelayCb, arg, ZDP_ADDR_REQ_DELAY_MS);
}

void zdo_descriptorsIndicate(void *arg)
{
	aps_data_ind_t *ad = (aps_data_ind_t *)arg;
	const u8 *payload = ad->asdu;
	const zdo_simple_descriptor_req_t *descriptorReq =
		(const zdo_simple_descriptor_req_t *)(payload + 1);
	u8 seqNum = payload[0];
	u16 nwkAddrReq = descriptorReq->nwk_addr_interest;
	zdo_status_t status;
	zdo_zdp_req_t zzr;
	af_simple_descriptor_t *simpleDesc;

	if (ZB_NWK_IS_ADDRESS_BROADCAST(ad->dst_addr)) {
		zb_buf_free((zb_buf_t *)arg);
		return;
	}

	memset(&zzr, 0, sizeof(zzr));
	TL_BUF_INITIAL_ALLOC((zb_buf_t *)arg, ZDO_CMD_PAYLOAD_MAX, zzr.zdu, u8 *);

	u8 *ptr = zzr.zdu;
	*ptr++ = seqNum;

	if (nwkAddrReq != ZB_PIB_SHORT_ADDRESS()) {
		status = (af_nodeDevTypeGet() == DEVICE_TYPE_COORDINATOR) ? ZDO_INVALID_REQUEST
									  : ZDO_DEVICE_NOT_FOUND;
		*ptr++ = status;
		*ptr++ = LO_UINT16(nwkAddrReq);
		*ptr++ = HI_UINT16(nwkAddrReq);

		if (ad->cluster_id == SIMPLE_DESC_REQ_CLID) {
			*ptr++ = descriptorReq->endpoint;
		}
	} else {
		*ptr++ = ZDO_SUCCESS;
		*ptr++ = LO_UINT16(nwkAddrReq);
		*ptr++ = HI_UINT16(nwkAddrReq);

		switch (ad->cluster_id) {
		case NODE_DESC_REQ_CLID:
			af_nodeDescriptorCopy((node_descriptor_t *)ptr);
			ptr += sizeof(node_descriptor_t);
			break;
		case POWER_DESC_REQ_CLID:
			af_powerDescriptorCopy((power_descriptor_t *)ptr);
			ptr += sizeof(power_descriptor_t);
			break;
		case SIMPLE_DESC_REQ_CLID:
			if ((descriptorReq->endpoint == 0) || (descriptorReq->endpoint > 240)) {
				zzr.zdu[1] = ZDO_INVALID_EP;
				*ptr++ = descriptorReq->endpoint;
				break;
			}

			simpleDesc = af_simpleDescGet(descriptorReq->endpoint);
			if (simpleDesc == NULL) {
				zzr.zdu[1] = ZDO_NOT_ACTIVE;
				*ptr++ = descriptorReq->endpoint;
				break;
			}

			{
				u8 lenPtrOffset = (u8)(ptr - zzr.zdu);
				ptr++;
				u8 simpleLen = af_simpleDescriptorCopy(ptr, simpleDesc);
				zzr.zdu[lenPtrOffset] = simpleLen;
				ptr += simpleLen;
			}
			break;
		default:
			break;
		}
	}

	zzr.cluster_id = ad->cluster_id | 0x8000U;
	zzr.zduLen = (u8)(ptr - zzr.zdu);
	zzr.buff_addr = (u8 *)arg;
	zzr.dst_addr_mode = SHORT_ADDR_MODE;
	zzr.dst_nwk_addr = ad->src_short_addr;
	zdo_send_req(&zzr);
	zb_buf_free((zb_buf_t *)arg);
}

void zdo_activeEpIndicate(void *arg)
{
	aps_data_ind_t *ad = (aps_data_ind_t *)arg;
	const u8 *payload = ad->asdu;
	u8 seqNum = payload[0];
	u16 nwkAddrReq;
	zdo_status_t status;
	u8 epNum;
	zdo_zdp_req_t zzr;

	if (ZB_NWK_IS_ADDRESS_BROADCAST(ad->dst_addr)) {
		zb_buf_free((zb_buf_t *)arg);
		return;
	}

	COPY_BUFFERTOU16(nwkAddrReq, payload + 1);
	memset(&zzr, 0, sizeof(zzr));
	epNum = af_availableEpNumGet();
	TL_BUF_INITIAL_ALLOC((zb_buf_t *)arg, (u8)(5 + epNum), zzr.zdu, u8 *);

	u8 *ptr = zzr.zdu;
	*ptr++ = seqNum;

	if (nwkAddrReq != ZB_PIB_SHORT_ADDRESS()) {
		status = (af_nodeDevTypeGet() == DEVICE_TYPE_COORDINATOR) ? ZDO_INVALID_REQUEST
									  : ZDO_DEVICE_NOT_FOUND;
		*ptr++ = status;
		*ptr++ = LO_UINT16(nwkAddrReq);
		*ptr++ = HI_UINT16(nwkAddrReq);
		*ptr++ = 0;
	} else {
		af_endpoint_descriptor_t *epDesc = af_epDescriptorGet();

		*ptr++ = ZDO_SUCCESS;
		*ptr++ = LO_UINT16(nwkAddrReq);
		*ptr++ = HI_UINT16(nwkAddrReq);
		*ptr++ = epNum;

		for (u8 i = 0; i < epNum; i++) {
			*ptr++ = epDesc[i].ep;
		}
	}

	zzr.cluster_id = ACTIVE_EP_RSP_CLID;
	zzr.zduLen = (u8)(ptr - zzr.zdu);
	zzr.buff_addr = (u8 *)arg;
	zzr.dst_addr_mode = SHORT_ADDR_MODE;
	zzr.dst_nwk_addr = ad->src_short_addr;
	zdo_send_req(&zzr);
	zb_buf_free((zb_buf_t *)arg);
}

void zdo_matchDescriptorIndicate(void *arg)
{
	aps_data_ind_t *ad = (aps_data_ind_t *)arg;
	const u8 *payload = ad->asdu;
	const zdo_match_descriptor_req_t *req = (const zdo_match_descriptor_req_t *)(payload + 1);
	u8 seqNum = payload[0];
	u16 nwkAddrReq = req->nwk_addr_interest;
	u16 profileId = req->profile_id;
	u8 inClusterNum = req->num_in_clusters;
	bool emptyRspSuppressed = ad->dst_addr_mode == APS_SHORT_DSTADDR_WITHEP &&
				  ZB_NWK_IS_ADDRESS_BROADCAST(ad->dst_addr);
	u16 inClusters[16];
	u16 outClusters[16];
	u8 outClusterNum;
	u8 *respPayload;
	u8 matchList[MAX_REQUESTED_CLUSTER_NUMBER];
	u8 matchCount = 0;
	zdo_status_t status = ZDO_SUCCESS;
	zdo_zdp_req_t zzr;

	if (inClusterNum > 16) {
		zb_buf_free((zb_buf_t *)arg);
		return;
	}

	const u8 *clusterPtr = (const u8 *)req->cluster_list;
	for (u8 i = 0; i < inClusterNum; i++) {
		COPY_BUFFERTOU16(inClusters[i], clusterPtr);
		clusterPtr += 2;
	}

	outClusterNum = *clusterPtr++;
	if ((u8)(inClusterNum + outClusterNum) > 16) {
		zb_buf_free((zb_buf_t *)arg);
		return;
	}

	for (u8 i = 0; i < outClusterNum; i++) {
		COPY_BUFFERTOU16(outClusters[i], clusterPtr);
		clusterPtr += 2;
	}

	if ((nwkAddrReq != ZB_PIB_SHORT_ADDRESS()) && !ZB_NWK_IS_ADDRESS_BROADCAST(nwkAddrReq)) {
		status = (af_nodeDevTypeGet() == DEVICE_TYPE_COORDINATOR) ? ZDO_INVALID_REQUEST
									  : ZDO_DEVICE_NOT_FOUND;
	} else {
		af_endpoint_descriptor_t *epDesc = af_epDescriptorGet();
		u8 epNum = af_availableEpNumGet();

		for (u8 i = 0; i < epNum; i++) {
			af_simple_descriptor_t *sd = epDesc[i].correspond_simple_desc;
			bool matched = TRUE;

			if (sd == NULL) {
				continue;
			}

			if ((profileId != WILDCARD_PROFILE_ID) &&
			    (sd->app_profile_id != profileId)) {
				continue;
			}

			for (u8 j = 0; j < inClusterNum; j++) {
				if (!cluster_list_contains(sd->app_in_cluster_lst,
							   sd->app_in_cluster_count,
							   inClusters[j])) {
					matched = FALSE;
					break;
				}
			}

			if (!matched) {
				continue;
			}

			for (u8 j = 0; j < outClusterNum; j++) {
				if (!cluster_list_contains(sd->app_out_cluster_lst,
							   sd->app_out_cluster_count,
							   outClusters[j])) {
					matched = FALSE;
					break;
				}
			}

			if (!matched) {
				continue;
			}

			if (matchCount < MAX_REQUESTED_CLUSTER_NUMBER) {
				bool duplicate = FALSE;

				for (u8 j = 0; j < matchCount; j++) {
					if (matchList[j] == epDesc[i].ep) {
						duplicate = TRUE;
						break;
					}
				}

				if (!duplicate) {
					matchList[matchCount++] = epDesc[i].ep;
				}
			}
		}
	}

	if ((matchCount == 0) && emptyRspSuppressed && (status == ZDO_SUCCESS)) {
		zb_buf_free((zb_buf_t *)arg);
		return;
	}

	memset(&zzr, 0, sizeof(zzr));
	TL_BUF_INITIAL_ALLOC((zb_buf_t *)arg, (u8)(5 + matchCount), zzr.zdu, u8 *);

	respPayload = zzr.zdu;
	respPayload[0] = seqNum;
	respPayload[1] = status;
	respPayload[2] = LO_UINT16(nwkAddrReq);
	respPayload[3] = HI_UINT16(nwkAddrReq);
	respPayload[4] = matchCount;
	memcpy(&respPayload[5], matchList, matchCount);

	zzr.cluster_id = MATCH_DESC_RSP_CLID;
	zzr.zduLen = (u8)(5 + matchCount);
	zzr.buff_addr = (u8 *)arg;
	zzr.dst_addr_mode = SHORT_ADDR_MODE;
	zzr.dst_nwk_addr = ad->src_short_addr;
	zdo_send_req(&zzr);
	zb_buf_free((zb_buf_t *)arg);
}

void zdo_SysServerDiscoveryIndicate(void *arg)
{
	aps_data_ind_t *ad = (aps_data_ind_t *)arg;
	const u8 *payload = ad->asdu;
	const zdo_system_server_discovery_req_t *req =
		(const zdo_system_server_discovery_req_t *)(payload + 1);
	u8 seqNum = payload[0];
	u16 reqMask = req->server_mask;
	node_descriptor_t nodeDesc;
	u16 rspMask;
	zdo_zdp_req_t zzr;

	af_nodeDescriptorCopy(&nodeDesc);
	rspMask = nodeDesc.server_mask & reqMask;
	if (rspMask == 0) {
		zb_buf_free((zb_buf_t *)arg);
		return;
	}

	memset(&zzr, 0, sizeof(zzr));
	TL_BUF_INITIAL_ALLOC((zb_buf_t *)arg, sizeof(zdo_system_server_discovery_resp_t), zzr.zdu,
			     u8 *);

	zzr.zdu[0] = seqNum;
	zzr.zdu[1] = ZDO_SUCCESS;
	zzr.zdu[2] = LO_UINT16(rspMask);
	zzr.zdu[3] = HI_UINT16(rspMask);

	zzr.cluster_id = SYSTEM_SERVER_DISCOVERY_RSP_CLID;
	zzr.zduLen = sizeof(zdo_system_server_discovery_resp_t);
	zzr.buff_addr = (u8 *)arg;
	zzr.dst_addr_mode = SHORT_ADDR_MODE;
	zzr.dst_nwk_addr = ad->src_short_addr;
	zdo_send_req(&zzr);
	zb_buf_free((zb_buf_t *)arg);
}
u8 zdo_bind_unbind_req(const zdo_bind_req_t *req, zdo_zdp_req_t *zzr, bool bind)
{
	zb_buf_t *buf;
	u8 payloadLen;

	buf = zb_buf_allocate();
	if (buf == NULL) {
		return ZDO_INSUFFICIENT_SPACE;
	}

	zzr->buff_addr = (u8 *)buf;
	payloadLen = (req->dst_addr_mode == LONG_EXADDR_DSTENDPOINT) ? 21U : 14U;
	TL_BUF_INITIAL_ALLOC(buf, (u8)(payloadLen + 1U), zzr->zdu, u8 *);

	zzr->zdpSeqNum = zdp_txSeqNo++;
	zzr->zdu[0] = zzr->zdpSeqNum;
	memcpy(zzr->zdu + 1, req, payloadLen);
	zzr->zduLen = (u8)(payloadLen + 1U);
	zzr->cluster_id = bind ? BIND_REQ_CLID : UNBIND_REQ_CLID;
	zzr->dst_addr_mode = SHORT_ADDR_MODE;

	zdo_send_req(zzr);
	zb_buf_free(buf);

	return ZDO_SUCCESS;
}

_attribute_no_inline_ static void zdo_bind_toggle_action(void *arg)
{
	zdo_bind_req_t bindReq;
	zdo_zdp_req_t zzr;
	u16 clusterId = ed_bind_ctx()->clusterList[0];
	u16 srcShortAddr;
	u16 dstShortAddr;
	u8 srcEp;
	u8 dstEp;
	u16 addrMapIdx;

	(void)arg;

	memset(&bindReq, 0, sizeof(bindReq));
	memset(&zzr, 0, sizeof(zzr));

	if (ed_bind_ctx()->src1MatchCount != 0U) {
		srcEp = ed_bind_ctx()->src1Endpoint;
		srcShortAddr = ed_bind_ctx()->src1ShortAddr;
		dstShortAddr = ed_bind_ctx()->src2ShortAddr;
		dstEp = ed_bind_ctx()->src2Endpoint;
		zzr.dst_nwk_addr = srcShortAddr;
	} else {
		srcEp = ed_bind_ctx()->src2Endpoint;
		srcShortAddr = ed_bind_ctx()->src2ShortAddr;
		dstShortAddr = ed_bind_ctx()->src1ShortAddr;
		dstEp = ed_bind_ctx()->src1Endpoint;
		zzr.dst_nwk_addr = srcShortAddr;
	}

	tl_zbExtAddrByShortAddr(srcShortAddr, bindReq.src_addr, &addrMapIdx);
	tl_zbExtAddrByShortAddr(dstShortAddr, bindReq.dst_ext_addr, &addrMapIdx);
	bindReq.src_endpoint = srcEp;
	bindReq.cid16_l = LO_UINT16(clusterId);
	bindReq.cid16_h = HI_UINT16(clusterId);
	bindReq.dst_addr_mode = LONG_EXADDR_DSTENDPOINT;
	bindReq.dst_endpoint = dstEp;
	zzr.zdoRspReceivedIndCb = zdo_bind_toggle_cb;

	if (zdo_bind_unbind_req(&bindReq, &zzr, FALSE) == ZDO_INSUFFICIENT_SPACE) {
		tl_zbTaskPost(zdo_bind_toggle_action, NULL);
	}
}

_attribute_no_inline_ static void zdo_bind_unbind_after_toggle_clone_1(
	bool bind, zdo_callback cb) __asm__("zdo_bind_unbind_after_toggle.clone.1");
_attribute_no_inline_ static void zdo_bind_unbind_after_toggle_clone_1(bool bind, zdo_callback cb)
{
	zdo_bind_req_t bindReq;
	zdo_zdp_req_t zzr;
	bool useFirstDirection;
	u8 totalCount = ed_bind_ctx()->clusterTotalCount;
	u16 clusterId;
	u16 srcShortAddr;
	u16 dstShortAddr;
	u8 srcEp;
	u8 dstEp;
	u16 addrMapIdx;

	if (!bind && (totalCount == 1U)) {
		ed_bind_ctx_reset();
		return;
	}

	if (totalCount == 0U) {
		ed_bind_ctx_reset();
		return;
	}

	useFirstDirection = (ed_bind_ctx()->src1MatchCount >= totalCount);
	memset(&bindReq, 0, sizeof(bindReq));
	memset(&zzr, 0, sizeof(zzr));

	if (useFirstDirection) {
		srcEp = ed_bind_ctx()->src1Endpoint;
		srcShortAddr = ed_bind_ctx()->src1ShortAddr;
		dstShortAddr = ed_bind_ctx()->src2ShortAddr;
		dstEp = ed_bind_ctx()->src2Endpoint;
	} else {
		srcEp = ed_bind_ctx()->src2Endpoint;
		srcShortAddr = ed_bind_ctx()->src2ShortAddr;
		dstShortAddr = ed_bind_ctx()->src1ShortAddr;
		dstEp = ed_bind_ctx()->src1Endpoint;
	}

	tl_zbExtAddrByShortAddr(srcShortAddr, bindReq.src_addr, &addrMapIdx);
	tl_zbExtAddrByShortAddr(dstShortAddr, bindReq.dst_ext_addr, &addrMapIdx);
	bindReq.src_endpoint = srcEp;
	bindReq.dst_addr_mode = LONG_EXADDR_DSTENDPOINT;
	bindReq.dst_endpoint = dstEp;
	zzr.dst_addr_mode = SHORT_ADDR_MODE;
	zzr.dst_nwk_addr = srcShortAddr;
	zzr.zdoRspReceivedIndCb = cb;

	ed_bind_ctx()->clusterTotalCount--;
	clusterId = ed_bind_ctx()->clusterList[ed_bind_ctx()->clusterTotalCount];
	bindReq.cid16_l = LO_UINT16(clusterId);
	bindReq.cid16_h = HI_UINT16(clusterId);

	(void)zdo_bind_unbind_req(&bindReq, &zzr, bind);
}

void zdo_bind_toggle_cb(void *arg)
{
	aps_data_ind_t *ad = (aps_data_ind_t *)arg;

	if (ad->asdu[1] == ZDO_NO_ENTRY) {
		zdo_bind_unbind_after_toggle_clone_1(TRUE, zdo_end_device_bind_cb);
		return;
	}

	zdo_bind_unbind_after_toggle_clone_1(FALSE, zdo_end_device_unbind_cb);
}

void zdo_end_device_unbind_cb(void *arg)
{
	(void)arg;
	zdo_bind_unbind_after_toggle_clone_1(FALSE, zdo_end_device_unbind_cb);
}

void zdo_end_device_bind_cb(void *arg)
{
	(void)arg;
	zdo_bind_unbind_after_toggle_clone_1(TRUE, zdo_end_device_bind_cb);
}

u8 zdo_end_device_bind_req(const zdo_edBindReq_t *req, zdo_zdp_req_t *zzr)
{
	zb_buf_t *buf;
	u8 payloadLen = (u8)(16U + req->num_in_clusters * 2U + req->num_out_clusters * 2U);
	u8 *ptr;

	buf = zb_buf_allocate();
	if (buf == NULL) {
		return ZDO_INSUFFICIENT_SPACE;
	}

	zzr->buff_addr = (u8 *)buf;
	TL_BUF_INITIAL_ALLOC(buf, payloadLen, zzr->zdu, u8 *);

	zzr->zdpSeqNum = zdp_txSeqNo++;
	ptr = zzr->zdu;
	*ptr++ = zzr->zdpSeqNum;
	*ptr++ = LO_UINT16(req->binding_target_addr);
	*ptr++ = HI_UINT16(req->binding_target_addr);
	ZB_IEEE_ADDR_COPY(ptr, req->src_ext_addr);
	ptr += EXT_ADDR_LEN;
	*ptr++ = req->src_endpoint;
	*ptr++ = LO_UINT16(req->profile_id);
	*ptr++ = HI_UINT16(req->profile_id);
	*ptr++ = req->num_in_clusters;
	memcpy(ptr, req->in_cluster_lst, req->num_in_clusters * 2U);
	ptr += req->num_in_clusters * 2U;
	*ptr++ = req->num_out_clusters;
	memcpy(ptr, req->out_cluster_lst, req->num_out_clusters * 2U);

	zzr->cluster_id = END_DEVICE_BIND_REQ_CLID;
	zzr->zduLen = payloadLen;
	zzr->dst_addr_mode = SHORT_ADDR_MODE;
	zzr->dst_nwk_addr = req->binding_target_addr;
	zdo_send_req(zzr);
	zb_buf_free(buf);

	return ZDO_SUCCESS;
}

zdo_status_t zdo_apsmeBindUnBind(bool bind, aps_me_bind_req_t *req)
{
	aps_status_t apsStatus;
	zdo_status_t status = ZDO_NOT_SUPPORTED;

	if (!ZB_IEEE_ADDR_CMP(req->ext_src_addr, ZB_PIB_EXTENDED_ADDRESS())) {
		return status;
	}

	if ((req->src_ep == 0U) || (req->src_ep == 0xffU)) {
		return ZDO_INVALID_EP;
	}

	if (req->dst_addr_mode == SHORT_GROUPADDR_NODSTENDPOINT) {
		if (bind) {
			apsStatus = aps_me_bind_req(req);
			if (apsStatus == APS_STATUS_SUCCESS) {
				return ZDO_SUCCESS;
			}
			return (apsStatus == APS_STATUS_TABLE_FULL) ? ZDO_TABLE_FULL
								    : ZDO_NOT_SUPPORTED;
		}

		apsStatus = aps_me_unbind_req(req);
		if (apsStatus == APS_STATUS_SUCCESS) {
			return ZDO_SUCCESS;
		}
		return (apsStatus == APS_STATUS_INVALID_BINDING) ? ZDO_NO_ENTRY : ZDO_NOT_SUPPORTED;
	}

	if ((req->dst_addr_mode != LONG_EXADDR_DSTENDPOINT) || (req->dst_ep == 0U)) {
		return ZDO_INVALID_EP;
	}

	if (bind) {
		apsStatus = aps_me_bind_req(req);
		if (apsStatus == APS_STATUS_SUCCESS) {
			return ZDO_SUCCESS;
		}
		return (apsStatus == APS_STATUS_TABLE_FULL) ? ZDO_TABLE_FULL : ZDO_NOT_SUPPORTED;
	}

	apsStatus = aps_me_unbind_req(req);
	if (apsStatus == APS_STATUS_SUCCESS) {
		return ZDO_SUCCESS;
	}

	return (apsStatus == APS_STATUS_INVALID_BINDING) ? ZDO_NO_ENTRY : ZDO_NOT_SUPPORTED;
}

void zdo_bindOrUnbindIndicate(void *arg)
{
	aps_data_ind_t *ad = (aps_data_ind_t *)arg;
	aps_me_bind_req_t bindReq;
	zdo_status_t status;
	zdo_zdp_req_t zzr;
	bool bind;

	memcpy(&bindReq, ad->asdu + 1, ad->asduLength - 1U);
	bind = (ad->cluster_id == BIND_REQ_CLID);
	status = zdo_apsmeBindUnBind(bind, &bindReq);

	memset(&zzr, 0, sizeof(zzr));
	TL_BUF_INITIAL_ALLOC((zb_buf_t *)arg, 2, zzr.zdu, u8 *);
	zzr.zdu[0] = ad->asdu[0];
	zzr.zdu[1] = status;
	zzr.cluster_id = bind ? BIND_RSP_CLID : UNBIND_RSP_CLID;
	zzr.zduLen = 2;
	zzr.buff_addr = (u8 *)arg;
	zzr.dst_addr_mode = SHORT_ADDR_MODE;
	zzr.dst_nwk_addr = ad->src_short_addr;
	zdo_send_req(&zzr);
	zb_buf_free((zb_buf_t *)arg);
}

void zdo_endDeviceBindIndicate(void *arg)
{
	aps_data_ind_t *ad = (aps_data_ind_t *)arg;
	zdo_ed_bind_req_view_t currentReq;
	zdo_ed_bind_req_view_t pendingReq;
	aps_data_ind_t *pendingAd;

	if ((ad->asdu[9] == 0U) || (ad->asdu[9] == 0xffU)) {
		zdo_end_device_bind_resp_send(arg, ZDO_INVALID_EP, ad->asdu[0], ad->src_short_addr);
		return;
	}

	zdo_ed_bind_req_parse(ad->asdu, &currentReq);

	pendingAd = (aps_data_ind_t *)ed_bind_ctx()->pendingBuf;
	if (pendingAd == NULL) {
		if (zdo_bind_timer_event != NULL) {
			ev_timer_taskCancel(&zdo_bind_timer_event);
		}

		zdo_bind_timer_event =
			ev_timer_taskPost(zdo_end_device_bind_timeout_cb, arg, 20000);
		ed_bind_ctx()->pendingBuf = arg;
		return;
	}

	if (zdo_bind_timer_event != NULL) {
		ev_timer_taskCancel(&zdo_bind_timer_event);
	}

	zdo_ed_bind_req_parse(pendingAd->asdu, &pendingReq);
	if (pendingReq.profileId != currentReq.profileId) {
		zdo_end_device_bind_resp_send(arg, ZDO_NO_MATCH, currentReq.seqNum,
					      ad->src_short_addr);
		zdo_end_device_bind_resp_send(pendingAd, ZDO_NO_MATCH, pendingReq.seqNum,
					      pendingAd->src_short_addr);
		ed_bind_ctx_reset();
		return;
	}

	ed_bind_ctx()->clusterTotalCount = 0;
	zdo_cluster_list_match_process_clone_2(pendingReq.numOutClusters, pendingReq.outClusters,
					       currentReq.numInClusters, currentReq.inClusters);
	ed_bind_ctx()->src1MatchCount = ed_bind_ctx()->clusterTotalCount;
	zdo_cluster_list_match_process_clone_2(pendingReq.numInClusters, pendingReq.inClusters,
					       currentReq.numOutClusters, currentReq.outClusters);
	ed_bind_ctx()->src2MatchCount =
		(u8)(ed_bind_ctx()->clusterTotalCount - ed_bind_ctx()->src1MatchCount);

	if (ed_bind_ctx()->clusterTotalCount == 0U) {
		zdo_end_device_bind_resp_send(arg, ZDO_NO_MATCH, currentReq.seqNum,
					      ad->src_short_addr);
		zdo_end_device_bind_resp_send(pendingAd, ZDO_NO_MATCH, pendingReq.seqNum,
					      pendingAd->src_short_addr);
		ed_bind_ctx_reset();
		return;
	}

	ed_bind_ctx()->src1ShortAddr = pendingAd->src_short_addr;
	ed_bind_ctx()->src1Endpoint = pendingReq.srcEndpoint;
	ed_bind_ctx()->src2ShortAddr = ad->src_short_addr;
	ed_bind_ctx()->src2Endpoint = currentReq.srcEndpoint;
	tl_zbTaskPost(zdo_bind_toggle_action, NULL);

	zdo_end_device_bind_resp_send(arg, ZDO_SUCCESS, currentReq.seqNum, ad->src_short_addr);
	zdo_end_device_bind_resp_send(pendingAd, ZDO_SUCCESS, pendingReq.seqNum,
				      pendingAd->src_short_addr);
}

void zdo_nwkUpdateNotifyRespSend(void *arg)
{
	u8 scanResult[224];
	zdo_zdp_req_t zzr;
	u8 energyCnt;
	u8 *ptr;

	memcpy(scanResult, arg, sizeof(scanResult));
	energyCnt = scanResult[3];

	memset(&zzr, 0, sizeof(zzr));
	TL_BUF_INITIAL_ALLOC((zb_buf_t *)arg, (u8)(11 + energyCnt), zzr.zdu, u8 *);

	ptr = zzr.zdu;
	*ptr++ = zdo_nur.seqNum;
	*ptr++ = scanResult[0];
	*ptr++ = (u8)(zdo_nur.scanChannels);
	*ptr++ = (u8)(zdo_nur.scanChannels >> 8);
	*ptr++ = (u8)(zdo_nur.scanChannels >> 16);
	*ptr++ = (u8)(zdo_nur.scanChannels >> 24);
	*ptr++ = LO_UINT16(g_zbInfo.nwkNib.txTotal);
	*ptr++ = HI_UINT16(g_zbInfo.nwkNib.txTotal);
	*ptr++ = LO_UINT16(g_zbInfo.nwkNib.txFail);
	*ptr++ = HI_UINT16(g_zbInfo.nwkNib.txFail);
	*ptr++ = energyCnt;
	memcpy(ptr, &scanResult[8], energyCnt);
	ptr += energyCnt;

	zzr.cluster_id = MGMT_NWK_UPDATE_NOTIFY_CLID;
	zzr.zduLen = (u8)(ptr - zzr.zdu);
	zzr.buff_addr = (u8 *)arg;
	zzr.dst_addr_mode = SHORT_ADDR_MODE;
	zzr.dst_nwk_addr = zdo_nur.srcAddr;
	zdo_send_req(&zzr);
	zb_buf_free((zb_buf_t *)arg);
}

void zdo_mgmtNwkUpdateIndicate(void *arg)
{
	aps_data_ind_t *ad = (aps_data_ind_t *)arg;
	const u8 *payload = ad->asdu;
	const zdo_mgmt_nwk_update_req_t *req = (const zdo_mgmt_nwk_update_req_t *)(payload + 1);
	u8 seqNum = payload[0];
	u32 scanChannels = req->scan_ch;
	u8 scanDuration = req->scan_duration;
	u8 scanCountOrUpdateId = req->scan_cnt;
	u16 nwkManagerAddr = req->nwk_manager_addr;
	zdo_status_t status = ZDO_INVALID_REQUEST;

	if (scanDuration == ZDO_NWK_MANAGER_CHANNEL_CHANGE) {
		u8 channel = zdo_channel_page2num(scanChannels);
		u8 acceptChannel = zdo_af_get_accept_nwk_update_channel();

		if ((channel >= 11) && (channel <= 26) &&
		    ((acceptChannel == 0) || (acceptChannel == 0xff) ||
		     (acceptChannel == channel))) {
			g_zbInfo.nwkNib.updateId = scanCountOrUpdateId;
#if defined(ZB_ED_ROLE)
			zdo_nwkRejoinStart(scanChannels,
					   zdo_cfg_attributes.config_nwk_scan_duration);
#else
			ev_timer_taskPost(zdo_change_channel_cb, (void *)(uintptr_t)channel, 46U);
#endif

			zb_buf_free((zb_buf_t *)arg);
			return;
		}
	}

	if (scanDuration == ZDO_NWK_MANAGER_ATTRIBUTES_CHANGE) {
		aps_ib.aps_channel_mask = scanChannels;
		if (ZB_IEEE_ADDR_IS_INVALID(ss_ib.trust_center_address) || (nwkManagerAddr == 0)) {
			g_zbInfo.nwkNib.managerAddr = nwkManagerAddr;
		}

		zb_buf_free((zb_buf_t *)arg);
		return;
	}

	if ((scanDuration <= ZDO_NWK_MANAGER_MAX_SCAN_DURATION) && (scanCountOrUpdateId <= 5)) {
		zdo_nur.scanChannels = scanChannels;
		zdo_nur.scanDuration = scanDuration;
		zdo_nur.scanCount = scanCountOrUpdateId;
		zdo_nur.srcAddr = ad->src_short_addr;
		zdo_nur.seqNum = seqNum;

		status = zdo_nlmeEdScanReq(scanChannels, scanDuration, scanCountOrUpdateId);
		if (status == ZDO_SUCCESS) {
			zdo_mgmt_nwk_flag |= 0x01;
			zb_buf_free((zb_buf_t *)arg);
			return;
		}

		status = ZDO_NOT_SUPPORTED;
	}

	if (!ZB_NWK_IS_ADDRESS_BROADCAST(ad->dst_addr)) {
		zdo_zdp_req_t zzr;

		memset(&zzr, 0, sizeof(zzr));
		TL_BUF_INITIAL_ALLOC((zb_buf_t *)arg, 2, zzr.zdu, u8 *);
		zzr.zdu[0] = seqNum;
		zzr.zdu[1] = status;
		zzr.cluster_id = MGMT_NWK_UPDATE_NOTIFY_CLID;
		zzr.zduLen = 2;
		zzr.buff_addr = (u8 *)arg;
		zzr.dst_addr_mode = SHORT_ADDR_MODE;
		zzr.dst_nwk_addr = ad->src_short_addr;
		zdo_send_req(&zzr);
	}

	zb_buf_free((zb_buf_t *)arg);
}
void zdo_mgmtBindIndicate(void *arg)
{
	aps_data_ind_t *ad = (aps_data_ind_t *)arg;
	aps_binding_entry_t *table;
	u8 totalEntries;
	u8 startIndex = ad->asdu[1];
	u8 listCount = 0;
	u8 currentValid = 0;
	zdo_zdp_req_t zzr;
	u8 *ptr;

	if (ZB_NWK_IS_ADDRESS_BROADCAST(ad->dst_addr)) {
		zb_buf_free((zb_buf_t *)arg);
		return;
	}

	totalEntries = aps_bindingTblEntryNum();
	table = aps_bindingTblEntryGet();
	if (startIndex < totalEntries) {
		listCount = (u8)(totalEntries - startIndex);
		if (listCount > 2U) {
			listCount = 2U;
		}
	}

	memset(&zzr, 0, sizeof(zzr));
	TL_BUF_INITIAL_ALLOC((zb_buf_t *)arg, 47, zzr.zdu, u8 *);

	ptr = zzr.zdu;
	*ptr++ = ad->asdu[0];
	*ptr++ = ZDO_SUCCESS;
	*ptr++ = totalEntries;
	*ptr++ = startIndex;
	*ptr++ = listCount;

	for (u8 i = 0; (i < APS_BINDING_TABLE_SIZE) && (listCount != 0U); i++) {
		if (!table[i].used) {
			continue;
		}

		if (currentValid++ < startIndex) {
			continue;
		}

		ZB_IEEE_ADDR_COPY(ptr, ZB_PIB_EXTENDED_ADDRESS());
		ptr += EXT_ADDR_LEN;
		*ptr++ = table[i].srcEp;
		*ptr++ = LO_UINT16(table[i].clusterId);
		*ptr++ = HI_UINT16(table[i].clusterId);
		*ptr++ = table[i].dstAddrMode;

		if (table[i].dstAddrMode == APS_LONG_DSTADDR_WITHEP) {
			ZB_IEEE_ADDR_COPY(ptr, table[i].dstExtAddrInfo.extAddr);
			ptr += EXT_ADDR_LEN;
			*ptr++ = table[i].dstExtAddrInfo.dstEp;
		} else {
			*ptr++ = LO_UINT16(table[i].groupAddr);
			*ptr++ = HI_UINT16(table[i].groupAddr);
		}

		listCount--;
	}

	zzr.cluster_id = MGMT_BIND_RSP_CLID;
	zzr.zduLen = (u8)(ptr - zzr.zdu);
	zzr.buff_addr = (u8 *)arg;
	zzr.dst_addr_mode = SHORT_ADDR_MODE;
	zzr.dst_nwk_addr = ad->src_short_addr;
	zdo_send_req(&zzr);
	zb_buf_free((zb_buf_t *)arg);
}

void zdo_mgmtLqiIndicate(void *arg)
{
	aps_data_ind_t *ad = (aps_data_ind_t *)arg;
	u8 startIndex = ad->asdu[1];
	u8 totalEntries;
	u8 listCount = 0;
	u8 validIndex = 0;
	zdo_zdp_req_t zzr;
	u8 *ptr;

	if (ZB_NWK_IS_ADDRESS_BROADCAST(ad->dst_addr)) {
		zb_buf_free((zb_buf_t *)arg);
		return;
	}

	totalEntries = tl_zbNeighborTableNumGet();

	memset(&zzr, 0, sizeof(zzr));
	TL_BUF_INITIAL_ALLOC((zb_buf_t *)arg, 49, zzr.zdu, u8 *);

	ptr = zzr.zdu;
	*ptr++ = ad->asdu[0];
	*ptr++ = ZDO_SUCCESS;
	*ptr++ = totalEntries;
	*ptr++ = startIndex;

	if ((totalEntries > startIndex) && (totalEntries != 0U)) {
		listCount = (u8)(totalEntries - startIndex);
		if (listCount > 2U) {
			listCount = 2U;
		}
	}

	*ptr++ = listCount;

	for (u8 i = 0, out = 0; (i < TL_ZB_NEIGHBOR_TABLE_SIZE) && (out < listCount); i++) {
		tl_zb_normal_neighbor_entry_t *entry = &g_zb_neighborTbl.neighborTbl[i];
		neighbor_tbl_lst_t n;

		if (!entry->used) {
			continue;
		}

		if (validIndex++ < startIndex) {
			continue;
		}

		memset(&n, 0, sizeof(n));
		ZB_EXTPANID_COPY(n.ext_pan_id, g_zbNIB.extPANId);
		tl_zbExtAddrByIdx(entry->addrmapIdx, n.ext_addr);
		n.network_addr = tl_zbshortAddrByIdx(entry->addrmapIdx);
		n.deviceType = entry->deviceType & 0x03U;
		n.rxOnWhenIdle = entry->rxOnWhileIdle ? 3U : 0U;
		n.relationship = entry->relationship & 0x07U;
		n.permitJoining = 2U;
		n.depth = entry->depth;
		n.lqi = entry->lqi;
		memcpy(ptr, &n, sizeof(n));
		ptr += sizeof(n);
		out++;
	}

	zzr.cluster_id = MGMT_LQI_RSP_CLID;
	zzr.zduLen = (u8)(ptr - zzr.zdu);
	zzr.buff_addr = (u8 *)arg;
	zzr.dst_addr_mode = SHORT_ADDR_MODE;
	zzr.dst_nwk_addr = ad->src_short_addr;
	zdo_send_req(&zzr);
	zb_buf_free((zb_buf_t *)arg);
}

void zdo_mgmtLeaveIndicate(void *arg)
{
	aps_data_ind_t *ad = (aps_data_ind_t *)arg;
	zdo_mgmt_leave_req_t req;
#if defined(ZB_COORDINATOR_ROLE)
	zdo_status_t status = ZDO_NOT_SUPPORTED;
#else
	zdo_status_t status = ZDO_SUCCESS;
#endif

	if (ad->asduLength != 10U) {
		zb_buf_free((zb_buf_t *)arg);
		return;
	}

	memcpy(&req, ad->asdu + 1, sizeof(req));
#if defined(ZB_ED_ROLE)
	if (ad->dst_addr != tl_zbNeighborParentShortAddrGet()) {
		zb_buf_free((zb_buf_t *)arg);
		return;
	}
#endif

	if (!ZB_IEEE_ADDR_IS_ZERO(req.device_addr) &&
	    !ZB_IEEE_ADDR_CMP(req.device_addr, ZB_PIB_EXTENDED_ADDRESS())) {
		tl_zb_normal_neighbor_entry_t *entry = nwk_neTblGetByExtAddr(req.device_addr);

		status = ZDO_MGMT_LEAVE_UNKNOWN_DEVICE;
		if ((entry != NULL) && (entry->deviceType != NWK_DEVICE_TYPE_ED)) {
			status = ZDO_SUCCESS;
		}
	}

	if (zdo_af_get_mgmtLeave_use_aps_sec() &&
	    ((ad->security_status & SECURITY_IN_APSLAYER) == 0U)) {
		status = ZDO_NOT_AUTHORIZED;
	}

	if (!ZB_NWK_IS_ADDRESS_BROADCAST(ad->dst_addr)) {
		zdo_zdp_req_t zzr;

		memset(&zzr, 0, sizeof(zzr));
		TL_BUF_INITIAL_ALLOC((zb_buf_t *)arg, 2, zzr.zdu, u8 *);
		zzr.zdu[0] = ad->asdu[0];
		zzr.zdu[1] = status;
		zzr.cluster_id = MGMT_LEAVE_RSP_CLID;
		zzr.zduLen = 2;
		zzr.buff_addr = (u8 *)arg;
		zzr.dst_addr_mode = SHORT_ADDR_MODE;
		zzr.dst_nwk_addr = ad->src_short_addr;
		zdo_send_req(&zzr);
		zb_buf_free((zb_buf_t *)arg);
	} else {
		zb_buf_free((zb_buf_t *)arg);
	}

	if (status == ZDO_SUCCESS) {
		zb_buf_t *buf = zb_buf_allocate();

		if (buf != NULL) {
			nlme_leave_req_t *leaveReq = (nlme_leave_req_t *)buf->buf;

			ZB_IEEE_ADDR_COPY(leaveReq->deviceAddr, req.device_addr);
			leaveReq->removeChildren = req.lr_bitfields.remove_children;
			leaveReq->rejoin = req.lr_bitfields.rejoin;
			ev_timer_taskPost(zdoMgmtLeaveCmdProcessCb, buf, 100);
		}
	}
}
