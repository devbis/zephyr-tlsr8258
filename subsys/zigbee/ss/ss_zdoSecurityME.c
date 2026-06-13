/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Adapted from libzigbee/src/ss_zdoSecurityME.c. Vendor file kept structurally
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

#if 0 /* vendor-pinned offsets disabled in Zephyr port */
STATIC_ASSERT(OFFSETOF(zdo_nwk_manager_t, authEvt) == 4);
STATIC_ASSERT(OFFSETOF(zdo_nwk_manager_t, savedBuf) == 20);
STATIC_ASSERT(sizeof(zdo_nwk_manager_t) == 38);
#if defined(ZB_ED_ROLE)
STATIC_ASSERT(OFFSETOF(zb_info_t, bdbAttr.nodeIsOnANetwork) == 170);
#endif
#endif

#if defined(ZB_ROUTER_ROLE)
typedef struct _attribute_packed_ {
    addrExt_t srcAddr;
    addrExt_t devAddr;
    u16 devShortAddr;
    u8 useParent;
    u8 rejoinNwk;
    bool secureRejoin;
} ss_zdo_auth_req_t;

STATIC_ASSERT(sizeof(ss_zdo_auth_req_t) == 21);
STATIC_ASSERT(sizeof(zb_addrForNeighbor_t) == 12);

extern void nwkRoutingTabEntryDstDel(u16 dstAddr);

static inline u8 ss_zdo_child_auth_status(const ss_zdo_auth_req_t *req)
{
    u8 state = (u8)(((req->rejoinNwk != 0U) << 1) | (req->secureRejoin != 0U));

    if (state == 0U) {
        return SS_STANDARD_DEV_UNSECURED_JOIN;
    }
    if (state == 3U) {
        return SS_STANDARD_DEV_SECURED_REJOIN;
    }
    if (state == 2U) {
        return SS_STANDARD_DEV_TC_REJOIN;
    }
    return 0xffU;
}
#endif

static inline void build_join_confirm(void *buf)
{
    nlme_join_cnf_t *cnf = (nlme_join_cnf_t *)buf;

    cnf->nwkAddr = g_zbInfo.nwkNib.nwkAddr;
    cnf->status = 0;
    cnf->activeChannel = g_zbInfo.macPib.phyChannelCur;
    memcpy(cnf->extPANId, g_zbInfo.nwkNib.extPANId, EXT_ADDR_LEN);
}

int ss_devKeyPairTimeoutCb(void *arg)
{
    (void)arg;

    if (ss_ib.keyPairSetNew != NULL) {
        memset(ss_ib.keyPairSetNew, 0, 40);
    }

    return -1;
}

void ss_zdoInsecureRejoin(void *arg)
{
    (void)arg;

    memcpy(aps_ib.aps_use_ext_panid, g_zbInfo.nwkNib.extPANId, EXT_ADDR_LEN);
    aps_ib.aps_use_insecure_join = 1;
    aps_ib.aps_authenticated = 0;

    if (zdo_nwkRejoinStart(1UL << g_zbInfo.macPib.phyChannelCur,
                           zdo_cfg_attributes.config_nwk_scan_duration) == ZDO_INSUFFICIENT_SPACE) {
        tl_zbTaskPost(ss_zdoInsecureRejoin, NULL);
    }
}

