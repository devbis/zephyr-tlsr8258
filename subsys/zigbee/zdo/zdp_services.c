/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Adapted from libzigbee/src/zdp_services.c. Vendor file kept structurally
 * one-for-one; vendor zb_local.h / ev_timer.h are replaced by the
 * Zephyr include set.
 */
#include "zb_common_stub.h"
#include "common/static_assert.h"
#include "os/ev_timer.h"
#include "mac/includes/tl_zb_mac.h"
#include "mac/includes/tl_zb_mac_pib.h"
#include "nwk/includes/nwk.h"
#include "nwk/includes/nwk_internal.h"
#include "aps/aps_api.h"
#include "aps/aps_internal.h"
#include "zdo/zdo_api.h"
#include "zdo/zdp.h"
#include "zdo/zdo_internal.h"
#include <zephyr/zigbee/zb_bootstrap.h>


/* Decl mirrors gp/dGP_stub.h::gpDeviceAnnounceCheckCb_t without
 * pulling in the full GP stub header (which has a cyclic include
 * with the broader Zigbee header set).
 */
typedef bool (*gpDeviceAnnounceCheckCb_t)(u16 sinkNwkAddr, addrExt_t sinkIeeeAddr);

#ifndef _always_inline
#define _always_inline inline __attribute__((always_inline))
#endif

#ifndef TL_BUF_INITIAL_ALLOC
#define TL_BUF_INITIAL_ALLOC(buf, size, ptr, type) \
	do { (ptr) = (type)tl_bufInitalloc((buf), (size)); } while (0)
#endif

