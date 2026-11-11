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
	uint16_t seq;
	uint8_t used;
	uint8_t active;
} zdp_cb_info_t;

typedef struct _attribute_packed_ {
	uint32_t scanChannels;
	uint8_t scanDuration;
	uint8_t scanCount;
	uint16_t srcAddr;
	uint8_t seqNum;
} zdo_nwk_update_req_state_t;

typedef struct {
	uint8_t seqNum;
	uint8_t srcEndpoint;
	uint16_t profileId;
	uint8_t numInClusters;
	const uint8_t *inClusters;
	uint8_t numOutClusters;
	const uint8_t *outClusters;
} zdo_ed_bind_req_view_t;

typedef struct _attribute_packed_ {
	void *pendingBuf;
	uint16_t src1ShortAddr;
	uint8_t src1Endpoint;
	uint8_t src1MatchCount;
	uint8_t unused_after_src1[4]; /* offset 0x0a, never accessed */
	uint16_t src2ShortAddr;
	uint8_t src2Endpoint;
	uint8_t src2MatchCount;
	uint16_t clusterList[ED_BIND_CLUSTER_LIST_MAX];
	uint8_t clusterTotalCount;
	uint8_t unused_tail; /* last byte, never accessed */
} ed_binding_ctx_t;

static uint8_t remainChildListNum_8733 __asm__("remainChildListNum.8733") = 0;
static zdo_nwk_update_req_state_t zdo_nur;
static uint8_t zdp_txSeqNo = 0;
static uint8_t zdpCblWptr = 0;
ev_timer_event_t *zdo_bind_timer_event = NULL;
static void *ed_binding_state __asm__("ent.8732") = NULL;
zdp_cb_info_t zdp_cbl[ZDP_CB_MAX];
ed_binding_ctx_t ed_binding_ctx;

