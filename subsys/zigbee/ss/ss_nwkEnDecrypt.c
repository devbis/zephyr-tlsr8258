/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Adapted from libzigbee/src/ss_nwkEnDecrypt.c. Vendor file kept structurally
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
    addrExt_t srcAddr;
    u8 keySeqNum;
} ss_apsNwkAuxFrameHdr_t;

typedef struct _attribute_packed_ {
    addrExt_t srcAddr;
    u32 frameCnt;
    u8 secureCtrl;
} ss_securityCcmNonce_t;

#define SS_CLR_SECURITY_LEVEL(d)    ((*(u8 *)(d)) &= 0xf8U)
#define SS_SET_SECURITY_LEVEL(d, t) do { (*(u8 *)(d)) = (u8)((*(u8 *)(d) & 0xf8U) | (t)); } while (0)
#define NWK_STATIC_PATH_COST_LOCAL  7

u8 T_DBG_decFrameCnt = 0;


_CODE_SS_ static void ss_nwkSecureStatus(void *arg, u16 addrShort, u8 status)
{
    nlme_nwkStatus_ind_t *cmd = (nlme_nwkStatus_ind_t *)arg;

    cmd->status = (nwk_statusCode_t)status;
    cmd->nwkAddr = addrShort;
    tl_zbTaskPost(zdo_nlme_status_indication, arg);
}

_CODE_SS_ u8 ss_nwkSecureFrame(zb_buf_t *src, u8 nwkHdrAuxLen)
{
    u8 ret = RET_OK;
    zb_mscp_data_req_t *req = (zb_mscp_data_req_t *)src;
    ss_apsNwkAuxFrameHdr_t aux;
    u8 auxLen = sizeof(ss_apsNwkAuxFrameHdr_t);
    u8 nwkHdrLen = (u8)(nwkHdrAuxLen - auxLen);

    memset(&aux, 0, sizeof(aux));

    aux.keyIdentifer = SS_SECUR_NWK_KEY;
    aux.extendedNonce = 1;
    aux.frameCnt = ss_ib.outgoingFrameCounter++;
    aux.keySeqNum = ss_ib.activeKeySeqNum;
    memcpy(aux.srcAddr, g_zbMacPib.extAddress, EXT_ADDR_LEN);
    aux.securityLevel = 5;

    memcpy(req->msdu + nwkHdrLen, &aux, auxLen);

    {
        u8 *key = ss_zdoGetNwkKeyBySeqNum(aux.keySeqNum);

        if (key == NULL) {
            ret = RET_ERROR;
        } else {
            ss_securityCcmNonce_t nonce;
            u8 *srcMsg;
            u8 srcMsgLen;
            u8 len;

            memcpy(nonce.srcAddr, aux.srcAddr, EXT_ADDR_LEN);
            nonce.frameCnt = aux.frameCnt;
            nonce.secureCtrl = *(u8 *)&aux;

            srcMsg = req->msdu + nwkHdrAuxLen;
            srcMsgLen = (u8)(req->msduLength - nwkHdrAuxLen);
            len = ss_ccmEncryption(key, (u8 *)&nonce, nwkHdrAuxLen, req->msdu, srcMsgLen, srcMsg);

            req->msduLength = (u8)(nwkHdrAuxLen + len);
            SS_CLR_SECURITY_LEVEL(req->msdu + nwkHdrLen);
        }
    }

    return ret;
}