enum {
    ZBINFO_ACTIVE_CHANNEL_OFFSET = 70,
    ZDP_CB_MAX = 4,
    ZDP_ADDR_REQ_DELAY_MS = 200,
    ZDP_ASSOC_LIST_LIMIT = 15,
    ED_BIND_CLUSTER_LIST_MAX = 16,
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
    u16 bindingTargetAddr;
    addrExt_t srcExtAddr;
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
static zb_buf_t *zdo_mgmt_leave_pending;
ev_timer_event_t *zdo_bind_timer_event = NULL;
static void *ent_8732 __asm__("ent.8732") = NULL;
zdp_cb_info_t zdp_cbl[ZDP_CB_MAX];
ed_binding_ctx_t ed_binding_ctx;

extern gpDeviceAnnounceCheckCb_t g_gpDeviceAnnounceCheckCb;


static inline u16 rd_le16(const u8 *p)
{
    return (u16)p[0] | ((u16)p[1] << 8);
}

static inline u32 rd_le32(const u8 *p)
{
    return (u32)p[0] |
           ((u32)p[1] << 8) |
           ((u32)p[2] << 16) |
           ((u32)p[3] << 24);
}

static inline bool is_short_broadcast(u16 addr)
{
    return (addr & 0xfff8U) == 0xfff8U;
}

static inline u16 zb_info_short_addr(void)
{
    return g_zbInfo.macPib.shortAddress;
}

static inline const u8 *zb_info_ieee_addr(void)
{
    return g_zbInfo.macPib.extAddress;
}

static inline void *ed_binding_state_get(void)
{
    return ent_8732;
}

static inline void ed_binding_state_set(void *state)
{
    ent_8732 = state;
}

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

static inline void *ed_bind_pending_buf_get(void)
{
    return ed_bind_ctx()->pendingBuf;
}

static inline void ed_bind_pending_buf_set(void *buf)
{
    ed_bind_ctx()->pendingBuf = buf;
}

static inline u16 ed_bind_cluster_get(u8 idx)
{
    return ed_bind_ctx()->clusterList[idx];
}

static inline void ed_bind_cluster_set(u8 idx, u16 clusterId)
{
    ed_bind_ctx()->clusterList[idx] = clusterId;
}

static inline bool neighbor_is_previous_child(const tl_zb_normal_neighbor_entry_t *entry)
{
    return entry->relationship == NEIGHBOR_IS_PREVIOUS_CHILD;
}

static void ed_bind_ctx_reset(void)
{
    memset(&ed_binding_ctx, 0, sizeof(ed_binding_ctx));
    zdo_bind_timer_event = NULL;
}

static _always_inline bool zdo_ed_bind_req_parse(const u8 *payload, u16 payloadLen, zdo_ed_bind_req_view_t *req)
{
    u16 inBytes;
    u16 outPos;
    u16 outBytes;

    if ((payload == NULL) || (req == NULL) || (payloadLen < 16U)) {
        return FALSE;
    }

    req->seqNum = payload[0];
    req->bindingTargetAddr = rd_le16(payload + 1);
    memcpy(req->srcExtAddr, payload + 3, EXT_ADDR_LEN);
    req->srcEndpoint = payload[11];
    req->profileId = rd_le16(payload + 12);
    req->numInClusters = payload[14];
    inBytes = (u16)req->numInClusters * 2U;
    if ((u16)15U + inBytes >= payloadLen) {
        return FALSE;
    }

    req->inClusters = payload + 15;
    outPos = (u16)(15U + inBytes);
    req->numOutClusters = payload[outPos];
    outBytes = (u16)req->numOutClusters * 2U;
    if ((u16)(outPos + 1U + outBytes) > payloadLen) {
        return FALSE;
    }

    req->outClusters = payload + outPos + 1U;
    return TRUE;
}

static u8 zdo_assoc_child_list_fill(u8 startIndex, u8 maxCount, u8 *dst)
{
    tl_zb_normal_neighbor_entry_t *entry = NULL;
    u8 skipped = 0;
    u8 written = 0;

    while ((written < maxCount) && ((entry = tl_zbNeighborTabSearchForChildEndDev(entry)) != NULL)) {
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

static u8 zdo_assoc_child_rsp_count(u8 startIndex)
{
    u8 childNum = tl_zbNeighborTableChildEDNumGet();

    if ((childNum == 0U) || (startIndex >= childNum)) {
        return 0;
    }

    childNum = (u8)(childNum - startIndex);
    if (childNum > ZDP_ASSOC_LIST_LIMIT) {
        childNum = ZDP_ASSOC_LIST_LIMIT;
    }

    return childNum;
}

_attribute_no_inline_ static int zdoMgmtLeaveCmdProcessCb(void *arg)
{
	/* This is the local-router half of remove-without-force.  The vendor
	 * implementation invokes the NWK leave handler directly from this timer;
	 * queueing it once more loses the only completion path because a local
	 * broadcast leave has no NLME-LEAVE.confirm. */
	tl_zbNwkNlmeLeaveRequestHandler(arg);

	/* nwkLeaveReqStart() has already built and submitted the leave frame from
	 * the old joined state.  Now make the removal terminal locally: otherwise
	 * Z2M deletes its database entry while this router persists and restores
	 * the old PAN/short address. */
	(void)zb_platform_clear_persistent_state();
	zb_platform_app_network_left();
	return -1;
}

void zdo_mgmt_leave_response_confirm(void *arg, u8 status)
{
    zb_buf_t *leave_buf = zdo_mgmt_leave_pending;

    zdo_mgmt_leave_pending = NULL;
    zb_buf_free((zb_buf_t *)arg);

    if (leave_buf != NULL) {
        /* The response has completed its radio transaction. Match the
         * vendor's short post-confirm handoff without a pre-confirm race. */
        ev_timer_taskPost(zdoMgmtLeaveCmdProcessCb, leave_buf, 100);
    }
}

_attribute_no_inline_ static int zdo_change_channel_cb(void *arg)
{
    u8 channel = (u8)(u32)arg;

    tl_zbMacChannelSet(channel);
    g_zbInfo.macPib.phyChannelCur = channel;
    g_zbInfo.nwkNib.txTotal = 0;
    g_zbInfo.nwkNib.txFail = 0;
    tl_zbTaskPost(zb_info_save, NULL);

    return -1;
}
_attribute_no_inline_ static void zdo_cluster_list_match_process_clone_2(u8 clusterNum1, const u8 *clusterList1,
                                                                         u8 clusterNum2, const u8 *clusterList2)
    __asm__("zdo_cluster_list_match_process.clone.2");
_attribute_no_inline_ static void zdo_cluster_list_match_process_clone_2(u8 clusterNum1, const u8 *clusterList1,
                                                                         u8 clusterNum2, const u8 *clusterList2)
{
    if (clusterList1 == NULL) {
        return;
    }

    for (u8 i = 0; i < clusterNum1; i++) {
        u16 clusterId = rd_le16(clusterList1 + i * 2U);

        for (u8 j = 0; j < clusterNum2; j++) {
            if (clusterId != rd_le16(clusterList2 + j * 2U)) {
                continue;
            }

            ed_bind_cluster_set(ed_bind_ctx()->clusterTotalCount, clusterId);
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

u8 zdo_send_req(zdo_zdp_req_t *req)
{
    epInfo_t dstEpInfo;
    u8 apsCnt = 0;
	u8 payload[ZB_BUF_SIZE];
	zb_buf_t *owned_buf;
	u16 cluster_id;
	u8 zdu_len;
	u8 dst_addr_mode;
	u16 dst_nwk_addr;
	addrExt_t dst_ext_addr;
	zdo_callback rsp_cb;
	u8 rsp_seq;

	if (req == NULL || req->zdu == NULL || req->zduLen > sizeof(payload)) {
		return ZDO_INVALID_REQUEST;
	}
	/* Snapshot every request field before releasing the RX carrier.  In the
	 * normal indication path `req` itself lives in that carrier. */
	cluster_id = req->cluster_id;
	zdu_len = req->zduLen;
	dst_addr_mode = req->dst_addr_mode;
	dst_nwk_addr = req->dst_nwk_addr;
	memcpy(dst_ext_addr, req->st_ext_addr, EXT_ADDR_LEN);
	rsp_cb = req->zdoRspReceivedIndCb;
	rsp_seq = req->zdpSeqNum;
	memcpy(payload, req->zdu, req->zduLen);
	/* The response was built in the RX indication's slab block. Release that
	 * block before af_dataSend allocates the separate TX carrier. Callers must
	 * not free req->buff_addr again after this point. */
	owned_buf = (zb_buf_t *)req->buff_addr;
	if (owned_buf != NULL) {
		zb_buf_free(owned_buf);
	}


    memset(&dstEpInfo, 0, sizeof(dstEpInfo));
    dstEpInfo.txOptions = APS_TX_OPT_ACK_TX;
    dstEpInfo.dstEp = ZDO_EP;
    dstEpInfo.profileId = ZDO_PROFILE_ID;

	if (dst_addr_mode == SHORT_ADDR_MODE) {
		dstEpInfo.dstAddrMode = APS_SHORT_DSTADDR_WITHEP;
		memcpy(&dstEpInfo.dstAddr, &dst_nwk_addr, sizeof(dst_nwk_addr));
	} else {
		dstEpInfo.dstAddrMode = APS_LONG_DSTADDR_WITHEP;
		memcpy(&dstEpInfo.dstAddr, dst_ext_addr, EXT_ADDR_LEN);
	}

	if (cluster_id == DEVICE_ANNCE_CLID) {
        u16 announceAddr = (u16)payload[1] | ((u16)payload[2] << 8);

        if (announceAddr != g_zbNIB.nwkAddr) {
            dstEpInfo.useAlias = TRUE;
            dstEpInfo.aliasSrcAddr = announceAddr;
            dstEpInfo.aliasSeqNum = 0;
        }
    }

	    (void)af_dataSend(ZDO_EP, &dstEpInfo, cluster_id, zdu_len,
	                      payload, &apsCnt);
	    if (rsp_cb != NULL) {
	        zdp_cb_info_t *entry = &zdp_cbl[zdpCblWptr++ & (ZDP_CB_MAX - 1U)];

	        entry->cb = rsp_cb;
	        entry->seq = rsp_seq;
        entry->used = 1;
        entry->active = 1;
    }
    return ZDO_SUCCESS;
}
_attribute_no_inline_ static void zdo_end_device_bind_resp_send(void *arg, zdo_status_t status, u8 seqNum, u16 dstNwkAddr)
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
    addrExt_t ieeeAddr;
    tl_zb_normal_neighbor_entry_t *entry;
    u16 nwkAddrInterest = rd_le16(payload + 1);
    u8 reqType = payload[3];
    u8 startIndex = payload[4];
    u8 assocCount = 0;
    u8 *ptr;
    zdo_status_t status = ZDO_DEVICE_NOT_FOUND;
    bool localLike;
    zdo_zdp_req_t zzr;

    memcpy(ieeeAddr, g_invalid_addr, EXT_ADDR_LEN);
    entry = nwk_neTblGetByShortAddr(nwkAddrInterest);
    localLike = (nwkAddrInterest == g_zbInfo.nwkNib.nwkAddr) ||
                ((entry != NULL) && neighbor_is_previous_child(entry));

    if (!localLike && is_short_broadcast(ad->dst_addr)) {
        zb_buf_free((zb_buf_t *)arg);
        return -1;
    }

    if (reqType > ZDO_ADDR_REQ_EXTENDED_REQ) {
        if (localLike && (reqType <= ZDO_ADDR_REQ_EXTENDED_RESP)) {
            status = ZDO_SUCCESS;
        } else if (is_short_broadcast(ad->dst_addr)) {
            zb_buf_free((zb_buf_t *)arg);
            return -1;
        } else {
            status = ZDO_INVALID_REQUEST;
        }
    } else if (localLike) {
        status = ZDO_SUCCESS;
    } else if (tl_zbExtAddrByShortAddr(nwkAddrInterest, ieeeAddr, NULL) == RET_OK) {
        status = ZDO_SUCCESS;
    }

    if (status == ZDO_SUCCESS) {
        if (localLike) {
            memcpy(ieeeAddr, zb_info_ieee_addr(), EXT_ADDR_LEN);
            nwkAddrInterest = zb_info_short_addr();

            if ((reqType == ZDO_ADDR_REQ_EXTENDED_REQ) &&
                (af_nodeDevTypeGet() != DEVICE_TYPE_END_DEVICE)) {
                assocCount = zdo_assoc_child_rsp_count(startIndex);
            }
        } else if ((reqType == ZDO_ADDR_REQ_EXTENDED_REQ) &&
                   (af_nodeDevTypeGet() != DEVICE_TYPE_END_DEVICE) &&
                   (entry != NULL) && neighbor_is_previous_child(entry)) {
            assocCount = zdo_assoc_child_rsp_count(startIndex);
        }
    }

    memset(&zzr, 0, sizeof(zzr));
    TL_BUF_INITIAL_ALLOC((zb_buf_t *)arg,
                         (u8)(12U + ((reqType == ZDO_ADDR_REQ_EXTENDED_REQ) ? (2U + assocCount * 2U) : 0U)),
                         zzr.zdu, u8 *);

    ptr = zzr.zdu;
    *ptr++ = payload[0];
    *ptr++ = status;
    memcpy(ptr, ieeeAddr, EXT_ADDR_LEN);
    ptr += EXT_ADDR_LEN;
    *ptr++ = LO_UINT16(nwkAddrInterest);
    *ptr++ = HI_UINT16(nwkAddrInterest);

    if (reqType == ZDO_ADDR_REQ_EXTENDED_REQ) {
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

    return -1;
}

_attribute_no_inline_ static int zdo_nwkAddrReqDelayCb(void *arg)
{
    aps_data_ind_t *ad = (aps_data_ind_t *)arg;
    const u8 *payload = ad->asdu;
    addrExt_t ieeeAddr;
    tl_zb_normal_neighbor_entry_t *entry;
    u16 nwkAddrInterest = ZB_UNKNOWN_SHORT_ADDR;
    u8 reqType = payload[9];
    u8 startIndex = payload[10];
    u8 assocCount = 0;
    u8 *ptr;
    zdo_status_t status = ZDO_DEVICE_NOT_FOUND;
    bool localLike;
    zdo_zdp_req_t zzr;

    memcpy(ieeeAddr, payload + 1, EXT_ADDR_LEN);
    entry = nwk_neTblGetByExtAddr(ieeeAddr);
    localLike = (memcmp(ieeeAddr, zb_info_ieee_addr(), EXT_ADDR_LEN) == 0) ||
                ((entry != NULL) && neighbor_is_previous_child(entry));

    if (!localLike && is_short_broadcast(ad->dst_addr)) {
        zb_buf_free((zb_buf_t *)arg);
        return -1;
    }

    if (reqType > ZDO_ADDR_REQ_EXTENDED_REQ) {
        if (is_short_broadcast(ad->dst_addr)) {
            zb_buf_free((zb_buf_t *)arg);
            return -1;
        }
        status = ZDO_INVALID_REQUEST;
    } else if (localLike || (tl_zbShortAddrByExtAddr(&nwkAddrInterest, ieeeAddr, NULL) == RET_OK)) {
        status = ZDO_SUCCESS;
    }

    if (status == ZDO_SUCCESS) {
        if (localLike && (reqType == ZDO_ADDR_REQ_EXTENDED_REQ) &&
            (af_nodeDevTypeGet() != DEVICE_TYPE_END_DEVICE)) {
            memcpy(ieeeAddr, zb_info_ieee_addr(), EXT_ADDR_LEN);
            nwkAddrInterest = zb_info_short_addr();
            assocCount = zdo_assoc_child_rsp_count(startIndex);
        }

        if ((reqType == ZDO_ADDR_REQ_EXTENDED_REQ) && (assocCount == 0U) &&
            (af_nodeDevTypeGet() != DEVICE_TYPE_END_DEVICE) &&
            tl_zbNeighborTableChildEDNumGet() != 0U &&
            memcmp(ieeeAddr, zb_info_ieee_addr(), EXT_ADDR_LEN) == 0) {
            assocCount = zdo_assoc_child_rsp_count(startIndex);
        }
    }

    memset(&zzr, 0, sizeof(zzr));
    TL_BUF_INITIAL_ALLOC((zb_buf_t *)arg,
                         (u8)(12U + ((reqType == ZDO_ADDR_REQ_EXTENDED_REQ) ? (2U + assocCount * 2U) : 0U)),
                         zzr.zdu, u8 *);

    ptr = zzr.zdu;
    *ptr++ = payload[0];
    *ptr++ = status;
    memcpy(ptr, ieeeAddr, EXT_ADDR_LEN);
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

    return -1;
}

#if defined(ZB_ROUTER_ROLE)
_attribute_no_inline_ static int zdo_parentAnnounceIndicateDelay(void *arg)
{
    aps_data_ind_t *ad = (aps_data_ind_t *)arg;
    u8 *payload = ad->asdu;
    u8 childNum = payload[1];
    u8 copiedNum = 0;
    u8 overflowNum = 0;
    u8 *childList;
    zb_buf_t *buf;
    zdo_zdp_req_t zzr;
    u8 *ptr;

    if ((childNum == 0U) || (childNum > 10U)) {
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
    zzr.zdu[2] = 0;
    ptr = zzr.zdu + 3;

    for (u8 i = 0; i < childNum; i++) {
        tl_zb_normal_neighbor_entry_t *entry = NULL;

        while ((entry = tl_zbNeighborTabSearchForChildEndDev(entry)) != NULL) {
            addrExt_t extAddr;

            tl_zbExtAddrByIdx(entry->addrmapIdx, extAddr);
            if (memcmp(extAddr, childList + (u16)i * EXT_ADDR_LEN, EXT_ADDR_LEN) != 0) {
                continue;
            }

            if (copiedNum < 9U) {
                memcpy(ptr, childList + (u16)i * EXT_ADDR_LEN, EXT_ADDR_LEN);
                ptr += EXT_ADDR_LEN;
                copiedNum++;
            } else {
                memcpy(payload + 2U + (u16)overflowNum * EXT_ADDR_LEN,
                       childList + (u16)i * EXT_ADDR_LEN,
                       EXT_ADDR_LEN);
                overflowNum++;
            }
            break;
        }
    }

    payload[1] = overflowNum;
    zzr.zdu[2] = copiedNum;
    ev_buf_free(childList);

    if (copiedNum != 0U) {
        zzr.cluster_id = (u16)(PARENT_ANNCE_CLID | 0x8000U);
        zzr.zduLen = (u8)(3U + copiedNum * EXT_ADDR_LEN);
        zzr.buff_addr = (u8 *)buf;
        zzr.dst_addr_mode = SHORT_ADDR_MODE;
        zzr.dst_nwk_addr = ad->src_short_addr;
        zdo_send_req(&zzr);
    } else {
		zb_buf_free(buf);
    }

    /* zdo_send_req() owns and releases buf when a response is sent. */

    if (overflowNum != 0U) {
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
        return 0x8a;
    }

    req->zdpSeqNum = zdp_txSeqNo++;
    req->buff_addr = buf;
    req->zdu = buf;
    req->zdu[0] = req->zdpSeqNum;
    req->zduLen = payloadLen;

    if (req->cluster_id == MGMT_NWK_UPDATE_REQ_CLID) {
        memcpy(req->zdu + 1, payload, 5);
        if (payload[4] <= ZDO_NWK_MANAGER_MAX_SCAN_DURATION) {
            req->zdu[6] = payload[5];
        } else if (payload[4] == ZDO_NWK_MANAGER_CHANNEL_CHANGE) {
            req->zdu[6] = (u8)(g_zbInfo.nwkNib.updateId + 1U);
            ev_timer_taskPost(zdo_change_channel_cb, (void *)(u32)zdo_channel_page2num(rd_le32(payload)), 460);
        } else if (payload[4] == ZDO_NWK_MANAGER_ATTRIBUTES_CHANGE) {
            req->zdu[6] = (u8)(g_zbInfo.nwkNib.updateId + 1U);
            req->zdu[7] = payload[6];
            req->zdu[8] = payload[7];
        }
    } else if (req->cluster_id == MATCH_DESC_REQ_CLID) {
        memcpy(req->zdu + 1, payload, 4);
        req->zdu[5] = payload[4];
        memcpy(req->zdu + 6, payload + 6, payload[4] * 2U);
        req->zdu[6 + payload[4] * 2U] = payload[5];
        memcpy(req->zdu + 7 + payload[4] * 2U, payload + 6 + payload[4] * 2U, payload[5] * 2U);
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
    TL_BUF_INITIAL_ALLOC(buf, sizeof(zdo_device_annce_req_t), zzr.zdu, u8 *);

    zzr.zdu[0] = zdp_txSeqNo++;
    zzr.zdu[1] = LO_UINT16(nwkAddr);
    zzr.zdu[2] = HI_UINT16(nwkAddr);
    memcpy(&zzr.zdu[3], ieeeAddr, EXT_ADDR_LEN);
    zzr.zdu[11] = capability;

    zzr.cluster_id = DEVICE_ANNCE_CLID;
    zzr.zduLen = sizeof(zdo_device_annce_req_t);
    zzr.buff_addr = (u8 *)buf;
    zzr.dst_addr_mode = SHORT_ADDR_MODE;
    zzr.dst_nwk_addr = NWK_BROADCAST_RX_ON_WHEN_IDLE;
    zdo_send_req(&zzr);
}

void zdo_device_announce_send(void)
{
    zdo_devAnnce(zb_info_short_addr(), (const u8 *)zb_info_ieee_addr(), af_nodeMacCapabilityGet());
}

void zdo_apsParentAnnceTimerStart(void)
{
    aps_ib.aps_parent_announce_timer =
        (u8)(APS_PARENT_ANNOUNCE_BASE_TIMER + (drv_u32Rand() % APS_PARENT_ANNOUNCE_JITTER_MAX));
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
    void *state = ed_binding_state_get();

    while (actualChildNum < childNum) {
        state = tl_zbNeighborTabSearchForChildEndDev(state);
        ed_binding_state_set(state);
        if (state == NULL) {
            remainChildListNum_8733 = 0;
            break;
        }

        tl_zbExtAddrByIdx(rd_le16((const u8 *)state + 22), ptr);
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

#if defined(ZB_ROUTER_ROLE)
void zdo_deviceAnnounceIndicate(void *arg)
{
    aps_data_ind_t *ad = (aps_data_ind_t *)arg;
    zdo_device_annce_req_t req;

    memcpy(&req, ad->asdu + 1, sizeof(req));

    if (tl_zbNwkAddrConflictDetect(arg, req.nwk_addr_local, req.ieee_addr_local)) {
        return;
    }

    if ((g_gpDeviceAnnounceCheckCb != NULL) &&
        g_gpDeviceAnnounceCheckCb(req.nwk_addr_local, req.ieee_addr_local)) {
        zb_buf_free((zb_buf_t *)arg);
        return;
    }

    {
        tl_zb_normal_neighbor_entry_t *entry = nwk_neTblGetByExtAddr(req.ieee_addr_local);

        if ((entry != NULL) && neighbor_is_previous_child(entry) &&
            (ad->srcMacAddr != req.nwk_addr_local)) {
            zb_addrForNeighbor_t addrInfo;

            memset(&addrInfo, 0, sizeof(addrInfo));
            addrInfo.relationship = NEIGHBOR_IS_PREVIOUS_CHILD;
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
    zb_buf_free((zb_buf_t *)arg);

    if ((zdoAppIndCbLst != NULL) && (zdoAppIndCbLst->zdpDevAnnounceIndCb != NULL)) {
        zdoAppIndCbLst->zdpDevAnnounceIndCb(&req);
    }
}

void zdo_parentAnnounceIndicate(void *arg)
{
    ev_timer_taskPost(zdo_parentAnnounceIndicateDelay,
                      arg,
                      1000U + (u16)((drv_u32Rand() & 0x1fU) * 50U));
}

void zdo_remoteAddrNotify(void *arg)
{
    aps_data_ind_t *ad = (aps_data_ind_t *)arg;
    const u8 *payload = ad->asdu;
    addrExt_t extAddr;

    if (payload[1] != ZDO_SUCCESS) {
        return;
    }

    memcpy(extAddr, payload + 2, EXT_ADDR_LEN);
    (void)tl_zbNwkAddrMapAdd(rd_le16(payload + 10), extAddr, NULL);
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
            addrInfo.relationship = NEIGHBOR_IS_PREVIOUS_CHILD;

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

    if (!is_short_broadcast(ad->dst_addr)) {
        zb_buf_t *buf = zb_buf_allocate();
        zdo_zdp_req_t zzr;

        if (buf == NULL) {
            zb_buf_free((zb_buf_t *)arg);
            return;
        }

        memset(&zzr, 0, sizeof(zzr));
        TL_BUF_INITIAL_ALLOC(buf, 2, zzr.zdu, u8 *);

        zzr.zdu[0] = payload[0];
        zzr.zdu[1] = ZDO_SUCCESS;
        zzr.cluster_id = MGMT_PERMIT_JOINING_RSP_CLID;
        zzr.zduLen = 2;
        zzr.buff_addr = (u8 *)buf;
        zzr.dst_addr_mode = SHORT_ADDR_MODE;
        zzr.dst_nwk_addr = ad->src_short_addr;
        zdo_send_req(&zzr);
    }

    ((nlme_permitJoining_req_t *)arg)->permitDuration = req.permitDuration;
    tl_zbPrimitivePost(TL_Q_HIGH2NWK, NWK_NLME_PERMIT_JOINING_REQ, arg);

    if ((zdoAppIndCbLst != NULL) && (zdoAppIndCbLst->zdpPermitJoinIndCb != NULL)) {
        zdoAppIndCbLst->zdpPermitJoinIndCb(&req);
    }
}
#endif

void zdo_nwkAddrIndicate(void *arg) { ev_timer_taskPost(zdo_nwkAddrReqDelayCb, arg, ZDP_ADDR_REQ_DELAY_MS); }
void zdo_ieeeAddrIndicate(void *arg) { ev_timer_taskPost(zdo_ieeeAddrReqDelayCb, arg, ZDP_ADDR_REQ_DELAY_MS); }

void zdo_descriptorsIndicate(void *arg)
{
    aps_data_ind_t *ad = (aps_data_ind_t *)arg;
    const u8 *payload = ad->asdu;
    u8 seqNum = payload[0];
    u16 nwkAddrReq = rd_le16(payload + 1);
    zdo_status_t status;
    zdo_zdp_req_t zzr;
    af_simple_descriptor_t *simpleDesc;


    if (is_short_broadcast(ad->dst_addr)) {
        zb_buf_free((zb_buf_t *)arg);
        return;
    }

    memset(&zzr, 0, sizeof(zzr));
    TL_BUF_INITIAL_ALLOC((zb_buf_t *)arg, ZDO_CMD_PAYLOAD_MAX, zzr.zdu, u8 *);


    u8 *ptr = zzr.zdu;
    *ptr++ = seqNum;

    if (nwkAddrReq != zb_info_short_addr()) {
        status = (af_nodeDevTypeGet() == DEVICE_TYPE_COORDINATOR) ? ZDO_INVALID_REQUEST : ZDO_DEVICE_NOT_FOUND;
        *ptr++ = status;
        *ptr++ = LO_UINT16(nwkAddrReq);
        *ptr++ = HI_UINT16(nwkAddrReq);

        if (ad->cluster_id == SIMPLE_DESC_REQ_CLID) {
            *ptr++ = payload[3];
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
            if ((payload[3] == 0) || (payload[3] > 240)) {
                zzr.zdu[1] = ZDO_INVALID_EP;
                *ptr++ = payload[3];
                break;
            }

            simpleDesc = af_simpleDescGet(payload[3]);
            if (simpleDesc == NULL) {
                zzr.zdu[1] = ZDO_NOT_ACTIVE;
                *ptr++ = payload[3];
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
}

void zdo_activeEpIndicate(void *arg)
{
	aps_data_ind_t *ad = (aps_data_ind_t *)arg;
	const u8 *payload = ad->asdu;
	u8 seqNum = payload[0];
	u16 nwkAddrReq = rd_le16(payload + 1);
	zdo_status_t status;
	u8 epNum = af_availableEpNumGet();
	u8 response[5U + MAX_ACTIVE_EP_NUMBER] = {0};
	u8 responseLen = 5U;
	zdo_zdp_req_t zzr;

    if (is_short_broadcast(ad->dst_addr)) {
        zb_buf_free((zb_buf_t *)arg);
        return;
    }

	/* A router exposes one application endpoint (1) in this build.  Keep the
	 * wire response bounded and explicitly serialized: this avoids sending
	 * stale bytes if an endpoint descriptor is transiently unavailable and
	 * makes the response independent of packed-struct/pointer arithmetic. */
	if (epNum > MAX_ACTIVE_EP_NUMBER) {
		epNum = MAX_ACTIVE_EP_NUMBER;
	}
	response[0] = seqNum;
	response[1] = ZDO_SUCCESS;
	response[2] = LO_UINT16(nwkAddrReq);
	response[3] = HI_UINT16(nwkAddrReq);
	response[4] = 0U;

	if (nwkAddrReq != zb_info_short_addr()) {
		status = (af_nodeDevTypeGet() == DEVICE_TYPE_COORDINATOR) ? ZDO_INVALID_REQUEST : ZDO_DEVICE_NOT_FOUND;
		response[1] = status;
	} else {
		af_endpoint_descriptor_t *epDesc = af_epDescriptorGet();

		for (u8 i = 0U; i < epNum; i++) {
			u8 ep = epDesc[i].ep;
			bool duplicate = FALSE;

			if (ep == 0U || ep > 240U) {
				continue;
			}
			for (u8 j = 0U; j < response[4]; j++) {
				if (response[5U + j] == ep) {
					duplicate = TRUE;
					break;
				}
			}
			if (!duplicate && response[4] < MAX_ACTIVE_EP_NUMBER) {
				response[5U + response[4]] = ep;
				response[4]++;
			}
		}
	}
	responseLen = (u8)(5U + response[4]);

	memset(&zzr, 0, sizeof(zzr));
	TL_BUF_INITIAL_ALLOC((zb_buf_t *)arg, responseLen, zzr.zdu, u8 *);
	memcpy(zzr.zdu, response, responseLen);

	zzr.cluster_id = ACTIVE_EP_RSP_CLID;
	zzr.zduLen = responseLen;
    zzr.buff_addr = (u8 *)arg;
    zzr.dst_addr_mode = SHORT_ADDR_MODE;
    zzr.dst_nwk_addr = ad->src_short_addr;

	zdo_send_req(&zzr);
}

void zdo_matchDescriptorIndicate(void *arg)
{
    aps_data_ind_t *ad = (aps_data_ind_t *)arg;
    const u8 *payload = ad->asdu;
    u8 seqNum = payload[0];
    u16 nwkAddrReq = rd_le16(payload + 1);
    u16 profileId = rd_le16(payload + 3);
    u8 inClusterNum = payload[5];
    bool emptyRspSuppressed = is_short_broadcast(ad->dst_addr);
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

    const u8 *clusterPtr = payload + 6;
    for (u8 i = 0; i < inClusterNum; i++) {
        inClusters[i] = rd_le16(clusterPtr);
        clusterPtr += 2;
    }

    outClusterNum = *clusterPtr++;
    if ((u8)(inClusterNum + outClusterNum) > 16) {
        zb_buf_free((zb_buf_t *)arg);
        return;
    }

    for (u8 i = 0; i < outClusterNum; i++) {
        outClusters[i] = rd_le16(clusterPtr);
        clusterPtr += 2;
    }

    if ((nwkAddrReq != zb_info_short_addr()) && !is_short_broadcast(nwkAddrReq)) {
        status = (af_nodeDevTypeGet() == DEVICE_TYPE_COORDINATOR) ? ZDO_INVALID_REQUEST : ZDO_DEVICE_NOT_FOUND;
    } else {
        af_endpoint_descriptor_t *epDesc = af_epDescriptorGet();
        u8 epNum = af_availableEpNumGet();

        for (u8 i = 0; i < epNum; i++) {
            af_simple_descriptor_t *sd = epDesc[i].correspond_simple_desc;
            bool matched = TRUE;

            if (sd == NULL) {
                continue;
            }

            if ((profileId != WILDCARD_PROFILE_ID) && (sd->app_profile_id != profileId)) {
                continue;
            }

            for (u8 j = 0; j < inClusterNum; j++) {
                if (!cluster_list_contains(sd->app_in_cluster_lst, sd->app_in_cluster_count, inClusters[j])) {
                    matched = FALSE;
                    break;
                }
            }

            if (!matched) {
                continue;
            }

            for (u8 j = 0; j < outClusterNum; j++) {
                if (!cluster_list_contains(sd->app_out_cluster_lst, sd->app_out_cluster_count, outClusters[j])) {
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
}

void zdo_SysServerDiscoveryIndicate(void *arg)
{
    aps_data_ind_t *ad = (aps_data_ind_t *)arg;
    const u8 *payload = ad->asdu;
    u8 seqNum = payload[0];
    u16 reqMask = rd_le16(payload + 1);
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
    TL_BUF_INITIAL_ALLOC((zb_buf_t *)arg, sizeof(zdo_system_server_discovery_resp_t), zzr.zdu, u8 *);

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
}
u8 zdo_bind_unbind_req(const zdo_bind_req_t *req, zdo_zdp_req_t *zzr, bool bind)
{
    zb_buf_t *buf;
    u8 payloadLen;

    buf = zb_buf_allocate();
    if (buf == NULL) {
        return 0x8a;
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
    u16 clusterId = ed_bind_cluster_get(0);
    u16 srcShortAddr;
    u16 dstShortAddr;
    u8 srcEp;
    u8 dstEp;

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

    tl_zbExtAddrByShortAddr(srcShortAddr, bindReq.src_addr, NULL);
    tl_zbExtAddrByShortAddr(dstShortAddr, bindReq.dst_ext_addr, NULL);
    bindReq.src_endpoint = srcEp;
    bindReq.cid16_l = LO_UINT16(clusterId);
    bindReq.cid16_h = HI_UINT16(clusterId);
    bindReq.dst_addr_mode = LONG_EXADDR_DSTENDPOINT;
    bindReq.dst_endpoint = dstEp;
    zzr.zdoRspReceivedIndCb = zdo_bind_toggle_cb;

    if (zdo_bind_unbind_req(&bindReq, &zzr, FALSE) == 0x8aU) {
        tl_zbTaskPost(zdo_bind_toggle_action, NULL);
    }
}

_attribute_no_inline_ static void zdo_bind_unbind_after_toggle_clone_1(bool bind, zdo_callback cb)
    __asm__("zdo_bind_unbind_after_toggle.clone.1");
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

    tl_zbExtAddrByShortAddr(srcShortAddr, bindReq.src_addr, NULL);
    tl_zbExtAddrByShortAddr(dstShortAddr, bindReq.dst_ext_addr, NULL);
    bindReq.src_endpoint = srcEp;
    bindReq.dst_addr_mode = LONG_EXADDR_DSTENDPOINT;
    bindReq.dst_endpoint = dstEp;
    zzr.dst_addr_mode = SHORT_ADDR_MODE;
    zzr.dst_nwk_addr = srcShortAddr;
    zzr.zdoRspReceivedIndCb = cb;

    ed_bind_ctx()->clusterTotalCount--;
    clusterId = ed_bind_cluster_get(ed_bind_ctx()->clusterTotalCount);
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
        return 0x8a;
    }

    zzr->buff_addr = (u8 *)buf;
    TL_BUF_INITIAL_ALLOC(buf, payloadLen, zzr->zdu, u8 *);

    zzr->zdpSeqNum = zdp_txSeqNo++;
    ptr = zzr->zdu;
    *ptr++ = zzr->zdpSeqNum;
    *ptr++ = LO_UINT16(req->binding_target_addr);
    *ptr++ = HI_UINT16(req->binding_target_addr);
    memcpy(ptr, req->src_ext_addr, EXT_ADDR_LEN);
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

    if (memcmp(req->ext_src_addr, zb_info_ieee_addr(), EXT_ADDR_LEN) != 0) {
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
            return (apsStatus == APS_STATUS_TABLE_FULL) ? ZDO_TABLE_FULL : ZDO_NOT_SUPPORTED;
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
}

void zdo_endDeviceBindIndicate(void *arg)
{
    aps_data_ind_t *ad = (aps_data_ind_t *)arg;
    zdo_ed_bind_req_view_t currentReq;
    zdo_ed_bind_req_view_t pendingReq;
    aps_data_ind_t *pendingAd;

    if (!zdo_ed_bind_req_parse(ad->asdu, ad->asduLength, &currentReq) ||
        (currentReq.srcEndpoint == 0U) || (currentReq.srcEndpoint == 0xffU)) {
        zdo_end_device_bind_resp_send(arg, ZDO_INVALID_EP, ad->asdu[0], ad->src_short_addr);
        return;
    }

    pendingAd = (aps_data_ind_t *)ed_bind_pending_buf_get();
    if (pendingAd == NULL) {
        if (zdo_bind_timer_event != NULL) {
            ev_timer_taskCancel(&zdo_bind_timer_event);
        }

        zdo_bind_timer_event = ev_timer_taskPost(zdo_end_device_bind_timeout_cb, arg, 20000);
        ed_bind_pending_buf_set(arg);
        return;
    }

    if (zdo_bind_timer_event != NULL) {
        ev_timer_taskCancel(&zdo_bind_timer_event);
    }

    if (!zdo_ed_bind_req_parse(pendingAd->asdu, pendingAd->asduLength, &pendingReq) ||
        (pendingReq.profileId != currentReq.profileId)) {
        zdo_end_device_bind_resp_send(arg, ZDO_NO_MATCH, currentReq.seqNum, ad->src_short_addr);
        zdo_end_device_bind_resp_send(pendingAd, ZDO_NO_MATCH, pendingReq.seqNum, pendingAd->src_short_addr);
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
        zdo_end_device_bind_resp_send(arg, ZDO_NO_MATCH, currentReq.seqNum, ad->src_short_addr);
        zdo_end_device_bind_resp_send(pendingAd, ZDO_NO_MATCH, pendingReq.seqNum, pendingAd->src_short_addr);
        ed_bind_ctx_reset();
        return;
    }

    ed_bind_ctx()->src1ShortAddr = pendingAd->src_short_addr;
    ed_bind_ctx()->src1Endpoint = pendingReq.srcEndpoint;
    ed_bind_ctx()->src2ShortAddr = ad->src_short_addr;
    ed_bind_ctx()->src2Endpoint = currentReq.srcEndpoint;
    tl_zbTaskPost(zdo_bind_toggle_action, NULL);

    zdo_end_device_bind_resp_send(arg, ZDO_SUCCESS, currentReq.seqNum, ad->src_short_addr);
    zdo_end_device_bind_resp_send(pendingAd, ZDO_SUCCESS, pendingReq.seqNum, pendingAd->src_short_addr);
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
}

void zdo_mgmtNwkUpdateIndicate(void *arg)
{
    aps_data_ind_t *ad = (aps_data_ind_t *)arg;
    const u8 *payload = ad->asdu;
    u8 seqNum = payload[0];
    u32 scanChannels = rd_le32(payload + 1);
    u8 scanDuration = payload[5];
    u8 scanCountOrUpdateId = payload[6];
    u16 nwkManagerAddr = rd_le16(payload + 7);
    zdo_status_t status = ZDO_INVALID_REQUEST;

    if (scanDuration == ZDO_NWK_MANAGER_CHANNEL_CHANGE) {
        u8 channel = zdo_channel_page2num(scanChannels);
        u8 acceptChannel = zdo_af_get_accept_nwk_update_channel();

        if ((channel >= 11) && (channel <= 26) &&
            ((acceptChannel == 0) || (acceptChannel == 0xff) || (acceptChannel == channel))) {
            g_zbInfo.nwkNib.updateId = scanCountOrUpdateId;
            zdo_nwkRejoinStart(scanChannels, zdo_cfg_attributes.config_nwk_scan_duration);
        }

        zb_buf_free((zb_buf_t *)arg);
        return;
    }

    if (scanDuration == ZDO_NWK_MANAGER_ATTRIBUTES_CHANGE) {
        aps_ib.aps_channel_mask = scanChannels;
        if (ZB_IS_64BIT_ADDR_INVALID(ss_ib.trust_center_address) || (nwkManagerAddr == 0)) {
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

    if (!is_short_broadcast(ad->dst_addr)) {
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
    } else {
		zb_buf_free((zb_buf_t *)arg);
    }
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

    if (is_short_broadcast(ad->dst_addr)) {
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

        memcpy(ptr, zb_info_ieee_addr(), EXT_ADDR_LEN);
        ptr += EXT_ADDR_LEN;
        *ptr++ = table[i].srcEp;
        *ptr++ = LO_UINT16(table[i].clusterId);
        *ptr++ = HI_UINT16(table[i].clusterId);
        *ptr++ = table[i].dstAddrMode;

        if (table[i].dstAddrMode == APS_BIND_DST_ADDR_LONG) {
            memcpy(ptr, table[i].dstExtAddrInfo.extAddr, EXT_ADDR_LEN);
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

    if (is_short_broadcast(ad->dst_addr)) {
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
        memcpy(n.ext_pan_id, g_zbNIB.extPANId, EXT_ADDR_LEN);
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
}

void zdo_mgmtLeaveIndicate(void *arg)
{
    aps_data_ind_t *ad = (aps_data_ind_t *)arg;
    zdo_mgmt_leave_req_t req;
    zdo_status_t status = ZDO_SUCCESS;
    u16 parent_short;
    u16 coord_short;

    if (ad->asduLength != 10U) {
        zb_buf_free((zb_buf_t *)arg);
        return;
    }

    memcpy(&req, ad->asdu + 1, sizeof(req));
    /* A router normally keeps the coordinator in the neighbor table as its
     * parent.  After a rejoin, however, the address map/MAC PIB can already
     * contain the current coordinator while the normal neighbor entry has
     * not been restored yet.  Rejecting that valid source here makes Z2M's
     * non-force remove look like a silent timeout.  Keep both checks: the
     * source must still be either the recorded parent or the current
     * coordinator, never an arbitrary unicast sender. */
    parent_short = tl_zbNeighborParentShortAddrGet();
    coord_short = g_zbInfo.macPib.coordShortAddress;
    if (ad->srcMacAddr != parent_short &&
        (coord_short == MAC_SHORT_ADDR_NONE || ad->srcMacAddr != coord_short)) {
        zb_buf_free((zb_buf_t *)arg);
        return;
    }

    if (memcmp(req.device_addr, g_zero_addr, EXT_ADDR_LEN) != 0 &&
        memcmp(req.device_addr, zb_info_ieee_addr(), EXT_ADDR_LEN) != 0) {
        if (nwk_neTblGetByExtAddr(req.device_addr) == NULL) {
            status = 0xc8;
        }
    }

    if ((status == ZDO_SUCCESS) && zdo_af_get_mgmtLeave_use_aps_sec() &&
        ((ad->security_status & SECURITY_IN_APSLAYER) == 0U)) {
        status = ZDO_NOT_AUTHORIZED;
    }
    if (!is_short_broadcast(ad->dst_addr)) {
        zdo_zdp_req_t zzr;
        u8 tx_status;

        memset(&zzr, 0, sizeof(zzr));
        TL_BUF_INITIAL_ALLOC((zb_buf_t *)arg, 2, zzr.zdu, u8 *);
        zzr.zdu[0] = ad->asdu[0];
        zzr.zdu[1] = status;
        zzr.cluster_id = MGMT_LEAVE_RSP_CLID;
        zzr.zduLen = 2;
        zzr.buff_addr = (u8 *)arg;
        zzr.dst_addr_mode = SHORT_ADDR_MODE;
        zzr.dst_nwk_addr = ad->src_short_addr;
        tx_status = zdo_send_req(&zzr);
    } else {
        zb_buf_free((zb_buf_t *)arg);
    }

    if (status == ZDO_SUCCESS) {
        zb_buf_t *buf = zb_buf_allocate();

        if (buf != NULL) {
            memcpy(buf, req.device_addr, EXT_ADDR_LEN);
            ((u8 *)buf)[8] = req.lr_bitfields.remove_children;
            ((u8 *)buf)[9] = req.lr_bitfields.rejoin;
            /* zdo_send_req() queues the leave response asynchronously. Keep
             * the leave request pending and let NWK confirmation perform the
             * response-then-leave handoff. */
            zdo_mgmt_leave_pending = buf;
        }
    }
}