bool ss_securityModeIsDistributed(void)
{
    return memcmp(ss_ib.trust_center_address, g_invalid_addr, EXT_ADDR_LEN) == 0;
}
void ss_zdoTransportKeyIndHandle(void *arg)
{
    ss_apsmeTransportKeyInd_t *ind = (ss_apsmeTransportKeyInd_t *)arg;

    if (ind->keyType == SS_STANDARD_NETWORK_KEY) {
        if (ss_securityModeIsDistributed()) {
            u16 shortAddr = 0;

            (void)tl_zbNwkAddrMapAdd(0, ind->srcAddr, &shortAddr);
        }

        if (ss_ib.preConfiguredKeyType != SS_PRECONFIGURED_NWKKEY &&
            !ZB_IS_16BYTE_SECURITY_KEY_ZERO(ind->key)) {
            bool duplicate = FALSE;
            u8 idx = (u8)((ss_ib.activeSecureMaterialIndex + ind->keySeqNum - ss_ib.activeKeySeqNum) & 0x01U);

            for (u8 i = 0; i < SECUR_N_SECUR_MATERIAL; i++) {
                if (memcmp(ss_ib.nwkSecurMaterialSet[i].key, ind->key, SEC_KEY_LEN) == 0 &&
                    ss_ib.nwkSecurMaterialSet[i].keySeqNum == ind->keySeqNum &&
                    ss_ib.nwkSecurMaterialSet[i].keyType == ind->keyType) {
                    duplicate = TRUE;
                    break;
                }
            }

            if (!duplicate) {
                memcpy(ss_ib.nwkSecurMaterialSet[idx].key, ind->key, SEC_KEY_LEN);
                ss_ib.nwkSecurMaterialSet[idx].keySeqNum = ind->keySeqNum;
                ss_ib.nwkSecurMaterialSet[idx].keyType = ind->keyType;
                if (ind->keySeqNum != ss_ib.activeKeySeqNum) {
                    ss_ib.activeKeySeqNum = ind->keySeqNum;
                    ss_ib.reserved = (u8)((ss_ib.reserved & (u8)~0x30U) | ((idx & 0x03U) << 4));
                }
            }
        }

        if (aps_ib.aps_authenticated) {
            zb_buf_free((zb_buf_t *)arg);
            return;
        }

        if (zdo_nwk_mngr()->authEvt == NULL) {
            zb_buf_free((zb_buf_t *)arg);
            return;
        }

        if (zdo_nwk_mngr()->savedBuf != NULL) {
            zb_buf_free((zb_buf_t *)arg);
            zdo_nwk_mngr()->savedBuf = NULL;
        }

        ev_timer_taskCancel((ev_timer_event_t **)zdo_nwk_mngr()->authEvt);

        if (ss_ib.preConfiguredKeyType != SS_PRECONFIGURED_NWKKEY &&
            !ZB_IS_16BYTE_SECURITY_KEY_ZERO(ind->key)) {
            ss_ib.reserved = (u8)((ss_ib.reserved & (u8)~0x30U) |
                                  ((ss_ib.activeSecureMaterialIndex & 0x03U) << 4));
        }

        aps_ib.aps_authenticated = 1;
        build_join_confirm(arg);
        tl_zbTaskPost(zdo_nlme_join_confirm, arg);
        return;
    }

    if (ind->keyType != SS_TC_LINK_KEY) {
        zb_buf_free((zb_buf_t *)arg);
        return;
    }

    if (g_ssDevKeyPair.pTimeoutEvt != NULL) {
        ev_timer_taskCancel(&g_ssDevKeyPair.pTimeoutEvt);
    }
    memset(&g_ssDevKeyPair.pTimeoutEvt, 0, sizeof(g_ssDevKeyPair.pTimeoutEvt));

    memcpy(g_ssDevKeyPair.keyPair.device_address, ind->srcAddr, EXT_ADDR_LEN);
    memcpy(g_ssDevKeyPair.keyPair.linkKey, ind->key, SEC_KEY_LEN);
    g_ssDevKeyPair.keyPair.keyAttr = SS_UNVERIFIED_KEY;
    g_ssDevKeyPair.keyPair.apsLinkKeyType = SS_UNIQUE_LINK_KEY;
    g_ssDevKeyPair.keyPair.used = 1;
    g_ssDevKeyPair.keyPair.rsv = 0;
    g_ssDevKeyPair.keyPair.outgoingFrameCounter = 0;
    g_ssDevKeyPair.keyPair.incomingFrameCounter = 0;
    ss_ib.tcLinkKeyType = 0;

    tl_zbTaskPost(ss_apsmeVerifyKeyReq, arg);
    g_ssDevKeyPair.pTimeoutEvt = ev_timer_taskPost(ss_devKeyPairTimeoutCb, NULL, ss_ib.ssTimeoutPeriod);
}

#if defined(ZB_ROUTER_ROLE)
void ss_zdoChildTableStore(void *arg)
{
    tl_zb_normal_neighbor_entry_t *entry = (tl_zb_normal_neighbor_entry_t *)arg;
    zb_addrForNeighbor_t *info;

    if (entry == NULL) {
        return;
    }

    info = (zb_addrForNeighbor_t *)ev_buf_allocate(sizeof(zb_addrForNeighbor_t));
    if (info == NULL) {
        return;
    }

    memset(info, 0, sizeof(*info));
    info->shortAddr = tl_zbshortAddrByIdx(entry->addrmapIdx);
    tl_zbExtAddrByIdx(entry->addrmapIdx, info->extAddr);
    info->depth = entry->depth;
    info->rxOnWhileIdle = entry->rxOnWhileIdle;
    info->deviceType = entry->deviceType;
    info->relationship = entry->relationship;
    info->used = entry->used;
    tl_zbTaskPost(nwk_nodeAddrInfoStore, info);
}

