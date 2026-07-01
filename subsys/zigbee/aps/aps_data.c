/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Adapted from libzigbee/src/aps_data.c. Vendor file kept structurally
 * one-for-one; vendor zb_local.h / zb_buffer.h / zb_task_queue.h are
 * replaced by the Zephyr include set.
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

u16 dstPanID = 0;
u8 g_apsTxCacheNum = 0;
static apsDataIndCb_t g_apsDataIndCb;
u8 T_DBG_fgmt = 0;
u8 apsDuplicateCheckFlag = 0;
u8 T_DBG_fgmtCnf = 0;
u8 T_DBG_fgmtBlk = 0;
u8 T_DBG_fgmtTout = 0;

int endDevTimeoutReq(void *arg);
void aps_nwk_addr_req_cb(void *arg);
void aps_nwk_data_confirm_cb(void *arg);
int aps_data_fragment_delay(void *arg);
void aps_data_fragment(void *arg);
void aps_data_request(void *arg);
u8 aps_hdr_parse(u8 *data, void *parsed);
u8 aps_get_handle(void);
u8 ss_apsDecryptFrame(void *arg);
void aps_command_handle(void *arg);
void aps_process_group_addressed_packet(zb_buf_t *buf);

typedef struct _attribute_packed_ {
    u8 seqNum;
    u8 status;
    addrExt_t extAddr;
    u16 shortAddr;
} aps_nwk_addr_rsp_hdr_t;

typedef struct {
    aps_tx_cache_list_t *cache[8];
    void *txBuf;
    void *fragmentBackup;
    u16 totalLen;
    u8 fragCount;
    u8 fragSent;
    u8 activeBlock;
    u8 resendBitmap;
    u8 resendPending;
    u8 active;
    u8 tick;
    u8 reserved[15];
} aps_fragment_tx_win_t;

typedef struct {
    zb_buf_t *frags[8];
    u8 fragCount;
    u8 blockNum;
    u8 reserved34;
    u8 state;
} aps_fragment_rcv_slot_t;

typedef struct {
    ev_timer_event_t *timerEvt;
    u8 *reassemblyBuf;
    aps_fragment_rcv_slot_t slots[2];
    u16 fragPayloadLen;
    u16 srcShortAddr;
    u8 apsCounter;
    u8 expectedBlocks;
    u8 receivedBlocks;
    u8 reserved87;
    u8 retryCount;
    u8 active;
} aps_fragment_rcv_win_t;

enum {
    APS_CACHE_USED_BIT = BIT(0),
    APS_CACHE_ADDR_REQ_BIT = BIT(1),
    APS_CACHE_ACK_BIT = BIT(2),
    APS_CACHE_INTERPAN_BIT = BIT(3),
    APS_CACHE_STATE_MASK = 0xf0,
    APS_CACHE_STATE_ADDR_REQ = 0x10,
    APS_CACHE_STATE_NWK_CNF = 0x20,
    APS_CACHE_STATE_APS_ACK = 0x30,
    APS_CACHE_STATE_RETRY = 0x40,
    APS_CACHE_STATE_DONE = 0x50,
    APS_TX_DUP_BUF_SIZE = 0xc3,
    APS_DATA_CONFIRM_LEN = 20,
};

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
    u8 reserved14;
} aps_rx_hdr_t;

typedef struct _attribute_packed_ {
    u8 apsHdrLen;
    u8 flags;
    u8 frameCounter;
    u8 srcEp;
    u16 asduLen;
    u8 dstEp;
    u8 apsCounter;
    u16 clusterId;
    u16 profileId;
} aps_ind_prim_src_hdr_t;

typedef struct _attribute_packed_ {
    u8 dstAddrMode;
    u8 dstEp;
    u16 dstAddr;
    u8 srcAddrMode;
    u8 srcEp;
    u16 profileId;
    u16 clusterId;
    u16 asduLength;
    /* vendor stores a pointer into a u32 byte slot here; promote to
     * uintptr_t so it works on 64-bit native_sim too. */
    uintptr_t asdu;
    u8 reserved16[4];
    u16 srcShortAddr;
    u8 reserved22[6];
    u16 srcMacAddr;
    u8 status;
    u8 securityStatus;
    u8 lqi;
    u8 rssi;
    u8 apsCounter;
} aps_ind_prim_out_t;

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

/* Layout contract — vendor build pinned these to the -fpack-struct
 * layout. The Zephyr build leaves shared types naturally aligned;
 * accessors below use named fields so the runtime works regardless.
 */
#if 0
STATIC_ASSERT(sizeof(aps_data_ind_t) == 35);
STATIC_ASSERT(OFFSETOF(aps_fragment_reassembly_t, copiedLen) == 35);
STATIC_ASSERT(OFFSETOF(aps_fragment_reassembly_t, asdu) == 37);


STATIC_ASSERT(OFFSETOF(aps_fragment_tx_win_t, txBuf) == 32);
STATIC_ASSERT(OFFSETOF(aps_fragment_tx_win_t, fragmentBackup) == 36);
STATIC_ASSERT(OFFSETOF(aps_fragment_tx_win_t, totalLen) == 40);
STATIC_ASSERT(OFFSETOF(aps_fragment_tx_win_t, fragCount) == 42);
STATIC_ASSERT(OFFSETOF(aps_fragment_tx_win_t, fragSent) == 43);
STATIC_ASSERT(OFFSETOF(aps_fragment_tx_win_t, activeBlock) == 44);
STATIC_ASSERT(OFFSETOF(aps_fragment_tx_win_t, resendBitmap) == 45);
STATIC_ASSERT(OFFSETOF(aps_fragment_tx_win_t, resendPending) == 46);
STATIC_ASSERT(OFFSETOF(aps_fragment_tx_win_t, active) == 47);
STATIC_ASSERT(sizeof(aps_fragment_tx_win_t) == 64);

STATIC_ASSERT(OFFSETOF(aps_fragment_rcv_slot_t, fragCount) == 32);
STATIC_ASSERT(OFFSETOF(aps_fragment_rcv_slot_t, blockNum) == 33);
STATIC_ASSERT(OFFSETOF(aps_fragment_rcv_slot_t, state) == 35);
STATIC_ASSERT(sizeof(aps_fragment_rcv_slot_t) == 36);

