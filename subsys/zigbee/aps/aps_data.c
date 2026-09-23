/*
 * Copyright 2026 libzigbee
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "zb_common.h"
#include "aps.h"
#include "aps_data.h"
#include "ss_apsEnDecrypt.h"
#include "ss_zdoSecurityME.h"
#include "zb_initialize.h"
#include "zb_nwk_addr_map.h"
#include "nwk_data.h"
#include "zb_nwk_core.h"
#include <stdint.h>

u16 dstPanID = 0;
u8 g_apsTxCacheNum = 0;
static apsDataIndCb_t g_apsDataIndCb;
u8 T_DBG_fgmt[16] = {0};
u8 apsDuplicateCheckFlag = 0;
u8 T_DBG_fgmtCnf[8] = {0};
u8 T_DBG_fgmtBlk[8] = {0};
u8 T_DBG_fgmtTout = 0;

enum {
	APS_FRAGMENT_RCV_TIMEOUT_TICKS_PER_ACK = 124U,
	APS_FRAGMENT_REASSEMBLY_MAX_PAYLOAD = 504U,
};

typedef struct _attribute_packed_ {
	u8 seqNum;
	u8 status;
	addrExt_t extAddr;
	u16 shortAddr;
} aps_nwk_addr_rsp_hdr_t;

typedef apsDataFragmentTransWin_t aps_fragment_tx_win_t;

typedef struct _attribute_packed_ {
	u8 hdrLen;
	u8 frameCtrl;
	u8 apsCounter;
	u8 srcEp;
	u16 srcShortAddr;
	u8 dstEp;
	u8 securityStatus;
	u16 clusterId;
	u16 profileId;
	u8 extHdr;
	u8 blockNum;
	u8 ackBit;
} aps_rx_hdr_t;

typedef struct _attribute_packed_ {
	union {
		u16 addr_short;
		addrExt_t addr_long;
	} dstAddr;
	union {
		struct _attribute_packed_ {
			u8 dstAddrMode;
			u8 dstEndpoint;
			u8 srcEndpoint;
		} af;
		struct _attribute_packed_ {
			u8 dstEndpoint;
			u8 srcEndpoint;
			u8 dstAddrMode;
		} extConfirm;
	} ep;
	u8 status;
	u8 reserved12[4];
	u8 handle;
	u8 apsCnt;
	u16 clusterId;
} aps_confirm_buf_t;

/* Local packed overlay for fragment reassembly. */
typedef struct _attribute_packed_ {
	aps_data_ind_t ind;
	u16 copiedLen;
	u8 asdu[];
} aps_fragment_reassembly_t;

/* Layout contract */
#if UINTPTR_MAX == UINT32_MAX
STATIC_ASSERT(sizeof(aps_data_ind_t) == 35);
STATIC_ASSERT(OFFSETOF(aps_fragment_reassembly_t, copiedLen) == 35);
STATIC_ASSERT(OFFSETOF(aps_fragment_reassembly_t, asdu) == 37);
#endif
#if UINTPTR_MAX == UINT32_MAX
STATIC_ASSERT(OFFSETOF(aps_fragment_tx_win_t, req) == 32);
STATIC_ASSERT(OFFSETOF(aps_fragment_tx_win_t, payload) == 36);
STATIC_ASSERT(OFFSETOF(aps_fragment_tx_win_t, payloadLen) == 40);
STATIC_ASSERT(OFFSETOF(aps_fragment_tx_win_t, totalBlockNum) == 42);
STATIC_ASSERT(OFFSETOF(aps_fragment_tx_win_t, curBlockNum) == 43);
STATIC_ASSERT(OFFSETOF(aps_fragment_tx_win_t, activeListNum) == 44);
STATIC_ASSERT(OFFSETOF(aps_fragment_tx_win_t, ackBit) == 45);
STATIC_ASSERT(OFFSETOF(aps_fragment_tx_win_t, reSend) == 46);
STATIC_ASSERT(OFFSETOF(aps_fragment_tx_win_t, used) == 47);
STATIC_ASSERT(sizeof(aps_fragment_tx_win_t) == 48);
STATIC_ASSERT(OFFSETOF(apsDataFragmentRcvWin_t, rcvFragmentlen) == 80);
STATIC_ASSERT(OFFSETOF(apsDataFragmentRcvWin_t, srcAddr) == 82);
STATIC_ASSERT(OFFSETOF(apsDataFragmentRcvWin_t, apsCnt) == 84);
STATIC_ASSERT(OFFSETOF(apsDataFragmentRcvWin_t, totalBlockNum) == 85);
STATIC_ASSERT(OFFSETOF(apsDataFragmentRcvWin_t, blockNumProcessed) == 86);
STATIC_ASSERT(OFFSETOF(apsDataFragmentRcvWin_t, waitCnt) == 88);
STATIC_ASSERT(OFFSETOF(apsDataFragmentRcvWin_t, used) == 89);
STATIC_ASSERT(sizeof(apsDataFragmentRcvWin_t) == 90);
#endif

static aps_fragment_tx_win_t g_apsDataFragmentTransWin = {0};
static apsDataFragmentRcvWin_t g_apsDataFragmentRcvWin = {0};

static int endDevTimeoutReq(void *arg);
static void aps_nwk_addr_req_cb(void *arg);
static int aps_data_fragment_delay(void *arg);
static void aps_data_fragment(void *arg);
static void apsDataFragmentReSend(void);
static void bindingTxBack(void *arg);

static inline aps_fragment_tx_win_t *aps_frag_tx_win(void)
{
	return &g_apsDataFragmentTransWin;
}

static inline apsDataFragmentRcvWin_t *aps_frag_rcv_win(void)
{
	return &g_apsDataFragmentRcvWin;
}

static inline u8 aps_fragment_window_size(const apsDataFragmentRcvWin_t *win, u8 minBlock)
{
	u8 windowSize = aps_ib.aps_max_window_size;
	u8 remaining = (u8)(win->totalBlockNum - minBlock);

	if (windowSize > 8U) {
		windowSize = 8U;
	}
	if (windowSize > remaining) {
		windowSize = remaining;
	}
	return windowSize;
}

static inline u8 aps_fragment_window_mask(u8 validBufNum)
{
	return (validBufNum == 8U) ? 0xffU : (u8)((1U << validBufNum) - 1U);
}

static inline bool aps_fragment_ack_window_ready(const aps_fragment_tx_win_t *win)
{
	return win->activeListNum == aps_ib.aps_max_window_size ||
	       (u8)(win->curBlockNum + 1U) == win->totalBlockNum;
}

static aps_rcv_info_t *aps_fragment_rx_slot(apsDataFragmentRcvWin_t *win, u8 blockNum)
{
	u8 maxWindow = aps_ib.aps_max_window_size;

	if (maxWindow == 0U) {
		return NULL;
	}

	for (u8 i = 0; i < 2U; i++) {
		aps_rcv_info_t *slot = &aps_frag_rcv_win()->rcvInfo[i];
		u16 endBlock;

		if (slot->state != APS_RCV_FRAGMENT_RCVING) {
			continue;
		}

		endBlock = (u16)slot->minBlockNum + slot->validBufNum;
		if ((u16)blockNum >= slot->minBlockNum && (u16)blockNum < endBlock) {
			return slot;
		}
	}

	for (u8 i = 0; i < 2U; i++) {
		aps_rcv_info_t *slot = &aps_frag_rcv_win()->rcvInfo[i];
		u8 minBlock;
		u8 windowSize;

		if (slot->state != APS_RCV_FRAGMENT_IDLE) {
			continue;
		}

		minBlock = (u8)(blockNum - (blockNum % maxWindow));
		if (minBlock >= win->totalBlockNum) {
			return NULL;
		}

		windowSize = aps_fragment_window_size(win, minBlock);
		if (windowSize == 0U) {
			return NULL;
		}

		memset(slot, 0, sizeof(*slot));
		slot->validBufNum = windowSize;
		slot->minBlockNum = minBlock;
		slot->ackBit = (u8)~aps_fragment_window_mask(windowSize);
		slot->state = APS_RCV_FRAGMENT_RCVING;
		win->curRcvMinBlockNum = minBlock;
		return slot;
	}

	return NULL;
}

static inline u8 aps_cache_state(const aps_tx_cache_list_t *cache)
{
	return cache->state;
}

static inline void aps_cache_state_set(aps_tx_cache_list_t *cache, u8 state)
{
	cache->state = state;
}

static inline aps_rx_hdr_t *aps_rx_hdr(void *arg)
{
	return (aps_rx_hdr_t *)((u8 *)arg + sizeof(nlde_data_ind_t));
}

static void aps_indPrimBuild(void *arg)
{
	u8 *buf = (u8 *)arg;
	nlde_data_ind_t in;
	aps_rx_hdr_t hdr;
	aps_data_ind_t *out = (aps_data_ind_t *)arg;

	memcpy(&in, arg, sizeof(in));
	memcpy(&hdr, buf + sizeof(nlde_data_ind_t), sizeof(hdr));
	memset(out, 0, sizeof(*out));

	if (in.nsduLen < hdr.hdrLen) {
		return;
	}

	out->cluster_id = hdr.clusterId;
	out->profile_id = hdr.profileId;
	out->src_ep = hdr.srcEp;
	out->dst_ep = hdr.dstEp;
	out->dst_addr = in.dstAddr;
	out->src_addr_mode = APS_SHORT_SRCADDR_WITHEP;
	out->src_short_addr = in.srcAddr;
	out->srcMacAddr = in.srcMacAddr;
	out->asdu = in.nsdu + hdr.hdrLen;
	out->asduLength = (u16)(in.nsduLen - hdr.hdrLen);
	out->rx_tick = in.rxTime;
	out->lqi = in.lqi;
	out->rssi = ((zb_buf_t *)arg)->hdr.rssi;
	out->aps_counter = hdr.extHdr;

	if ((hdr.frameCtrl & APS_FRAME_CTRL_DELIVERY_MODE_MASK) ==
	    APS_FRAME_CTRL_DELIVERY_MODE_GROUP) {
		out->dst_addr_mode = APS_SHORT_GROUPADDR_NOEP;
	} else {
		out->dst_addr_mode = APS_SHORT_DSTADDR_WITHEP;
	}

	if ((hdr.frameCtrl & APS_FRAME_CTRL_SECURITY) != 0U) {
		out->security_status |= SECURITY_IN_APSLAYER;
	}
	if (in.securityUse) {
		out->security_status |= SECURITY_IN_NWKLAYER;
	}
}

