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

    memset(&aux, 0, sizeof(aux));
    memset(&nonce, 0, sizeof(nonce));
    memset(&keyPair, 0, sizeof(keyPair));
    memset(keyTemp, 0, sizeof(keyTemp));

    auxStart = ind->nsdu + apsHdr->apsHdrLen;
    cursor = auxStart;
    memcpy(&aux, auxStart, sizeof(aux));
    cursor += sizeof(aux);

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

        if (ss_keyHash(&pad, key, keyTemp) == RET_OK) {
            key = keyTemp;
        }
    }

    nonce.frameCnt = aux.frameCnt;
    aux.securityLevel = 5;
    nonce.secureCtrl = *(u8 *)&aux;

    ret = ss_ccmDecryption(key,
                           (u8 *)&nonce,
                           (u8)(cursor - ind->nsdu),
                           ind->nsdu,
                           (u8)((ind->nsdu + ind->nsduLen) - cursor),
                           cursor);
    if (ret == RET_OK) {
        ind->nsduLen = (u8)(ind->nsduLen - ((cursor - auxStart) + 4));
        ind->nsdu = ind->nsdu + (cursor - auxStart);
    }

    return ret;
}

void ss_pubLinkKeySelect(ss_pubLinkKeyOpt_e ks)
{
    g_zbDefaultLinkKeyEn = (u8)ks;
}