_CODE_SS_ u8 ss_nwkDecryptFrame(void *p, u8 nwkHdrSize, u8 payloadSize, u8 *payloadAddr, nwk_hdr_t *nwkHdr, u8 lqi)
{
    zb_buf_t *nsdu = (zb_buf_t *)p;
    zb_mscp_data_ind_t *pInd = (zb_mscp_data_ind_t *)p;
    tl_zb_normal_neighbor_entry_t *nbe = NULL;
    tl_zb_normal_neighbor_entry_t *curNbe;
    tl_zb_normal_neighbor_entry_t *nbEntyBackup = NULL;
    ss_apsNwkAuxFrameHdr_t aux;
    ss_securityCcmNonce_t nonce;
    u8 auxLen = sizeof(ss_apsNwkAuxFrameHdr_t);
    u16 addrRef = 0;
    u8 ret = RET_OK;
    bool validationNewNeighbor = FALSE;
    u16 neighborAddr = pInd->srcAddr.addr.shortAddr;
    u8 *key;

    nwkHdrSize = (u8)(nwkHdrSize - auxLen);
    SS_SET_SECURITY_LEVEL(payloadAddr + nwkHdrSize, 5);
    memcpy(&aux, payloadAddr + nwkHdrSize, auxLen);

    payloadSize = (u8)(payloadSize - nwkHdrSize);
    payloadSize = (u8)(payloadSize - auxLen);

    if (!nwkHdr->frameControl.multicastFlg) {
        ret = tl_zbNwkAddrMapAdd(neighborAddr, aux.srcAddr, &addrRef);
    } else {
        ret = tl_idxByShortAddr(&addrRef, neighborAddr);
    }

    if (ret == RET_OK) {
        nbe = tl_zbNeighborTableSearchFromAddrmapIdx(addrRef);

        if (nbe != NULL &&
            nbe->relationship != NEIGHBOR_IS_CHILD &&
            nbe->relationship != NEIGHBOR_IS_UNAUTH_CHILD) {
            nbe->rxOnWhileIdle = 1;

            if (nbe->relationship == NEIGHBOR_IS_NONE_OF_ABOVE) {
                nbe->relationship = NEIGHBOR_IS_SIBLING;
            }
        } else if (nbe == NULL) {
            nbEntyBackup = (tl_zb_normal_neighbor_entry_t *)zb_buf_allocate();
            if (nbEntyBackup != NULL) {
                memset(nbEntyBackup, 0, sizeof(tl_zb_normal_neighbor_entry_t));
                nbEntyBackup->addrmapIdx = addrRef;
                nbEntyBackup->deviceType = NWK_DEVICE_TYPE_ROUTER;
                nbEntyBackup->relationship = NEIGHBOR_IS_SIBLING;
                nbEntyBackup->rxOnWhileIdle = 1;
                nbEntyBackup->lqi = lqi;
                nbEntyBackup->outgoingCost = NWK_STATIC_PATH_COST_LOCAL;
                nbe = tl_zbNeighborTableUpdate(nbEntyBackup, 0);
                if (nbe != NULL) {
                    zb_buf_free((zb_buf_t *)nbEntyBackup);
                    nbEntyBackup = NULL;
                }
                validationNewNeighbor = TRUE;
            }
        }
    }

    curNbe = (nbe != NULL) ? nbe : nbEntyBackup;

    if (ret == RET_OK && curNbe != NULL) {
        if (aux.keySeqNum == 0U) {
            aux.keySeqNum = ss_ib.activeKeySeqNum;
        }

        key = ss_zdoGetNwkKeyBySeqNum(aux.keySeqNum);

        if (curNbe->relationship == NEIGHBOR_IS_CHILD) {
            curNbe->relationship = NEIGHBOR_IS_UNAUTH_CHILD;
        }

        if (curNbe->keySeqNum != aux.keySeqNum) {
            curNbe->incomingFrameCnt = 0;
            curNbe->keySeqNum = aux.keySeqNum;
        }

        if (key == NULL) {
            ret = RET_ERROR;
            ss_nwkSecureStatus(nsdu, neighborAddr, NWK_COMMAND_STATUS_BAD_KEY_SEQUENCE_NUMBER);
        } else if ((curNbe->incomingFrameCnt > aux.frameCnt) || (curNbe->incomingFrameCnt == (u32)~0U)) {
            ret = RET_ERROR;
            ss_nwkSecureStatus(nsdu, neighborAddr, NWK_COMMAND_STATUS_BAD_FRAME_COUNTER);
        } else {
            curNbe->incomingFrameCnt = aux.frameCnt;
        }
    } else {
        ss_nwkSecureStatus(nsdu, neighborAddr, NWK_COMMAND_STATUS_BAD_KEY_SEQUENCE_NUMBER);
        ret = RET_ERROR;
    }

    if (ret == RET_OK) {
        if (payloadSize < ZB_CCM_M) {
            ret = RET_ERROR;
            ss_nwkSecureStatus(nsdu, neighborAddr, NWK_COMMAND_STATUS_BAD_KEY_SEQUENCE_NUMBER);
        }

        if (ret == RET_OK) {
            memcpy(nonce.srcAddr, aux.srcAddr, EXT_ADDR_LEN);
            nonce.frameCnt = aux.frameCnt;
            nonce.secureCtrl = *(u8 *)&aux;
            ret = ss_ccmDecryption(key, (u8 *)&nonce, (u8)(nwkHdrSize + auxLen), payloadAddr, payloadSize,
                                   payloadAddr + auxLen + nwkHdrSize);
        }

        if (ret == RET_OK) {
            if (curNbe->relationship == NEIGHBOR_IS_UNAUTH_CHILD) {
#if defined(ZB_ROUTER_ROLE)
                if (curNbe->deviceType == NWK_DEVICE_TYPE_ROUTER) {
                    curNbe->relationship = NEIGHBOR_IS_SIBLING;
                } else if (curNbe->deviceType == NWK_DEVICE_TYPE_ED) {
                    curNbe->relationship = NEIGHBOR_IS_CHILD;
                    ss_zdoChildTableStore(curNbe);
                }
#else
                curNbe->relationship = NEIGHBOR_IS_CHILD;
#endif
            }
        } else {
            if (validationNewNeighbor && nbe != NULL) {
                tl_zbNeighborTableDelete(nbe);
            } else if (nbEntyBackup != NULL) {
                zb_buf_free((zb_buf_t *)nbEntyBackup);
                nbEntyBackup = NULL;
            }
            ss_nwkSecureStatus(nsdu, neighborAddr, NWK_COMMAND_STATUS_BAD_KEY_SEQUENCE_NUMBER);
        }
    } else if (nbEntyBackup != NULL) {
        zb_buf_free((zb_buf_t *)nbEntyBackup);
        nbEntyBackup = NULL;
    }

    if (ret == RET_OK &&
        aux.keySeqNum != ss_ib.activeKeySeqNum &&
        (u8)(aux.keySeqNum - ss_ib.activeKeySeqNum) < (u8)127) {
        ss_zdoNwkKeySwitch(aux.keySeqNum);
    }

    return ret;
}