static void aps_conf(void *arg)
{
	aps_confirm_buf_t *buf = (aps_confirm_buf_t *)arg;

	if (buf->handle < APS_CMD_HANDLE_TRANSPORT_KEY) {
		u8 *cnf = ev_buf_allocate(sizeof(aps_confirm_buf_t));

		if (cnf == NULL) {
			return;
		}

		memset(cnf, 0, sizeof(aps_confirm_buf_t));
		memcpy(cnf, buf, sizeof(aps_confirm_buf_t));
		tl_zbTaskPost(af_dataCnfHandler, cnf);
		return;
	}

#if defined(ZB_COORDINATOR_ROLE)
	if (buf->status != APS_STATUS_SUCCESS) {
		return;
	}

	if (buf->handle == APS_CMD_HANDLE_SWITCH_KEY) {
		ss_tcSwitchKey((u8)(ss_ib.activeKeySeqNum + 1U));
		return;
	}

	if ((buf->handle == APS_CMD_HANDLE_TXKEYCMD_RELAY ||
	     buf->handle == APS_CMD_HANDLE_NWK_KEY) &&
	    buf->ep.af.dstAddrMode == APS_SHORT_DSTADDR_WITHEP &&
	    ZB_NWK_IS_ADDRESS_BROADCAST(buf->dstAddr.addr_short)) {
		ss_tcSwitchKeyTimerStart();
		return;
	}
#endif

	if (buf->status != APS_STATUS_SUCCESS || buf->handle != APS_CMD_HANDLE_DEVICE_ANNOUNCE) {
		return;
	}

	{
		u16 idx = 0;
		u16 shortAddr = 0;

		if (tl_zbShortAddrByExtAddr(&shortAddr, g_zbInfo.macPib.extAddress, &idx) ==
		    RET_OK) {
			u16 localShort = g_zbInfo.macPib.shortAddress;

			if (shortAddr != localShort) {
				tl_zbNwkAddrMapAdd(idx, g_zbInfo.macPib.extAddress, &shortAddr);
				zb_info_save(NULL);
			}
		}
	}

	if (af_nodeDescStackRevisionGet() > 20U) {
		ev_timer_taskPost((ev_timer_callback_t)endDevTimeoutReq, NULL, 200);
	}
}
static int endDevTimeoutReq(void *arg)
{
	(void)arg;

#if defined(ZB_ROUTER_ROLE)
	return -1;
#else
	nwkEndDevTimeoutReqSend((reqTimeoutEnum_t)g_zbNIB.endDevTimeoutDefault, 0);
	return -1;
#endif
}
static void aps_txCacheAsNoShortAddr(addrExt_t extAddr, u8 *seqNo)
{
	zdo_nwk_addr_req_t req;

	memcpy(req.ieee_addr_interest, extAddr, sizeof(req.ieee_addr_interest));
	req.req_type = 0;
	req.start_index = 0;
	(void)zb_zdoNwkAddrReq(NWK_BROADCAST_ROUTER_COORDINATOR, &req, seqNo, aps_nwk_addr_req_cb);
}

static void apsDataFragmentRcvWinClear(void)
{
	apsDataFragmentRcvWin_t *win = aps_frag_rcv_win();

	if (win->used == 0U) {
		return;
	}

	if (win->rcvWinTimeoutEvt != NULL) {
		ev_timer_taskCancel(&aps_frag_rcv_win()->rcvWinTimeoutEvt);
	}

	win->evBuf = NULL;

	for (u8 i = 0; i < 2; i++) {
		aps_rcv_info_t *slot = &aps_frag_rcv_win()->rcvInfo[i];

		if (slot->state != APS_RCV_FRAGMENT_IDLE && slot->validBufNum != 0U) {
			for (u8 j = 0; j < slot->validBufNum; j++) {
				zb_buf_t *frag = (zb_buf_t *)slot->buf[j];

				if (frag != NULL) {
					zb_buf_free(frag);
					slot->buf[j] = NULL;
				}
			}
		}
	}

	memset(&g_apsDataFragmentRcvWin, 0, sizeof(g_apsDataFragmentRcvWin));
}

static void apsRcvingWindowHandling(void *arg)
{
	aps_rcv_info_t *slot = (aps_rcv_info_t *)arg;
	apsDataFragmentRcvWin_t *win = aps_frag_rcv_win();

	if (slot->state != APS_RCV_FRAGMENT_HANDLING) {
		return;
	}

	aps_fragment_reassembly_t *reassemblyObj =
		(aps_fragment_reassembly_t *)aps_frag_rcv_win()->evBuf;

	if (reassemblyObj == NULL) {
		ZB_EXCEPTION_POST(SYS_EXCEPTTION_ZB_APS_FRAGMENT_RCV);
		return;
	}

	if (slot->validBufNum != 0U) {
		for (u8 i = 0; i < slot->validBufNum; i++) {
			zb_buf_t *frag = (zb_buf_t *)slot->buf[i];

			if (frag == NULL) {
				continue;
			}

			aps_indPrimBuild(frag);
			if (slot->minBlockNum == 0U) {
				win->rcvFragmentlen = ((aps_data_ind_t *)frag)->asduLength;
				memcpy(&reassemblyObj->ind, frag, sizeof(reassemblyObj->ind));
			}

			{
				u16 offset = reassemblyObj->copiedLen;
				aps_data_ind_t *fragInd = (aps_data_ind_t *)frag;
				u16 fragLen = fragInd->asduLength;
				u8 *asdu = fragInd->asdu;

				memcpy(reassemblyObj->asdu + offset, asdu, fragLen);
				offset = (u16)(offset + fragLen);
				reassemblyObj->copiedLen = offset;
				reassemblyObj->ind.asduLength = offset;
			}

			zb_buf_free(frag);
			slot->buf[i] = NULL;
			win->blockNumProcessed++;
		}
	}

	memset(slot, 0, sizeof(*slot));
	if (win->blockNumProcessed > win->totalBlockNum) {
		ZB_EXCEPTION_POST(SYS_EXCEPTTION_ZB_APS_FRAGMENT_RCV);
		return;
	}

	if (win->blockNumProcessed == win->totalBlockNum) {
		reassemblyObj->ind.asdu = reassemblyObj->asdu;
		tl_zbTaskPost(af_aps_data_fragment_entry, reassemblyObj);
		apsDataFragmentRcvWinClear();
	}
}
static int aps_data_fragment_process_timeout(void *arg)
{
	(void)arg;

	apsDataFragmentRcvWin_t *win = aps_frag_rcv_win();

	if (win->used == 0U) {
		return -1;
	}

	if (win->blockNumProcessed == win->totalBlockNum && win->evBuf == NULL) {
		memset(&g_apsDataFragmentRcvWin, 0, sizeof(g_apsDataFragmentRcvWin));
		return -1;
	}

	win->waitCnt++;
	if (win->waitCnt < APS_MAX_FRAME_RETRIES) {
		return 0;
	}

	if (win->evBuf != NULL) {
		T_DBG_fgmtTout++;
		ev_buf_free(win->evBuf);
	}

	apsDataFragmentRcvWinClear();
	win->waitCnt = 0;
	win->rcvWinTimeoutEvt = NULL;
	return -1;
}
static u8 aps_ack_send(void *arg, u8 blockAck)
{
	zb_buf_t *buf = zb_buf_allocate();
	nlde_data_req_t *req;
	aps_rx_hdr_t *hdr = aps_rx_hdr(arg);
	u8 auxHdr[8] = {0};
	u8 *payload;
	u8 payloadLen;
	u8 baseLen;
	u8 fc;

	if (buf == NULL) {
		return APS_STATUS_INTERNAL_BUF_FULL;
	}

	if ((hdr->frameCtrl & APS_FRAME_CTRL_FRAME_TYPE_MASK) ==
	    APS_FRAME_CTRL_FRAME_TYPE_COMMAND) {
		fc = APS_FRAME_CTRL_COMMAND_ACK;
		baseLen = 2;
	} else if ((hdr->frameCtrl & APS_FRAME_CTRL_EXTENDED_HEADER) != 0U) {
		fc = APS_FRAME_CTRL_EXTENDED_DATA_ACK;
		baseLen = (hdr->extHdr != 0U) ? 11U : 9U;
	} else {
		fc = APS_FRAME_CTRL_DATA_ACK;
		baseLen = 8;
	}

	payloadLen = baseLen;
	if ((hdr->frameCtrl & APS_FRAME_CTRL_FRAME_TYPE_MASK) !=
		    APS_FRAME_CTRL_FRAME_TYPE_COMMAND &&
	    (hdr->frameCtrl & APS_FRAME_CTRL_SECURITY) != 0U) {
		fc |= APS_FRAME_CTRL_SECURITY;
		payloadLen = (u8)(payloadLen + ss_apsEnAuxHdrFill(auxHdr, NULL, 0));
	}

	TL_BUF_INITIAL_ALLOC(buf, payloadLen, payload, u8 *);
	if (payload == NULL) {
		zb_buf_free(buf);
		return APS_STATUS_INTERNAL_BUF_FULL;
	}

	payload[0] = fc;
	if ((hdr->frameCtrl & APS_FRAME_CTRL_FRAME_TYPE_MASK) ==
	    APS_FRAME_CTRL_FRAME_TYPE_COMMAND) {
		payload[1] = hdr->apsCounter;
		if ((hdr->frameCtrl & APS_FRAME_CTRL_EXTENDED_HEADER) != 0U) {
			payload[2] = hdr->extHdr ? 2U : 0U;
			if (hdr->extHdr != 0U) {
				payload[3] = hdr->blockNum;
				payload[4] = blockAck;
			}
		}
	} else {
		payload[1] = hdr->srcEp;
		COPY_U16TOBUFFER(payload + 2, hdr->clusterId);
		COPY_U16TOBUFFER(payload + 4, hdr->profileId);
		payload[6] = hdr->dstEp;
		payload[7] = hdr->apsCounter;
		if ((hdr->frameCtrl & APS_FRAME_CTRL_EXTENDED_HEADER) != 0U) {
			payload[8] = hdr->extHdr ? 2U : 0U;
			if (hdr->extHdr != 0U) {
				payload[9] = hdr->blockNum;
				payload[10] = blockAck;
			}
		}
	}

	if ((hdr->frameCtrl & APS_FRAME_CTRL_FRAME_TYPE_MASK) !=
		    APS_FRAME_CTRL_FRAME_TYPE_COMMAND &&
	    (hdr->frameCtrl & APS_FRAME_CTRL_SECURITY) != 0U) {
		u8 auxLen = (u8)(payloadLen - baseLen);

		memcpy(payload + baseLen, auxHdr, auxLen);
		if (zb_address_ieee_by_short(hdr->srcShortAddr, auxHdr) == 0U) {
			zb_buf_free(buf);
			return APS_STATUS_SECURITY_FAIL;
		}
		if (ss_apsSecureFrame(buf, payloadLen, baseLen, *(addrExt_t *)auxHdr) != RET_OK) {
			zb_buf_free(buf);
			return APS_STATUS_SECURITY_FAIL;
		}
	}

	memset(buf, 0, sizeof(nlde_data_req_t));
	req = (nlde_data_req_t *)buf;
	req->dstAddr = hdr->srcShortAddr;
	req->addrMode = APS_SHORT_DSTADDR_WITHEP;
	req->discoverRoute = 1;
	req->securityEnable = ((nlde_data_ind_t *)arg)->securityUse;
	req->ndsuHandle = APS_CMD_HANDLE_ACK;
	req->nsdu = payload;
	req->nsduLen = payloadLen;

	tl_zbNwkNldeDataRequest(buf);
	return 0;
}