static bool cluster_list_contains(const uint16_t *list, uint8_t cnt, uint16_t clusterId)
{
	for (uint8_t i = 0; i < cnt; i++) {
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

static _always_inline void zdo_ed_bind_req_parse(const uint8_t *payload, zdo_ed_bind_req_view_t *req)
{
	uint16_t outPos;

	req->seqNum = payload[0];
	req->srcEndpoint = payload[11];
	COPY_BUFFERTOU16(req->profileId, payload + 12);
	req->numInClusters = payload[14];

	req->inClusters = payload + 15;
	outPos = (uint16_t)(15U + (uint16_t)req->numInClusters * 2U);
	req->numOutClusters = payload[outPos];
	req->outClusters = payload + outPos + 1U;
}

static inline uint8_t zdo_assoc_child_list_fill(uint8_t startIndex, uint8_t maxCount, uint8_t *dst)
{
	zb_normal_neighbor_entry_t *entry = NULL;
	uint8_t skipped = 0;
	uint8_t written = 0;

	while ((written < maxCount) &&
	       ((entry = zb_NeighborTabSearchForChildEndDev(entry)) != NULL)) {
		if (skipped < startIndex) {
			skipped++;
			continue;
		}

		{
			uint16_t addrmapIdx = entry->addrmapIdx;
			uint16_t shortAddr = zb_shortAddrByIdx(addrmapIdx);

			dst[written * 2U] = LO_UINT16(shortAddr);
			dst[written * 2U + 1U] = HI_UINT16(shortAddr);
			written++;
		}
	}

	return written;
}

static inline uint8_t zdo_assoc_child_rsp_alloc_size(uint8_t startIndex, uint8_t *count)
{
	uint8_t childNum = zb_NeighborTableChildEDNumGet();

	if ((childNum == 0U) || (startIndex >= childNum)) {
		*count = 0;
		return ZDO_ADDR_RSP_ALLOC_BASE;
	}

	childNum = (uint8_t)(childNum - startIndex);
	if (childNum > ZDP_ASSOC_LIST_LIMIT) {
		*count = ZDP_ASSOC_LIST_LIMIT;
		return ZDO_ADDR_RSP_ALLOC_MAX;
	}

	*count = childNum;
	return (uint8_t)(ZDO_ADDR_RSP_ALLOC_BASE + childNum * 2U);
}

_attribute_no_inline_ static int zdoMgmtLeaveCmdProcessCb(void *arg)
{
	zb_PrimitivePost(ZB_Q_HIGH2NWK, NWK_NLME_LEAVE_REQ, arg);
	return -1;
}

_attribute_no_inline_ static int zdo_change_channel_cb(void *arg)
{
	uint8_t channel = (uint8_t)(uintptr_t)arg;

	zb_MacChannelSet(channel);
	g_zbInfo.macPib.phyChannelCur = channel;
	g_zbInfo.nwkNib.txTotal = 0;
	g_zbInfo.nwkNib.txFail = 0;
	zb_TaskPost(zb_info_save, NULL);

	return -1;
}
_attribute_no_inline_ static void zdo_cluster_list_match_process_clone_2(
	uint8_t clusterNum1, const uint8_t *clusterList1, uint8_t clusterNum2,
	const uint8_t *clusterList2) __asm__("zdo_cluster_list_match_process.clone.2");
_attribute_no_inline_ static void zdo_cluster_list_match_process_clone_2(uint8_t clusterNum1,
									 const uint8_t *clusterList1,
									 uint8_t clusterNum2,
									 const uint8_t *clusterList2)
{
	for (uint8_t i = 0; i < clusterNum1; i++) {
		uint16_t clusterId;

		COPY_BUFFERTOU16(clusterId, clusterList1 + i * 2U);

		for (uint8_t j = 0; j < clusterNum2; j++) {
			uint16_t otherClusterId;

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

void zdp_cb_process(uint16_t tsn, void *buf)
{
	for (uint8_t i = 0; i < ZDP_CB_MAX; i++) {
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

zdp_cb_info_t *zdo_cb_exist(uint16_t seqNo)
{
	for (uint8_t i = 0; i < ZDP_CB_MAX; i++) {
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
	uint8_t apsCnt = 0;

	memset(&dstEpInfo, 0, sizeof(dstEpInfo));
	dstEpInfo.txOptions = APS_TX_OPT_ACK_TX;
	dstEpInfo.dstEp = ZDO_EP;
	dstEpInfo.profileId = ZDO_PROFILE_ID;

	/* The vendor library tests the two modes separately and leaves
	 * dstAddrMode at 0 with no address copied for anything else. */
	if (req->dst_addr_mode == SHORT_ADDR_MODE) {
		dstEpInfo.dstAddrMode = APS_SHORT_DSTADDR_WITHEP;
		memcpy(&dstEpInfo.dstAddr, &req->dst_nwk_addr, sizeof(req->dst_nwk_addr));
	} else if (req->dst_addr_mode == EXT_ADDR_MODE) {
		dstEpInfo.dstAddrMode = APS_LONG_DSTADDR_WITHEP;
		ZB_IEEE_ADDR_COPY(&dstEpInfo.dstAddr, req->st_ext_addr);
	}

	if (req->cluster_id == DEVICE_ANNCE_CLID) {
		uint16_t announceAddr = (uint16_t)req->zdu[1] | ((uint16_t)req->zdu[2] << 8);

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
								uint8_t seqNum, uint16_t dstNwkAddr)
{
	zdo_zdp_req_t zzr;

	memset(&zzr, 0, sizeof(zzr));
	ZB_BUF_INITIAL_ALLOC((zb_buf_t *)arg, 2, zzr.zdu, uint8_t *);

	zzr.zdu[0] = seqNum;
	zzr.zdu[1] = status;
	zzr.cluster_id = END_DEVICE_BIND_RSP_CLID;
	zzr.zduLen = 2;
	zzr.buff_addr = (uint8_t *)arg;
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
	const uint8_t *payload = ad->asdu;
	const zdo_ieee_addr_req_t *req = (const zdo_ieee_addr_req_t *)(payload + 1);
	addrExt_t ieeeAddr;
	zb_normal_neighbor_entry_t *entry;
	uint16_t nwkAddrInterest;
	uint8_t reqType = req->req_type;
	uint8_t startIndex = req->start_index;
	uint8_t assocCount = 0;
	uint8_t allocSize = ZDO_ADDR_RSP_ALLOC_BASE;
	uint8_t *ptr;
	zdo_status_t status = ZDO_DEVICE_NOT_FOUND;
	bool includeAssocList;
	bool targetIsLocal;
	bool localLike;
	zdo_zdp_req_t zzr;
	uint16_t addrMapIdx;

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
		(void)zb_ExtAddrByShortAddr(nwkAddrInterest, ieeeAddr, &addrMapIdx);
	} else if (localLike) {
		status = ZDO_SUCCESS;
		if (reqType == ZDO_ADDR_REQ_SINGLE_REQ) {
			(void)zb_ExtAddrByShortAddr(nwkAddrInterest, ieeeAddr, &addrMapIdx);
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
	ZB_BUF_INITIAL_ALLOC((zb_buf_t *)arg, allocSize, zzr.zdu, uint8_t *);

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
	zzr.zduLen = (uint8_t)(ptr - zzr.zdu);
	zzr.buff_addr = (uint8_t *)arg;
	zzr.dst_addr_mode = SHORT_ADDR_MODE;
	zzr.dst_nwk_addr = ad->src_short_addr;
	zdo_send_req(&zzr);
	zb_buf_free((zb_buf_t *)arg);

	return -1;
}

_attribute_no_inline_ static int zdo_nwkAddrReqDelayCb(void *arg)
{
	aps_data_ind_t *ad = (aps_data_ind_t *)arg;
	const uint8_t *payload = ad->asdu;
	const zdo_nwk_addr_req_t *req = (const zdo_nwk_addr_req_t *)(payload + 1);
	addrExt_t ieeeAddr;
	zb_normal_neighbor_entry_t *entry;
	uint16_t nwkAddrInterest = ZB_UNKNOWN_SHORT_ADDR;
	uint16_t addrMapIdx;
	uint8_t reqType = req->req_type;
	uint8_t startIndex = req->start_index;
	uint8_t assocCount = 0;
	uint8_t allocSize = ZDO_ADDR_RSP_ALLOC_BASE;
	uint8_t *ptr;
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
		   (zb_ShortAddrByExtAddr(&nwkAddrInterest, ieeeAddr, &addrMapIdx) == RET_OK)) {
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
		    zb_NeighborTableChildEDNumGet() != 0U &&
		    ZB_IEEE_ADDR_CMP(ieeeAddr, ZB_PIB_EXTENDED_ADDRESS())) {
			allocSize = zdo_assoc_child_rsp_alloc_size(startIndex, &assocCount);
		}
	}

	memset(&zzr, 0, sizeof(zzr));
	ZB_BUF_INITIAL_ALLOC((zb_buf_t *)arg, allocSize, zzr.zdu, uint8_t *);

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
	zzr.zduLen = (uint8_t)(ptr - zzr.zdu);
	zzr.buff_addr = (uint8_t *)arg;
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
	uint8_t *payload = ad->asdu;
	int8_t childNum = (int8_t)payload[1];
	uint8_t copiedNum = 0;
	uint8_t matchedNum = 0;
	uint8_t *childList;
	zb_buf_t *buf;
	zdo_zdp_req_t zzr;
	uint8_t *ptr;

	if (childNum > 10) {
		zb_buf_free((zb_buf_t *)arg);
		return -1;
	}

	childList = ev_buf_allocate((uint16_t)childNum * EXT_ADDR_LEN);
	if (childList == NULL) {
		zb_buf_free((zb_buf_t *)arg);
		return -1;
	}

	memcpy(childList, payload + 2, (uint16_t)childNum * EXT_ADDR_LEN);

	buf = zb_buf_allocate();
	if (buf == NULL) {
		ev_buf_free(childList);
		zb_buf_free((zb_buf_t *)arg);
		return -1;
	}

	memset(&zzr, 0, sizeof(zzr));
	ZB_BUF_INITIAL_ALLOC(buf, 75, zzr.zdu, uint8_t *);

	zzr.zdu[0] = payload[0];
	zzr.zdu[1] = 0;
	ptr = zzr.zdu + 3;

	for (int8_t i = 0; i < childNum; i++) {
		zb_normal_neighbor_entry_t *entry = NULL;

		while ((entry = zb_NeighborTabSearchForChildEndDev(entry)) != NULL) {
			addrExt_t extAddr;

			zb_ExtAddrByIdx(entry->addrmapIdx, extAddr);
			if (!ZB_IEEE_ADDR_CMP(extAddr, childList + (uint16_t)i * EXT_ADDR_LEN)) {
				continue;
			}

			matchedNum++;
			if (copiedNum < 9U) {
				ZB_IEEE_ADDR_COPY(ptr, childList + (uint16_t)i * EXT_ADDR_LEN);
				ptr += EXT_ADDR_LEN;
				copiedNum++;
			} else {
				payload[1] = (uint8_t)(matchedNum - copiedNum);
				ZB_IEEE_ADDR_COPY(
					payload + 2U + (uint16_t)(matchedNum - copiedNum) * EXT_ADDR_LEN,
					childList + (uint16_t)i * EXT_ADDR_LEN);
			}
			break;
		}
	}

	zzr.zdu[1] = copiedNum;
	ev_buf_free(childList);

	if (copiedNum != 0U) {
		zzr.cluster_id = (uint16_t)(PARENT_ANNCE_CLID | 0x8000U);
		zzr.zduLen = (uint8_t)(3U + copiedNum * EXT_ADDR_LEN);
		zzr.buff_addr = (uint8_t *)buf;
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

uint8_t zdp_data_send(uint8_t *payload, uint8_t payloadLen, zdo_zdp_req_t *req)
{
	uint8_t *buf = ev_buf_allocate(payloadLen);

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
			req->zdu[6] = (uint8_t)(g_zbInfo.nwkNib.updateId + 1U);
			ev_timer_taskPost(
				zdo_change_channel_cb,
				(void *)(uintptr_t)zdo_channel_page2num(updateReq->scan_ch), 460);
		} else if (updateReq->scan_duration == ZDO_NWK_MANAGER_ATTRIBUTES_CHANGE) {
			req->zdu[6] = (uint8_t)(g_zbInfo.nwkNib.updateId + 1U);
			COPY_U16TOBUFFER(req->zdu + 7, updateReq->nwk_manager_addr);
		}
	} else if (req->cluster_id == MATCH_DESC_REQ_CLID) {
		const zdo_match_descriptor_req_t *matchReq =
			(const zdo_match_descriptor_req_t *)payload;
		const uint8_t *clusterList = (const uint8_t *)matchReq->cluster_list;

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

void zdo_devAnnce(uint16_t nwkAddr, const addrExt_t ieeeAddr, uint8_t capability)
{
	zb_buf_t *buf = zb_buf_allocate();
	zdo_zdp_req_t zzr;

	if (buf == NULL) {
		return;
	}

	memset(&zzr, 0, sizeof(zzr));
	ZB_BUF_INITIAL_ALLOC(buf, sizeof(zdo_device_annce_req_t) + 1U, zzr.zdu, uint8_t *);

	zzr.zdu[0] = zdp_txSeqNo++;
	zzr.zdu[1] = LO_UINT16(nwkAddr);
	zzr.zdu[2] = HI_UINT16(nwkAddr);
	ZB_IEEE_ADDR_COPY(&zzr.zdu[3], ieeeAddr);
	zzr.zdu[11] = capability;

	zzr.cluster_id = DEVICE_ANNCE_CLID;
	zzr.zduLen = sizeof(zdo_device_annce_req_t) + 1U;
	zzr.buff_addr = (uint8_t *)buf;
	zzr.dst_addr_mode = SHORT_ADDR_MODE;
	zzr.dst_nwk_addr = NWK_BROADCAST_RX_ON_WHEN_IDLE;
	zdo_send_req(&zzr);
	zb_buf_free(buf);
}

void zdo_device_announce_send(void)
{
	zdo_devAnnce(ZB_PIB_SHORT_ADDRESS(), (const uint8_t *)ZB_PIB_EXTENDED_ADDRESS(),
		     af_nodeMacCapabilityGet());
}

void zdo_apsParentAnnceTimerStart(void)
{
	aps_ib.aps_parent_announce_timer = (uint8_t)(APS_PARENT_ANNOUNCE_BASE_TIMER +
						(drv_u32Rand() % APS_PARENT_ANNOUNCE_JITTER_MAX));
}

void zdo_parent_announce_send(void)
{
	uint8_t childNum = remainChildListNum_8733;

	if (childNum == 0) {
		childNum = zb_NeighborTableChildEDNumGet();
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
	ZB_BUF_INITIAL_ALLOC(buf, (uint8_t)(2 + childNum * EXT_ADDR_LEN), zzr.zdu, uint8_t *);

	zzr.zdu[0] = zdp_txSeqNo++;
	zzr.zdu[1] = childNum;

	uint8_t actualChildNum = 0;
	uint8_t *ptr = &zzr.zdu[2];
	zb_normal_neighbor_entry_t *state = (zb_normal_neighbor_entry_t *)ed_binding_state;

	while (actualChildNum < childNum) {
		state = zb_NeighborTabSearchForChildEndDev(state);
		ed_binding_state = state;
		if (state == NULL) {
			remainChildListNum_8733 = 0;
			break;
		}

		zb_ExtAddrByIdx(state->addrmapIdx, ptr);
		ptr += EXT_ADDR_LEN;
		actualChildNum++;
	}

	zzr.zdu[1] = actualChildNum;
	zzr.cluster_id = PARENT_ANNCE_CLID;
	zzr.zduLen = (uint8_t)(2 + actualChildNum * EXT_ADDR_LEN);
	zzr.buff_addr = (uint8_t *)buf;
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

	if (zb_NwkAddrConflictDetect(arg, req.nwk_addr_local, req.ieee_addr_local) == 1U) {
		return;
	}

	if ((g_gpDeviceAnnounceCheckCb != NULL) &&
	    g_gpDeviceAnnounceCheckCb(req.nwk_addr_local, req.ieee_addr_local)) {
		zb_buf_free((zb_buf_t *)arg);
		return;
	}

	{
		zb_normal_neighbor_entry_t *entry = nwk_neTblGetByExtAddr(req.ieee_addr_local);

		if ((entry != NULL) && (entry->deviceType == NWK_DEVICE_TYPE_ED) &&
		    (ad->srcMacAddr != req.nwk_addr_local)) {
			zb_addrForNeighbor_t addrInfo;

			memset(&addrInfo, 0, sizeof(addrInfo));
			addrInfo.relationship = NEIGHBOR_IS_CHILD;
			addrInfo.shortAddr = zb_shortAddrByIdx(entry->addrmapIdx);
			zb_ExtAddrByIdx(entry->addrmapIdx, addrInfo.extAddr);
			(void)nwk_nodeAddrInfoDelete(&addrInfo);
			zb_NeighborTableDelete(entry);
		}
	}

	{
		uint16_t addrRef = 0;

		(void)zb_NwkAddrMapAdd(req.nwk_addr_local, req.ieee_addr_local, &addrRef);
	}

	nwkRoutingTabEntryDstDel(req.nwk_addr_local);
#if defined(ZB_COORDINATOR_ROLE)
	nwkRouteRecTabEntryDstDel(req.nwk_addr_local);
	if (g_zbNIB.isConcentrator) {
		zb_TaskPost(zdo_manyToOneRouteDisc, arg);
	} else {
		zb_buf_free((zb_buf_t *)arg);
	}
#else
	zb_buf_free((zb_buf_t *)arg);
#endif

	if ((zdoAppIndCbLst != NULL) && (zdoAppIndCbLst->zdpDevAnnounceIndCb != NULL)) {
		zdoAppIndCbLst->zdpDevAnnounceIndCb(&req);
	}
}

void zdo_parentAnnounceIndicate(void *arg)
{
	ev_timer_taskPost(zdo_parentAnnounceIndicateDelay, arg,
			  1000U + (uint16_t)((drv_u32Rand() & 0x1fU) * 50U));
}

void zdo_remoteAddrNotify(void *arg)
{
	aps_data_ind_t *ad = (aps_data_ind_t *)arg;
	const uint8_t *payload = ad->asdu;
	const zdo_nwk_addr_resp_t *resp = (const zdo_nwk_addr_resp_t *)payload;
	addrExt_t extAddr;
	uint16_t addrMapIdx;

	if (resp->status != ZDO_SUCCESS) {
		return;
	}

	ZB_IEEE_ADDR_COPY(extAddr, resp->ieee_addr_remote);
	(void)zb_NwkAddrMapAdd(resp->nwk_addr_remote, extAddr, &addrMapIdx);
}

void zdo_parentAnnounceNotify(void *arg)
{
	aps_data_ind_t *ad = (aps_data_ind_t *)arg;
	const uint8_t *payload = ad->asdu;
	uint8_t childNum = payload[2];
	const uint8_t *ptr = payload + 3;

	if ((payload[1] != ZDO_SUCCESS) || (childNum == 0U)) {
		return;
	}

	for (uint8_t i = 0; i < childNum; i++) {
		zb_normal_neighbor_entry_t *entry = nwk_neTblGetByExtAddr((uint8_t *)ptr);

		if (entry != NULL) {
			zb_addrForNeighbor_t addrInfo;

			memset(&addrInfo, 0, sizeof(addrInfo));
			addrInfo.shortAddr = zb_shortAddrByIdx(entry->addrmapIdx);
			zb_ExtAddrByIdx(entry->addrmapIdx, addrInfo.extAddr);
			addrInfo.relationship = NEIGHBOR_IS_CHILD;

			(void)nwk_nodeAddrInfoDelete(&addrInfo);
			zb_NeighborTableDelete(entry);
		}

		ptr += EXT_ADDR_LEN;
	}
}

void zdo_mgmtPermitJoinIndicate(void *arg)
{
	aps_data_ind_t *ad = (aps_data_ind_t *)arg;
	const uint8_t *payload = ad->asdu;
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
		ZB_BUF_INITIAL_ALLOC(buf, 2, zzr.zdu, uint8_t *);

		zzr.zdu[0] = payload[0];
#if defined(ZB_COORDINATOR_ROLE)
		zzr.zdu[1] = SS_ALLOW_REMOTE_TC_POLICY_CHANGE ? ZDO_SUCCESS : ZDO_INVALID_REQUEST;
#else
		zzr.zdu[1] = ZDO_SUCCESS;
#endif
		zzr.cluster_id = MGMT_PERMIT_JOINING_RSP_CLID;
		zzr.zduLen = 2;
		zzr.buff_addr = (uint8_t *)buf;
		zzr.dst_addr_mode = SHORT_ADDR_MODE;
		zzr.dst_nwk_addr = ad->src_short_addr;
		zdo_send_req(&zzr);
		zb_buf_free(buf);
	}

#if defined(ZB_COORDINATOR_ROLE)
	if (!SS_ALLOW_REMOTE_TC_POLICY_CHANGE) {
		zb_buf_free((zb_buf_t *)arg);
		goto notify_permit_join;
	}
#endif

	((nlme_permitJoining_req_t *)arg)->permitDuration = req.permitDuration;
	zb_PrimitivePost(ZB_Q_HIGH2NWK, NWK_NLME_PERMIT_JOINING_REQ, arg);

#if defined(ZB_COORDINATOR_ROLE)
notify_permit_join:
#endif
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
	const uint8_t *payload = ad->asdu;
	const zdo_simple_descriptor_req_t *descriptorReq =
		(const zdo_simple_descriptor_req_t *)(payload + 1);
	uint8_t seqNum = payload[0];
	uint16_t nwkAddrReq = descriptorReq->nwk_addr_interest;
	zdo_status_t status;
	zdo_zdp_req_t zzr;
	af_simple_descriptor_t *simpleDesc;

	if (ZB_NWK_IS_ADDRESS_BROADCAST(ad->dst_addr)) {
		zb_buf_free((zb_buf_t *)arg);
		return;
	}

	memset(&zzr, 0, sizeof(zzr));
	ZB_BUF_INITIAL_ALLOC((zb_buf_t *)arg, ZDO_CMD_PAYLOAD_MAX, zzr.zdu, uint8_t *);

	uint8_t *ptr = zzr.zdu;
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
			/*
			 * An NCP host registers the Green Power endpoint (242) like any
			 * other and queries its descriptor.
			 */
			if ((descriptorReq->endpoint == 0) ||
			    ((descriptorReq->endpoint > 240) &&
			     !(IS_ENABLED(CONFIG_ZIGBEE_NCP) && descriptorReq->endpoint == 242))) {
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
				uint8_t lenPtrOffset = (uint8_t)(ptr - zzr.zdu);
				ptr++;
				uint8_t simpleLen = af_simpleDescriptorCopy(ptr, simpleDesc);
				zzr.zdu[lenPtrOffset] = simpleLen;
				ptr += simpleLen;
			}
			break;
		default:
			break;
		}
	}

	zzr.cluster_id = ad->cluster_id | 0x8000U;
	zzr.zduLen = (uint8_t)(ptr - zzr.zdu);
	zzr.buff_addr = (uint8_t *)arg;
	zzr.dst_addr_mode = SHORT_ADDR_MODE;
	zzr.dst_nwk_addr = ad->src_short_addr;
	zdo_send_req(&zzr);
	zb_buf_free((zb_buf_t *)arg);
}

void zdo_activeEpIndicate(void *arg)
{
	aps_data_ind_t *ad = (aps_data_ind_t *)arg;
	const uint8_t *payload = ad->asdu;
	uint8_t seqNum = payload[0];
	uint16_t nwkAddrReq;
	zdo_status_t status;
	uint8_t epNum;
	zdo_zdp_req_t zzr;

	if (ZB_NWK_IS_ADDRESS_BROADCAST(ad->dst_addr)) {
		zb_buf_free((zb_buf_t *)arg);
		return;
	}

	COPY_BUFFERTOU16(nwkAddrReq, payload + 1);
	memset(&zzr, 0, sizeof(zzr));
	epNum = af_availableEpNumGet();
	ZB_BUF_INITIAL_ALLOC((zb_buf_t *)arg, (uint8_t)(5 + epNum), zzr.zdu, uint8_t *);

	uint8_t *ptr = zzr.zdu;
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

		for (uint8_t i = 0; i < epNum; i++) {
			*ptr++ = epDesc[i].ep;
		}
	}

	zzr.cluster_id = ACTIVE_EP_RSP_CLID;
	zzr.zduLen = (uint8_t)(ptr - zzr.zdu);
	zzr.buff_addr = (uint8_t *)arg;
	zzr.dst_addr_mode = SHORT_ADDR_MODE;
	zzr.dst_nwk_addr = ad->src_short_addr;
	zdo_send_req(&zzr);
	zb_buf_free((zb_buf_t *)arg);
}

void zdo_matchDescriptorIndicate(void *arg)
{
	aps_data_ind_t *ad = (aps_data_ind_t *)arg;
	const uint8_t *payload = ad->asdu;
	const zdo_match_descriptor_req_t *req = (const zdo_match_descriptor_req_t *)(payload + 1);
	uint8_t seqNum = payload[0];
	uint16_t nwkAddrReq = req->nwk_addr_interest;
	uint16_t profileId = req->profile_id;
	uint8_t inClusterNum = req->num_in_clusters;
	bool emptyRspSuppressed = ad->dst_addr_mode == APS_SHORT_DSTADDR_WITHEP &&
				  ZB_NWK_IS_ADDRESS_BROADCAST(ad->dst_addr);
	uint16_t inClusters[16];
	uint16_t outClusters[16];
	uint8_t outClusterNum;
	uint8_t *respPayload;
	uint8_t matchList[MAX_REQUESTED_CLUSTER_NUMBER];
	uint8_t matchCount = 0;
	zdo_status_t status = ZDO_SUCCESS;
	zdo_zdp_req_t zzr;

	if (inClusterNum > 16) {
		zb_buf_free((zb_buf_t *)arg);
		return;
	}

	const uint8_t *clusterPtr = (const uint8_t *)req->cluster_list;
	for (uint8_t i = 0; i < inClusterNum; i++) {
		COPY_BUFFERTOU16(inClusters[i], clusterPtr);
		clusterPtr += 2;
	}

	outClusterNum = *clusterPtr++;
	if ((uint8_t)(inClusterNum + outClusterNum) > 16) {
		zb_buf_free((zb_buf_t *)arg);
		return;
	}

	for (uint8_t i = 0; i < outClusterNum; i++) {
		COPY_BUFFERTOU16(outClusters[i], clusterPtr);
		clusterPtr += 2;
	}

	if ((nwkAddrReq != ZB_PIB_SHORT_ADDRESS()) && !ZB_NWK_IS_ADDRESS_BROADCAST(nwkAddrReq)) {
		status = (af_nodeDevTypeGet() == DEVICE_TYPE_COORDINATOR) ? ZDO_INVALID_REQUEST
									  : ZDO_DEVICE_NOT_FOUND;
	} else {
		af_endpoint_descriptor_t *epDesc = af_epDescriptorGet();
		uint8_t epNum = af_availableEpNumGet();

		for (uint8_t i = 0; i < epNum; i++) {
			af_simple_descriptor_t *sd = epDesc[i].correspond_simple_desc;
			bool matched = TRUE;

			if (sd == NULL) {
				continue;
			}

			if ((profileId != WILDCARD_PROFILE_ID) &&
			    (sd->app_profile_id != profileId)) {
				continue;
			}

			for (uint8_t j = 0; j < inClusterNum; j++) {
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

			for (uint8_t j = 0; j < outClusterNum; j++) {
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

				for (uint8_t j = 0; j < matchCount; j++) {
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
	ZB_BUF_INITIAL_ALLOC((zb_buf_t *)arg, (uint8_t)(5 + matchCount), zzr.zdu, uint8_t *);

	respPayload = zzr.zdu;
	respPayload[0] = seqNum;
	respPayload[1] = status;
	respPayload[2] = LO_UINT16(nwkAddrReq);
	respPayload[3] = HI_UINT16(nwkAddrReq);
	respPayload[4] = matchCount;
	memcpy(&respPayload[5], matchList, matchCount);

	zzr.cluster_id = MATCH_DESC_RSP_CLID;
	zzr.zduLen = (uint8_t)(5 + matchCount);
	zzr.buff_addr = (uint8_t *)arg;
	zzr.dst_addr_mode = SHORT_ADDR_MODE;
	zzr.dst_nwk_addr = ad->src_short_addr;
	zdo_send_req(&zzr);
	zb_buf_free((zb_buf_t *)arg);
}

void zdo_SysServerDiscoveryIndicate(void *arg)
{
	aps_data_ind_t *ad = (aps_data_ind_t *)arg;
	const uint8_t *payload = ad->asdu;
	const zdo_system_server_discovery_req_t *req =
		(const zdo_system_server_discovery_req_t *)(payload + 1);
	uint8_t seqNum = payload[0];
	uint16_t reqMask = req->server_mask;
	node_descriptor_t nodeDesc;
	uint16_t rspMask;
	zdo_zdp_req_t zzr;

	af_nodeDescriptorCopy(&nodeDesc);
	rspMask = nodeDesc.server_mask & reqMask;
	if (rspMask == 0) {
		zb_buf_free((zb_buf_t *)arg);
		return;
	}

	memset(&zzr, 0, sizeof(zzr));
	ZB_BUF_INITIAL_ALLOC((zb_buf_t *)arg, sizeof(zdo_system_server_discovery_resp_t), zzr.zdu,
			     uint8_t *);

	zzr.zdu[0] = seqNum;
	zzr.zdu[1] = ZDO_SUCCESS;
	zzr.zdu[2] = LO_UINT16(rspMask);
	zzr.zdu[3] = HI_UINT16(rspMask);

	zzr.cluster_id = SYSTEM_SERVER_DISCOVERY_RSP_CLID;
	zzr.zduLen = sizeof(zdo_system_server_discovery_resp_t);
	zzr.buff_addr = (uint8_t *)arg;
	zzr.dst_addr_mode = SHORT_ADDR_MODE;
	zzr.dst_nwk_addr = ad->src_short_addr;
	zdo_send_req(&zzr);
	zb_buf_free((zb_buf_t *)arg);
}
uint8_t zdo_bind_unbind_req(const zdo_bind_req_t *req, zdo_zdp_req_t *zzr, bool bind)
{
	zb_buf_t *buf;
	uint8_t payloadLen;

	buf = zb_buf_allocate();
	if (buf == NULL) {
		return ZDO_INSUFFICIENT_SPACE;
	}

	zzr->buff_addr = (uint8_t *)buf;
	payloadLen = (req->dst_addr_mode == LONG_EXADDR_DSTENDPOINT) ? 21U : 14U;
	ZB_BUF_INITIAL_ALLOC(buf, (uint8_t)(payloadLen + 1U), zzr->zdu, uint8_t *);

	zzr->zdpSeqNum = zdp_txSeqNo++;
	zzr->zdu[0] = zzr->zdpSeqNum;
	memcpy(zzr->zdu + 1, req, payloadLen);
	zzr->zduLen = (uint8_t)(payloadLen + 1U);
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
	uint16_t clusterId = ed_bind_ctx()->clusterList[0];
	uint16_t srcShortAddr;
	uint16_t dstShortAddr;
	uint8_t srcEp;
	uint8_t dstEp;
	uint16_t addrMapIdx;

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

	zb_ExtAddrByShortAddr(srcShortAddr, bindReq.src_addr, &addrMapIdx);
	zb_ExtAddrByShortAddr(dstShortAddr, bindReq.dst_ext_addr, &addrMapIdx);
	bindReq.src_endpoint = srcEp;
	bindReq.cid16_l = LO_UINT16(clusterId);
	bindReq.cid16_h = HI_UINT16(clusterId);
	bindReq.dst_addr_mode = LONG_EXADDR_DSTENDPOINT;
	bindReq.dst_endpoint = dstEp;
	zzr.zdoRspReceivedIndCb = zdo_bind_toggle_cb;

	if (zdo_bind_unbind_req(&bindReq, &zzr, FALSE) == ZDO_INSUFFICIENT_SPACE) {
		zb_TaskPost(zdo_bind_toggle_action, NULL);
	}
}

_attribute_no_inline_ static void zdo_bind_unbind_after_toggle_clone_1(
	bool bind, zdo_callback cb) __asm__("zdo_bind_unbind_after_toggle.clone.1");
_attribute_no_inline_ static void zdo_bind_unbind_after_toggle_clone_1(bool bind, zdo_callback cb)
{
	zdo_bind_req_t bindReq;
	zdo_zdp_req_t zzr;
	bool useFirstDirection;
	uint8_t totalCount = ed_bind_ctx()->clusterTotalCount;
	uint16_t clusterId;
	uint16_t srcShortAddr;
	uint16_t dstShortAddr;
	uint8_t srcEp;
	uint8_t dstEp;
	uint16_t addrMapIdx;

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

	zb_ExtAddrByShortAddr(srcShortAddr, bindReq.src_addr, &addrMapIdx);
	zb_ExtAddrByShortAddr(dstShortAddr, bindReq.dst_ext_addr, &addrMapIdx);
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

uint8_t zdo_end_device_bind_req(const zdo_edBindReq_t *req, zdo_zdp_req_t *zzr)
{
	zb_buf_t *buf;
	uint8_t payloadLen = (uint8_t)(16U + req->num_in_clusters * 2U + req->num_out_clusters * 2U);
	uint8_t *ptr;

	buf = zb_buf_allocate();
	if (buf == NULL) {
		return ZDO_INSUFFICIENT_SPACE;
	}

	zzr->buff_addr = (uint8_t *)buf;
	ZB_BUF_INITIAL_ALLOC(buf, payloadLen, zzr->zdu, uint8_t *);

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
	ZB_BUF_INITIAL_ALLOC((zb_buf_t *)arg, 2, zzr.zdu, uint8_t *);
	zzr.zdu[0] = ad->asdu[0];
	zzr.zdu[1] = status;
	zzr.cluster_id = bind ? BIND_RSP_CLID : UNBIND_RSP_CLID;
	zzr.zduLen = 2;
	zzr.buff_addr = (uint8_t *)arg;
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
		(uint8_t)(ed_bind_ctx()->clusterTotalCount - ed_bind_ctx()->src1MatchCount);

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
	zb_TaskPost(zdo_bind_toggle_action, NULL);

	zdo_end_device_bind_resp_send(arg, ZDO_SUCCESS, currentReq.seqNum, ad->src_short_addr);
	zdo_end_device_bind_resp_send(pendingAd, ZDO_SUCCESS, pendingReq.seqNum,
				      pendingAd->src_short_addr);
}

void zdo_nwkUpdateNotifyRespSend(void *arg)
{
	uint8_t scanResult[224];
	zdo_zdp_req_t zzr;
	uint8_t energyCnt;
	uint8_t *ptr;

	memcpy(scanResult, arg, sizeof(scanResult));
	energyCnt = scanResult[3];

	memset(&zzr, 0, sizeof(zzr));
	ZB_BUF_INITIAL_ALLOC((zb_buf_t *)arg, (uint8_t)(11 + energyCnt), zzr.zdu, uint8_t *);

	ptr = zzr.zdu;
	*ptr++ = zdo_nur.seqNum;
	*ptr++ = scanResult[0];
	*ptr++ = (uint8_t)(zdo_nur.scanChannels);
	*ptr++ = (uint8_t)(zdo_nur.scanChannels >> 8);
	*ptr++ = (uint8_t)(zdo_nur.scanChannels >> 16);
	*ptr++ = (uint8_t)(zdo_nur.scanChannels >> 24);
	*ptr++ = LO_UINT16(g_zbInfo.nwkNib.txTotal);
	*ptr++ = HI_UINT16(g_zbInfo.nwkNib.txTotal);
	*ptr++ = LO_UINT16(g_zbInfo.nwkNib.txFail);
	*ptr++ = HI_UINT16(g_zbInfo.nwkNib.txFail);
	*ptr++ = energyCnt;
	memcpy(ptr, &scanResult[8], energyCnt);
	ptr += energyCnt;

	zzr.cluster_id = MGMT_NWK_UPDATE_NOTIFY_CLID;
	zzr.zduLen = (uint8_t)(ptr - zzr.zdu);
	zzr.buff_addr = (uint8_t *)arg;
	zzr.dst_addr_mode = SHORT_ADDR_MODE;
	zzr.dst_nwk_addr = zdo_nur.srcAddr;
	zdo_send_req(&zzr);
	zb_buf_free((zb_buf_t *)arg);
}

void zdo_mgmtNwkUpdateIndicate(void *arg)
{
	aps_data_ind_t *ad = (aps_data_ind_t *)arg;
	const uint8_t *payload = ad->asdu;
	const zdo_mgmt_nwk_update_req_t *req = (const zdo_mgmt_nwk_update_req_t *)(payload + 1);
	uint8_t seqNum = payload[0];
	uint32_t scanChannels = req->scan_ch;
	uint8_t scanDuration = req->scan_duration;
	uint8_t scanCountOrUpdateId = req->scan_cnt;
	uint16_t nwkManagerAddr = req->nwk_manager_addr;
	zdo_status_t status = ZDO_INVALID_REQUEST;

	if (scanDuration == ZDO_NWK_MANAGER_CHANNEL_CHANGE) {
		uint8_t channel = zdo_channel_page2num(scanChannels);
		uint8_t acceptChannel = zdo_af_get_accept_nwk_update_channel();

		if ((channel >= 11) && (channel <= 26) &&
		    ((acceptChannel == 0) || (zdo_af_get_accept_nwk_update_channel() == 0xff) ||
		     (zdo_af_get_accept_nwk_update_channel() == channel))) {
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
		ZB_BUF_INITIAL_ALLOC((zb_buf_t *)arg, 2, zzr.zdu, uint8_t *);
		zzr.zdu[0] = seqNum;
		zzr.zdu[1] = status;
		zzr.cluster_id = MGMT_NWK_UPDATE_NOTIFY_CLID;
		zzr.zduLen = 2;
		zzr.buff_addr = (uint8_t *)arg;
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
	uint8_t totalEntries;
	uint8_t startIndex = ad->asdu[1];
	uint8_t listCount = 0;
	uint8_t currentValid = 0;
	zdo_zdp_req_t zzr;
	uint8_t *ptr;

	if (ZB_NWK_IS_ADDRESS_BROADCAST(ad->dst_addr)) {
		zb_buf_free((zb_buf_t *)arg);
		return;
	}

	totalEntries = aps_bindingTblEntryNum();
	table = aps_bindingTblEntryGet();
	if (startIndex < totalEntries) {
		listCount = (uint8_t)(totalEntries - startIndex);
		if (listCount > 2U) {
			listCount = 2U;
		}
	}

	memset(&zzr, 0, sizeof(zzr));
	ZB_BUF_INITIAL_ALLOC((zb_buf_t *)arg, 47, zzr.zdu, uint8_t *);

	ptr = zzr.zdu;
	*ptr++ = ad->asdu[0];
	*ptr++ = ZDO_SUCCESS;
	*ptr++ = totalEntries;
	*ptr++ = startIndex;
	*ptr++ = listCount;

	for (uint8_t i = 0; (i < APS_BINDING_TABLE_SIZE) && (listCount != 0U); i++) {
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
	zzr.zduLen = (uint8_t)(ptr - zzr.zdu);
	zzr.buff_addr = (uint8_t *)arg;
	zzr.dst_addr_mode = SHORT_ADDR_MODE;
	zzr.dst_nwk_addr = ad->src_short_addr;
	zdo_send_req(&zzr);
	zb_buf_free((zb_buf_t *)arg);
}

void zdo_mgmtLqiIndicate(void *arg)
{
	aps_data_ind_t *ad = (aps_data_ind_t *)arg;
	uint8_t startIndex = ad->asdu[1];
	uint8_t totalEntries;
	uint8_t listCount = 0;
	uint8_t validIndex = 0;
	zdo_zdp_req_t zzr;
	uint8_t *ptr;

	if (ZB_NWK_IS_ADDRESS_BROADCAST(ad->dst_addr)) {
		zb_buf_free((zb_buf_t *)arg);
		return;
	}

	totalEntries = zb_NeighborTableNumGet();

	memset(&zzr, 0, sizeof(zzr));
	ZB_BUF_INITIAL_ALLOC((zb_buf_t *)arg, 49, zzr.zdu, uint8_t *);

	ptr = zzr.zdu;
	*ptr++ = ad->asdu[0];
	*ptr++ = ZDO_SUCCESS;
	*ptr++ = totalEntries;
	*ptr++ = startIndex;

	if ((totalEntries > startIndex) && (totalEntries != 0U)) {
		listCount = (uint8_t)(totalEntries - startIndex);
		if (listCount > 2U) {
			listCount = 2U;
		}
	}

	*ptr++ = listCount;

	for (uint8_t i = 0, out = 0; (i < ZB_NEIGHBOR_TABLE_SIZE) && (out < listCount); i++) {
		zb_normal_neighbor_entry_t *entry = &g_zb_neighborTbl.neighborTbl[i];
		neighbor_tbl_lst_t n;

		if (!entry->used) {
			continue;
		}

		if (validIndex++ < startIndex) {
			continue;
		}

		memset(&n, 0, sizeof(n));
		ZB_EXTPANID_COPY(n.ext_pan_id, g_zbNIB.extPANId);
		zb_ExtAddrByIdx(entry->addrmapIdx, n.ext_addr);
		n.network_addr = zb_shortAddrByIdx(entry->addrmapIdx);
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
	zzr.zduLen = (uint8_t)(ptr - zzr.zdu);
	zzr.buff_addr = (uint8_t *)arg;
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
	if (ad->dst_addr != zb_NeighborParentShortAddrGet()) {
		zb_buf_free((zb_buf_t *)arg);
		return;
	}
#endif

	if (!ZB_IEEE_ADDR_IS_ZERO(req.device_addr) &&
	    !ZB_IEEE_ADDR_CMP(req.device_addr, ZB_PIB_EXTENDED_ADDRESS())) {
		zb_normal_neighbor_entry_t *entry = nwk_neTblGetByExtAddr(req.device_addr);

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
		ZB_BUF_INITIAL_ALLOC((zb_buf_t *)arg, 2, zzr.zdu, uint8_t *);
		zzr.zdu[0] = ad->asdu[0];
		zzr.zdu[1] = status;
		zzr.cluster_id = MGMT_LEAVE_RSP_CLID;
		zzr.zduLen = 2;
		zzr.buff_addr = (uint8_t *)arg;
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