void ss_zdoChildAuthStart(void *arg)
{
    ss_zdo_auth_req_t req;
    tl_zb_normal_neighbor_entry_t *entry;

    memcpy(&req, arg, sizeof(req));
    entry = nwk_neTblGetByExtAddr(req.devAddr);
    if (entry != NULL &&
        entry->relationship == NEIGHBOR_IS_UNAUTH_CHILD &&
        ss_zdo_child_auth_status(&req) == SS_STANDARD_DEV_UNSECURED_JOIN) {
        ss_zdoChildTableStore(entry);
    }

    zb_buf_free((zb_buf_t *)arg);
}

void ss_zdoUpdateDeviceIndHandle(void *arg)
{
    ss_apsmeUpdateDeviceInd_t *ind = (ss_apsmeUpdateDeviceInd_t *)arg;
    u16 addrMapIdx;

    if (ind->status != SS_DEV_LEFT) {
        zb_buf_free((zb_buf_t *)arg);
        return;
    }

    if (tl_idxByExtAddr(&addrMapIdx, ind->devAddr) != RET_OK &&
        tl_idxByShortAddr(&addrMapIdx, ind->devShortAddr) != RET_OK) {
        zb_buf_free((zb_buf_t *)arg);
        return;
    }

    if (zdoAppIndCbLst != NULL && zdoAppIndCbLst->zdpLeaveIndCb != NULL) {
        nlme_leave_ind_t leaveInd;

        memset(&leaveInd, 0, sizeof(leaveInd));
        tl_zbExtAddrByIdx(addrMapIdx, leaveInd.deviceAddr);
        if (!ZB_IS_64BIT_ADDR_ZERO(leaveInd.deviceAddr)) {
            zdoAppIndCbLst->zdpLeaveIndCb(&leaveInd);
        }
    }

    nwkRoutingTabEntryDstDel(tl_zbshortAddrByIdx(addrMapIdx));
    aps_bindingTblEntryDelByDstExtAddr(ind->devAddr);
    tl_nwkNeighborDeleteByAddrmapIdx(addrMapIdx);
    tl_zbNwkAddrMapDelete(addrMapIdx);
    zb_buf_free((zb_buf_t *)arg);
}

void ss_zdoRemoveDeviceIndHandle(void *arg)
{
    ss_apsmeRemoveDeviceInd_t *ind = (ss_apsmeRemoveDeviceInd_t *)arg;
    nlme_leave_req_t *req = (nlme_leave_req_t *)arg;

    if (memcmp(ind->tcAddr, ss_ib.trust_center_address, EXT_ADDR_LEN) != 0) {
        zb_buf_free((zb_buf_t *)arg);
        return;
    }

    if (memcmp(ind->childExtAddr, g_zbInfo.macPib.extAddress, EXT_ADDR_LEN) == 0) {
        memset(req->deviceAddr, 0, EXT_ADDR_LEN);
    } else {
        memcpy(req->deviceAddr, ind->childExtAddr, EXT_ADDR_LEN);
    }

    req->removeChildren = 1;
    req->rejoin = 0;
    tl_zbNwkNlmeLeaveRequest(arg);
}
#endif

void ss_zdoNwkKeySwitch(u8 keySeqNum)
{
    u8 idx;

    if (ss_ib.nwkSecurMaterialSet[0].keySeqNum == keySeqNum) {
        idx = 0;
    } else if (ss_ib.nwkSecurMaterialSet[1].keySeqNum == keySeqNum) {
        idx = 1;
    } else {
        return;
    }

    if (ss_ib.activeKeySeqNum == keySeqNum) {
        return;
    }

    ss_ib.reserved = (u8)((ss_ib.reserved & (u8)~0x30U) | (idx << 4));
    ss_ib.activeKeySeqNum = keySeqNum;

    if (ss_ib.outgoingFrameCounter > 0x80000000UL) {
        ss_ib.outgoingFrameCounter = 0;
    }

    tl_neighborFrameCntReset();
    nv_nwkFrameCountSaveToFlash(ss_ib.outgoingFrameCounter);
    zdo_ssInfoSaveToFlash();
}

void ss_zdoNwkKeyConfigure(u8 *key, u8 keySeqNum, bool active)
{
    u8 idx = keySeqNum;

    if (idx > 1U) {
        ZB_EXCEPTION_POST(SYS_EXCEPTTION_ZB_SS_KEY_INDEX);
        return;
    }

    memcpy(ss_ib.nwkSecurMaterialSet[idx].key, key, SEC_KEY_LEN);
    ss_ib.nwkSecurMaterialSet[idx].keySeqNum = keySeqNum;
    ss_ib.nwkSecurMaterialSet[idx].keyType = active ? 1U : 0U;
}