static void aps_txCacheConfirm(void *arg, u8 status)
{
	aps_tx_cache_list_t *cache = (aps_tx_cache_list_t *)arg;
	aps_fragment_tx_win_t *win;
	aps_data_req_t *req;
	aps_confirm_buf_t cnf;

	memset(&cnf, 0, sizeof(cnf));

	if (cache->extFrameCtrl == 0U) {
		cnf.handle = cache->handler;
		cnf.apsCnt = cache->apsCount;
		cnf.status = status;
		cnf.ep.af.srcEndpoint = cache->ep;
		cnf.ep.af.dstAddrMode = cache->dstAddrMode;
		ZB_IEEE_ADDR_COPY(&cnf.dstAddr, &cache->dstAddr);
		cnf.ep.af.dstEndpoint = cache->dstEndpoint;
		cnf.clusterId = cache->clusterId;
		aps_conf(&cnf);

		if (cache->payload != NULL) {
			zb_buf_free((zb_buf_t *)cache->payload);
		}

		memset(arg, 0, sizeof(aps_tx_cache_list_t));
		g_apsTxCacheNum--;
		return;
	}

	T_DBG_fgmtCnf[0]++;

	if (aps_cache_state(cache) == APX_TX_CACHE_STA_ADDR_REQ ||
	    aps_cache_state(cache) == APX_TX_CACHE_STA_TXING ||
	    aps_cache_state(cache) == APX_TX_CACHE_STA_WAITING_ACK ||
	    aps_cache_state(cache) == APX_TX_CACHE_STA_RETRY) {
		T_DBG_fgmtBlk[0]++;

		if (status != APS_STATUS_SUCCESS) {
			goto fragment_done;
		}

		win = aps_frag_tx_win();
		if (win->activeListNum >= win->curBlockNum - 1U && win->ackBit == 0xffU) {
			goto fragment_done;
		}
	}

	win = aps_frag_tx_win();
	if (win->activeListNum >= aps_ib.aps_max_window_size && win->ackBit == 0xffU) {
		for (u8 i = 0; i < win->activeListNum; i++) {
			aps_tx_cache_list_t *pending = win->list[i];

			if (pending == NULL) {
				continue;
			}

			if (pending->payload != NULL) {
				zb_buf_free((zb_buf_t *)pending->payload);
			}
			memset(pending, 0, sizeof(*pending));
			g_apsTxCacheNum--;
		}

		win->activeListNum = 0;
		win->reSend = 0;
		win->ackBit = 0xffU;
	}

	T_DBG_fgmtTout++;

	if (win->reSend == 0U) {
		win->curBlockNum++;
	}

	{
		u8 delay = aps_ib.aps_interframe_delay;

		if (delay == 0U) {
			delay = 100U;
		}
		ev_timer_taskPost(aps_data_fragment_delay, NULL, delay);
	}
	return;

fragment_done:
	win = aps_frag_tx_win();
	req = (aps_data_req_t *)win->req;

	cnf.handle = req->handle;
	cnf.apsCnt = req->apsCnt;
	cnf.status = status;
	cnf.ep.extConfirm.dstAddrMode = req->src_endpoint;
	cnf.ep.extConfirm.dstEndpoint = req->dst_addr_mode;
	ZB_IEEE_ADDR_COPY(cnf.dstAddr.addr_long, req->aps_addr.dst_ext_addr);
	cnf.ep.extConfirm.srcEndpoint = req->aps_addr.dst_endpoint;
	cnf.clusterId = req->cluster_id;
	aps_conf(&cnf);

	if (win->used != 0U) {
		if (win->payload != NULL) {
			ev_buf_free((u8 *)win->payload);
		}
		if (win->req != NULL) {
			ev_buf_free((u8 *)win->req);
		}

		for (u8 i = 0; i < win->activeListNum; i++) {
			aps_tx_cache_list_t *pending = win->list[i];

			if (pending == NULL) {
				continue;
			}

			if (pending->payload != NULL) {
				zb_buf_free((zb_buf_t *)pending->payload);
			}
			memset(pending, 0, sizeof(*pending));
			g_apsTxCacheNum--;
		}

		memset(win, 0, sizeof(*win));
		return;
	}

	if (cache->payload != NULL) {
		zb_buf_free((zb_buf_t *)cache->payload);
	}
	memset(cache, 0, sizeof(*cache));
	g_apsTxCacheNum--;
}

static u8 apsTxDataSendStart(aps_tx_cache_list_t *cache)
{
	zb_buf_t *buf;
	zb_buf_t *src;
	nlde_data_req_t *req;
	size_t nsduOffset;

	if (cache == NULL || cache->payload == NULL) {
		return 1;
	}

	src = (zb_buf_t *)cache->payload;
	buf = zb_buf_allocate();
	if (buf == NULL) {
		aps_txCacheConfirm(cache, APS_STATUS_INTERNAL_BUF_FULL);
		return 1;
	}

	TL_COPY_BUF(buf, src);
	req = (nlde_data_req_t *)buf;
	nsduOffset = (size_t)(req->nsdu - src->buf);
	req->nsdu = buf->buf + nsduOffset;

	if (cache->interPAN) {
		(void)tl_zbNwkInterPanDataReq(buf);
	} else {
		tl_zbNwkNldeDataRequest(buf);
	}

	return 0;
}
void tl_apsDataIndRegister(apsDataIndCb_t cb)
{
	g_apsDataIndCb = cb;
}
void apsCleanToStopSecondClock(void)
{
	if (apsDuplicateCheckFlag != 0U) {
		return;
	}

	if (APS_TX_CACHE_TABLE_SIZE == 0U) {
		secondClockStop();
		return;
	}

	for (u8 i = 0; i < APS_TX_CACHE_TABLE_SIZE; i++) {
		aps_tx_cache_list_t *cache = &aps_txCache_tbl[i];

		if (!cache->used) {
			continue;
		}

		if (cache->ackNeed && cache->state == APX_TX_CACHE_STA_ADDR_REQ) {
			return;
		}

		if (cache->interPAN && cache->state == APX_TX_CACHE_STA_WAITING_ACK) {
			return;
		}
	}

	secondClockStop();
}
int apsDuplicatePeriodic(void *arg)
{
	(void)arg;

	if (apsDuplicateCheckFlag == 0U) {
		return 0;
	}

	if (g_nwkAddrMap.validNum == 0U) {
		apsDuplicateCheckFlag = 0;
		return 0;
	}

	{
		bool anyClock = FALSE;

		u32 usedNum = 0;

		for (u32 i = 0; usedNum < g_nwkAddrMap.validNum; i++) {
			tl_zb_addr_map_entry_t *entry = &g_nwkAddrMap.addrMap[i];

			if (!entry->used) {
				continue;
			}

			usedNum++;

			if (entry->aps_dup_clock != 0U) {
				entry->aps_dup_clock--;
				if (entry->aps_dup_clock != 0U) {
					anyClock = TRUE;
				}
			}
		}

		if (!anyClock) {
			apsDuplicateCheckFlag = 0;
		}
	}

	return 0;
}

