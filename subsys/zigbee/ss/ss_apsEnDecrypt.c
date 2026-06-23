/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Adapted from libzigbee/src/ss_apsEnDecrypt.c. Vendor file kept structurally
 * one-for-one; vendor zb_local.h / ev_timer.h / nwk_neighbor.h /
 * security_service.h includes are replaced by the Zephyr include
 * set.
 */
#include "zb_common_stub.h"
#include "common/static_assert.h"
#include "os/ev_timer.h"
#include "mac/includes/tl_zb_mac.h"
#include "mac/includes/tl_zb_mac_pib.h"
#include "nwk/includes/nwk.h"
#include "nwk/includes/nwk_internal.h"
#include "nwk/includes/nwk_neighbor.h"
#include "aps/aps_api.h"
#include "aps/aps_internal.h"
#include "zdo/zdo_api.h"
#include "zdo/zdo_internal.h"
#include "ss/security_service.h"
#include "ss/ss_internal.h"

typedef struct _attribute_packed_ {
    u8 securityLevel:3;
    u8 keyIdentifer:2;
    u8 extendedNonce:1;
    u8 reserved:2;
    u32 frameCnt;
} ss_apsEncryAuxCommonHdr_t;

typedef struct _attribute_packed_ {
    u8 securityLevel:3;
    u8 keyIdentifer:2;
    u8 extendedNonce:1;
    u8 reserved:2;
    u32 frameCnt;
    addrExt_t srcAddr;
    u8 keySeqNum;
} ss_apsNwkAuxFrameHdr_t;

typedef struct _attribute_packed_ {
    addrExt_t srcAddr;
    u32 frameCnt;
    u8 secureCtrl;
} ss_securityCcmNonce_t;

typedef struct _attribute_packed_ {
    u8 apsHdrLen;
    u8 frameCtrl;
    u8 dstEp;
    u8 srcEp;
    u8 clusterIdLo;
    u8 clusterIdHi;
    u8 profileIdLo;
    u8 profileIdHi;
    u8 srcAddrLo;
    u8 srcAddrHi;
    u8 apsCounter;
    u8 extHdr;
    u8 blockNum;
} ss_apsHdrParsed_t;

#define SS_CLR_SECURITY_LEVEL(d)    ((*(u8 *)(d)) &= 0xf8U)
#define SS_SET_SECURITY_LEVEL(d, v) ((*(u8 *)(d)) = (u8)((*(u8 *)(d) & 0xf8U) | ((v) & 0x07U)))
#define SS_AUX_NONCE_INCLUDE(d)     ((d) & BIT(5))

u8 g_zbDefaultLinkKeyEn;

/*
 * Latched raw-frame dump for the FIRST inbound APS-secured (transport-key)
 * frame ss_apsDecryptFrame sees after boot. zb_aps_rx_dump_len stays 0 until
 * latched, so a single lucky association is enough to capture the frame for
 * SWS readout + offline decryption — no dependence on flaky join timing.
 * [0] = the raw ind->nsdu bytes (APS hdr + aux + cipher + MIC).
 */
volatile u8 zb_aps_rx_dump[80] = {0};
volatile u8 zb_aps_rx_dump_len = 0U;     /* bytes copied; set last to mark complete */
volatile u8 zb_aps_rx_dump_nsdulen = 0U; /* ind->nsduLen as seen by APS */
volatile u8 zb_aps_rx_dump_seq = 0U;     /* increments each latched capture */

/*
 * Per-layer length probes for the first large (>50 byte) inbound frame,
 * latched once after boot. Pins down which layer drops the 9 bytes that
 * truncate ind->nsduLen (54 -> 45) on the transport-key frame.
 *   zb_dbg_mac_*  written by mac_data.c tl_zbPhyMldeIndication
 *   zb_dbg_nwk_*  written by nwk_data.c  nwkNldeDataInd
 */
volatile u8 zb_dbg_mac_payloadlen = 0U;
volatile u8 zb_dbg_mac_hdrlen = 0U;
volatile u8 zb_dbg_mac_seq = 0U;
volatile u8 zb_dbg_nwk_msdulen = 0U;
volatile u8 zb_dbg_nwk_framehdr = 0U;
volatile u8 zb_dbg_nwk_seq = 0U;