void ss_zdoLinkKeyConfigure(addrExt_t extAddr, u8 *key, u8 keyAttr, u8 apsLinkKeyType)
{
    ss_dev_pair_set_t keyPair;

    memcpy(keyPair.linkKey, key, SEC_KEY_LEN);
    keyPair.keyAttr = keyAttr;
    keyPair.apsLinkKeyType = apsLinkKeyType;
    keyPair.outgoingFrameCounter = 0;
    keyPair.incomingFrameCounter = 0;
    memcpy(keyPair.device_address, extAddr, EXT_ADDR_LEN);
    ss_devKeyPairSave(&keyPair);
}

void ss_zdoUseKey(u8 keySeqNum)
{
    if (keySeqNum > 1U) {
        ZB_EXCEPTION_POST(SYS_EXCEPTTION_ZB_SS_KEY_INDEX);
        return;
    }

    ss_ib.activeKeySeqNum = keySeqNum;
    ss_ib.reserved = (u8)((ss_ib.reserved & (u8)~0x30U) | ((keySeqNum & 0x03U) << 4));
}
u8 ss_keyIsEmpty(const u8 *key, u8 len) { while (len--) { if (*key++) return 0; } return 1; }

bool ss_keyPreconfigured(void)
{
    return ss_keyIsEmpty(ss_ib.nwkSecurMaterialSet[ss_ib.activeSecureMaterialIndex].key, 16) ? FALSE : TRUE;
}

void *ss_zdoGetNwkKeyBySeqNum(u8 seqNum)
{
    for (u8 i = 0; i < SECUR_N_SECUR_MATERIAL; i++) {
        u8 idx = (u8)((ss_ib.activeSecureMaterialIndex + i) & 0x01U);

        if (ss_ib.nwkSecurMaterialSet[idx].keySeqNum == seqNum) {
            return ss_ib.nwkSecurMaterialSet[idx].key;
        }
    }

    return NULL;
}

void zdo_ssInfoUpdate(void)
{
    if (ss_ib.outgoingFrameCounter < 1024U &&
        g_bdbAttrs.nodeIsOnANetwork != 0U &&
        g_bdbCtx.forceJoin == 0U) {
        ss_ib.outgoingFrameCounter++;
        nv_nwkFrameCountSaveToFlash(ss_ib.outgoingFrameCounter);
    }
}

void ss_zdoInit(bool enSecurity)
{
    u32 frameCounter = 0;

    if (!g_zbNwkCtx.is_factory_new && zdo_ssInfoInit() == 0U) {
        aps_ib.aps_authenticated = 1;
        tl_neighborFrameCntReset();
        ss_devKeyPairInfoGet();

        if (!ZB_IS_64BIT_ADDR_ZERO(ss_ib.trust_center_address) &&
            !ZB_IS_64BIT_ADDR_INVALID(ss_ib.trust_center_address)) {
            u16 shortAddr = 0;
            tl_zbNwkAddrMapAdd(0, ss_ib.trust_center_address, &shortAddr);
        }
        return;
    }

    ss_ib.ssTimeoutPeriod = 5000;
    ss_ib.securityLevel = 0;

    if (enSecurity) {
        ss_ib.securityLevel = 5;
        ss_ib.secureAllFresh = 1;
        ss_ib.preConfiguredKeyType = 0;
        ss_ib.devKeyPairNum = 0;
    }

    if (nv_nwkFrameCountFromFlash(&frameCounter) == 0U) {
        ss_ib.outgoingFrameCounter = frameCounter;
    } else {
        ss_ib.outgoingFrameCounter = 0;
    }
}

void ss_securityModeSet(ss_securityMode_e m)
{
    if (aps_ib.aps_authenticated &&
        !ZB_IS_64BIT_ADDR_ZERO(ss_ib.trust_center_address) &&
        !ZB_IS_64BIT_ADDR_INVALID(ss_ib.trust_center_address)) {
        ZB_EXCEPTION_POST(SYS_EXCEPTTION_ZB_NWK_GET_ENTRY);
        return;
    }

    if (m == SS_SEMODE_DISTRIBUTED) {
        ss_ib.tcPolicy.updateTCLKrequired = 0;
        memcpy(ss_ib.trust_center_address, g_invalid_addr, EXT_ADDR_LEN);
    } else if (m == SS_SEMODE_CENTRALIZED) {
        ss_ib.tcPolicy.updateTCLKrequired = 1;
        memset(ss_ib.trust_center_address, 0, EXT_ADDR_LEN);
    }
}