u8 aps_duplicate_check(u16 src_addr, u8 aps_counter)
{
	u16 idx;

	/* The vendor library creates the mapping when it is missing:
	 * "10: tmovs r1,#1; 12: tmovs r2,#0; 16: tjl tl_addrByShort". */
	if (tl_addrByShort(src_addr, 1, 0, &idx) != RET_OK) {
		return 0;
	}

	{
		tl_zb_addr_map_entry_t *entry = &g_nwkAddrMap.addrMap[idx];
		u8 duplicate = 0U;

		/* "68: tsubs r2,r2,r5; 6a: tnegs r4,r2; 6c: taddcs r2,r4" is the
		 * TC32 idiom for a logical NOT, so the result is 1 only when the
		 * stored counter equals the received one.  Reading it as the
		 * arithmetic difference inverted the test: every frame with a new
		 * counter was discarded as a duplicate and only real duplicates
		 * got through, which silenced the whole interview after the join.
		 */
		if (entry->aps_dup_clock != 0U) {
			duplicate = (entry->aps_dup_cnt == aps_counter) ? 1U : 0U;
		}

		entry->aps_dup_cnt = aps_counter;
		entry->aps_dup_clock =
			(u8)(((APS_ACK_EXPIRY * APS_MAX_FRAME_RETRIES) + 1U) & 0x07U);
		apsDuplicateCheckFlag = 1;
		return duplicate;
	}
}
static void aps_data_indication_process(void *arg)
{
	u8 *buf = (u8 *)arg;
	aps_rx_hdr_t *hdr = aps_rx_hdr(buf);
	u8 frameType;
	u8 frameCtrl;

	if ((hdr->frameCtrl & APS_FRAME_CTRL_SECURITY) != 0U) {
		if (ss_apsDecryptFrame(arg) != RET_OK) {
			zb_buf_free((zb_buf_t *)arg);
			return;
		}
		hdr = aps_rx_hdr(buf);
	}

	if ((hdr->frameCtrl & APS_FRAME_CTRL_EXTENDED_HEADER) != 0U && hdr->extHdr != 0U) {
		nlde_data_ind_t *networkInd = (nlde_data_ind_t *)arg;
		u8 *reassembly;
		apsDataFragmentRcvWin_t *win = aps_frag_rcv_win();
		aps_rcv_info_t *slot;
		u16 srcShortAddr = hdr->srcShortAddr;
		u8 apsCounter = hdr->apsCounter;
		u8 extHdr = hdr->extHdr;
		u8 blockNum = hdr->blockNum;
		u16 payloadBytes;

		if ((hdr->frameCtrl & APS_FRAME_CTRL_FRAME_TYPE_MASK) !=
			    APS_FRAME_CTRL_FRAME_TYPE_DATA ||
		    (hdr->frameCtrl & APS_FRAME_CTRL_DELIVERY_MODE_MASK) ==
			    APS_FRAME_CTRL_DELIVERY_MODE_GROUP ||
		    hdr->dstEp == 0xffU) {
			zb_buf_free((zb_buf_t *)arg);
			return;
		}

		T_DBG_fgmt[0]++;
		if (extHdr > 2U) {
			zb_buf_free((zb_buf_t *)arg);
			return;
		}

		if (extHdr == 1U) {
			u16 allocLen;

			payloadBytes = (u16)blockNum * (u16)(networkInd->nsduLen - hdr->hdrLen);
			if (payloadBytes > APS_FRAGMENT_REASSEMBLY_MAX_PAYLOAD) {
				apsDataFragmentRcvWinClear();
				zb_buf_free((zb_buf_t *)arg);
				return;
			}

			allocLen = (u16)(sizeof(aps_fragment_reassembly_t) + payloadBytes);
			reassembly = (payloadBytes == 0U) ? long_ev_buf_get()
							  : ev_buf_allocate(allocLen);
			if (reassembly == NULL) {
				zb_buf_free((zb_buf_t *)arg);
				return;
			}

			aps_fragment_reassembly_t *reassemblyObj;

			memset(reassembly, 0, sizeof(aps_fragment_reassembly_t));
			reassemblyObj = (aps_fragment_reassembly_t *)reassembly;
			aps_frag_rcv_win()->evBuf = (u8 *)reassemblyObj;
			aps_frag_rcv_win()->srcAddr = srcShortAddr;
			aps_frag_rcv_win()->apsCnt = apsCounter;
			aps_frag_rcv_win()->totalBlockNum = blockNum;
			aps_frag_rcv_win()->rcvFragmentlen = 0;
			aps_frag_rcv_win()->blockNumProcessed = 0;
			aps_frag_rcv_win()->curRcvMinBlockNum = 0;
			aps_frag_rcv_win()->waitCnt = 0;
			aps_frag_rcv_win()->used = 1;
			aps_frag_rcv_win()->rcvWinTimeoutEvt = ev_timer_taskPost(
				aps_data_fragment_process_timeout, NULL,
				(u32)(APS_ACK_EXPIRY * APS_FRAGMENT_RCV_TIMEOUT_TICKS_PER_ACK));
		} else if (aps_frag_rcv_win()->used == 0U || aps_frag_rcv_win()->evBuf == NULL) {
			zb_buf_free((zb_buf_t *)arg);
			return;
		} else if (aps_frag_rcv_win()->srcAddr != srcShortAddr ||
			   aps_frag_rcv_win()->apsCnt != apsCounter) {
			T_DBG_fgmt[4]++;
			zb_buf_free((zb_buf_t *)arg);
			return;
		} else {
			T_DBG_fgmtBlk[0] = win->totalBlockNum;
		}

		{
			u8 receivedBlock = (extHdr == 1U) ? 0U : blockNum;
			u8 maxWindow = aps_ib.aps_max_window_size;
			u8 slotIndex;

			if (maxWindow == 0U) {
				zb_buf_free((zb_buf_t *)arg);
				return;
			}

			slot = aps_fragment_rx_slot(win, receivedBlock);
			if (slot == NULL) {
				zb_buf_free((zb_buf_t *)arg);
				return;
			}

			slotIndex = (u8)(receivedBlock % slot->validBufNum);
			if (slot->buf[slotIndex] != NULL) {
				zb_buf_free((zb_buf_t *)arg);
				return;
			}

			slot->buf[slotIndex] = (void *)arg;
			slot->ackBit |= (u8)(1U << slotIndex);
			hdr->blockNum = slot->minBlockNum;
			(void)aps_ack_send(arg, slot->ackBit);
		}

		if ((slot->ackBit & aps_fragment_window_mask(slot->validBufNum)) ==
		    aps_fragment_window_mask(slot->validBufNum)) {
			slot->state = APS_RCV_FRAGMENT_HANDLING;
			if (win->blockNumProcessed + slot->validBufNum >= win->totalBlockNum &&
			    win->rcvWinTimeoutEvt != NULL) {
				ev_timer_taskCancel(&aps_frag_rcv_win()->rcvWinTimeoutEvt);
				win->rcvWinTimeoutEvt = NULL;
			}

			tl_zbTaskPost((tl_zb_callback_t)apsRcvingWindowHandling, slot);
		}
		return;
	}

	if ((hdr->frameCtrl & APS_FRAME_CTRL_ACK_REQUEST) != 0U) {
		if (aps_ack_send(arg, 0) != 0U) {
			zb_buf_free((zb_buf_t *)arg);
			return;
		}
	}

	if (aps_duplicate_check(hdr->srcShortAddr, hdr->apsCounter) != 0U) {
		zb_buf_free((zb_buf_t *)arg);
		return;
	}

	frameCtrl = hdr->frameCtrl;
	frameType = (u8)(frameCtrl & APS_FRAME_CTRL_FRAME_TYPE_MASK);
	aps_indPrimBuild(arg);

	if (g_apsDataIndCb != NULL) {
		g_apsDataIndCb(arg);
	}

	if (frameType == APS_FRAME_CTRL_FRAME_TYPE_COMMAND) {
		aps_command_handle(arg);
		return;
	}

	if (!g_zbNwkCtx.joined) {
		zb_buf_free((zb_buf_t *)arg);
		return;
	}

	if ((frameCtrl & APS_FRAME_CTRL_DELIVERY_MODE_MASK) == APS_FRAME_CTRL_DELIVERY_MODE_GROUP) {
		tl_zbTaskPost((tl_zb_callback_t)aps_process_group_addressed_packet, arg);
		return;
	}

	tl_zbTaskPost(af_aps_data_entry, arg);
}
void aps_interPanDataIndCb(void *arg)
{
	zb_mscp_data_ind_t *macInd = (zb_mscp_data_ind_t *)arg;
	aps_data_ind_t localInd;
	aps_rx_hdr_t *hdr = aps_rx_hdr(arg);
	u8 *apsPayload = macInd->msdu + 2;
	u8 apsPayloadLen = (u8)(macInd->msduLength - 2U);
	u8 hdrLen;
	af_endpoint_descriptor_t *epList;
	u8 epNum;

	dstPanID = macInd->srcPanId;

	memset(&localInd, 0, sizeof(localInd));
	localInd.lqi = macInd->mpduLinkQuality;
	localInd.src_addr_mode = macInd->srcAddr.addrMode;

	if (macInd->srcAddr.addrMode == ADDR_MODE_EXT) {
		ZB_IEEE_ADDR_COPY(localInd.src_ext_addr, macInd->srcAddr.addr.extAddr);
	} else {
		localInd.src_short_addr = macInd->srcAddr.addr.shortAddr;
	}

	if (macInd->dstAddr.addrMode == ADDR_MODE_SHORT) {
		localInd.dst_addr = macInd->dstAddr.addr.shortAddr;
	}

	hdrLen = aps_hdr_parse(apsPayload, hdr);
	hdr->hdrLen = hdrLen;
	if (apsPayloadLen < hdrLen) {
		zb_buf_free((zb_buf_t *)arg);
		return;
	}

	localInd.cluster_id = hdr->clusterId;
	localInd.profile_id = hdr->profileId;
	localInd.asdu = apsPayload + hdrLen;
	localInd.asduLength = (u16)(apsPayloadLen - hdrLen);

	epList = af_epDescriptorGet();
	epNum = af_availableEpNumGet();
	for (u8 i = 0; i < epNum; i++) {
		if (!af_clsuterIdMatched(localInd.cluster_id, epList[i].correspond_simple_desc)) {
			continue;
		}

		localInd.dst_ep = epList[i].ep;
		memcpy(arg, &localInd, sizeof(localInd));
		tl_zbTaskPost(af_aps_data_entry, arg);
		return;
	}

	zb_buf_free((zb_buf_t *)arg);
}
void apsTxEventPost(aps_tx_cache_list_t *cache, aps_tx_cache_evt_e event, u8 status)
{
	(void)status;

	switch (cache->state) {
	case APX_TX_CACHE_STA_TODO:
		if (cache->addrReqNeed) {
			aps_cache_state_set(cache, APX_TX_CACHE_STA_ADDR_REQ);
			return;
		}

		if (event == APS_TX_EVENT_TX_TODO && apsTxDataSendStart(cache) == 0U) {
			cache->apsAckWaitTimeOut = APS_ACK_EXPIRY;
			aps_cache_state_set(cache, APX_TX_CACHE_STA_TXING);
		}
		return;
	case APX_TX_CACHE_STA_ADDR_REQ:
		if (event == APS_TX_EVENT_DONE) {
			aps_txCacheConfirm(cache, status);
			return;
		}

		if (event == APS_TX_EVENT_TX_TODO && apsTxDataSendStart(cache) == 0U) {
			aps_cache_state_set(cache, APX_TX_CACHE_STA_TXING);
		}
		return;
	case APX_TX_CACHE_STA_TXING:
	case APX_TX_CACHE_STA_WAITING_ACK:
	case APX_TX_CACHE_STA_RETRY:
		if (event == APS_TX_EVENT_RETRY) {
			if (cache->ackNeed) {
				u8 retries = cache->retries;

				cache->retries = (u8)(retries - 1U);
				if (retries != 0U) {
					if (apsTxDataSendStart(cache) == 0U) {
						cache->apsAckWaitTimeOut = APS_ACK_EXPIRY;
						aps_cache_state_set(cache, APX_TX_CACHE_STA_RETRY);
					}
					return;
				}
			}

			aps_txCacheConfirm(cache, status);
			return;
		}

		if (event == APS_TX_EVENT_WAITING_ACK) {
			aps_cache_state_set(cache, APX_TX_CACHE_STA_WAITING_ACK);
			return;
		}

		if (event == APS_TX_EVENT_DONE) {
			aps_txCacheConfirm(cache, status);
		}
		return;
	case APX_TX_CACHE_STA_DONE:
		aps_txCacheConfirm(cache, status);
		return;
	default:
		return;
	}
}

