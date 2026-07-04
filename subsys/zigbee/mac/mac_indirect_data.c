/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Adapted from libzigbee/src/mac_indirect_data.c. Vendor file kept structurally
 * one-for-one; vendor zb_local.h / mac_trx_api.h / ev_timer.h / mac_phy.h
 * are replaced by the Zephyr include set.
 */
#include "zb_common_stub.h"
#include "common/static_assert.h"
#include "os/ev_timer.h"
#include "mac/includes/tl_zb_mac.h"
#include "mac/includes/tl_zb_mac_pib.h"
#include "mac/includes/mac_phy.h"
#include "mac/includes/mac_trx_api.h"
#include "nwk/includes/nwk.h"
#include "nwk/includes/nwk_neighbor.h"
#include "mac/includes/mac_internal.h"

#if defined(ZB_ROUTER_ROLE)
LIST(macPendingQueue)

static inline int mac_pending_addr_matches(const mac_pending_entry_t *entry,
                                           u8 addrMode,
                                           const u8 *addr)
{
    u8 len = (addrMode == ADDR_MODE_EXT) ? EXT_ADDR_LEN : 2;

    return entry->addrMode == addrMode && memcmp(addr, entry->addr, len) == 0;
}

void macDataPendingListProc(void *arg)
{
    mac_pending_entry_t *entry = (mac_pending_entry_t *)arg;
    void *buf;
    u8 status;

    if (entry == NULL) {
        return;
    }

    if (entry->state != 4U) {
        status = entry->status;
        if (status != MAC_STA_FRAME_PENDING && status != MAC_SUCCESS) {
            entry->state = 1;
            return;
        }
    } else {
        status = entry->status;
    }

    buf = entry->buf;
    tl_zbMaxTxConfirmCb(buf, status);
    listRemove(macPendingQueue, entry);
    ev_buf_free((u8 *)entry);
}

u8 macDataPending(void *buf, u32 dstAddrLo, u32 dstAddrHi, u8 dstAddrMode)
{
    mac_pending_entry_t *entry;
    u8 status = MAC_STA_TRANSACTION_OVERFLOW;
    u8 match[9];

    if (listLength(macPendingQueue) >= ZB_MAC_PENDING_TRANS_QUEUE_SIZE) {
        return status;
    }

    entry = (mac_pending_entry_t *)ev_buf_allocate(sizeof(*entry));
    if (entry == NULL) {
        return status;
    }

    memset(entry, 0, sizeof(*entry));
    listAdd(macPendingQueue, entry);
    entry->buf = buf;

    match[0] = (u8)dstAddrLo;
    match[1] = (u8)(dstAddrLo >> 8);
    match[2] = (u8)(dstAddrLo >> 16);
    match[3] = (u8)(dstAddrLo >> 24);
    match[4] = (u8)dstAddrHi;
    match[5] = (u8)(dstAddrHi >> 8);
    match[6] = (u8)(dstAddrHi >> 16);
    match[7] = (u8)(dstAddrHi >> 24);
    match[8] = dstAddrMode;
    memcpy(entry->addr, match, sizeof(entry->addr));
    entry->addrMode = match[8];

    entry->timeout = (u8)(((((u32)g_zbInfo.macPib.transactionPersistenceTime << 4) -
                            g_zbInfo.macPib.transactionPersistenceTime)
                           << 10) /
                          1000000U);
    entry->expiry = ZB_MAC_EXT_EXPEIRY_CNT;
    entry->state = 1;
    status = MAC_SUCCESS;

    return status;
}

void macDataPendingListManage(void *arg, u8 status)
{
    mac_pending_entry_t *entry = (mac_pending_entry_t *)arg;

    entry->status = status;
    tl_zbTaskPost(macDataPendingListProc, entry);
}

_attribute_ram_code_ u8 tl_zbMacPendingDataCheck(u8 addrMode, u8 *addr, u8 update)
{
    mac_pending_entry_t *entry;

    if (listLength(macPendingQueue) == 0) {
        return MAC_STA_OUT_OF_CAP;
    }

    for (entry = (mac_pending_entry_t *)listHead(macPendingQueue); entry != NULL;
         entry = entry->next) {
        if (!mac_pending_addr_matches(entry, addrMode, addr)) {
            continue;
        }

        if (!update) {
            return MAC_STA_OUT_OF_CAP;
        }

        if ((u8)(entry->state - 1U) > 1U) {
            return MAC_STA_OUT_OF_CAP;
        }

        entry->state = 2;
        return MAC_SUCCESS;
    }

    return MAC_STA_OUT_OF_CAP;
}

_attribute_no_inline_ int tl_zbMacPendingDataSearch(u8 addrMode, u8 *addr)
{
    mac_pending_entry_t *entry;
    u8 count = 0;

    if (listLength(macPendingQueue) == 0) {
        return (s8)count; /* Vendor returns the signed 8-bit count through the int ABI. */
    }

    for (entry = (mac_pending_entry_t *)listHead(macPendingQueue); entry != NULL;
         entry = entry->next) {
        if (!mac_pending_addr_matches(entry, addrMode, addr)) {
            continue;
        }

        /*
         * A matching DataRequest arrived: promote the queued transaction from
         * "buffered" (state 1) to "ready to transmit" (state 2) so the
         * DataRequest handler's delivery loop actually sends it. Without this
         * the entry stays at state 1 forever and the indirect frame (e.g. an
         * AssociationResponse to a joining child) is never delivered — the
         * vendor's split search/check step was lost in the port.
         */
        if (entry->state == 1U) {
            entry->state = 2U;
        }
        count++;
    }

    return (s8)count;
}