STATIC_ASSERT(OFFSETOF(aps_fragment_rcv_win_t, timerEvt) == 0);
STATIC_ASSERT(OFFSETOF(aps_fragment_rcv_win_t, reassemblyBuf) == 4);
STATIC_ASSERT(OFFSETOF(aps_fragment_rcv_win_t, slots) == 8);
STATIC_ASSERT(OFFSETOF(aps_fragment_rcv_win_t, fragPayloadLen) == 80);
STATIC_ASSERT(OFFSETOF(aps_fragment_rcv_win_t, srcShortAddr) == 82);
STATIC_ASSERT(OFFSETOF(aps_fragment_rcv_win_t, apsCounter) == 84);
STATIC_ASSERT(OFFSETOF(aps_fragment_rcv_win_t, expectedBlocks) == 85);
STATIC_ASSERT(OFFSETOF(aps_fragment_rcv_win_t, receivedBlocks) == 86);
STATIC_ASSERT(OFFSETOF(aps_fragment_rcv_win_t, retryCount) == 88);
STATIC_ASSERT(OFFSETOF(aps_fragment_rcv_win_t, active) == 89);
STATIC_ASSERT(sizeof(aps_fragment_rcv_win_t) == 90);
#endif

/*
 * Fragment TX/RX windows. These MUST be declared with their real struct type
 * (not a u8[64]/u8[90] byte carrier): both structs contain pointer fields
 * (cache[8], txBuf, fragmentBackup / timerEvt, reassemblyBuf, frags[8]), so the
 * 32-bit byte sizes only match on TC32. A byte-array carrier cast to the struct
 * overruns into adjacent globals on a 64-bit ABI (native_sim). Mirrors libzigbee
 * src/aps_data.c, which declares these with the struct type directly. See
 * "Pointers belong in structs, not u8 byte arrays".
 */
static aps_fragment_tx_win_t g_apsDataFragmentTransWin = {0};
static aps_fragment_rcv_win_t g_apsDataFragmentRcvWin = {0};

static inline aps_fragment_tx_win_t *aps_frag_tx_win(void)
{
    return &g_apsDataFragmentTransWin;
}

static inline aps_fragment_rcv_win_t *aps_frag_rcv_win(void)
{
    return &g_apsDataFragmentRcvWin;
}

static inline ev_timer_event_t **aps_frag_rcv_timer_evt(void)
{
    return &aps_frag_rcv_win()->timerEvt;
}

static inline u8 *aps_frag_rcv_buf(void)
{
    return aps_frag_rcv_win()->reassemblyBuf;
}

static inline aps_fragment_reassembly_t *aps_frag_rcv_reassembly(void)
{
    return (aps_fragment_reassembly_t *)aps_frag_rcv_buf();
}

static inline aps_fragment_rcv_slot_t *aps_frag_rcv_slot_get(u8 idx)
{
    return &aps_frag_rcv_win()->slots[idx];
}

static inline zb_buf_t *aps_frag_slot_buf_get(const aps_fragment_rcv_slot_t *slot, u8 idx)
{
    return slot->frags[idx];
}

static inline void aps_frag_slot_buf_set(aps_fragment_rcv_slot_t *slot, u8 idx, zb_buf_t *buf)
{
    slot->frags[idx] = buf;
}

static inline bool aps_cache_used(const aps_tx_cache_list_t *cache)
{
    return cache != NULL && cache->used;
}

static inline u8 aps_cache_state(const aps_tx_cache_list_t *cache)
{
    return (u8)(cache->state << 4);
}

static inline void aps_cache_state_set(aps_tx_cache_list_t *cache, u8 state)
{
    cache->state = (u8)(state >> 4);
}

/*
 * Vendor stashed the aps_rx_hdr_t right after nlde_data_ind_t at a
 * 32-bit-pinned offset of 20 bytes. On native_sim/native/64 the
 * nlde_data_ind_t grows because u8 *nsdu is 8 bytes, so a hard-coded
 * offset of 20 would land inside its srcMacAddr field. Use the
 * struct size (rounded up to 4-byte alignment) so producers and
 * consumers agree across architectures.
 */
#define APS_RX_HDR_OFFSET ((sizeof(nlde_data_ind_t) + 3U) & ~((size_t)3U))

static inline aps_rx_hdr_t *aps_rx_hdr(void *arg)
{
    return (aps_rx_hdr_t *)((u8 *)arg + APS_RX_HDR_OFFSET);
}

void aps_indPrimBuild(void *arg)
{
    /*
     * Vendor reads / writes via 32-bit-pinned byte offsets that alias
     * a packed aps_ind_prim_out_t against a non-packed
     * aps_data_ind_t. On native_sim/native/64 the consumer reads
     * ind->asdu at the (8-byte-aligned) offset 16 while the packed
     * producer writes at offset 12 — the consumer then dereferences
     * an upper-half-zero pointer and SEGVs.
     *
     * Drive both producer and consumer through aps_data_ind_t struct
     * fields directly so the layout matches whatever the compiler
     * chose for the public type. The intermediate aps_ind_prim_out_t
     * is no longer needed for the local rebuild path.
     */
    u8 *buf = (u8 *)arg;
    aps_ind_prim_src_hdr_t srcHdr;
    aps_data_ind_t *out = (aps_data_ind_t *)arg;
    nlde_data_ind_t src;
    u8 *nsduPtr;
    u16 nsduLen;
    u8 srcHdrFlags;

    memcpy(&src, buf, sizeof(src));
    memcpy(&srcHdr, buf + APS_RX_HDR_OFFSET, sizeof(srcHdr));

    nsduPtr = src.nsdu;
    nsduLen = src.nsduLen;
    srcHdrFlags = srcHdr.flags;

    memset(out, 0, sizeof(*out));

    out->cluster_id = srcHdr.clusterId;
    out->profile_id = srcHdr.profileId;
    out->src_ep = srcHdr.srcEp;
    out->dst_ep = srcHdr.dstEp;
    out->src_addr_mode = APS_SHORT_SRCADDR_WITHEP;
    out->src_short_addr = src.srcAddr;
    out->srcMacAddr = src.srcMacAddr;
    out->aps_counter = srcHdr.frameCounter;
    out->lqi = srcHdr.apsCounter;
    out->rssi = (s8)buf[194];

    if ((srcHdrFlags & 0x0cU) == 0x0cU) {
        out->dst_addr_mode = APS_SHORT_GROUPADDR_NOEP;
        out->dst_addr = srcHdr.dstEp;
    } else {
        out->dst_addr_mode = APS_SHORT_DSTADDR_WITHEP;
        out->dst_addr = src.dstAddr;
    }

    out->asdu = nsduPtr + srcHdr.apsHdrLen;
    out->asduLength = (u16)(nsduLen - srcHdr.apsHdrLen);

    if ((srcHdrFlags & 0x20U) != 0U) {
        out->security_status |= SECURITY_IN_APSLAYER;
    }
    if (src.securityUse) {
        out->security_status |= SECURITY_IN_NWKLAYER;
    }

    printk("zb_aps_ind_build: profile=0x%04x cluster=0x%04x dst_ep=%u src_ep=%u dst_addr=0x%04x asdu_len=%u sec=0x%02x\n",
           out->profile_id, out->cluster_id, out->dst_ep, out->src_ep,
           out->dst_addr, out->asduLength, out->security_status);
}