static void apsDataFragmentReSend(void)
{
	aps_fragment_tx_win_t *win = aps_frag_tx_win();

	if (!win->used || !win->reSend) {
		return;
	}

	if (win->ackBit == 0xffU) {
		win->reSend = 0U;
		return;
	}

	for (u8 i = 0; i < 8; i++) {
		if (((win->ackBit >> i) & 0x01U) == 0U) {
			aps_tx_cache_list_t *cache = win->list[i];

			if (cache == NULL) {
				continue;
			}

			aps_cache_state_set(cache, APX_TX_CACHE_STA_TODO);
			cache->apsAckWaitTimeOut = APS_ACK_EXPIRY;
			cache->retries = APS_MAX_FRAME_RETRIES;
			apsTxEventPost(cache, APS_TX_EVENT_TX_TODO, APS_STATUS_SUCCESS);
			return;
		}
	}
}

static int aps_data_fragment_delay(void *arg)
{
	(void)arg;

	if (aps_frag_tx_win()->used != 0U) {
		if (aps_frag_tx_win()->reSend != 0U) {
			apsDataFragmentReSend();
		} else {
			tl_zbTaskPost(aps_data_fragment, NULL);
		}
	}

	return -1;
}

static void aps_nwk_addr_req_cb(void *arg)
{
	aps_nwk_addr_rsp_hdr_t *rsp = (aps_nwk_addr_rsp_hdr_t *)arg;
	u16 addrMapIdx;

	if (rsp->status != ZDO_SUCCESS) {
		return;
	}

	(void)tl_zbNwkAddrMapAdd(rsp->shortAddr, rsp->extAddr, &addrMapIdx);

	for (u8 i = 0; i < APS_TX_CACHE_TABLE_SIZE; i++) {
		aps_tx_cache_list_t *cache = &aps_txCache_tbl[i];

		if (!cache->used || !cache->addrReqNeed) {
			continue;
		}
		if (cache->zdpSeqnoAddrReq != rsp->seqNum ||
		    cache->state != APX_TX_CACHE_STA_ADDR_REQ) {
			continue;
		}

		((nlde_data_req_t *)cache->payload)->dstAddr = rsp->shortAddr;
		cache->addrReqNeed = 0;
		aps_cache_state_set(cache, APX_TX_CACHE_STA_TODO);
		apsTxEventPost(cache, APS_TX_EVENT_TX_TODO, APS_STATUS_SUCCESS);
		return;
	}
}

void aps_nwk_data_confirm_cb(void *arg)
{
	nlde_data_cnf_t *cnf = (nlde_data_cnf_t *)arg;
	u8 status = cnf->status;
	u8 handle = cnf->nsduHandle;

	for (u8 i = 0; i < APS_TX_CACHE_TABLE_SIZE; i++) {
		aps_tx_cache_list_t *cache = &aps_txCache_tbl[i];

		if (!cache->used || cache->handler != handle) {
			continue;
		}
		if (cache->state != APX_TX_CACHE_STA_TXING &&
		    cache->state != APX_TX_CACHE_STA_RETRY) {
			continue;
		}

		if (status == MAC_SUCCESS || status == MAC_STA_FRAME_PENDING) {
			if (cache->ackNeed) {
				apsTxEventPost(cache, APS_TX_EVENT_WAITING_ACK, APS_STATUS_SUCCESS);
			} else {
				apsTxEventPost(cache, APS_TX_EVENT_DONE, APS_STATUS_SUCCESS);
			}
		} else if (status == NWK_STATUS_ROUTE_DISCOVERY_FAILED ||
			   status == MAC_STA_TRANSACTION_EXPIRED) {
			apsTxEventPost(cache, APS_TX_EVENT_DONE, status);
		} else {
			apsTxEventPost(cache, APS_TX_EVENT_RETRY, status);
		}
		break;
	}

	zb_buf_free((zb_buf_t *)arg);
}
void aps_nwk_data_indication_cb(void *arg)
{
	nlde_data_ind_t *ind = (nlde_data_ind_t *)arg;
	aps_rx_hdr_t *hdr = aps_rx_hdr(arg);
	u8 hdrLen = aps_hdr_parse(ind->nsdu, hdr);
	u8 frameType;

	hdr->hdrLen = hdrLen;
	hdr->srcShortAddr = ind->srcAddr;
	frameType = (u8)(hdr->frameCtrl & APS_FRAME_CTRL_FRAME_TYPE_MASK);

	if (ind->dstAddrMode == APS_SHORT_GROUPADDR_NOEP) {
		hdr->frameCtrl = (u8)((hdr->frameCtrl & (u8)~APS_FRAME_CTRL_DELIVERY_MODE_MASK) |
				      APS_FRAME_CTRL_DELIVERY_MODE_GROUP);
		COPY_U16TOBUFFER((u8 *)&hdr->dstEp, ind->dstAddr);
	} else if (frameType != APS_FRAME_CTRL_FRAME_TYPE_ACK && ind->nsduLen < hdrLen) {
		zb_buf_free((zb_buf_t *)arg);
		return;
	}

	if (frameType == APS_FRAME_CTRL_FRAME_TYPE_ACK) {
		aps_fragment_tx_win_t *win;

		if (APS_TX_CACHE_TABLE_SIZE == 0U) {
			zb_buf_free((zb_buf_t *)arg);
			return;
		}

		win = aps_frag_tx_win();

		for (u8 i = 0; i < APS_TX_CACHE_TABLE_SIZE; i++) {
			aps_tx_cache_list_t *cache = &aps_txCache_tbl[i];

			if (!cache->used ||
			    aps_cache_state(cache) != APX_TX_CACHE_STA_WAITING_ACK ||
			    cache->apsCount != hdr->apsCounter) {
				continue;
			}

			if (cache->extFrameCtrl == 0U) {
				apsTxEventPost(cache, APS_TX_EVENT_DONE, APS_STATUS_SUCCESS);
				break;
			}

			/* Extended ACKs carry the fragment bitmap in the parser's
			 * trailing byte.  Vendor code only accepts extended ACK
			 * frames here and associates the bitmap with the current
			 * transmission window. */
			{
				u8 firstBlock = win->list[0]->blockNum;
				u8 ackBlock = (firstBlock == win->totalBlockNum) ? 0U : firstBlock;

				if ((hdr->frameCtrl & APS_FRAME_CTRL_EXTENDED_HEADER) == 0U ||
				    hdr->blockNum != ackBlock) {
					continue;
				}

				if (!aps_fragment_ack_window_ready(win)) {
					continue;
				}
			}

			if (hdr->ackBit == 0xffU) {
				win->ackBit = hdr->ackBit;
				apsTxEventPost(cache, APS_TX_EVENT_DONE, APS_STATUS_SUCCESS);
				break;
			}

			cache->state = APX_TX_CACHE_STA_TODO;
			cache->apsAckWaitTimeOut = APS_ACK_EXPIRY;
			cache->retries = APS_MAX_FRAME_RETRIES;
			cache->ackNeed = 0U;
			win->ackBit = hdr->ackBit;
			if (aps_ib.aps_max_window_size < 8U) {
				win->ackBit |= (u8)(0xffU << aps_ib.aps_max_window_size);
			}
			win->reSend = 1U;
			apsDataFragmentReSend();
			break;
		}

		zb_buf_free((zb_buf_t *)arg);
		return;
	}

	if (frameType == APS_FRAME_CTRL_FRAME_TYPE_DATA &&
	    (hdr->frameCtrl & APS_FRAME_CTRL_DELIVERY_MODE_MASK) !=
		    APS_FRAME_CTRL_DELIVERY_MODE_GROUP &&
	    !af_profileMatchedLocal(hdr->profileId, hdr->dstEp)) {
		zb_buf_free((zb_buf_t *)arg);
		return;
	}

	tl_zbTaskPost(aps_data_indication_process, arg);
}
_attribute_no_inline_ u8 apsHandleIsExit(u8 handle)
{
	for (u8 i = 0; i < APS_TX_CACHE_TABLE_SIZE; i++) {
		aps_tx_cache_list_t *cache = &aps_txCache_tbl[i];

		if (cache->used && cache->handler == handle) {
			return 1;
		}
	}

	return 0;
}