/*
 * Join-completion chain probes (post-CCM-fix): trace why the router obtains
 * the network key but never emits a Device Announce. Each global is written
 * by the corresponding link in the transport-key -> announce chain.
 *   zb_dbg_cmdh   aps_command_handle() reached for cmdId==5 (transport key)  [count]
 *   zb_dbg_tk     ss_zdoTransportKeyIndHandle gate bitmap:
 *                   bit0 reached(STD_NWK) bit1 aps_authenticated
 *                   bit2 authEvt==NULL    bit3 posted zdo_nlme_join_confirm
 *   zb_dbg_jc_cnt    zdo_nlme_join_confirm invocation count
 *   zb_dbg_jc_state  last state seen by zdo_nlme_join_confirm
 *   zb_dbg_jc_status last status seen by zdo_nlme_join_confirm
 *   zb_dbg_jc_path   bit0 shortcut(line908) bit1 armed authEvt(line913) bit2 startDevCnf
 *   zb_dbg_anns      zdo_device_announce_send() reached  [count]
 */
volatile u8 zb_dbg_cmdh = 0U;
volatile u8 zb_dbg_tk = 0U;
volatile u8 zb_dbg_jc_cnt = 0U;
volatile u8 zb_dbg_jc_state = 0U;
volatile u8 zb_dbg_jc_status = 0U;
volatile u8 zb_dbg_jc_path = 0U;
volatile u8 zb_dbg_anns = 0U;
volatile u16 zb_dbg_dst = 0xeeeeU;      /* ind->dst_addr for a cmdId==5 frame */
volatile u16 zb_dbg_local = 0xeeeeU;    /* g_zbInfo.nwkNib.nwkAddr at that time */
volatile u16 zb_dbg_macshort = 0xeeeeU; /* g_zbInfo.macPib.shortAddress at that time */
/* which short-addr assignment sites ran: bit0 assoc-cnf(nwk_join:342)
 * bit1 rejoin-rand(:411) bit2 rejoinRsp(:456) bit3 rejoinRsp(:488) */
volatile u8 zb_dbg_addrpath = 0U;
/* raw first 16 bytes of the aps_data_ind_t buffer at aps_command_handle
 * (cmdId==5), to locate where dst_addr's high byte is lost. */
volatile u8 zb_dbg_apsind[16] = {0};
volatile u8 zb_dbg_apsind_seq = 0U;
/* router TX-path diag (remove once join E2E):
 *   zb_dbg_afds       af_dataSend entry count
 *   zb_dbg_afds_post  af_dataSend NWK_NLDE_DATA_REQ posts
 *   zb_dbg_nwktx      nwk_tx entry count
 *   zb_dbg_nwktx_bail bit0 !joined-bail, bit1 encrypt-fail, bit7 reached MAC TX */
volatile u8 zb_dbg_afds = 0U;
volatile u8 zb_dbg_afds_post = 0U;
volatile u8 zb_dbg_afds_rc = 0xeeU;  /* last tl_zbPrimitivePost return from af_dataSend */
volatile u8 zb_dbg_nwktx = 0U;
volatile u8 zb_dbg_nwktx_bail = 0U;
/* tl_zbNwkNldeDataRequestHandler entry diag:
 *   zb_dbg_nlde   handler entry count
 *   zb_dbg_nlde_g bit0 !joined, bit1 secAllFrames, bit2 user_state!=IDLE,
 *                 bit3 dstAddr==self, bit7 reached fwd/tx */
volatile u8 zb_dbg_nlde = 0U;
volatile u8 zb_dbg_nlde_g = 0U;
/* tl_zbNwkTaskProc HIGH2NWK pop diag:
 *   zb_dbg_h2n_cnt  total HIGH2NWK tasks popped
 *   zb_dbg_h2n_last last primitive (hdr.id) popped
 *   zb_dbg_h2n_nlde count of NWK_NLDE_DATA_REQ (0x70) popped */
volatile u16 zb_dbg_h2n_cnt = 0U;
volatile u8 zb_dbg_h2n_last = 0U;
volatile u8 zb_dbg_h2n_nlde = 0U;
/* ev_timer_execute_cb wedge diag:
 *   zb_dbg_tmr_cb   fn-ptr of the timer callback about to be invoked
 *   zb_dbg_tmr_done fn-ptr of the last callback that RETURNED
 *   zb_dbg_tmr_cnt  total callbacks invoked
 * On a post-join wedge, zb_dbg_tmr_cb != zb_dbg_tmr_done identifies the hung cb. */