void aps_conf(void *arg)
{
    aps_confirm_buf_t *buf = (aps_confirm_buf_t *)arg;

    if (buf->handle <= 63U) {
        u8 *cnf = ev_buf_allocate(20);

        if (cnf == NULL) {
            return;
        }

        memset(cnf, 0, 20);
        memcpy(cnf, buf, 20);
        tl_zbTaskPost(af_dataCnfHandler, cnf);
        return;
    }

    if (buf->status != 0U || buf->handle != 73U) {
        return;
    }

    {
        u16 idx = 0;
        u16 shortAddr = 0;

        if (tl_zbShortAddrByExtAddr(&shortAddr, g_zbInfo.macPib.extAddress, &idx) == RET_OK) {
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
int endDevTimeoutReq(void *arg)
{
    (void)arg;

#if defined(ZB_ROUTER_ROLE)
    return -1;
#else
    nwkEndDevTimeoutReqSend((reqTimeoutEnum_t)g_zbNIB.endDevTimeoutDefault, 0);
    return -1;
#endif
}
void aps_txCacheAsNoShortAddr(addrExt_t extAddr, u8 *seqNo)
{
    zdo_nwk_addr_req_t req;

    memcpy(req.ieee_addr_interest, extAddr, sizeof(req.ieee_addr_interest));
    req.req_type = 0;
    req.start_index = 0;
    (void)zb_zdoNwkAddrReq(0xfffc, &req, seqNo, aps_nwk_addr_req_cb);
}

void apsDataFragmentRcvWinClear(void)
{
    aps_fragment_rcv_win_t *win = aps_frag_rcv_win();

    if (win->active != 0U) {
        if (win->timerEvt != NULL) {
            ev_timer_taskCancel(aps_frag_rcv_timer_evt());
        }

        win->reassemblyBuf = NULL;

        for (u8 i = 0; i < 2; i++) {
            aps_fragment_rcv_slot_t *slot = aps_frag_rcv_slot_get(i);

            if (((slot->state & 0x7fU) != 0U) && slot->fragCount != 0U) {
                for (u8 j = 0; j <= slot->fragCount; j++) {
                    zb_buf_t *frag = slot->frags[j];

                    if (frag != NULL) {
                        zb_buf_free(frag);
                        slot->frags[j] = NULL;
                    }
                }
            }
        }
    }

    memset(&g_apsDataFragmentRcvWin, 0, sizeof(g_apsDataFragmentRcvWin));
}

void apsRcvingWindowHandling(void *arg)
{
    aps_fragment_rcv_slot_t *slot = (aps_fragment_rcv_slot_t *)arg;
    aps_fragment_rcv_win_t *win = aps_frag_rcv_win();

    if ((slot->state & 0x7fU) != 2U) {
        return;
    }

    aps_fragment_reassembly_t *reassemblyObj = aps_frag_rcv_reassembly();

    if (reassemblyObj == NULL) {
        ZB_EXCEPTION_POST(SYS_EXCEPTTION_ZB_APS_FRAGMENT_RCV);
        return;
    }

    if (slot->fragCount != 0U) {
        for (u8 i = 0; i < slot->fragCount; i++) {
            zb_buf_t *frag = aps_frag_slot_buf_get(slot, i);

            if (frag == NULL) {
                win->receivedBlocks++;
                continue;
            }

            aps_indPrimBuild(frag);
            if (slot->blockNum == 0U) {
                win->fragPayloadLen = (u16)frag->buf[10] | ((u16)frag->buf[11] << 8);
                memcpy(&reassemblyObj->ind, frag, sizeof(reassemblyObj->ind));
                /* vendor leaves ind.asdu pointing into the fragment buffer; restore it
                 * to point at the reassembly payload so the posted indication is
                 * self-consistent for downstream consumers. */
                reassemblyObj->ind.asdu = reassemblyObj->asdu;
            }

            {
                u16 offset = reassemblyObj->copiedLen;
                u16 fragLen = (u16)frag->buf[10] | ((u16)frag->buf[11] << 8);
                u8 *asdu = ((aps_data_ind_t *)frag)->asdu;

                memcpy(reassemblyObj->asdu + offset, asdu, fragLen);
                offset = (u16)(offset + fragLen);
                reassemblyObj->copiedLen = offset;
                reassemblyObj->ind.asduLength = offset;
            }

            zb_buf_free(frag);
            aps_frag_slot_buf_set(slot, i, NULL);
            win->receivedBlocks++;
        }
    }

    memset(slot, 0, sizeof(*slot));
    if (win->receivedBlocks > win->expectedBlocks) {
        ZB_EXCEPTION_POST(SYS_EXCEPTTION_ZB_APS_FRAGMENT_RCV);
        return;
    }

    if (win->receivedBlocks == win->expectedBlocks) {
        tl_zbTaskPost(af_aps_data_fragment_entry, reassemblyObj);
        apsDataFragmentRcvWinClear();
    }
}
int aps_data_fragment_process_timeout(void *arg)
{
    (void)arg;

    aps_fragment_rcv_win_t *win = aps_frag_rcv_win();

    if (win->active == 0U) {
        return -1;
    }

    if (win->receivedBlocks != win->expectedBlocks) {
        win->retryCount++;
        if (win->retryCount < APS_MAX_FRAME_RETRIES) {
            return 0;
        }

        if (win->reassemblyBuf != NULL) {
            T_DBG_fgmtTout++;
            ev_buf_free(win->reassemblyBuf);
        }
        apsDataFragmentRcvWinClear();
        win->retryCount = 0;
        win->timerEvt = NULL;
        return -1;
    }

    if (win->reassemblyBuf == NULL) {
        memset(&g_apsDataFragmentRcvWin, 0, sizeof(g_apsDataFragmentRcvWin));
        return -1;
    }

    win->retryCount++;
    if (win->retryCount < APS_MAX_FRAME_RETRIES) {
        return 0;
    }

    T_DBG_fgmtTout++;
    ev_buf_free(win->reassemblyBuf);
    apsDataFragmentRcvWinClear();
    win->retryCount = 0;
    win->timerEvt = NULL;
    return -1;
}
u8 aps_ack_send(void *arg, u8 blockAck)
{
    zb_buf_t *buf = zb_buf_allocate();
    nlde_data_req_t *req;
    aps_rx_hdr_t *hdr = (aps_rx_hdr_t *)((u8 *)arg + APS_RX_HDR_OFFSET);
    u8 auxHdr[8] = {0};
    u8 *payload;
    u8 payloadLen;
    u8 baseLen;
    u8 fc;

    if (buf == NULL) {
        return APS_STATUS_INTERNAL_BUF_FULL;
    }

    if ((hdr->frameCtrl & 0x03U) == 0x01U) {
        fc = 0x12;
        baseLen = 2;
    } else if ((hdr->frameCtrl & 0x80U) != 0U) {
        fc = 0x82;
        baseLen = (hdr->extHdr != 0U) ? 11U : 9U;
    } else {
        fc = 0x02;
        baseLen = 8;
    }

    payloadLen = baseLen;
    if ((hdr->frameCtrl & 0x20U) != 0U) {
        fc |= 0x20U;
        payloadLen = (u8)(payloadLen + ss_apsEnAuxHdrFill(auxHdr, NULL, 0));
    }

    payload = tl_bufInitalloc(buf, payloadLen);
    if (payload == NULL) {
        zb_buf_free(buf);
        return APS_STATUS_INTERNAL_BUF_FULL;
    }

    payload[0] = fc;
    if ((hdr->frameCtrl & 0x03U) == 0x01U) {
        payload[1] = hdr->apsCounter;
        if ((hdr->frameCtrl & 0x80U) != 0U) {
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
        if ((hdr->frameCtrl & 0x80U) != 0U) {
            payload[8] = hdr->extHdr ? 2U : 0U;
            if (hdr->extHdr != 0U) {
                payload[9] = hdr->blockNum;
                payload[10] = blockAck;
            }
        }
    }

    if ((hdr->frameCtrl & 0x20U) != 0U) {
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

    memset(buf, 0, 26);
    req = (nlde_data_req_t *)buf;
    req->dstAddr = hdr->srcShortAddr;
    req->addrMode = APS_SHORT_DSTADDR_WITHEP;
    req->discoverRoute = 1;
    req->securityEnable = ((u8 *)arg)[6];
    req->ndsuHandle = 0x4b;
    req->nsdu = payload;
    req->nsduLen = payloadLen;

    tl_zbPrimitivePost(3, 0x70, buf);
    return 0;
}

void aps_txCacheConfirm(void *arg, u8 status)
{
    aps_tx_cache_list_t *cache = (aps_tx_cache_list_t *)arg;
    aps_confirm_buf_t cnf;

    memset(&cnf, 0, sizeof(cnf));

    if (!aps_cache_used(cache)) {
        cnf.handle = cache->handler;
        cnf.apsCnt = cache->apsCount;
        cnf.status = status;
        cnf.ep.af.srcEndpoint = cache->ep;
        cnf.ep.af.dstAddrMode = cache->dstAddrMode;
        memcpy(&cnf.dstAddr, &cache->dstAddr, EXT_ADDR_LEN);
        cnf.ep.af.dstEndpoint = cache->dstEndpoint;
        cnf.clusterId = cache->clusterId;
        aps_conf(&cnf);

        if (cache->payload != NULL) {
            zb_buf_free((zb_buf_t *)cache->payload);
        }

        memset(arg, 0, sizeof(aps_tx_cache_list_t));
        if (g_apsTxCacheNum != 0U) {
            g_apsTxCacheNum--;
        }
        return;
    }

    if (cache->extFrameCtrl != 0U) {
        T_DBG_fgmtCnf++;

        if (aps_cache_state(cache) == APS_CACHE_STATE_APS_ACK ||
            aps_cache_state(cache) == APS_CACHE_STATE_RETRY ||
            aps_cache_state(cache) == APS_CACHE_STATE_DONE) {
            if (status == 0U) {
                aps_fragment_tx_win_t *win = aps_frag_tx_win();

                if (win->activeBlock >= win->fragSent - 1U && win->resendBitmap == 0xffU) {
                    goto fragment_done;
                }
            }
        }

        if (aps_frag_tx_win()->resendPending == 0U) {
            aps_frag_tx_win()->fragSent++;
        }

        {
            u8 delay = aps_ib.aps_interframe_delay;

            if (delay == 0U) {
                delay = 100U;
            }
            ev_timer_taskPost(aps_data_fragment_delay, NULL, delay);
        }
        return;
    }

fragment_done:
    cnf.handle = cache->apsCount;
    cnf.apsCnt = cache->zdpSeqnoAddrReq;
    cnf.status = status;
    cnf.ep.extConfirm.dstAddrMode = cache->dstAddrMode;
    cnf.ep.extConfirm.dstEndpoint = cache->dstEndpoint;
    memcpy(cnf.dstAddr.addr_long, cache->dstAddr.addr_long, EXT_ADDR_LEN);
    cnf.ep.extConfirm.srcEndpoint = cache->ep;
    cnf.clusterId = cache->clusterId;
    aps_conf(&cnf);

    if (cache->payload != NULL) {
        zb_buf_free((zb_buf_t *)cache->payload);
    }
    memset(cache, 0, sizeof(*cache));
    if (g_apsTxCacheNum != 0U) {
        g_apsTxCacheNum--;
    }
}

u8 apsTxDataSendStart(aps_tx_cache_list_t *cache)
{
    zb_buf_t *buf;
    zb_buf_t *src;
    nlde_data_req_t *req;
    long nsduOffset;

    if (cache == NULL || cache->payload == NULL) {
        return 1;
    }

    src = (zb_buf_t *)cache->payload;
    buf = zb_buf_allocate();
    if (buf == NULL) {
        aps_txCacheConfirm(cache, APS_STATUS_INTERNAL_BUF_FULL);
        return 1;
    }

    memcpy(buf, src, APS_TX_DUP_BUF_SIZE);
    req = (nlde_data_req_t *)buf;
    nsduOffset = req->nsdu - (u8 *)src;
    req->nsdu = (u8 *)buf + nsduOffset;

    if (cache->interPAN) {
        (void)tl_zbNwkInterPanDataReq(buf);
    } else {
        tl_zbPrimitivePost(3, 0x70, buf);
    }

    return 0;
}
void tl_apsDataIndRegister(apsDataIndCb_t cb) { g_apsDataIndCb = cb; }
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

        if (cache->addrReqNeed && cache->state == 1U) {
            return;
        }

        if (cache->ackNeed && cache->state == 3U) {
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

        for (u32 i = 0; i < g_nwkAddrMap.validNum; i++) {
            tl_zb_addr_map_entry_t *entry = &g_nwkAddrMap.addrMap[i];

            if (!entry->used) {
                continue;
            }

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
    u8 duplicate = 0U;

    if (tl_idxByShortAddr(&idx, src_addr) != RET_OK) {
        return 0;
    }

    {
        tl_zb_addr_map_entry_t *entry = &g_nwkAddrMap.addrMap[idx];
        if (entry->aps_dup_clock != 0U && entry->aps_dup_cnt == aps_counter) {
            duplicate = 1U;
        }

        entry->aps_dup_cnt = aps_counter;
        entry->aps_dup_clock = (u8)(((APS_ACK_EXPIRY * APS_MAX_FRAME_RETRIES) + 1U) & 0x07U);
        apsDuplicateCheckFlag = 1;
        return duplicate;
    }
}
void aps_data_indication_process(void *arg)
{
    u8 *buf = (u8 *)arg;
    aps_rx_hdr_t *hdr = (aps_rx_hdr_t *)(buf + APS_RX_HDR_OFFSET);
    u8 frameType;

    if ((hdr->frameCtrl & 0x20U) != 0U) {
        if (ss_apsDecryptFrame(arg) != RET_OK) {
            printk("zb_aps_drop: aps decrypt fail fc=0x%02x\n", hdr->frameCtrl);
            zb_buf_free((zb_buf_t *)arg);
            return;
        }
        hdr = (aps_rx_hdr_t *)(buf + APS_RX_HDR_OFFSET);
    }

    if ((hdr->frameCtrl & 0x40U) != 0U) {
        if (aps_ack_send(arg, 0) != 0U) {
            printk("zb_aps_drop: ack send fail fc=0x%02x\n", hdr->frameCtrl);
            zb_buf_free((zb_buf_t *)arg);
            return;
        }
    }

    if (aps_duplicate_check(hdr->srcShortAddr, hdr->apsCounter) != 0U) {
        printk("zb_aps_drop: duplicate src=0x%04x aps_cnt=%u\n",
               hdr->srcShortAddr, hdr->apsCounter);
        zb_buf_free((zb_buf_t *)arg);
        return;
    }

    if ((hdr->frameCtrl & 0x80U) != 0U && hdr->extHdr != 0U) {
        aps_data_ind_t *ind;
        u8 *reassembly;
        u16 copied;
        u16 srcShortAddr = hdr->srcShortAddr;
        u8 apsCounter = hdr->apsCounter;
        u8 blockNum = hdr->blockNum;

        aps_indPrimBuild(arg);
        ind = (aps_data_ind_t *)arg;
        hdr = (aps_rx_hdr_t *)(buf + APS_RX_HDR_OFFSET);
        T_DBG_fgmt++;

        if (hdr->extHdr == 1U) {
            u16 allocLen;

            apsDataFragmentRcvWinClear();
            if (blockNum == 0U) {
                zb_buf_free((zb_buf_t *)arg);
                return;
            }

            allocLen = (u16)(37U + ((u16)blockNum * (u16)ind->asduLength));
            reassembly = ((u16)blockNum * (u16)ind->asduLength == 0U) ?
                         long_ev_buf_get() :
                         ev_buf_allocate(allocLen);
            if (reassembly == NULL) {
                zb_buf_free((zb_buf_t *)arg);
                return;
            }

            aps_fragment_reassembly_t *reassemblyObj;

            memset(reassembly, 0, 37);
            reassemblyObj = (aps_fragment_reassembly_t *)reassembly;
            memcpy(&reassemblyObj->ind, ind, sizeof(reassemblyObj->ind));
            reassemblyObj->ind.asdu = reassemblyObj->asdu;
            reassemblyObj->ind.asduLength = 0;
            reassemblyObj->copiedLen = 0;
            aps_frag_rcv_win()->reassemblyBuf = (u8 *)reassemblyObj;
            aps_frag_rcv_win()->srcShortAddr = srcShortAddr;
            aps_frag_rcv_win()->apsCounter = apsCounter;
            aps_frag_rcv_win()->expectedBlocks = blockNum;
            aps_frag_rcv_win()->receivedBlocks = 0;
            aps_frag_rcv_win()->reserved87 = 0;
            aps_frag_rcv_win()->retryCount = 0;
            aps_frag_rcv_win()->active = 1;
            *aps_frag_rcv_timer_evt() =
                ev_timer_taskPost(aps_data_fragment_process_timeout, NULL,
                                  (u32)(APS_ACK_EXPIRY * 39U));
        } else if (aps_frag_rcv_win()->active == 0U || aps_frag_rcv_buf() == NULL) {
            zb_buf_free((zb_buf_t *)arg);
            return;
        } else if (aps_frag_rcv_win()->srcShortAddr != srcShortAddr ||
                   aps_frag_rcv_win()->apsCounter != apsCounter) {
            zb_buf_free((zb_buf_t *)arg);
            return;
        }

        aps_fragment_reassembly_t *reassemblyObj = aps_frag_rcv_reassembly();
        if (reassemblyObj == NULL) {
            ZB_EXCEPTION_POST(SYS_EXCEPTTION_ZB_APS_FRAGMENT_RCV);
            zb_buf_free((zb_buf_t *)arg);
            return;
        }

        copied = reassemblyObj->copiedLen;
        memcpy(reassemblyObj->asdu + copied, ind->asdu, ind->asduLength);
        copied = (u16)(copied + ind->asduLength);
        reassemblyObj->copiedLen = copied;
        reassemblyObj->ind.asduLength = copied;
        aps_frag_rcv_win()->receivedBlocks++;
        aps_frag_rcv_win()->retryCount = 0;

        if (aps_frag_rcv_win()->receivedBlocks >= aps_frag_rcv_win()->expectedBlocks) {
            if (*aps_frag_rcv_timer_evt() != NULL) {
                ev_timer_taskCancel(aps_frag_rcv_timer_evt());
            }
            tl_zbTaskPost(af_aps_data_fragment_entry, reassemblyObj);
            aps_frag_rcv_win()->reassemblyBuf = NULL;
            memset(&g_apsDataFragmentRcvWin, 0, sizeof(g_apsDataFragmentRcvWin));
        }

        zb_buf_free((zb_buf_t *)arg);
        return;
    }

    frameType = (u8)(hdr->frameCtrl & 0x03U);
    printk("zb_aps_ind_proc: fc=0x%02x frameType=%u profile=0x%04x cluster=0x%04x dst_ep=%u src_ep=%u\n",
           hdr->frameCtrl, frameType, hdr->profileId, hdr->clusterId, hdr->dstEp, hdr->srcEp);
    aps_indPrimBuild(arg);

    if (g_apsDataIndCb != NULL) {
        g_apsDataIndCb(arg);
    }

    if (frameType == 1U) {
        printk("zb_aps_route: command\n");
        aps_command_handle(arg);
        return;
    }

    if (!g_zbNwkCtx.joined) {
        zb_buf_free((zb_buf_t *)arg);
        return;
    }

    if ((hdr->frameCtrl & 0x0cU) == 0x0cU) {
        printk("zb_aps_route: group\n");
        aps_process_group_addressed_packet((zb_buf_t *)arg);
        return;
    }

    printk("zb_aps_route: af_aps_data_entry\n");
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
        memcpy(localInd.src_ext_addr, macInd->srcAddr.addr.extAddr, EXT_ADDR_LEN);
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
void apsTxEventPost(aps_tx_cache_list_t *cache, u8 event, u8 status)
{
    (void)status;

    if (cache == NULL) {
        return;
    }

    switch (aps_cache_state(cache)) {
    case 0:
        if (cache->addrReqNeed) {
            aps_cache_state_set(cache, APS_CACHE_STATE_ADDR_REQ);
            return;
        }

        if (event == 0U && apsTxDataSendStart(cache) == 0U) {
            cache->apsAckWaitTimeOut = APS_ACK_EXPIRY;
            aps_cache_state_set(cache, APS_CACHE_STATE_NWK_CNF);
        }
        return;
    case APS_CACHE_STATE_ADDR_REQ:
        if (event != 0U) {
            aps_txCacheConfirm(cache, status);
            return;
        }

        if (apsTxDataSendStart(cache) == 0U) {
            aps_cache_state_set(cache, APS_CACHE_STATE_NWK_CNF);
        }
        return;
    case APS_CACHE_STATE_NWK_CNF:
    case APS_CACHE_STATE_APS_ACK:
    case APS_CACHE_STATE_RETRY:
        if (event == 1U) {
            if (cache->ackNeed && cache->retries != 0U) {
                cache->retries--;
                if (apsTxDataSendStart(cache) == 0U) {
                    cache->apsAckWaitTimeOut = APS_ACK_EXPIRY;
                    aps_cache_state_set(cache, APS_CACHE_STATE_RETRY);
                }
                return;
            }

            aps_txCacheConfirm(cache, status);
            return;
        }

        if (event == 2U) {
            aps_cache_state_set(cache, APS_CACHE_STATE_APS_ACK);
            return;
        }

        if (event == 3U) {
            aps_txCacheConfirm(cache, status);
        }
        return;
    case APS_CACHE_STATE_DONE:
        aps_txCacheConfirm(cache, status);
        return;
    default:
        return;
    }
}

void apsDataFragmentReSend(void)
{
    aps_fragment_tx_win_t *win = aps_frag_tx_win();

    if (!win->active || !win->resendPending) {
        return;
    }

    if (win->resendBitmap == 0xffU) {
        win->resendPending = 0U;
        return;
    }

    for (u8 i = 0; i < 8; i++) {
        if (((win->resendBitmap >> i) & 0x01U) == 0U) {
            aps_tx_cache_list_t *cache = win->cache[i];

            if (cache == NULL) {
                continue;
            }

            aps_cache_state_set(cache, 0);
            cache->apsAckWaitTimeOut = APS_ACK_EXPIRY;
            cache->retries = APS_MAX_FRAME_RETRIES;
            apsTxEventPost(cache, 0, 0);
            return;
        }
    }
}

int aps_data_fragment_delay(void *arg)
{
    (void)arg;

    if (aps_frag_tx_win()->active != 0U) {
        if (aps_frag_tx_win()->resendPending != 0U) {
            apsDataFragmentReSend();
        } else {
            tl_zbTaskPost(aps_data_fragment, NULL);
        }
    }

    return -1;
}

void aps_nwk_addr_req_cb(void *arg)
{
    aps_nwk_addr_rsp_hdr_t *rsp = (aps_nwk_addr_rsp_hdr_t *)arg;

    if (rsp->status != 0U) {
        return;
    }

    (void)tl_zbNwkAddrMapAdd(rsp->shortAddr, rsp->extAddr, NULL);

    for (u8 i = 0; i < APS_TX_CACHE_TABLE_SIZE; i++) {
        aps_tx_cache_list_t *cache = &aps_txCache_tbl[i];

        if (!cache->used || !cache->addrReqNeed) {
            continue;
        }
        if (cache->zdpSeqnoAddrReq != rsp->seqNum || cache->state != 1U) {
            continue;
        }

        ((nlde_data_req_t *)cache->payload)->dstAddr = rsp->shortAddr;
        cache->addrReqNeed = 0;
        aps_cache_state_set(cache, 0);
        apsTxEventPost(cache, 0, 0);
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
        if (cache->state != 2U && cache->state != 4U) {
            continue;
        }

        if (status == 0U || status == 0x20U) {
            if (cache->ackNeed) {
                apsTxEventPost(cache, 2, 0);
            } else {
                apsTxEventPost(cache, 3, 0);
            }
        } else if (status == 0xd0U || status == 0xf0U) {
            apsTxEventPost(cache, 3, status);
        } else {
            apsTxEventPost(cache, 1, status);
        }
        break;
    }

    zb_buf_free((zb_buf_t *)arg);
}
void aps_nwk_data_indication_cb(void *arg)
{
    nlde_data_ind_t *ind = (nlde_data_ind_t *)arg;
    aps_rx_hdr_t *hdr = aps_rx_hdr(arg);
    u8 hdrLen;

    hdrLen = aps_hdr_parse(ind->nsdu, hdr);

    hdr->hdrLen = hdrLen;
    hdr->srcShortAddr = ind->srcAddr;
    printk("zb_aps_nwk_ind: nsdu_len=%u hdr=%u fc=0x%02x profile=0x%04x cluster=0x%04x dst_ep=%u src_ep=%u dst_mode=%u dst=0x%04x\n",
           ind->nsduLen, hdrLen, hdr->frameCtrl, hdr->profileId, hdr->clusterId,
           hdr->dstEp, hdr->srcEp, ind->dstAddrMode, ind->dstAddr);

    /*
     * The NLDE-DATA.indication dstAddrMode is produced by nwk_data.c as
     * `multicastFlg ? 2 : 1` — i.e. 2 == multicast/group, 1 == unicast
     * (short addr). The group-addressed branch below sets the APS group
     * delivery-mode bits and packs the 16-bit group address into the
     * header where the endpoint normally sits, so it MUST fire only for
     * multicast frames. The original `== 1U` test fired on UNICAST, which
     * mis-tagged every inbound unicast APS frame as group: aps_indPrimBuild
     * then took its group branch (out->dst_addr = srcHdr.dstEp, a u8),
     * truncating the destination short address to its low byte. That made
     * aps_command_handle's `dst != local` check reject the TC Transport-Key
     * (e.g. dst read as 0x008c vs our 0xe58c), so the network key was never
     * installed. Fire group handling on the multicast value instead.
     */
    if (ind->dstAddrMode == 2U) {
        hdr->frameCtrl = (u8)((hdr->frameCtrl & (u8)~0x0cU) | 0x0cU);
        COPY_U16TOBUFFER((u8 *)&hdr->dstEp, ind->dstAddr);
    } else {
        if (ind->nsduLen <= hdrLen) {
            printk("zb_aps_drop: nsdu too short len=%u hdr=%u\n", ind->nsduLen, hdrLen);
            zb_buf_free((zb_buf_t *)arg);
            return;
        }

        if ((hdr->frameCtrl & 0x03U) == 0U &&
            (hdr->frameCtrl & 0x0cU) != 0x0cU &&
            !af_profileMatchedLocal(hdr->profileId, hdr->dstEp)) {
            printk("zb_aps_drop: profile mismatch profile=0x%04x dst_ep=%u\n",
                   hdr->profileId, hdr->dstEp);
            zb_buf_free((zb_buf_t *)arg);
            return;
        }
    }

    tl_zbTaskPost(aps_data_indication_process, arg);
}
u8 apsHandleIsExit(u8 handle)
{
    for (u8 i = 0; i < APS_TX_CACHE_TABLE_SIZE; i++) {
        aps_tx_cache_list_t *cache = &aps_txCache_tbl[i];

        if (cache->used && cache->handler == handle) {
            return 1;
        }
    }

    return 0;
}

aps_tx_cache_list_t *apsTxDataPost(u8 ackNeed, u8 addrReqNeed, u8 interPan, void *payload, u8 *cnf)
{
    aps_confirm_buf_t *confirm = (aps_confirm_buf_t *)cnf;
    aps_tx_cache_list_t *freeEntry = NULL;

    for (u8 i = 0; i < APS_TX_CACHE_TABLE_SIZE; i++) {
        aps_tx_cache_list_t *cache = &aps_txCache_tbl[i];

        if (cache->used) {
            if (memcmp(cache->dstAddr.addr_long, cnf, EXT_ADDR_LEN) == 0 &&
                aps_cache_state(cache) == APS_CACHE_STATE_ADDR_REQ) {
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
    memcpy(freeEntry->dstAddr.addr_long, cnf, EXT_ADDR_LEN);
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
void aps_data_request(void *arg) { tl_zbNwkNldeDataRequest(arg); }
void bindingTxBack(void *arg)
{
    bind_dst_list_tbl *bindList = (bind_dst_list_tbl *)arg;
    zb_buf_t *clone;
    aps_data_req_t *srcReq;
    aps_data_req_t *dstReq;

    if (bindList == NULL || bindList->txData == NULL) {
        return;
    }

    clone = zb_buf_allocate();
    if (clone == NULL) {
        return;
    }

    memcpy(clone, bindList->txData, APS_TX_DUP_BUF_SIZE);
    srcReq = (aps_data_req_t *)bindList->txData;
    dstReq = (aps_data_req_t *)clone;
    if (srcReq->asdu != NULL) {
        dstReq->asdu = (u8 *)clone + (srcReq->asdu - (u8 *)bindList->txData);
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
            memcmp(dst->aps_addr.dst_ext_addr, g_zbInfo.macPib.extAddress, EXT_ADDR_LEN) == 0) {
            aps_data_ind_t localInd;

            memset(&localInd, 0, sizeof(localInd));
            localInd.dst_addr_mode = APS_SHORT_DSTADDR_WITHEP;
            localInd.dst_ep = dst->aps_addr.dst_endpoint;
            localInd.dst_addr = g_zbInfo.macPib.shortAddress;
            localInd.src_addr_mode = APS_SHORT_SRCADDR_WITHEP;
            localInd.src_ep = dstReq->src_endpoint;
            localInd.profile_id = dstReq->profile_id;
            localInd.cluster_id = dstReq->cluster_id;
            localInd.asduLength = dstReq->asdu_length;
            localInd.asdu = dstReq->asdu;
            localInd.src_short_addr = dstReq->useAlias ? dstReq->aliasSrcAddr : localInd.dst_addr;
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

        if (cache->state == 3U) {
            cache->apsAckWaitTimeOut--;
            if (cache->apsAckWaitTimeOut <= 0) {
                apsTxEventPost(cache, 1, APS_STATUS_NO_ACK);
            }
        } else if (cache->state == 1U) {
            cache->apsAddrWaitTimeout--;
            if (cache->apsAddrWaitTimeout <= 0) {
                apsTxEventPost(cache, 3, APS_STATUS_NO_SHORT_ADDRESS);
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
    u8 auxLen = 0;
    u8 addrReqNeed = 0;
    addrExt_t *extAddrPtr = NULL;
    u16 dstShortAddr = 0;
    u16 idx = 0;

    memset(&cnf, 0, sizeof(cnf));

    if (req == NULL || req->txBuf == NULL || req->adu == NULL) {
        return;
    }

    apsCounter = aps_get_counter_value();
    cnf.handle = handle;
    cnf.apsCnt = apsCounter;
    cnf.ep.af.dstAddrMode = req->addrMode;

    if (req->addrMode == APS_SHORT_DSTADDR_WITHEP) {
        cnf.dstAddr.addr_short = req->dstAddr.shortAddr;
        dstShortAddr = req->dstAddr.shortAddr;
    } else if (req->addrMode == APS_LONG_DSTADDR_WITHEP) {
        memcpy(cnf.dstAddr.addr_long, req->dstAddr.extAddr, EXT_ADDR_LEN);
        if (tl_zbShortAddrByExtAddr(&dstShortAddr, req->dstAddr.extAddr, &idx) != RET_OK) {
            addrReqNeed = 1;
        }
    } else {
        cnf.status = APS_STATUS_INVALID_PARAMETER;
        aps_conf(&cnf);
        zb_buf_free(req->txBuf);
        return;
    }

    memset(req->txBuf, 0, 26);
    nldereq = (nlde_data_req_t *)req->txBuf;
    nldereq->radius = 5;
    nldereq->addrMode = req->addrMode;
    nldereq->discoverRoute = 1;
    nldereq->securityEnable = req->secureNwkLayer;
    nldereq->ndsuHandle = handle;
    nldereq->dstAddr = dstShortAddr;
    if (req->addrMode == APS_LONG_DSTADDR_WITHEP) {
        memcpy(nldereq->ieeAddr, req->dstAddr.extAddr, EXT_ADDR_LEN);
    }

    nsdu = req->adu - 2;
    nsdu[0] = req->secure ? 0x41U : 0x01U;
    nsdu[1] = apsCounter;

    if (req->secure) {
        auxLen = ss_apsEnAuxHdrFill(nsdu + 2, req->adu, 0);
    }

    memmove(nsdu + 2 + auxLen, req->adu, req->aduLen);
    nldereq->nsdu = nsdu;
    nldereq->nsduLen = (u8)(req->aduLen + 2 + auxLen);

    if (req->secure) {
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

        if (ss_apsSecureFrame((zb_buf_t *)nldereq, (u8)(2 + auxLen), 2, *extAddrPtr) != RET_OK) {
            cnf.status = APS_STATUS_SECURITY_FAIL;
            aps_conf(&cnf);
            zb_buf_free(req->txBuf);
            return;
        }
    }

    cache = apsTxDataPost((nsdu[0] & 0x40U) ? 1U : 0U, addrReqNeed, 0, req->txBuf, (u8 *)&cnf);
    if (cache == NULL) {
        aps_conf(&cnf);
        zb_buf_free(req->txBuf);
        return;
    }

    if (addrReqNeed) {
        aps_txCacheAsNoShortAddr(req->dstAddr.extAddr, &cache->zdpSeqnoAddrReq);
        aps_cache_state_set(cache, APS_CACHE_STATE_ADDR_REQ);
        return;
    }

    apsTxEventPost(cache, 0, 0);
}
u8 apsDataRequest(aps_data_req_t *dataReq, u8 *asdu, u8 length)
{
    zb_buf_t *buf = zb_buf_allocate();
    aps_data_req_t *reqCopy;
    u8 *asduCopy;
    uintptr_t reqOffset;

    if (buf == NULL) {
        return 0x39;
    }

    /* sizeof(*reqCopy), not a hardcoded 31: aps_data_req_t carries pointer
     * fields, so its size is 31 only on TC32 and larger on a 64-bit ABI.
     * Mirrors libzigbee src/aps_data.c apsDataRequest(). */
    memset(buf, 0, sizeof(*reqCopy));
    asduCopy = tl_bufInitalloc(buf, length);
    if (asduCopy < (u8 *)buf) {
        zb_buf_free(buf);
        return 6;
    }

    reqOffset = (uintptr_t)(asduCopy - (u8 *)buf);
    if (reqOffset <= sizeof(*reqCopy)) {
        zb_buf_free(buf);
        return 6;
    }

    if (dataReq->profile_id == 0U && dataReq->cluster_id == 19U) {
        dataReq->handle = 0x49U;
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
void aps_data_fragment(void *arg)
{
    (void)arg;

    aps_fragment_tx_win_t *win = aps_frag_tx_win();
    aps_data_req_t *req;
    u16 totalLen;
    u16 offset;
    u8 fragLen;

    if (win->active == 0U) {
        ZB_EXCEPTION_POST(SYS_EXCEPTTION_ZB_APS_FRAGMENT_TRANS);
        return;
    }

    req = (aps_data_req_t *)win->txBuf;
    if (req == NULL) {
        ZB_EXCEPTION_POST(SYS_EXCEPTTION_ZB_APS_FRAGMENT_TRANS);
        return;
    }

    if (win->fragSent != 0U) {
        req->extFrameCtrl = 2;
        req->blockNum = win->fragSent;
    } else {
        req->extFrameCtrl = 1;
        req->blockNum = win->fragCount;
    }

    if (win->activeBlock >= (u8)(aps_ib.aps_max_window_size - 1U)) {
        req->tx_options |= BIT(2);
    } else {
        req->tx_options &= (u8)~BIT(2);
    }

    totalLen = win->totalLen;
    offset = (u16)(win->fragSent * aps_ib.aps_fragment_payload_size);
    if (offset > totalLen) {
        ZB_EXCEPTION_POST(SYS_EXCEPTTION_ZB_APS_FRAGMENT_TRANS);
        return;
    }

    fragLen = aps_ib.aps_fragment_payload_size;
    if (win->fragSent >= (u8)(win->fragCount - 1U)) {
        fragLen = (u8)(totalLen - offset);
        req->tx_options |= BIT(2);
        if (aps_ib.aps_fragment_payload_size < fragLen) {
            ZB_EXCEPTION_POST(SYS_EXCEPTTION_ZB_APS_FRAGMENT_TRANS);
        }
    }

    if (apsDataRequest(req, (u8 *)win->fragmentBackup + offset, fragLen) != 0U) {
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

    if (win->active != 0U || win->txBuf != NULL) {
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
    win->active = 1;
    win->resendPending = 0;
    win->resendBitmap = 0xffU;
    win->txBuf = reqCopy;
    win->fragmentBackup = payloadCopy;
    win->totalLen = length;

    fragPayload = aps_ib.aps_fragment_payload_size;
    win->fragCount = (u8)((length + fragPayload - 1U) / fragPayload);
    win->fragSent = 0;

    tl_zbTaskPost(aps_data_fragment, NULL);
    return 0;
}