aps_tx_cache_list_t *apsTxDataPost(bool ackNeed, bool addrReqNeed, bool interPan, u8 *payload,
				   apsdeDataConf_t *pCnf)
{
	aps_confirm_buf_t *cnf = (aps_confirm_buf_t *)pCnf;
	aps_confirm_buf_t *confirm = cnf;
	aps_tx_cache_list_t *freeEntry = NULL;

	for (u8 i = 0; i < APS_TX_CACHE_TABLE_SIZE; i++) {
		aps_tx_cache_list_t *cache = &aps_txCache_tbl[i];

		if (cache->used) {
			if (ZB_IEEE_ADDR_CMP(cache->dstAddr.addr_long, cnf) &&
			    aps_cache_state(cache) == APX_TX_CACHE_STA_ADDR_REQ) {
				confirm->status = APS_STATUS_SHORT_ADDR_REQUESTING;
				return NULL;
			}

			if (cache->handler == confirm->handle) {
				confirm->status = APS_STATUS_HANDLE_BACKING;
				return NULL;
			}
		} else if (freeEntry == NULL) {
			freeEntry = cache;
		}
	}

	if (freeEntry == NULL) {
		confirm->status = APS_STATUS_INTERNAL_BUF_FULL;
		return NULL;
	}

	memset(freeEntry, 0, sizeof(*freeEntry));
	freeEntry->used = 1;
	freeEntry->payload = payload;
	freeEntry->dstEndpoint = confirm->ep.af.dstEndpoint;
	ZB_IEEE_ADDR_COPY(freeEntry->dstAddr.addr_long, cnf);
	freeEntry->ep = confirm->ep.af.srcEndpoint;
	freeEntry->dstAddrMode = confirm->ep.af.dstAddrMode;
	freeEntry->handler = confirm->handle;
	freeEntry->apsCount = confirm->apsCnt;
	freeEntry->clusterId = confirm->clusterId;
	freeEntry->payload = payload;
	freeEntry->addrReqNeed = addrReqNeed ? 1U : 0U;
	freeEntry->ackNeed = ackNeed ? 1U : 0U;
	freeEntry->interPAN = interPan ? 1U : 0U;
	freeEntry->retries = APS_MAX_FRAME_RETRIES;
	freeEntry->apsAckWaitTimeOut = APS_ACK_EXPIRY;
	freeEntry->apsAddrWaitTimeout = (s8)((APS_ACK_EXPIRY + 1U) * APS_MAX_FRAME_RETRIES);
	g_apsTxCacheNum++;

	return freeEntry;
}
void aps_data_request(void *arg)
{
	aps_data_req_t *req = (aps_data_req_t *)arg;
	nlde_data_req_t nlde;
	aps_confirm_buf_t cnf;
	aps_tx_cache_list_t *cache;
	addrExt_t *extAddr = NULL;
	u8 auxHdr[16] = {0};
	u8 *asdu;
	u8 *nsdu;
	u8 frameCtrl;
	u8 baseLen;
	u8 headerLen;
	u8 auxLen = 0;
	u8 nsduLen;
	u8 ackNeed;
	u8 addrReqNeed = 0;
	u8 interPan;
	u8 extFrameCtrl;
	u8 blockNum;
	u16 dstAddr = 0;
	u16 addrMapIdx = 0;
	addrExt_t requestedExt = {0};

	if (req == NULL || req->asdu == NULL) {
		return;
	}

	asdu = req->asdu;
	extFrameCtrl = req->extFrameCtrl;
	blockNum = req->blockNum;
	if (req->dst_addr_mode == APS_LONG_DSTADDR_WITHEP) {
		ZB_IEEE_ADDR_COPY(requestedExt, req->aps_addr.dst_ext_addr);
	}
	memset(&cnf, 0, sizeof(cnf));
	cnf.handle = req->handle;
	cnf.apsCnt = req->apsCnt;
	cnf.ep.af.dstAddrMode = req->dst_addr_mode;
	cnf.ep.af.srcEndpoint = req->src_endpoint;
	cnf.clusterId = req->cluster_id;

	/* The vendor treats every mode other than group, short, and long
	 * addressing as the binding form.  This includes reserved mode values. */
	if (req->dst_addr_mode != APS_SHORT_GROUPADDR_NOEP &&
	    req->dst_addr_mode != APS_SHORT_DSTADDR_WITHEP &&
	    req->dst_addr_mode != APS_LONG_DSTADDR_WITHEP) {
		bind_dst_list_tbl *bindList;
		u16 bindSize =
			(u16)(8U + ((u16)APS_BINDING_TABLE_SIZE * (u16)sizeof(bind_dst_list)));
		aps_status_t status;

		if (req->extFrameCtrl != 0U) {
			sys_exceptionPost(0x5d5, SYS_EXCEPTTION_ZB_APS_PARAM);
		}

		bindList = (bind_dst_list_tbl *)ev_buf_allocate(bindSize);
		if (bindList == NULL) {
			cnf.status = APS_STATUS_INTERNAL_BUF_FULL;
			aps_conf(&cnf);
			zb_buf_free((zb_buf_t *)arg);
			return;
		}

		status = aps_search_dst_from_bind_tbl(req, bindList);
		if (status != APS_STATUS_SUCCESS) {
			cnf.status = status;
			ev_buf_free((u8 *)bindList);
			aps_conf(&cnf);
			zb_buf_free((zb_buf_t *)arg);
			return;
		}

		bindList->txData = (u8 *)req;
		ev_timer_taskPost((ev_timer_callback_t)bindingTxBack, bindList, 1);
		return;
	}

	if (req->dst_addr_mode == APS_SHORT_GROUPADDR_NOEP) {
		dstAddr = req->aps_addr.dst_group_addr;
		cnf.dstAddr.addr_short = dstAddr;
		cnf.ep.af.dstAddrMode = APS_SHORT_GROUPADDR_NOEP;
		frameCtrl = APS_FRAME_CTRL_DELIVERY_MODE_GROUP;
		baseLen = 9U;
	} else if (req->dst_addr_mode == APS_SHORT_DSTADDR_WITHEP) {
		dstAddr = req->aps_addr.dst_short_addr;
		cnf.ep.af.dstEndpoint = req->aps_addr.dst_endpoint;
		cnf.dstAddr.addr_short = dstAddr;
		if (ZB_NWK_IS_ADDRESS_BROADCAST(dstAddr)) {
			frameCtrl = APS_FRAME_CTRL_DELIVERY_MODE_BROADCAST;
			baseLen = 8U;
		} else {
			frameCtrl = 0;
			baseLen = 8U;
		}
	} else {
		cnf.ep.af.dstEndpoint = req->aps_addr.dst_endpoint;
		ZB_IEEE_ADDR_COPY(cnf.dstAddr.addr_long, req->aps_addr.dst_ext_addr);
		extAddr = (addrExt_t *)requestedExt;

		if (tl_zbShortAddrByExtAddr(&dstAddr, requestedExt, &addrMapIdx) == 0xffU ||
		    ZB_NWK_IS_ADDRESS_BROADCAST(dstAddr)) {
			/* Keep the extended destination in the NWK request.  The vendor
			 * also does this when the map resolves to a broadcast address. */
			addrReqNeed = 1U;
		} else {
			addrReqNeed = 0U;
			extAddr = NULL;
		}

		frameCtrl = 0;
		baseLen = 8U;
	}

	interPan = (u8)((req->tx_options & APS_TX_OPT_INTRA_PAN) != 0U);
	ackNeed = (u8)((req->tx_options & APS_TX_OPT_ACK_TX) != 0U);

	memset(&nlde, 0, sizeof(nlde));
	nlde.radius = req->radius;
	nlde.addrMode = APS_SHORT_DSTADDR_WITHEP;
	nlde.nonmemberRadius = aps_ib.aps_nonmember_radius;
	nlde.discoverRoute = g_zbNwkCtx.discoverRoute;
	nlde.securityEnable = req->enableNWKsecurity;
	nlde.ndsuHandle = req->handle;
	nlde.useAlias = req->useAlias;
	nlde.aliasSrcAddr = req->aliasSrcAddr;
	nlde.aliasSeqNum = req->aliasSeqNum;
	nlde.unicastSkipRouting = (u8)((req->tx_options & APS_TX_OPT_UNICAST_SKIP_ROUTING) != 0U);

	if (req->dst_addr_mode == APS_SHORT_GROUPADDR_NOEP) {
		nlde.addrMode = APS_SHORT_GROUPADDR_NOEP;
		if (g_zbNIB.useMulticast == 0U) {
			nlde.dstAddr = GROUP_MESSAGE_SEND_ADDRESS;
		} else {
			nlde.dstAddr = dstAddr;
		}
	} else if (ZB_NWK_IS_ADDRESS_BROADCAST(dstAddr)) {
		nlde.dstAddr = dstAddr;
	} else {
		nlde.dstAddr = dstAddr;
		if (addrReqNeed != 0U) {
			ZB_IEEE_ADDR_COPY(nlde.ieeAddr, requestedExt);
		}
	}

	if ((req->tx_options & APS_TX_OPT_ACK_TX) != 0U) {
		frameCtrl |= APS_FRAME_CTRL_ACK_REQUEST;
	}

	/* Inter-PAN frames use the compact five-byte APS header. */
	if (interPan != 0U) {
		nsdu = asdu - 5;
		nsdu[0] = frameCtrl;
		COPY_U16TOBUFFER(nsdu + 1, req->cluster_id);
		COPY_U16TOBUFFER(nsdu + 3, req->profile_id);
		nsduLen = (u8)(req->asdu_length + 5U);
		nlde.nsdu = nsdu;
		nlde.nsduLen = nsduLen;
		memcpy(arg, &nlde, sizeof(nlde));

		cache = apsTxDataPost(ackNeed, addrReqNeed, interPan, arg, (apsdeDataConf_t *)&cnf);
		if (cache == NULL) {
			aps_conf(&cnf);
			zb_buf_free((zb_buf_t *)arg);
			return;
		}
		apsTxEventPost(cache, APS_TX_EVENT_TX_TODO, APS_STATUS_SUCCESS);
		if (addrReqNeed != 0U) {
			aps_txCacheAsNoShortAddr(requestedExt, &cache->zdpSeqnoAddrReq);
		}
		return;
	}

	if (req->tx_options & APS_TX_OPT_SECURITY_ENABLED) {
		frameCtrl |= APS_FRAME_CTRL_SECURITY;
		auxLen = ss_apsEnAuxHdrFill(auxHdr, NULL, req->tx_options);
	}
	if (req->extFrameCtrl != 0U) {
		frameCtrl |= APS_FRAME_CTRL_EXTENDED_HEADER;
	}

	headerLen = (u8)(baseLen + (extFrameCtrl != 0U ? 2U : 0U));
	nsdu = asdu - headerLen - auxLen;
	nsdu[0] = frameCtrl;
	if ((frameCtrl & APS_FRAME_CTRL_DELIVERY_MODE_MASK) == APS_FRAME_CTRL_DELIVERY_MODE_GROUP) {
		COPY_U16TOBUFFER(nsdu + 1, dstAddr);
		COPY_U16TOBUFFER(nsdu + 3, req->cluster_id);
		COPY_U16TOBUFFER(nsdu + 5, req->profile_id);
		nsdu[7] = req->src_endpoint;
		nsdu[8] = req->apsCnt;
	} else {
		nsdu[1] = req->aps_addr.dst_endpoint;
		COPY_U16TOBUFFER(nsdu + 2, req->cluster_id);
		COPY_U16TOBUFFER(nsdu + 4, req->profile_id);
		nsdu[6] = req->src_endpoint;
		nsdu[7] = req->apsCnt;
	}

	if (req->extFrameCtrl != 0U) {
		nsdu[baseLen] = extFrameCtrl;
		nsdu[baseLen + 1U] = blockNum;
	}
	if (auxLen != 0U) {
		memcpy(nsdu + headerLen, auxHdr, auxLen);
	}

	nlde.nsdu = nsdu;
	nlde.nsduLen = (u8)(req->asdu_length + headerLen + auxLen);
	memcpy(arg, &nlde, sizeof(nlde));

	if ((frameCtrl & APS_FRAME_CTRL_SECURITY) != 0U) {
		if (extAddr == NULL) {
			extAddr = tl_zbExtAddrPtrByShortAddr(dstAddr);
			if (extAddr == NULL) {
				cnf.status = APS_STATUS_SECURITY_FAIL;
				aps_conf(&cnf);
				zb_buf_free((zb_buf_t *)arg);
				return;
			}
		}

		if (ss_apsSecureFrame(arg, (u8)(headerLen + auxLen), headerLen, *extAddr) ==
		    RET_ERROR) {
			if (extFrameCtrl != 0U) {
				tl_zbTaskPost(aps_data_fragment, NULL);
				return;
			}
			cnf.status = APS_STATUS_SECURITY_FAIL;
			aps_conf(&cnf);
			zb_buf_free((zb_buf_t *)arg);
			return;
		}
	}

	cache = apsTxDataPost(ackNeed, addrReqNeed, interPan, arg, (apsdeDataConf_t *)&cnf);
	if (cache == NULL) {
		if (extFrameCtrl != 0U) {
			tl_zbTaskPost(aps_data_fragment, NULL);
			return;
		}
		aps_conf(&cnf);
		zb_buf_free((zb_buf_t *)arg);
		return;
	}

	if (extFrameCtrl != 0U) {
		aps_fragment_tx_win_t *win = aps_frag_tx_win();

		cache->extFrameCtrl = extFrameCtrl;
		cache->blockNum = blockNum;
		if (win->activeListNum < 8U) {
			win->list[win->activeListNum] = cache;
			win->activeListNum++;
		}
	}

	apsTxEventPost(cache, APS_TX_EVENT_TX_TODO, APS_STATUS_SUCCESS);
	if (addrReqNeed != 0U) {
		aps_txCacheAsNoShortAddr(requestedExt, &cache->zdpSeqnoAddrReq);
	}
}
static void bindingTxBack(void *arg)
{
	bind_dst_list_tbl *bindList = (bind_dst_list_tbl *)arg;
	zb_buf_t *clone;
	zb_buf_t *srcBuf;
	aps_data_req_t *srcReq;
	aps_data_req_t *dstReq;

	if (bindList->txData == NULL) {
		return;
	}

	clone = zb_buf_allocate();
	if (clone == NULL) {
		return;
	}

	TL_COPY_BUF(clone, bindList->txData);
	srcBuf = (zb_buf_t *)bindList->txData;
	srcReq = (aps_data_req_t *)bindList->txData;
	dstReq = (aps_data_req_t *)clone;
	if (srcReq->asdu != NULL) {
		dstReq->asdu = clone->buf + (srcReq->asdu - srcBuf->buf);
	}

	if (bindList->txCnt >= bindList->totalCnt) {
		zb_buf_free(clone);
		zb_buf_free((zb_buf_t *)bindList->txData);
		ev_buf_free((u8 *)bindList);
		return;
	}

	{
		bind_dst_list *dst = &bindList->list[bindList->txCnt];

		dstReq->dst_addr_mode = dst->dst_addr_mode;
		if (dst->dst_addr_mode == APS_LONG_DSTADDR_WITHEP) {
			dstReq->aps_addr = dst->aps_addr;
		} else if (dst->dst_addr_mode == APS_SHORT_GROUPADDR_NOEP) {
			dstReq->aps_addr.dst_group_addr = dst->aps_addr.dst_group_addr;
		}

		bindList->txCnt++;
		if (bindList->txCnt > 1U) {
			dstReq->apsCnt = aps_get_counter_value();
			do {
				dstReq->handle = aps_get_handle();
			} while (apsHandleIsExit(dstReq->handle));
		}

		if (dst->dst_addr_mode == APS_LONG_DSTADDR_WITHEP &&
		    ZB_IEEE_ADDR_CMP(dst->aps_addr.dst_ext_addr, g_zbInfo.macPib.extAddress)) {
			aps_data_ind_t localInd;

			memset(&localInd, 0, sizeof(localInd));
			localInd.dst_addr_mode = APS_SHORT_DSTADDR_WITHEP;
			localInd.dst_ep = dst->aps_addr.dst_endpoint;
			localInd.dst_addr = g_zbNIB.nwkAddr;
			localInd.src_addr_mode = APS_SHORT_SRCADDR_WITHEP;
			localInd.src_ep = dstReq->src_endpoint;
			localInd.profile_id = dstReq->profile_id;
			localInd.cluster_id = dstReq->cluster_id;
			localInd.asduLength = dstReq->asdu_length;
			localInd.asdu = dstReq->asdu;
			localInd.src_short_addr =
				dstReq->useAlias ? dstReq->aliasSrcAddr : localInd.dst_addr;
			localInd.aps_counter = dstReq->apsCnt;
			memcpy(clone, &localInd, sizeof(localInd));
			tl_zbTaskPost(af_aps_data_entry, clone);
		} else {
			aps_data_request(clone);
		}
	}

	if (bindList->txCnt >= bindList->totalCnt) {
		zb_buf_free((zb_buf_t *)bindList->txData);
		ev_buf_free((u8 *)bindList);
	}
}
int apsAckPeriodic(void *arg)
{
	(void)arg;

	for (u8 i = 0; i < APS_TX_CACHE_TABLE_SIZE; i++) {
		aps_tx_cache_list_t *cache = &aps_txCache_tbl[i];

		if (!cache->used) {
			continue;
		}

		if (cache->state == APX_TX_CACHE_STA_WAITING_ACK) {
			cache->apsAckWaitTimeOut--;
			if (cache->apsAckWaitTimeOut <= 0) {
				apsTxEventPost(cache, APS_TX_EVENT_RETRY, APS_STATUS_NO_ACK);
			}
		} else if (cache->state == APX_TX_CACHE_STA_ADDR_REQ) {
			cache->apsAddrWaitTimeout--;
			if (cache->apsAddrWaitTimeout <= 0) {
				apsTxEventPost(cache, APS_TX_EVENT_DONE,
					       APS_STATUS_NO_SHORT_ADDRESS);
			}
		}
	}

	return 0;
}
void aps_cmd_send(void *arg, u8 handle)
{
	aps_cmd_send_req_t *req = (aps_cmd_send_req_t *)arg;
	aps_confirm_buf_t cnf;
	nlde_data_req_t *nldereq;
	aps_tx_cache_list_t *cache;
	u8 apsCounter;
	u8 *nsdu;
	u8 apsHdr[2];
	u8 auxHdr[16] = {0};
	u8 auxLen = 0;
	u8 addrReqNeed = 0;
	u8 discoverRoute;
	u8 nwkRadius = 0;
	addrExt_t *extAddrPtr = NULL;
	u16 dstShortAddr = 0;
	u16 idx = 0;

	memset(&cnf, 0, sizeof(cnf));

	if (req == NULL || req->txBuf == NULL || req->adu == NULL) {
		return;
	}

	apsCounter = aps_get_counter_value();
	discoverRoute = g_zbNwkCtx.discoverRoute;
	cnf.handle = handle;
	cnf.apsCnt = apsCounter;
	cnf.ep.af.dstAddrMode = req->addrMode;

	if (req->addrMode == APS_SHORT_DSTADDR_WITHEP) {
		cnf.dstAddr.addr_short = req->dstAddr.shortAddr;
		dstShortAddr = req->dstAddr.shortAddr;
	} else if (req->addrMode == APS_LONG_DSTADDR_WITHEP) {
		ZB_IEEE_ADDR_COPY(cnf.dstAddr.addr_long, req->dstAddr.extAddr);
		if (tl_zbShortAddrByExtAddr(&dstShortAddr, req->dstAddr.extAddr, &idx) != RET_OK) {
			addrReqNeed = 1;
		}
	} else {
		cnf.status = APS_STATUS_INVALID_PARAMETER;
		aps_conf(&cnf);
		zb_buf_free(req->txBuf);
		return;
	}

	memset(req->txBuf, 0, sizeof(nlde_data_req_t));
	nldereq = (nlde_data_req_t *)req->txBuf;
	if (req->aduLen > 1U && req->adu[0] == APS_CMD_TRANSPORT_KEY_ID && req->adu[1] == 1U) {
		/* The vendor suppresses route discovery while transporting a
		 * network-key command and evaluates the security mode on this path. */
		discoverRoute = 0U;
		if (!ss_securityModeIsDistributed() && handle != APS_CMD_HANDLE_CMD_TUNNEL &&
		    (req->addrMode != APS_SHORT_DSTADDR_WITHEP ||
		     !ZB_NWK_IS_ADDRESS_BROADCAST(req->dstAddr.shortAddr))) {
			nwkRadius = 1U;
		}
	}
	nldereq->radius = nwkRadius;
	nldereq->addrMode = APS_SHORT_DSTADDR_WITHEP;
	nldereq->discoverRoute = discoverRoute;
	nldereq->securityEnable = req->secureNwkLayer;
	nldereq->ndsuHandle = handle;
	nldereq->dstAddr = dstShortAddr;
	if (req->addrMode == APS_LONG_DSTADDR_WITHEP) {
		ZB_IEEE_ADDR_COPY(nldereq->ieeAddr, req->dstAddr.extAddr);
	}

	apsHdr[0] = req->reserved ? (APS_FRAME_CTRL_ACK_REQUEST | APS_FRAME_CTRL_FRAME_TYPE_COMMAND)
				  : APS_FRAME_CTRL_FRAME_TYPE_COMMAND;
	apsHdr[1] = apsCounter;

	if (req->secure || handle != APS_CMD_HANDLE_CMD_TUNNEL) {
		auxLen = ss_apsEnAuxHdrFill(auxHdr, req->adu, 0);
	}

	nsdu = req->adu - (2 + auxLen);
	memcpy(nsdu, apsHdr, sizeof(apsHdr));
	if (auxLen != 0U) {
		memcpy(nsdu + 2, auxHdr, auxLen);
	}
	nldereq->nsdu = nsdu;
	nldereq->nsduLen = (u8)(req->aduLen + 2 + auxLen);

	if (auxLen != 0U) {
		if (req->addrMode == APS_SHORT_DSTADDR_WITHEP) {
			extAddrPtr = tl_zbExtAddrPtrByShortAddr(req->dstAddr.shortAddr);
			if (extAddrPtr == NULL) {
				cnf.status = APS_STATUS_SECURITY_FAIL;
				aps_conf(&cnf);
				zb_buf_free(req->txBuf);
				return;
			}
		} else {
			extAddrPtr = &req->dstAddr.extAddr;
		}

		if (ss_apsSecureFrame(nldereq, (u8)(2 + auxLen), 2, *extAddrPtr) == RET_ERROR) {
			cnf.status = APS_STATUS_SECURITY_FAIL;
			aps_conf(&cnf);
			zb_buf_free(req->txBuf);
			return;
		}
	}

	cache = apsTxDataPost((nsdu[0] & APS_FRAME_CTRL_ACK_REQUEST) ? 1U : 0U, addrReqNeed, 0,
			      (u8 *)req->txBuf, (apsdeDataConf_t *)&cnf);
	if (cache == NULL) {
		aps_conf(&cnf);
		zb_buf_free(req->txBuf);
		return;
	}

	if (addrReqNeed) {
		aps_txCacheAsNoShortAddr(req->dstAddr.extAddr, &cache->zdpSeqnoAddrReq);
		aps_cache_state_set(cache, APX_TX_CACHE_STA_ADDR_REQ);
		return;
	}

	apsTxEventPost(cache, APS_TX_EVENT_TX_TODO, APS_STATUS_SUCCESS);
}
u8 apsDataRequest(aps_data_req_t *dataReq, u8 *asdu, u8 length)
{
	zb_buf_t *buf = zb_buf_allocate();
	aps_data_req_t *reqCopy;
	u8 *asduCopy;
	size_t reqOffset;

	if (buf == NULL) {
		return 0x39;
	}

	memset(buf, 0, sizeof(*reqCopy));
	TL_BUF_INITIAL_ALLOC(buf, length, asduCopy, u8 *);
	if (asduCopy < buf->buf) {
		zb_buf_free(buf);
		return 6;
	}

	reqOffset = (size_t)(asduCopy - buf->buf);
	if (reqOffset <= sizeof(*reqCopy)) {
		zb_buf_free(buf);
		return 6;
	}

	if (dataReq->profile_id == ZDO_PROFILE_ID && dataReq->cluster_id == DEVICE_ANNCE_CLID) {
		dataReq->handle = APS_CMD_HANDLE_DEVICE_ANNOUNCE;
	} else {
		do {
			dataReq->handle = aps_get_handle();
		} while (apsHandleIsExit(dataReq->handle));
	}

	memcpy(asduCopy, asdu, length);
	memcpy(buf, dataReq, sizeof(*reqCopy));
	reqCopy = (aps_data_req_t *)buf;
	reqCopy->asdu = asduCopy;
	reqCopy->asdu_length = length;

	tl_zbTaskPost(aps_data_request, buf);
	return 0;
}
static void aps_data_fragment(void *arg)
{
	(void)arg;

	aps_fragment_tx_win_t *win = aps_frag_tx_win();
	aps_data_req_t *req;
	u16 totalLen;
	u16 offset;
	u8 fragLen;

	if (win->used == 0U) {
		return;
	}

	req = (aps_data_req_t *)win->req;
	if (req == NULL) {
		ZB_EXCEPTION_POST(SYS_EXCEPTTION_ZB_APS_FRAGMENT_TRANS);
		return;
	}

	if (win->curBlockNum != 0U) {
		req->extFrameCtrl = 2;
		req->blockNum = win->curBlockNum;
	} else {
		req->extFrameCtrl = 1;
		req->blockNum = win->totalBlockNum;
	}

	if (win->activeListNum >= (u8)(aps_ib.aps_max_window_size - 1U)) {
		req->tx_options |= BIT(2);
	} else {
		req->tx_options &= (u8)~BIT(2);
	}

	totalLen = win->payloadLen;
	offset = (u16)(win->curBlockNum * aps_ib.aps_fragment_payload_size);
	if (offset >= totalLen) {
		ZB_EXCEPTION_POST(SYS_EXCEPTTION_ZB_APS_FRAGMENT_TRANS);
		return;
	}

	fragLen = aps_ib.aps_fragment_payload_size;
	if (win->curBlockNum >= (u8)(win->totalBlockNum - 1U)) {
		fragLen = (u8)(totalLen - offset);
		req->tx_options |= BIT(2);
		if (aps_ib.aps_fragment_payload_size < fragLen) {
			ZB_EXCEPTION_POST(SYS_EXCEPTTION_ZB_APS_FRAGMENT_TRANS);
		}
	}

	if (apsDataRequest(req, (u8 *)win->payload + offset, fragLen) != 0U) {
		u8 delay = aps_ib.aps_interframe_delay;

		if (delay == 0U) {
			delay = 100U;
		}
		ev_timer_taskPost(aps_data_fragment_delay, NULL, delay);
	}
}