volatile u32 zb_dbg_tmr_cb = 0U;
volatile u32 zb_dbg_tmr_done = 0U;
volatile u16 zb_dbg_tmr_cnt = 0U;
/* fatal-error capture (k_sys_fatal_error_handler override in zb_main.c):
 *   zb_dbg_fault_cnt    number of fatal errors seen
 *   zb_dbg_fault_reason Zephyr fatal reason code
 *   zb_dbg_fault_pc/_lr esf->pc / esf->lr at the fault */
volatile u32 zb_dbg_fault_cnt = 0U;
volatile u32 zb_dbg_fault_reason = 0xeeeeeeeeU;
volatile u32 zb_dbg_fault_pc = 0U;
volatile u32 zb_dbg_fault_lr = 0U;


_CODE_SS_ u8 ss_apsEnAuxHdrFill(u8 *auxHdr, void *keyInfo, u8 extNonceOpt)
{
    u8 *p = auxHdr + 5;
    u8 *key = (u8 *)keyInfo;

    COPY_U32TOBUFFER(auxHdr + 1, ss_ib.outgoingFrameCounter);
    ss_ib.outgoingFrameCounter++;

    auxHdr[0] = (u8)((auxHdr[0] & (u8)~0x07U) | 0x05U);

    if (key != NULL) {
        auxHdr[0] |= 0x20U;
        memcpy(auxHdr + 5, g_zbMacPib.extAddress, EXT_ADDR_LEN);
        p = auxHdr + 13;

        if (key[0] != 5U) {
            if (key[1] == 1U) {
                auxHdr[0] = (u8)((auxHdr[0] & (u8)~0x18U) | 0x10U);
            } else {
                auxHdr[0] |= 0x18U;
            }
        } else {
            auxHdr[0] &= (u8)~0x18U;
        }
    } else if ((extNonceOpt & 0x20U) != 0U) {
        auxHdr[0] |= 0x20U;
        memcpy(auxHdr + 5, g_zbMacPib.extAddress, EXT_ADDR_LEN);
        p = auxHdr + 13;
        auxHdr[0] &= (u8)~0x18U;
    } else {
        auxHdr[0] &= (u8)~0x18U;
    }

    return (u8)(p - auxHdr);
}

_CODE_SS_ static void ss_apsSecureStatus(void *arg, u16 addrShort, aps_status_t status)
{
    nlme_nwkStatus_ind_t *cmd = (nlme_nwkStatus_ind_t *)arg;

    cmd->status = (nwk_statusCode_t)status;
    cmd->nwkAddr = addrShort;
    tl_zbTaskPost(zdo_nlme_status_indication, arg);
}

_CODE_SS_ u8 ss_apsSecureFrame(void *p, u8 apsHdrAuxLen, u8 apsHdrLen, addrExt_t extAddr)
{
    nlde_data_req_t *nldereq = (nlde_data_req_t *)p;
    ss_apsNwkAuxFrameHdr_t aux;
    ss_securityCcmNonce_t nonce;
    ss_dev_pair_set_t keyPair;
    u8 *payloadAddr;
    u8 *key = NULL;
    u8 keyTemp[SEC_KEY_LEN];
    u8 *msgStartAddr = nldereq->nsdu;
    bool haveKeyPair = FALSE;

    memset(&aux, 0, sizeof(aux));
    memset(&nonce, 0, sizeof(nonce));
    memset(&keyPair, 0, sizeof(keyPair));
    memset(keyTemp, 0, sizeof(keyTemp));

    payloadAddr = msgStartAddr + apsHdrAuxLen;
    msgStartAddr[0] |= 0x20U;
    memcpy(&aux, msgStartAddr + apsHdrLen, (u16)(apsHdrAuxLen - apsHdrLen));

    if (aux.keyIdentifer == SS_SECUR_NWK_KEY) {
        key = ss_ib.nwkSecurMaterialSet[ss_ib.activeSecureMaterialIndex].key;
    } else {
        haveKeyPair = (ss_devKeyPairFind(extAddr, &keyPair) == NV_SUCC);
        if (!haveKeyPair && ss_ib.preConfiguredKeyType == SS_PRECONFIGURED_UNIQUELLINKKEY) {
            ss_apsSecureStatus(p, nldereq->dstAddr, APS_STATUS_SECURITY_FAIL);
            return RET_ERROR;
        }

        if (haveKeyPair) {
            key = keyPair.linkKey;
        } else {
            key = ss_securityModeIsDistributed() ? ss_ib.distributeLinkKey : ss_ib.tcLinkKey;
        }

        if (aux.keyIdentifer != SS_SECUR_DATA_KEY) {
            u8 pad = (aux.keyIdentifer == SS_SECUR_KEY_LOAD_KEY) ? 2U : 0U;
            if (ss_keyHash(&pad, key, keyTemp) == RET_OK) {
                key = keyTemp;
            }
        }
    }

    nonce.frameCnt = aux.frameCnt;
    aux.securityLevel = 5;
    nonce.secureCtrl = *(u8 *)&aux;
    memcpy(nonce.srcAddr, g_zbMacPib.extAddress, EXT_ADDR_LEN);

    {
        u8 srcMsgLen = (u8)(nldereq->nsduLen - apsHdrAuxLen);
        u8 len = ss_ccmEncryption(key, (u8 *)&nonce, apsHdrAuxLen, msgStartAddr, srcMsgLen, payloadAddr);

        nldereq->nsduLen = (u8)(apsHdrAuxLen + len);
    }

    SS_CLR_SECURITY_LEVEL(msgStartAddr + apsHdrLen);
    return RET_OK;
}

