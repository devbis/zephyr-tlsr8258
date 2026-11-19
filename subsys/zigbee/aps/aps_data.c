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

uint16_t dstPanID = 0;
uint8_t g_apsTxCacheNum = 0;
static apsDataIndCb_t g_apsDataIndCb;
uint8_t T_DBG_fgmt[16] = {0};
uint8_t apsDuplicateCheckFlag = 0;
uint8_t T_DBG_fgmtCnf[8] = {0};
uint8_t T_DBG_fgmtBlk[8] = {0};
uint8_t T_DBG_fgmtTout = 0;

enum {
	APS_FRAGMENT_RCV_TIMEOUT_MS_PER_ACK = 1000U,
	APS_FRAGMENT_REASSEMBLY_MAX_PAYLOAD = 504U,
	APS_BINDING_TX_INTERVAL_MS = 100,
};

typedef struct _attribute_packed_ {
	uint8_t seqNum;
	uint8_t status;
	addrExt_t extAddr;
	uint16_t shortAddr;
} aps_nwk_addr_rsp_hdr_t;

typedef apsDataFragmentTransWin_t aps_fragment_tx_win_t;

typedef struct _attribute_packed_ {
	union {
		uint16_t addr_short;
		addrExt_t addr_long;
	} dstAddr;
	union {
		struct _attribute_packed_ {
			uint8_t dstAddrMode;
			uint8_t dstEndpoint;
			uint8_t srcEndpoint;
		} af;
		struct _attribute_packed_ {
			uint8_t dstEndpoint;
			uint8_t srcEndpoint;
			uint8_t dstAddrMode;
		} extConfirm;
	} ep;
	uint8_t status;
	uint8_t pad[4]; /* unused bytes between status and handle */
	uint8_t handle;
	uint8_t apsCnt;
	uint16_t clusterId;
} aps_confirm_buf_t;