u8 apsDataFragmentRequest(aps_data_req_t *dataReq, u8 *asdu, u16 length)
{
	aps_fragment_tx_win_t *win = aps_frag_tx_win();
	u8 *payloadCopy;
	aps_data_req_t *reqCopy;
	u8 fragPayload;

	if (win->used != 0U || win->req != NULL) {
		return 4;
	}

	payloadCopy = ev_buf_allocate(length);
	if (payloadCopy == NULL) {
		return 10;
	}

	reqCopy = (aps_data_req_t *)ev_buf_allocate(sizeof(aps_data_req_t));
	if (reqCopy == NULL) {
		ev_buf_free(payloadCopy);
		return 10;
	}

	memset(reqCopy, 0, sizeof(*reqCopy));
	memcpy(reqCopy, dataReq, sizeof(*reqCopy));
	memcpy(payloadCopy, asdu, length);

	memset(win, 0, sizeof(*win));
	win->used = 1;
	win->reSend = 0;
	win->ackBit = 0xffU;
	win->req = (u8 *)reqCopy;
	win->payload = payloadCopy;
	win->payloadLen = length;

	fragPayload = aps_ib.aps_fragment_payload_size;
	win->totalBlockNum = (u8)((length + fragPayload - 1U) / fragPayload);
	win->curBlockNum = 0;

	tl_zbTaskPost(aps_data_fragment, NULL);
	return 0;
}