void tl_zbMacMlmeDataRequestCb(void *arg)
{
    u8 *req = (u8 *)arg;
    u8 addrMode = req[18];
    u8 *addr = req + 10;
    s8 pendingCnt = (s8)tl_zbMacPendingDataSearch(addrMode, addr);
    mac_pending_entry_t *entry;

    if (listLength(macPendingQueue) == 0) {
        goto post_poll_ind;
    }

    for (entry = (mac_pending_entry_t *)listHead(macPendingQueue); entry != NULL;
         entry = entry->next) {
        zb_buf_t *buf;
        mac_indirect_tx_scratch_t *scratch;
        u8 *txData;
        u8 txStatus;

        if (entry->state != 2U || !mac_pending_addr_matches(entry, addrMode, addr)) {
            continue;
        }

        buf = (zb_buf_t *)entry->buf;
        /* Recover the queued frame {payload ptr, len} — see mac_associate.c. */
        scratch = (mac_indirect_tx_scratch_t *)buf;
        txData = scratch->payload;
        if (pendingCnt > 1) {
            txData[0] |= MAC_FCF_FRAME_PENDING_MASK;
        }

        txStatus = tl_zbMacTx(buf, txData, scratch->psduLen, txData[0] & MAC_FCF_ACK_REQ_BIT, entry);
        entry->state = (txStatus == MAC_SUCCESS) ? 3U : 1U;
        break;
    }

post_poll_ind:
    if (listLength(macPendingQueue) == 0) {
        u8 saved[9];

        /* Preserve the vendor stack/code shape even though req[0] does not alias addr. */
        memcpy(saved, addr, sizeof(saved));
        req[0] = addrMode;
        memcpy(req + 1, saved, EXT_ADDR_LEN);
        tl_zbPrimitivePost(TL_Q_MAC2NWK, MAC_MLME_POLL_IND, req);
        return;
    }

    req[0] = addrMode;
    memcpy(req + 1, addr, EXT_ADDR_LEN);
    tl_zbPrimitivePost(TL_Q_MAC2NWK, MAC_MLME_POLL_IND, req);
}

int macIndirPeriodic(void *arg)
{
    mac_pending_entry_t *entry;

    (void)arg;

    if (listLength(macPendingQueue) == 0) {
        return 0;
    }

    for (entry = (mac_pending_entry_t *)listHead(macPendingQueue); entry != NULL;
         entry = entry->next) {
        if (entry->timeout != 0U) {
            entry->timeout--;
            continue;
        }

        if (entry->expiry != 0U) {
            entry->expiry--;
            continue;
        }

        if ((u8)(entry->state - 3U) <= 1U) {
            entry->state = 4;
            return 0;
        }

        if (entry->buf != NULL) {
            tl_zbMaxTxConfirmCb(entry->buf, MAC_STA_TRANSACTION_EXPIRED);
        }
        listRemove(macPendingQueue, entry);
        ev_buf_free((u8 *)entry);
        return 0;
    }

    return 0;
}
#endif


void tl_zbMacDataRequestStatusCheck(zb_buf_t *buf, u8 status)
{
    u8 savedStatus = ((u8 *)buf)[OFFSETOF(zb_buf_t, hdr) + 1];

    if (savedStatus == MAC_STA_NO_ACK) {
        zb_buf_free(buf);
        tl_zbMacAssocPollConfirm(status);
        return;
    }

    if (savedStatus == MAC_STA_INVALID_PARAMETER) {
        if (status == MAC_SUCCESS) {
            status = MAC_STA_NO_DATA;
        }

        ((u8 *)buf)[0] = status;
        tl_zbPrimitivePost(TL_Q_MAC2NWK, MAC_MLME_POLL_CNF, buf);
        return;
    }

    zb_buf_free(buf);
}

u8 tl_zbMacMlmeDataRequestCmdSend(zb_mlme_data_req_cmd_t *req, zb_buf_t *buf, u8 status)
{
    tl_zb_mac_mhr_t mhr;
    u8 hdrSize;
    u8 *payload;
    u8 txStatus;

    memset(&mhr, 0, sizeof(mhr));
    mhr.dstPanId = g_zbMacPib.panId;
    memcpy(&mhr.dstAddr, &req->dstAddr, sizeof(mhr.dstAddr));
    memcpy(&mhr.srcAddr, &req->srcAddr, sizeof(mhr.srcAddr));
    mhr.frameCtrl = 0x0063U |
                    ((u16)req->dstAddrMode << 10) |
                    ((u16)req->srcAddrMode << 14);

    hdrSize = (u8)(tl_zbMacHdrSize(mhr.frameCtrl) + 1U);
    /*
     * Vendor port bug: tl_zbMacHdrBuilder returns the position AFTER
     * the MAC header (so the caller can write the command byte at
     * payload[0]). The original code reassigned `payload` to that
     * return value and then passed it to tl_zbMacTx as the frame
     * start — which made the radio TX 16 bytes of garbage past the
     * header (sniffer shows FCF=0x0004 = Reserved). The ASSOC_REQ
     * sibling in mac_associate.c gets this right by keeping the
     * original psdu pointer. Preserve the frame-start here too.
     */
    {
        u8 *psdu = tl_bufInitalloc(buf, hdrSize);
        payload = tl_zbMacHdrBuilder(psdu, &mhr);
        payload[0] = MAC_CMD_DATA_REQUEST;

        ((u8 *)buf)[OFFSETOF(zb_buf_t, hdr) + 1] = status;

        txStatus = tl_zbMacTx(buf, psdu, hdrSize, 1, NULL);
    }
    if (txStatus != MAC_SUCCESS) {
        tl_zbMacDataRequestStatusCheck(buf, txStatus);
    }

    return txStatus;
}