/* Local packed overlay for fragment reassembly. */
typedef struct _attribute_packed_ {
	aps_data_ind_t ind;
	uint16_t copiedLen;
	uint8_t asdu[];
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
static int bindingTxBack(void *arg);

static inline aps_fragment_tx_win_t *aps_frag_tx_win(void)
{
	return &g_apsDataFragmentTransWin;
}

static inline apsDataFragmentRcvWin_t *aps_frag_rcv_win(void)
{
	return &g_apsDataFragmentRcvWin;
}

static inline bool aps_fragment_ack_window_ready(const aps_fragment_tx_win_t *win)
{
	return win->activeListNum == aps_ib.aps_max_window_size ||
	       (uint8_t)(win->curBlockNum + 1U) == win->totalBlockNum;
}

static inline uint8_t aps_cache_state(const aps_tx_cache_list_t *cache)
{
	return cache->state;
}

static inline void aps_cache_state_set(aps_tx_cache_list_t *cache, uint8_t state)
{
	cache->state = state;
}

static inline aps_rx_hdr_t *aps_rx_hdr(void *arg)
{
	return (aps_rx_hdr_t *)((uint8_t *)arg + sizeof(nlde_data_ind_t));
}

static void aps_indPrimBuild(void *arg)
{
	uint8_t *buf = (uint8_t *)arg;
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
	out->src_addr_mode = APS_SHORT_SRCADDR_WITHEP;
	out->src_short_addr = in.srcAddr;
	out->srcMacAddr = in.srcMacAddr;
	out->asdu = in.nsdu + hdr.hdrLen;
	out->asduLength = (uint16_t)(in.nsduLen - hdr.hdrLen);
	/* Deliberate deviation from the vendor object: it never fills rx_tick. */
	out->rx_tick = in.rxTime;
	out->lqi = in.lqi;
	out->rssi = ((zb_buf_t *)arg)->hdr.rssi;
	out->aps_counter = hdr.apsCounter;

	if ((hdr.frameCtrl & APS_FRAME_CTRL_DELIVERY_MODE_MASK) ==
	    APS_FRAME_CTRL_DELIVERY_MODE_GROUP) {
		out->dst_addr_mode = APS_SHORT_GROUPADDR_NOEP;
		/* The group address is the one of the APS header. */
		out->dst_addr = (uint16_t)(hdr.dstEp | ((uint16_t)hdr.dstAddrHi << 8));
	} else {
		out->dst_addr_mode = APS_SHORT_DSTADDR_WITHEP;
		out->dst_addr = in.dstAddr;
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
		uint8_t *cnf = ev_buf_allocate(sizeof(aps_confirm_buf_t));

		if (cnf == NULL) {
			return;
		}

		memset(cnf, 0, sizeof(aps_confirm_buf_t));
		memcpy(cnf, buf, sizeof(aps_confirm_buf_t));
		zb_TaskPost(af_dataCnfHandler, cnf);
		return;
	}

#if defined(ZB_COORDINATOR_ROLE)
	if (buf->status != APS_STATUS_SUCCESS) {
		return;
	}

	if (buf->handle == APS_CMD_HANDLE_SWITCH_KEY) {
		ss_tcSwitchKey((uint8_t)(ss_ib.activeKeySeqNum + 1U));
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
		uint16_t idx = 0;
		uint16_t shortAddr = 0;

		if (zb_ShortAddrByExtAddr(&shortAddr, g_zbInfo.macPib.extAddress, &idx) ==
		    RET_OK) {
			uint16_t localShort = g_zbInfo.macPib.shortAddress;

			if (shortAddr != localShort) {
				zb_NwkAddrMapAdd(localShort, g_zbInfo.macPib.extAddress, &idx);
				zb_info_save(NULL);
			}
		}
	}

	if (af_nodeDescStackRevisionGet() > APS_STACK_REVISION_R20) {
		ev_timer_taskPost((ev_timer_callback_t)endDevTimeoutReq, NULL,
				  APS_ED_TIMEOUT_REQ_DELAY_MS);
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
static void aps_txCacheAsNoShortAddr(addrExt_t extAddr, uint8_t *seqNo)
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

	for (uint8_t i = 0; i < APS_FRAG_RCV_SLOTS; i++) {
		aps_rcv_info_t *slot = &aps_frag_rcv_win()->rcvInfo[i];

		if (slot->state != APS_RCV_FRAGMENT_IDLE && slot->validBufNum != 0U) {
			for (uint8_t j = 0; j < slot->validBufNum; j++) {
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
		for (uint8_t i = 0; i < slot->validBufNum; i++) {
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
				uint16_t offset = reassemblyObj->copiedLen;
				aps_data_ind_t *fragInd = (aps_data_ind_t *)frag;
				uint16_t fragLen = fragInd->asduLength;
				uint8_t *asdu = fragInd->asdu;

				memcpy(reassemblyObj->asdu + offset, asdu, fragLen);
				offset = (uint16_t)(offset + fragLen);
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
		zb_TaskPost(af_aps_data_fragment_entry, reassemblyObj);
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
static uint8_t aps_ack_send(const aps_rx_hdr_t *hdr, uint8_t securityUse, uint8_t blockAck)
{
	zb_buf_t *buf = zb_buf_allocate();
	nlde_data_req_t *req;
	uint8_t auxHdr[8] = {0};
	uint8_t *payload;
	uint8_t payloadLen;
	uint8_t baseLen;
	uint8_t fc;

	if (buf == NULL) {
		return APS_STATUS_INTERNAL_BUF_FULL;
	}

	if ((hdr->frameCtrl & APS_FRAME_CTRL_FRAME_TYPE_MASK) ==
	    APS_FRAME_CTRL_FRAME_TYPE_COMMAND) {
		fc = APS_FRAME_CTRL_COMMAND_ACK;
		baseLen = APS_CMD_HDR_LEN;
	} else if ((hdr->frameCtrl & APS_FRAME_CTRL_EXTENDED_HEADER) != 0U) {
		fc = APS_FRAME_CTRL_EXTENDED_DATA_ACK;
		baseLen = (hdr->extHdr != 0U) ? APS_ACK_HDR_EXT_LEN : APS_ACK_HDR_LEN;
	} else {
		fc = APS_FRAME_CTRL_DATA_ACK;
		baseLen = APS_DATA_HDR_LEN;
	}

	payloadLen = baseLen;
	if ((hdr->frameCtrl & APS_FRAME_CTRL_FRAME_TYPE_MASK) !=
		    APS_FRAME_CTRL_FRAME_TYPE_COMMAND &&
	    (hdr->frameCtrl & APS_FRAME_CTRL_SECURITY) != 0U) {
		fc |= APS_FRAME_CTRL_SECURITY;
		payloadLen = (uint8_t)(payloadLen + ss_apsEnAuxHdrFill(auxHdr, NULL, 0));
	}

	ZB_BUF_INITIAL_ALLOC(buf, payloadLen, payload, uint8_t *);
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
		uint8_t auxLen = (uint8_t)(payloadLen - baseLen);

		memcpy(payload + baseLen, auxHdr, auxLen);
	}

	memset(buf, 0, sizeof(nlde_data_req_t));
	req = (nlde_data_req_t *)buf;
	req->dstAddr = hdr->srcShortAddr;
	req->addrMode = APS_SHORT_DSTADDR_WITHEP;
	req->discoverRoute = 1;
	req->securityEnable = securityUse;
	req->ndsuHandle = APS_CMD_HANDLE_ACK;
	req->nsdu = payload;
	req->nsduLen = payloadLen;

	if ((hdr->frameCtrl & APS_FRAME_CTRL_FRAME_TYPE_MASK) !=
		    APS_FRAME_CTRL_FRAME_TYPE_COMMAND &&
	    (hdr->frameCtrl & APS_FRAME_CTRL_SECURITY) != 0U) {
		if (zb_address_ieee_by_short(hdr->srcShortAddr, auxHdr) != 0U) {
			zb_buf_free(buf);
			return APS_STATUS_SECURITY_FAIL;
		}
		if (ss_apsSecureFrame(buf, payloadLen, baseLen, *(addrExt_t *)auxHdr) != RET_OK) {
			/* ss_apsSecureFrame posts a status indication with this buffer. */
			return APS_STATUS_SECURITY_FAIL;
		}
	}

	zb_NwkNldeDataRequest(buf);
	return 0;
}

static void aps_txCacheConfirm(void *arg, uint8_t status)
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
	win = aps_frag_tx_win();

	if (aps_cache_state(cache) == APX_TX_CACHE_STA_ADDR_REQ ||
	    aps_cache_state(cache) == APX_TX_CACHE_STA_TXING ||
	    aps_cache_state(cache) == APX_TX_CACHE_STA_WAITING_ACK ||
	    aps_cache_state(cache) == APX_TX_CACHE_STA_RETRY) {
		T_DBG_fgmtCnf[1]++;

		if (status != APS_STATUS_SUCCESS) {
			goto fragment_done;
		}

		if ((int)win->curBlockNum >= (int)win->totalBlockNum - 1 &&
		    win->ackBit == APS_FRAG_ACK_ALL) {
			goto fragment_done;
		}

		if (win->activeListNum >= aps_ib.aps_max_window_size &&
		    win->ackBit == APS_FRAG_ACK_ALL) {
			for (uint8_t i = 0; i < win->activeListNum; i++) {
				aps_tx_cache_list_t *pending = win->list[i];

				/* The vendor object does not test the entry; a hole in the list
				 * is skipped here instead of being dereferenced. */
				if (pending == NULL) {
					continue;
				}

				if (pending->payload != NULL) {
					zb_buf_free((zb_buf_t *)pending->payload);
				}
				memset(pending, 0, sizeof(*pending));
				g_apsTxCacheNum--;
				win->list[i] = NULL;
			}

			win->activeListNum = 0;
			win->reSend = 0;
			win->ackBit = APS_FRAG_ACK_ALL;
		}

		T_DBG_fgmtCnf[3]++;
	}

	if (win->reSend == 0U) {
		win->curBlockNum++;
	}

	{
		uint8_t delay = aps_ib.aps_interframe_delay;

		if (delay == 0U) {
			delay = APS_FRAG_INTERFRAME_DELAY_DEFAULT_MS;
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
			ev_buf_free((uint8_t *)win->payload);
		}
		if (win->req != NULL) {
			ev_buf_free((uint8_t *)win->req);
		}

		for (uint8_t i = 0; i < win->activeListNum; i++) {
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
		T_DBG_fgmtCnf[2]++;
		return;
	}

	/* Deliberate deviation from the vendor object: with the window already gone it
	 * only counts and returns, so the cache entry and its buffer stay allocated.
	 */
	T_DBG_fgmtCnf[2]++;
	if (cache->payload != NULL) {
		zb_buf_free((zb_buf_t *)cache->payload);
	}
	memset(cache, 0, sizeof(*cache));
	g_apsTxCacheNum--;
}

static uint8_t apsTxDataSendStart(aps_tx_cache_list_t *cache)
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

	ZB_COPY_BUF(buf, src);
	req = (nlde_data_req_t *)buf;
	nsduOffset = (size_t)(req->nsdu - src->buf);
	req->nsdu = buf->buf + nsduOffset;

	if (cache->interPAN) {
		(void)zb_NwkInterPanDataReq(buf);
	} else {
		zb_NwkNldeDataRequest(buf);
	}

	return 0;
}
void zb_apsDataIndRegister(apsDataIndCb_t cb)
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

	for (uint8_t i = 0; i < APS_TX_CACHE_TABLE_SIZE; i++) {
		aps_tx_cache_list_t *cache = &aps_txCache_tbl[i];

		if (!cache->used) {
			continue;
		}

		if (cache->addrReqNeed && cache->state == APX_TX_CACHE_STA_ADDR_REQ) {
			return;
		}

		if (cache->ackNeed && cache->state == APX_TX_CACHE_STA_WAITING_ACK) {
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

		uint32_t usedNum = 0;

		for (uint32_t i = 0; usedNum < g_nwkAddrMap.validNum; i++) {
			zb_addr_map_entry_t *entry = &g_nwkAddrMap.addrMap[i];

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

uint8_t aps_duplicate_check(uint16_t src_addr, uint8_t aps_counter)
{
	uint16_t idx;

	/* The vendor library creates the mapping when it is missing. */
	if (zb_addrByShort(src_addr, 1, 0, &idx) != RET_OK) {
		return 0;
	}

	{
		zb_addr_map_entry_t *entry = &g_nwkAddrMap.addrMap[idx];
		uint8_t duplicate = 0U;

		/* The result is 1 only when the stored counter equals the
		 * received one (a logical NOT of their difference).  Reading it as the
		 * arithmetic difference inverted the test: every frame with a new
		 * counter was discarded as a duplicate and only real duplicates
		 * got through, which silenced the whole interview after the join.
		 */
		if (entry->aps_dup_clock != 0U) {
			duplicate = (entry->aps_dup_cnt == aps_counter) ? 1U : 0U;
		}

		entry->aps_dup_cnt = aps_counter;
		entry->aps_dup_clock =
			(uint8_t)(((APS_ACK_EXPIRY * APS_MAX_FRAME_RETRIES) + 1U) & APS_DUP_CLOCK_MASK);
		apsDuplicateCheckFlag = 1;
		return duplicate;
	}
}
static void aps_data_indication_process(void *arg)
{
	uint8_t *buf = (uint8_t *)arg;
	aps_rx_hdr_t *hdr = aps_rx_hdr(buf);
	uint8_t frameType;
	uint8_t frameCtrl;

	if ((hdr->frameCtrl & APS_FRAME_CTRL_SECURITY) != 0U) {
		if (ss_apsDecryptFrame(arg) != RET_OK) {
			zb_buf_free((zb_buf_t *)arg);
			return;
		}
		hdr = aps_rx_hdr(buf);
	}

	if ((hdr->frameCtrl & APS_FRAME_CTRL_EXTENDED_HEADER) != 0U && hdr->extHdr != 0U) {
		nlde_data_ind_t *networkInd = (nlde_data_ind_t *)arg;
		apsDataFragmentRcvWin_t *win = aps_frag_rcv_win();
		aps_rcv_info_t *slot = NULL;
		aps_rx_hdr_t ackHdr;
		uint8_t extHdr = hdr->extHdr;
		uint8_t securityUse = networkInd->securityUse;
		uint8_t maxWindow;
		uint8_t blockNum;
		uint8_t slotIndex;
		bool sendAck;

		if ((hdr->frameCtrl & APS_FRAME_CTRL_FRAME_TYPE_MASK) !=
			    APS_FRAME_CTRL_FRAME_TYPE_DATA ||
		    (hdr->frameCtrl & APS_FRAME_CTRL_DELIVERY_MODE_MASK) ==
			    APS_FRAME_CTRL_DELIVERY_MODE_GROUP ||
		    hdr->dstEp == APS_EP_BROADCAST) {
			zb_buf_free((zb_buf_t *)arg);
			return;
		}

		T_DBG_fgmt[0]++;
		if (extHdr > APS_EXT_FRAME_CTRL_NEXT) {
			zb_buf_free((zb_buf_t *)arg);
			return;
		}

		if (win->used == 0U) {
			uint8_t *reassembly;
			uint8_t totalBlocks;

			if (win->evBuf != NULL) {
				T_DBG_fgmt[1]++;
				zb_buf_free((zb_buf_t *)arg);
				return;
			}

			if (extHdr == APS_EXT_FRAME_CTRL_FIRST) {
				uint16_t payloadBytes;

				totalBlocks = hdr->blockNum;
				payloadBytes = (uint16_t)((uint16_t)totalBlocks *
						     (uint16_t)(networkInd->nsduLen - hdr->hdrLen));
				reassembly =
					(payloadBytes == 0U)
						? long_ev_buf_get()
						: ev_buf_allocate(
							  (uint16_t)(payloadBytes +
								sizeof(aps_fragment_reassembly_t)));
			} else {
				totalBlocks = 0;
				if (aps_ib.aps_max_window_size <= hdr->blockNum) {
					T_DBG_fgmt[2]++;
					zb_buf_free((zb_buf_t *)arg);
					return;
				}
				reassembly = long_ev_buf_get();
			}

			if (reassembly == NULL) {
				T_DBG_fgmt[3]++;
				zb_buf_free((zb_buf_t *)arg);
				return;
			}

			memset(reassembly, 0, sizeof(aps_fragment_reassembly_t));
			win->used = 1;
			win->waitCnt = 0;
			win->evBuf = reassembly;
			win->rcvFragmentlen = 0;
			win->srcAddr = hdr->srcShortAddr;
			win->apsCnt = hdr->apsCounter;
			win->totalBlockNum = totalBlocks;
			win->blockNumProcessed = 0;
			win->curRcvMinBlockNum = 0;
		}

		T_DBG_fgmtBlk[0] = win->totalBlockNum;
		if (win->srcAddr != hdr->srcShortAddr || win->apsCnt != hdr->apsCounter) {
			T_DBG_fgmt[4]++;
			zb_buf_free((zb_buf_t *)arg);
			return;
		}

		if (extHdr == APS_EXT_FRAME_CTRL_FIRST) {
			win->totalBlockNum = hdr->blockNum;
			hdr->blockNum = 0;
			if ((uint16_t)((uint16_t)win->totalBlockNum *
				  (uint16_t)(networkInd->nsduLen - hdr->hdrLen)) >
			    APS_FRAGMENT_REASSEMBLY_MAX_PAYLOAD) {
				/* Deliberate deviation from the vendor object: it only clears the
				 * window pointer here and leaks the reassembly buffer that was just
				 * allocated (or the live one of a repeated first fragment).
				 */
				if (win->evBuf != NULL) {
					ev_buf_free(win->evBuf);
				}
				apsDataFragmentRcvWinClear();
				win->rcvWinTimeoutEvt = NULL;
				zb_buf_free((zb_buf_t *)arg);
				return;
			}
		}

		maxWindow = aps_ib.aps_max_window_size;
		blockNum = hdr->blockNum;
		T_DBG_fgmtBlk[1] = win->totalBlockNum;
		ackHdr = *hdr;

		/* Only the first receiving slot is looked at, as in the vendor object. */
		if (win->rcvInfo[0].state == APS_RCV_FRAGMENT_RCVING) {
			slot = &win->rcvInfo[0];
		} else if (win->rcvInfo[1].state == APS_RCV_FRAGMENT_RCVING) {
			slot = &win->rcvInfo[1];
		}

		if (slot != NULL) {
			if ((uint16_t)slot->validBufNum + slot->minBlockNum <= blockNum) {
				T_DBG_fgmt[5]++;
				zb_buf_free((zb_buf_t *)arg);
				return;
			}
			if (slot->minBlockNum > blockNum) {
				T_DBG_fgmt[0xb]++;
				slot = NULL;
			}
		} else if (win->curRcvMinBlockNum > blockNum) {
			T_DBG_fgmt[0xc]++;
		} else {
			if (win->rcvInfo[0].state == APS_RCV_FRAGMENT_IDLE) {
				slot = &win->rcvInfo[0];
			} else if (win->rcvInfo[1].state == APS_RCV_FRAGMENT_IDLE) {
				slot = &win->rcvInfo[1];
			}

			if (slot == NULL || (uint16_t)win->curRcvMinBlockNum + maxWindow <= blockNum) {
				T_DBG_fgmt[6]++;
				zb_buf_free((zb_buf_t *)arg);
				return;
			}

			slot->state = APS_RCV_FRAGMENT_RCVING;
			slot->acked = 0;
			slot->validBufNum = maxWindow;
			slot->minBlockNum = win->curRcvMinBlockNum;
			slot->ackBit = APS_FRAG_ACK_ALL;
			for (uint8_t i = 0; i < maxWindow; i++) {
				slot->ackBit &= (uint8_t) ~(1U << i);
			}

			if (win->curRcvMinBlockNum == 0U) {
				T_DBG_fgmt[7]++;
				win->waitCnt = 0;
				win->rcvWinTimeoutEvt = ev_timer_taskPost(
					aps_data_fragment_process_timeout, NULL,
					(uint32_t)APS_ACK_EXPIRY * APS_FRAGMENT_RCV_TIMEOUT_MS_PER_ACK);
			}
		}

		if (slot == NULL) {
			/* A block of an already acknowledged window: repeat its acknowledgement. */
			T_DBG_fgmt[0xd]++;
			ackHdr.blockNum = (uint8_t)(win->curRcvMinBlockNum - maxWindow);
			(void)aps_ack_send(&ackHdr, securityUse, APS_FRAG_ACK_ALL);
			zb_buf_free((zb_buf_t *)arg);
			return;
		}

		T_DBG_fgmtBlk[2] = win->totalBlockNum;
		slotIndex = (uint8_t)(blockNum % slot->validBufNum);
		if (slot->buf[slotIndex] != NULL) {
			zb_buf_free((zb_buf_t *)arg);
		} else {
			slot->buf[slotIndex] = (void *)arg;
			slot->ackBit |= (uint8_t)(1U << slotIndex);
			if (win->totalBlockNum != 0U && blockNum == (uint8_t)(win->totalBlockNum - 1U)) {
				T_DBG_fgmt[8]++;
				for (uint16_t b = win->totalBlockNum;
				     b < (uint16_t)slot->minBlockNum + slot->validBufNum; b++) {
					slot->ackBit |= (uint8_t)(1U << (b % slot->validBufNum));
				}
			}
		}

		T_DBG_fgmtBlk[3] = win->totalBlockNum;
		sendAck = (int)blockNum == (int)win->totalBlockNum - 1 ||
			  (int)blockNum == (int)slot->minBlockNum + slot->validBufNum - 1 ||
			  slot->acked != 0U;
		if (sendAck) {
			T_DBG_fgmt[9]++;
			slot->acked = 1;
			ackHdr.blockNum = win->curRcvMinBlockNum;
			(void)aps_ack_send(&ackHdr, securityUse, slot->ackBit);
			if (win->rcvWinTimeoutEvt != NULL) {
				ev_timer_taskCancel(&win->rcvWinTimeoutEvt);
			}
			win->waitCnt = 0;
			win->rcvWinTimeoutEvt = ev_timer_taskPost(
				aps_data_fragment_process_timeout, NULL,
				(uint32_t)APS_ACK_EXPIRY * APS_FRAGMENT_RCV_TIMEOUT_MS_PER_ACK);
		}

		T_DBG_fgmtBlk[4] = win->totalBlockNum;
		if (slot->ackBit == APS_FRAG_ACK_ALL) {
			T_DBG_fgmt[0xa]++;
			slot->state = APS_RCV_FRAGMENT_HANDLING;
			win->curRcvMinBlockNum =
				(uint8_t)(win->curRcvMinBlockNum + aps_ib.aps_max_window_size);
			zb_TaskPost((zb_callback_t)apsRcvingWindowHandling, slot);
		}
		T_DBG_fgmtBlk[5] = win->totalBlockNum;
		return;
	}

	if ((hdr->frameCtrl & APS_FRAME_CTRL_ACK_REQUEST) != 0U) {
		if (aps_ack_send(hdr, ((nlde_data_ind_t *)arg)->securityUse, 0) != 0U) {
			zb_buf_free((zb_buf_t *)arg);
			return;
		}
	}

	if (aps_duplicate_check(hdr->srcShortAddr, hdr->apsCounter) != 0U) {
		zb_buf_free((zb_buf_t *)arg);
		return;
	}

	frameCtrl = hdr->frameCtrl;
	frameType = (uint8_t)(frameCtrl & APS_FRAME_CTRL_FRAME_TYPE_MASK);
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
		zb_TaskPost((zb_callback_t)aps_process_group_addressed_packet, arg);
		return;
	}

	zb_TaskPost(af_aps_data_entry, arg);
}
void aps_interPanDataIndCb(void *arg)
{
	zb_mscp_data_ind_t *macInd = (zb_mscp_data_ind_t *)arg;
	aps_data_ind_t localInd;
	aps_rx_hdr_t *hdr = aps_rx_hdr(arg);
	uint8_t *apsPayload = macInd->msdu + 2;
	uint8_t apsPayloadLen = (uint8_t)(macInd->msduLength - 2U);
	uint8_t hdrLen;
	af_endpoint_descriptor_t *epList;
	uint8_t epNum;

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
	localInd.asduLength = (uint16_t)(apsPayloadLen - hdrLen);

	epList = af_epDescriptorGet();
	epNum = af_availableEpNumGet();
	for (uint8_t i = 0; i < epNum; i++) {
		if (!af_clsuterIdMatched(localInd.cluster_id, epList[i].correspond_simple_desc)) {
			continue;
		}

		localInd.dst_ep = epList[i].ep;
		memcpy(arg, &localInd, sizeof(localInd));
		zb_TaskPost(af_aps_data_entry, arg);
		return;
	}

	zb_buf_free((zb_buf_t *)arg);
}
void apsTxEventPost(aps_tx_cache_list_t *cache, aps_tx_cache_evt_e event, uint8_t status)
{
	(void)status;

	switch (cache->state) {
	case APX_TX_CACHE_STA_TODO:
		if (cache->addrReqNeed) {
			aps_cache_state_set(cache, APX_TX_CACHE_STA_ADDR_REQ);
			return;
		}

		if (apsTxDataSendStart(cache) == 0U) {
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
				uint8_t retries = cache->retries;

				cache->retries = (uint8_t)(retries - 1U);
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

	if (win->ackBit == APS_FRAG_ACK_ALL) {
		win->reSend = 0U;
		return;
	}

	for (uint8_t i = 0; i < 8; i++) {
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
			zb_TaskPost(aps_data_fragment, NULL);
		}
	}

	return -1;
}

static void aps_nwk_addr_req_cb(void *arg)
{
	aps_nwk_addr_rsp_hdr_t *rsp = (aps_nwk_addr_rsp_hdr_t *)arg;
	uint16_t addrMapIdx;

	if (rsp->status != ZDO_SUCCESS) {
		return;
	}

	(void)zb_NwkAddrMapAdd(rsp->shortAddr, rsp->extAddr, &addrMapIdx);

	for (uint8_t i = 0; i < APS_TX_CACHE_TABLE_SIZE; i++) {
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
	uint8_t status = cnf->status;
	uint8_t handle = cnf->nsduHandle;

	for (uint8_t i = 0; i < APS_TX_CACHE_TABLE_SIZE; i++) {
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
	uint8_t hdrLen = aps_hdr_parse(ind->nsdu, hdr);
	uint8_t frameType;

	hdr->hdrLen = hdrLen;
	hdr->srcShortAddr = ind->srcAddr;
	frameType = (uint8_t)(hdr->frameCtrl & APS_FRAME_CTRL_FRAME_TYPE_MASK);

	if (ind->dstAddrMode == APS_SHORT_GROUPADDR_NOEP) {
		hdr->frameCtrl = (uint8_t)((hdr->frameCtrl & (uint8_t)~APS_FRAME_CTRL_DELIVERY_MODE_MASK) |
				      APS_FRAME_CTRL_DELIVERY_MODE_GROUP);
		COPY_U16TOBUFFER((uint8_t *)&hdr->dstEp, ind->dstAddr);
	}

	if (frameType != APS_FRAME_CTRL_FRAME_TYPE_ACK && ind->nsduLen < hdrLen) {
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

		for (uint8_t i = 0; i < APS_TX_CACHE_TABLE_SIZE; i++) {
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
				uint8_t firstBlock = win->list[0]->blockNum;
				uint8_t ackBlock = (firstBlock == win->totalBlockNum) ? 0U : firstBlock;

				if ((hdr->frameCtrl & APS_FRAME_CTRL_EXTENDED_HEADER) == 0U ||
				    hdr->blockNum != ackBlock) {
					continue;
				}

				if (!aps_fragment_ack_window_ready(win)) {
					continue;
				}
			}

			if (hdr->ackBit == APS_FRAG_ACK_ALL) {
				win->ackBit = hdr->ackBit;
				apsTxEventPost(cache, APS_TX_EVENT_DONE, APS_STATUS_SUCCESS);
				break;
			}

			cache->state = APX_TX_CACHE_STA_TODO;
			cache->apsAckWaitTimeOut = APS_ACK_EXPIRY;
			cache->retries = APS_MAX_FRAME_RETRIES;
			cache->ackNeed = 0U;
			if (win->used != 0U) {
				uint8_t ackBit = hdr->ackBit;

				if (aps_ib.aps_max_window_size < APS_FRAG_WINDOW_MAX) {
					ackBit |= (uint8_t)(APS_FRAG_ACK_ALL
						       << aps_ib.aps_max_window_size);
				}

				/* The first block the receiver misses is sent again with an
				 * acknowledgement request. */
				for (uint8_t i = 0;
				     i < aps_ib.aps_max_window_size && i < APS_FRAG_WINDOW_MAX;
				     i++) {
					aps_tx_cache_list_t *missing = win->list[i];

					if (((ackBit >> i) & 0x01U) == 0U && missing != NULL &&
					    missing->used != 0U) {
						missing->ackNeed = 1U;
						((nlde_data_req_t *)missing->payload)->nsdu[0] |=
							APS_FRAME_CTRL_ACK_REQUEST;
						break;
					}
				}

				win->reSend = 1U;
				win->ackBit = ackBit;
			}
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

	zb_TaskPost(aps_data_indication_process, arg);
}
_attribute_no_inline_ uint8_t apsHandleIsExit(uint8_t handle)
{
	for (uint8_t i = 0; i < APS_TX_CACHE_TABLE_SIZE; i++) {
		aps_tx_cache_list_t *cache = &aps_txCache_tbl[i];

		if (cache->used && cache->handler == handle) {
			return 1;
		}
	}

	return 0;
}

aps_tx_cache_list_t *apsTxDataPost(bool ackNeed, bool addrReqNeed, bool interPan, uint8_t *payload,
				   apsdeDataConf_t *pCnf)
{
	aps_confirm_buf_t *cnf = (aps_confirm_buf_t *)pCnf;
	aps_confirm_buf_t *confirm = cnf;
	aps_tx_cache_list_t *freeEntry = NULL;

	for (uint8_t i = 0; i < APS_TX_CACHE_TABLE_SIZE; i++) {
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
	freeEntry->apsAddrWaitTimeout = (int8_t)((APS_ACK_EXPIRY + 1U) * APS_MAX_FRAME_RETRIES);
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
	uint8_t auxHdr[16] = {0};
	uint8_t *asdu;
	uint8_t *nsdu;
	uint8_t frameCtrl;
	uint8_t baseLen;
	uint8_t headerLen;
	uint8_t auxLen = 0;
	uint8_t tx;
	uint8_t ackNeed;
	uint8_t addrReqNeed = 0;
	uint8_t interPan;
	uint8_t extFrameCtrl;
	uint8_t blockNum;
	uint16_t dstAddr = 0;
	uint16_t addrMapIdx = 0;
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
		uint16_t bindSize =
			(uint16_t)(8U + ((uint16_t)APS_BINDING_TABLE_SIZE * (uint16_t)sizeof(bind_dst_list)));
		aps_status_t status;

		if (req->extFrameCtrl != 0U) {
			ZB_EXCEPTION_POST(SYS_EXCEPTTION_ZB_APS_PARAM);
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
			ev_buf_free((uint8_t *)bindList);
			aps_conf(&cnf);
			zb_buf_free((zb_buf_t *)arg);
			return;
		}

		bindList->txData = (uint8_t *)req;
		ev_timer_taskPost(bindingTxBack, bindList, 1);
		return;
	}

	tx = req->tx_options;
	interPan = (uint8_t)((tx & APS_TX_OPT_INTRA_PAN) != 0U);

	memset(&nlde, 0, sizeof(nlde));
	nlde.useAlias = req->useAlias;
	nlde.aliasSrcAddr = req->aliasSrcAddr;
	nlde.aliasSeqNum = req->aliasSeqNum;
	nlde.radius = req->radius;
	nlde.addrMode = APS_SHORT_DSTADDR_WITHEP;
	nlde.discoverRoute = g_zbNwkCtx.discoverRoute;
	nlde.unicastSkipRouting = (uint8_t)((tx & APS_TX_OPT_UNICAST_SKIP_ROUTING) != 0U);

	if (req->dst_addr_mode == APS_SHORT_GROUPADDR_NOEP) {
		dstAddr = req->aps_addr.dst_group_addr;
		cnf.dstAddr.addr_short = dstAddr;
		cnf.ep.af.dstAddrMode = APS_SHORT_GROUPADDR_NOEP;

		/* Group frames are never acknowledged or APS secured: the vendor clears
		 * the options in the request itself.  With NWK multicast the frame goes out
		 * as an APS broadcast to endpoint 0xff and the group is the NWK destination.
		 */
		if (g_zbNIB.useMulticast != 0U) {
			nlde.addrMode = APS_SHORT_GROUPADDR_NOEP;
			nlde.dstAddr = dstAddr;
			req->aps_addr.dst_endpoint = APS_EP_BROADCAST;
			nlde.nonmemberRadius = aps_ib.aps_nonmember_radius;
			frameCtrl = APS_FRAME_CTRL_DELIVERY_MODE_BROADCAST;
			tx &= (uint8_t)~APS_TX_OPT_SECURITY_ENABLED;
		} else {
			nlde.dstAddr = GROUP_MESSAGE_SEND_ADDRESS;
			frameCtrl = APS_FRAME_CTRL_DELIVERY_MODE_GROUP;
			tx &= (uint8_t) ~(APS_TX_OPT_ACK_TX | APS_TX_OPT_SECURITY_ENABLED);
		}
	} else if (req->dst_addr_mode == APS_SHORT_DSTADDR_WITHEP) {
		dstAddr = req->aps_addr.dst_short_addr;
		cnf.ep.af.dstEndpoint = req->aps_addr.dst_endpoint;
		cnf.dstAddr.addr_short = dstAddr;
		nlde.dstAddr = dstAddr;
		if (ZB_NWK_IS_ADDRESS_BROADCAST(dstAddr)) {
			frameCtrl = APS_FRAME_CTRL_DELIVERY_MODE_BROADCAST;
			tx &= (uint8_t) ~(APS_TX_OPT_ACK_TX | APS_TX_OPT_SECURITY_ENABLED);
		} else {
			frameCtrl = 0;
		}
	} else {
		cnf.ep.af.dstEndpoint = req->aps_addr.dst_endpoint;
		ZB_IEEE_ADDR_COPY(cnf.dstAddr.addr_long, req->aps_addr.dst_ext_addr);
		frameCtrl = 0;

		/* The vendor resolves the extended address only for intra-PAN frames. */
		if (interPan == 0U) {
			if (zb_ShortAddrByExtAddr(&nlde.dstAddr, requestedExt, &addrMapIdx) ==
				    NWK_ADDR_LOOKUP_FAILED ||
			    ZB_NWK_IS_ADDRESS_BROADCAST(nlde.dstAddr)) {
				/* Keep the extended destination in the NWK request.  The vendor
				 * also does this when the map resolves to a broadcast address. */
				ZB_IEEE_ADDR_COPY(nlde.ieeAddr, requestedExt);
				((zb_buf_t *)arg)->hdr.id = req->src_endpoint;
				((zb_buf_t *)arg)->hdr.handle = req->handle;
				addrReqNeed = 1U;
			}
		}
	}

	if (interPan != 0U) {
		/* Inter-PAN frames have frame type 3 and keep the delivery mode bits. */
		frameCtrl = (uint8_t)((frameCtrl & (uint8_t)~APS_FRAME_CTRL_FRAME_TYPE_MASK) |
				 APS_FRAME_CTRL_FRAME_TYPE_INTERPAN);
		tx &= (uint8_t)~APS_TX_OPT_ACK_TX;
		if (req->dst_addr_mode == APS_LONG_DSTADDR_WITHEP) {
			nlde.addrMode = APS_LONG_DSTADDR_WITHEP;
			ZB_IEEE_ADDR_COPY(nlde.ieeAddr, requestedExt);
		}
	}

	req->tx_options = tx;
	ackNeed = (uint8_t)((tx & APS_TX_OPT_ACK_TX) != 0U);
	if (ackNeed != 0U) {
		frameCtrl |= APS_FRAME_CTRL_ACK_REQUEST;
	}
	if ((tx & APS_TX_OPT_FRAG_PERMITTED) != 0U) {
		frameCtrl |= APS_FRAME_CTRL_EXTENDED_HEADER;
	}

	if (interPan != 0U) {
		/* Inter-PAN frames use the compact five-byte APS header. */
		nsdu = asdu - APS_INTERPAN_HDR_LEN;
		nsdu[0] = frameCtrl;
		COPY_U16TOBUFFER(nsdu + 1, req->cluster_id);
		COPY_U16TOBUFFER(nsdu + 3, req->profile_id);
		headerLen = APS_INTERPAN_HDR_LEN;
	} else {
		baseLen = ((frameCtrl & APS_FRAME_CTRL_DELIVERY_MODE_MASK) ==
			   APS_FRAME_CTRL_DELIVERY_MODE_GROUP)
				  ? APS_GROUP_HDR_LEN
				  : APS_DATA_HDR_LEN;
		headerLen = (uint8_t)(baseLen +
				 (((frameCtrl & APS_FRAME_CTRL_EXTENDED_HEADER) != 0U) ? 2U : 0U));

		if ((tx & APS_TX_OPT_SECURITY_ENABLED) != 0U) {
			frameCtrl |= APS_FRAME_CTRL_SECURITY;
			auxLen = ss_apsEnAuxHdrFill(auxHdr, NULL, tx);
		}

		nsdu = asdu - headerLen - auxLen;
		nsdu[0] = frameCtrl;
		if (baseLen == APS_GROUP_HDR_LEN) {
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
			/* Deliberate deviation from the vendor object: it writes
			 * aliasSeqNum instead of the APS counter when an alias is used,
			 * so the APS counter of the frame no longer matches the one the
			 * cache entry and the confirm carry.
			 */
			nsdu[7] = req->apsCnt;
		}

		if ((frameCtrl & APS_FRAME_CTRL_EXTENDED_HEADER) != 0U) {
			nsdu[baseLen] = extFrameCtrl;
			if (extFrameCtrl != 0U) {
				nsdu[baseLen + 1U] = blockNum;
			}
		}
		if (auxLen != 0U) {
			memcpy(nsdu + headerLen, auxHdr, auxLen);
		}
	}

	nlde.securityEnable = req->enableNWKsecurity;
	nlde.ndsuHandle = req->handle;
	nlde.nsduLen = (uint8_t)(req->asdu_length + headerLen + auxLen);
	nlde.nsdu = nsdu;
	((zb_buf_t *)arg)->hdr.handle = req->handle;
	memcpy(arg, &nlde, sizeof(nlde));

	if ((frameCtrl & APS_FRAME_CTRL_SECURITY) != 0U) {
		if ((frameCtrl & APS_FRAME_CTRL_DELIVERY_MODE_MASK) != 0U) {
			cnf.status = APS_STATUS_SECURITY_FAIL;
			if (extFrameCtrl != 0U) {
				zb_TaskPost(aps_data_fragment, NULL);
			} else {
				aps_conf(&cnf);
			}
			zb_buf_free((zb_buf_t *)arg);
			return;
		}

		extAddr = (addrReqNeed != 0U) ? &nlde.ieeAddr
					      : zb_ExtAddrPtrByShortAddr(nlde.dstAddr);
		if (extAddr == NULL) {
			cnf.status = APS_STATUS_SECURITY_FAIL;
			if (extFrameCtrl != 0U) {
				zb_TaskPost(aps_data_fragment, NULL);
			} else {
				aps_conf(&cnf);
			}
			zb_buf_free((zb_buf_t *)arg);
			return;
		}

		if (ss_apsSecureFrame(arg, (uint8_t)(headerLen + auxLen), headerLen, *extAddr) ==
		    RET_ERROR) {
			if (extFrameCtrl != 0U) {
				zb_TaskPost(aps_data_fragment, NULL);
				return;
			}
			cnf.status = APS_STATUS_SECURITY_FAIL;
			aps_conf(&cnf);
			/* ss_apsSecureFrame posts a status indication with this buffer. */
			return;
		}
	}

	cache = apsTxDataPost(ackNeed, addrReqNeed, interPan, arg, (apsdeDataConf_t *)&cnf);
	if (cache == NULL) {
		if (extFrameCtrl != 0U) {
			zb_TaskPost(aps_data_fragment, NULL);
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
		if (win->activeListNum < APS_FRAG_WINDOW_MAX) {
			win->list[win->activeListNum] = cache;
			win->activeListNum++;
		}
	}

	apsTxEventPost(cache, APS_TX_EVENT_TX_TODO, APS_STATUS_SUCCESS);
	if (addrReqNeed != 0U) {
		aps_txCacheAsNoShortAddr(nlde.ieeAddr, &cache->zdpSeqnoAddrReq);
	}
}
/* Timer callback: the return value is the delay until the next call, -1 stops the timer. */
static int bindingTxBack(void *arg)
{
	bind_dst_list_tbl *bindList = (bind_dst_list_tbl *)arg;
	zb_buf_t *clone;
	zb_buf_t *srcBuf;
	aps_data_req_t *srcReq;
	aps_data_req_t *dstReq;

	if (bindList->txData == NULL) {
		return -1;
	}

	clone = zb_buf_allocate();
	if (clone == NULL) {
		return APS_BINDING_TX_INTERVAL_MS;
	}

	ZB_COPY_BUF(clone, bindList->txData);
	srcBuf = (zb_buf_t *)bindList->txData;
	srcReq = (aps_data_req_t *)bindList->txData;
	dstReq = (aps_data_req_t *)clone;
	if (srcReq->asdu != NULL) {
		dstReq->asdu = clone->buf + (srcReq->asdu - srcBuf->buf);
	}

	if (bindList->txCnt >= bindList->totalCnt) {
		zb_buf_free(clone);
		zb_buf_free((zb_buf_t *)bindList->txData);
		ev_buf_free((uint8_t *)bindList);
		return -1;
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
			zb_TaskPost(af_aps_data_entry, clone);
		} else {
			aps_data_request(clone);
		}
	}

	if (bindList->txCnt >= bindList->totalCnt) {
		zb_buf_free((zb_buf_t *)bindList->txData);
		ev_buf_free((uint8_t *)bindList);
		return -1;
	}

	return APS_BINDING_TX_INTERVAL_MS;
}
int apsAckPeriodic(void *arg)
{
	(void)arg;

	for (uint8_t i = 0; i < APS_TX_CACHE_TABLE_SIZE; i++) {
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
void aps_cmd_send(void *arg, uint8_t handle)
{
	aps_cmd_send_req_t *req = (aps_cmd_send_req_t *)arg;
	aps_confirm_buf_t cnf;
	nlde_data_req_t *nldereq;
	aps_tx_cache_list_t *cache;
	uint8_t apsCounter;
	uint8_t *nsdu;
	uint8_t apsHdr[2];
	uint8_t auxHdr[16] = {0};
	uint8_t auxLen = 0;
	uint8_t addrReqNeed = 0;
	uint8_t discoverRoute;
	uint8_t nwkRadius = 0;
	addrExt_t *extAddrPtr = NULL;
	uint16_t dstShortAddr = 0;
	uint16_t idx = 0;

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
		if (zb_ShortAddrByExtAddr(&dstShortAddr, req->dstAddr.extAddr, &idx) != RET_OK) {
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

	/* A command the trust center tunnels through the parent is secured end
	 * to end with the joiner's link key, any other command only when
	 * req->secure asks for it. */
	if (req->secure || handle == APS_CMD_HANDLE_CMD_TUNNEL) {
		auxLen = ss_apsEnAuxHdrFill(auxHdr, req->adu, 0);
	}

	nsdu = req->adu - (APS_CMD_HDR_LEN + auxLen);
	memcpy(nsdu, apsHdr, sizeof(apsHdr));
	if (auxLen != 0U) {
		memcpy(nsdu + APS_CMD_HDR_LEN, auxHdr, auxLen);
	}
	nldereq->nsdu = nsdu;
	nldereq->nsduLen = (uint8_t)(req->aduLen + APS_CMD_HDR_LEN + auxLen);

	if (auxLen != 0U) {
		if (handle == APS_CMD_HANDLE_CMD_TUNNEL) {
			/* The caller put the address of the device the command is
			 * secured for in front of the APS header. */
			extAddrPtr = (addrExt_t *)(nsdu - EXT_ADDR_LEN);
		} else if (req->addrMode == APS_SHORT_DSTADDR_WITHEP) {
			extAddrPtr = zb_ExtAddrPtrByShortAddr(req->dstAddr.shortAddr);
			if (extAddrPtr == NULL) {
				cnf.status = APS_STATUS_SECURITY_FAIL;
				aps_conf(&cnf);
				zb_buf_free(req->txBuf);
				return;
			}
		} else {
			extAddrPtr = &req->dstAddr.extAddr;
		}

		if (ss_apsSecureFrame(nldereq, (uint8_t)(APS_CMD_HDR_LEN + auxLen), APS_CMD_HDR_LEN,
				      *extAddrPtr) == RET_ERROR) {
			cnf.status = APS_STATUS_SECURITY_FAIL;
			aps_conf(&cnf);
			/* ss_apsSecureFrame posts a status indication with this buffer. */
			return;
		}

		if (handle == APS_CMD_HANDLE_CMD_TUNNEL) {
			/* Wrap the secured command in a Tunnel command: 2 bytes of APS
			 * header, the command ID, then the 8-byte target address that
			 * already sits in front of the secured command. */
			const uint8_t wrapLen = APS_TUNNEL_PREFIX_LEN + EXT_ADDR_LEN;
			uint8_t *wrap = nldereq->nsdu - wrapLen;

			nldereq->nsdu = wrap;
			nldereq->nsduLen = (uint8_t)(nldereq->nsduLen + wrapLen);
			wrap[0] = apsHdr[0];
			wrap[1] = apsHdr[1];
			wrap[2] = APS_CMD_TUNNEL_ID;
		}
	}

	cache = apsTxDataPost((nsdu[0] & APS_FRAME_CTRL_ACK_REQUEST) ? 1U : 0U, addrReqNeed, 0,
			      (uint8_t *)req->txBuf, (apsdeDataConf_t *)&cnf);
	if (cache == NULL) {
		aps_conf(&cnf);
		zb_buf_free(req->txBuf);
		return;
	}

	/* Vendor posts TX_TODO for every cache entry; a fresh entry with
	 * addrReqNeed moves to ADDR_REQ there, then the address request is sent. */
	apsTxEventPost(cache, APS_TX_EVENT_TX_TODO, APS_STATUS_SUCCESS);
	if (addrReqNeed) {
		aps_txCacheAsNoShortAddr(req->dstAddr.extAddr, &cache->zdpSeqnoAddrReq);
	}
}
uint8_t apsDataRequest(aps_data_req_t *dataReq, uint8_t *asdu, uint8_t length)
{
	zb_buf_t *buf = zb_buf_allocate();
	aps_data_req_t *reqCopy;
	uint8_t *asduCopy;
	size_t reqOffset;

	if (buf == NULL) {
		return APS_DATA_REQUEST_NO_BUFFER;
	}

	memset(buf, 0, sizeof(*reqCopy));
	ZB_BUF_INITIAL_ALLOC(buf, length, asduCopy, uint8_t *);
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

	zb_TaskPost(aps_data_request, buf);
	return 0;
}
static void aps_data_fragment(void *arg)
{
	(void)arg;

	aps_fragment_tx_win_t *win = aps_frag_tx_win();
	aps_data_req_t *req;
	uint16_t totalLen;
	uint16_t offset;
	uint8_t fragLen;

	if (win->used == 0U) {
		return;
	}

	req = (aps_data_req_t *)win->req;
	if (req == NULL) {
		ZB_EXCEPTION_POST(SYS_EXCEPTTION_ZB_APS_FRAGMENT_TRANS);
		return;
	}

	if (win->curBlockNum != 0U) {
		req->extFrameCtrl = APS_EXT_FRAME_CTRL_NEXT;
		req->blockNum = win->curBlockNum;
	} else {
		req->extFrameCtrl = APS_EXT_FRAME_CTRL_FIRST;
		req->blockNum = win->totalBlockNum;
	}

	if (win->activeListNum >= (uint8_t)(aps_ib.aps_max_window_size - 1U)) {
		req->tx_options |= APS_TX_OPT_ACK_TX;
	} else {
		req->tx_options &= (uint8_t)~APS_TX_OPT_ACK_TX;
	}

	totalLen = win->payloadLen;
	offset = (uint16_t)(win->curBlockNum * aps_ib.aps_fragment_payload_size);
	if (offset >= totalLen) {
		ZB_EXCEPTION_POST(SYS_EXCEPTTION_ZB_APS_FRAGMENT_TRANS);
	}

	fragLen = aps_ib.aps_fragment_payload_size;
	if (win->curBlockNum >= (uint8_t)(win->totalBlockNum - 1U)) {
		fragLen = (uint8_t)(totalLen - offset);
		req->tx_options |= APS_TX_OPT_ACK_TX;
		if (aps_ib.aps_fragment_payload_size < fragLen) {
			ZB_EXCEPTION_POST(SYS_EXCEPTTION_ZB_APS_FRAGMENT_TRANS);
		}
	}

	if (apsDataRequest(req, (uint8_t *)win->payload + offset, fragLen) != 0U) {
		uint8_t delay = aps_ib.aps_interframe_delay;

		if (delay == 0U) {
			delay = APS_FRAG_INTERFRAME_DELAY_DEFAULT_MS;
		}
		ev_timer_taskPost(aps_data_fragment_delay, NULL, delay);
	}
}

uint8_t apsDataFragmentRequest(aps_data_req_t *dataReq, uint8_t *asdu, uint16_t length)
{
	aps_fragment_tx_win_t *win = aps_frag_tx_win();
	uint8_t *payloadCopy;
	aps_data_req_t *reqCopy;
	uint8_t fragPayload;

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
	win->ackBit = APS_FRAG_ACK_ALL;
	win->req = (uint8_t *)reqCopy;
	win->payload = payloadCopy;
	win->payloadLen = length;

	fragPayload = aps_ib.aps_fragment_payload_size;
	win->totalBlockNum = (uint8_t)((length + fragPayload - 1U) / fragPayload);
	win->curBlockNum = 0;

	zb_TaskPost(aps_data_fragment, NULL);
	return 0;
}