_CODE_SS_ u8 ss_apsDecryptFrame(void *arg)
{
    nlde_data_ind_t *ind = (nlde_data_ind_t *)arg;
    /*
     * Vendor pinned the apsHdr pointer at +20 (matching the libzigbee
     * vendor-build's sizeof(nlde_data_ind_t)). aps_data.c uses the
     * portable computation APS_RX_HDR_OFFSET = (sizeof(nlde_data_ind_t)+3)
     * & ~3 so producer/consumer agree on Zephyr's layout. On TC32 the two
     * happen to coincide (sizeof = 20), but keeping them in sync via the
     * same expression here avoids a class of silent garbage-read bugs if
     * nlde_data_ind_t ever changes (and matches the rationale in the
     * APS_RX_HDR_OFFSET comment in aps_data.c).
     */
    ss_apsHdrParsed_t *apsHdr = (ss_apsHdrParsed_t *)
        ((u8 *)arg + ((sizeof(nlde_data_ind_t) + 3U) & ~((size_t)3U)));
    ss_apsEncryAuxCommonHdr_t aux;
    ss_securityCcmNonce_t nonce;
    ss_dev_pair_set_t keyPair;
    u8 keyTemp[SEC_KEY_LEN];
    u8 *auxStart;
    u8 *cursor;
    u8 *key = NULL;
    u8 ret;
    extern volatile u32 zb_nwk_ed_trace[];

    /*
     * slot[47] low 16 = entry count, bits 16-23 = byte 0 of aux header
     * (secCtrl), bits 24-31 = result bitmap (0x40 ccm-fail, 0x80 ok).
     * slot[46] = first 4 bytes of ind->nsdu (LE) — should be the APS
     * frame_ctrl + counter + secCtrl + frame_counter low byte for a
     * properly-routed Transport-Key frame.
     */
    {
        u32 prev = zb_nwk_ed_trace[47];
        u8 secCtrl = ind->nsdu[apsHdr->apsHdrLen];
        zb_nwk_ed_trace[47] = (prev & 0xff000000U) | ((u32)secCtrl << 16) |
                              (((prev & 0xffffU) + 1U) & 0xffffU);
        zb_nwk_ed_trace[46] = ((u32)ind->nsdu[0]) |
                              ((u32)ind->nsdu[1] << 8) |
                              ((u32)ind->nsdu[2] << 16) |
                              ((u32)ind->nsdu[3] << 24);
    }

    memset(&aux, 0, sizeof(aux));
    memset(&nonce, 0, sizeof(nonce));
    memset(&keyPair, 0, sizeof(keyPair));
    memset(keyTemp, 0, sizeof(keyTemp));

    auxStart = ind->nsdu + apsHdr->apsHdrLen;
    cursor = auxStart;
    memcpy(&aux, auxStart, sizeof(aux));
    cursor += sizeof(aux);

    /*
     * Latch the FIRST APS-secured frame's raw bytes for SWS readout +
     * offline decryption. Holds until reboot so flaky association timing
     * doesn't matter. Lets us confirm whether ind->nsduLen / cipher are
     * truncated (the cipherLen=26-vs-35 question).
     */
    {
        if (zb_aps_rx_dump_seq == 0U) {
            /*
             * Dump a FIXED 64 bytes from ind->nsdu (NOT limited to nsduLen)
             * so we can see whether the bytes past nsduLen hold the rest of
             * the transport-key (last dst-IEEE byte + 8-byte src-IEEE + MIC).
             * If present, the frame is intact and only nsduLen is short;
             * if zero/garbage, the RX path truncated the frame.
             */
            for (u8 i = 0; i < 64U; i++) {
                zb_aps_rx_dump[i] = ind->nsdu[i];
            }
            zb_aps_rx_dump_nsdulen = ind->nsduLen;
            zb_aps_rx_dump_len = 64U;
            zb_aps_rx_dump_seq = 1U; /* set last: marks capture complete */
        }
    }

    if (aux.frameCnt == 0xffffffffUL || aux.keyIdentifer == SS_SECUR_NWK_KEY) {
        return RET_ERROR;
    }

    if (tl_zbExtAddrByShortAddr(ind->srcAddr, nonce.srcAddr, NULL) == TL_RETURN_INVALID &&
        aps_ib.aps_authenticated) {
        return RET_ERROR;
    }

    if (SS_AUX_NONCE_INCLUDE(*auxStart) != 0U) {
        memcpy(nonce.srcAddr, cursor, EXT_ADDR_LEN);
        cursor += EXT_ADDR_LEN;
    }

    if (ss_ib.preConfiguredKeyType == SS_PRECONFIGURED_UNIQUELLINKKEY) {
        return RET_ERROR;
    }
    /*
     * Vendor used:
     *   key = ss_securityModeIsDistributed()
     *           ? ss_ib.distributeLinkKey
     *           : ss_ib.tcLinkKey;
     * where ss_securityModeIsDistributed() returns TRUE iff
     * ss_ib.trust_center_address is the all-FF invalid sentinel. That
     * conflates two distinct states:
     *   (a) genuinely distributed network — no TC exists, distributed
     *       key is the right one;
     *   (b) centralized network during initial join — we just haven't
     *       LEARNED the TC IEEE yet (it arrives inside the Transport-
     *       Key payload we are about to decrypt). The TC's outgoing
     *       Transport-Key Command is APS-encrypted with the TC link
     *       key K_L = "ZigBeeAlliance09" (or whatever was preconfigured),
     *       not the distributed key. Picking distributeLinkKey here
     *       makes ss_keyHash derive the wrong K_T and CCM fails on
     *       every inbound Transport-Key. Verified via slot[45] capture:
     *       key[0..3] = 81 42 86 86 (= first 4 bytes of
     *       linkKeyDistributedMaster) instead of the expected
     *       5a 69 67 42 (= 'Z','i','g','B' from tcLinkKeyCentralDefault).
     * Fix: pick the link key based on whether the inbound aux flagged
     * KEY_TRANSPORT_KEY / KEY_LOAD_KEY (= keyed-hash of TC link key)
     * versus another keyId, NOT on TC-address state. For a Transport-
     * Key frame on initial join, tcLinkKey is the only sensible choice.
     */
    if (aux.keyIdentifer == SS_SECUR_KEY_TRANSPORT_KEY ||
        aux.keyIdentifer == SS_SECUR_KEY_LOAD_KEY) {
        key = ss_ib.tcLinkKey;
    } else {
        key = ss_securityModeIsDistributed() ? ss_ib.distributeLinkKey
                                             : ss_ib.tcLinkKey;
    }

    SS_SET_SECURITY_LEVEL(auxStart, 5);

    if (keyPair.apsLinkKeyType == SS_UNIQUE_LINK_KEY) {
        if (keyPair.incomingFrameCounter > aux.frameCnt) {
            return RET_ERROR;
        }
        keyPair.incomingFrameCounter = aux.frameCnt + 1;
    }

    if (aux.keyIdentifer != SS_SECUR_DATA_KEY) {
        u8 pad = (aux.keyIdentifer == SS_SECUR_KEY_LOAD_KEY) ? 2U : 0U;

        /* slot[45]: first 4 bytes of `key` BEFORE ss_keyHash. Expected
         * 'Z','i','g','B' = 5a 69 67 42 if key points to tcLinkKeyCentralDefault. */
        zb_nwk_ed_trace[45] = ((u32)key[0]) | ((u32)key[1] << 8) |
                              ((u32)key[2] << 16) | ((u32)key[3] << 24);

        if (ss_keyHash(&pad, key, keyTemp) == RET_OK) {
            key = keyTemp;
        }

        /* slot[45]: first 4 bytes of the DERIVED key (output of ss_keyHash).
         * Expected 4b ab 0f 17 (HMAC-MMO(tcLinkKey, pad=0x00)). Confirms the
         * key derivation runs correctly in THIS build (drv_aes_encrypt path). */
        zb_nwk_ed_trace[45] = ((u32)key[0]) | ((u32)key[1] << 8) |
                              ((u32)key[2] << 16) | ((u32)key[3] << 24);
    }

    nonce.frameCnt = aux.frameCnt;
    aux.securityLevel = 5;
    nonce.secureCtrl = *(u8 *)&aux;

    /* slot[44]: first 4 bytes of nonce.srcAddr (LE) — expected
     * 60 2d ce fe (TC IEEE LE first 4) since this is a TC Transport-Key
     * frame with extendedNonce=1. */
    zb_nwk_ed_trace[44] = ((u32)nonce.srcAddr[0]) | ((u32)nonce.srcAddr[1] << 8) |
                          ((u32)nonce.srcAddr[2] << 16) | ((u32)nonce.srcAddr[3] << 24);

    /* slot[46]: secureCtrl byte put into nonce (low8) + frameCnt low byte
     * (next 8) + AAD-len (next 8) + last secCtrl (next 8). */
    zb_nwk_ed_trace[46] = ((u32)nonce.secureCtrl) |
                          ((u32)(nonce.frameCnt & 0xff) << 8) |
                          ((u32)((cursor - ind->nsdu) & 0xff) << 16) |
                          ((u32)ind->nsdu[2] << 24);

    {
        u8 aadLen = (u8)(cursor - ind->nsdu);
        u8 srcMsgLen = (u8)((ind->nsdu + ind->nsduLen) - cursor);
        u8 cipherLen = (u8)(srcMsgLen - 4U);
        u8 *micPtr = cursor + cipherLen;

        /* slot[43]: cipherLen (low8) | ind->nsduLen (8) | aadLen (8) | srcMsgLen (8).
         * For a TC Transport-Key (NWK key) command, expect plaintext = cmdId(1)
         * + key(16) + keySeq(1) + dstIeee(8) + srcIeee(8) = 34, so cipherLen=34
         * (0x22), srcMsgLen=38 (0x26), aadLen=15 (0x0f). A wrong cipherLen here
         * is the prime suspect: it poisons b0[14..15] and the CBC-MAC length,
         * failing CCM identically under any implementation. */
        zb_nwk_ed_trace[43] = ((u32)cipherLen) | ((u32)ind->nsduLen << 8) |
                              ((u32)aadLen << 16) | ((u32)srcMsgLen << 24);
        /* slot[42]: first 4 ciphertext bytes (encrypted; just for sanity that
         * cursor points at payload, not header). */
        zb_nwk_ed_trace[42] = ((u32)cursor[0]) | ((u32)cursor[1] << 8) |
                              ((u32)cursor[2] << 16) | ((u32)cursor[3] << 24);
        /* slot[41]: the 4 on-wire MIC bytes. */
        zb_nwk_ed_trace[41] = ((u32)micPtr[0]) | ((u32)micPtr[1] << 8) |
                              ((u32)micPtr[2] << 16) | ((u32)micPtr[3] << 24);
    }

    ret = ss_ccmDecryption(key,
                           (u8 *)&nonce,
                           (u8)(cursor - ind->nsdu),
                           ind->nsdu,
                           (u8)((ind->nsdu + ind->nsduLen) - cursor),
                           cursor);
    if (ret == RET_OK) {
        ind->nsduLen = (u8)(ind->nsduLen - ((cursor - auxStart) + 4));
        ind->nsdu = ind->nsdu + (cursor - auxStart);
        zb_nwk_ed_trace[47] = (zb_nwk_ed_trace[47] & 0x00ffffffU) | 0x80000000U;
    } else {
        zb_nwk_ed_trace[47] = (zb_nwk_ed_trace[47] & 0x00ffffffU) | 0x40000000U;
    }

    return ret;
}

void ss_pubLinkKeySelect(ss_pubLinkKeyOpt_e ks)
{
    g_zbDefaultLinkKeyEn = (u8)ks;
}
