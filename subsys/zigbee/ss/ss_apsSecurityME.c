/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Adapted from libzigbee/src/ss_apsSecurityME.c. Vendor file kept structurally
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

enum {
    APS_CMD_TRANSPORT_KEY_ID = 5,
    APS_CMD_UPDATE_DEVICE_ID = 6,
    APS_CMD_REMOVE_DEVICE_ID = 7,
    APS_CMD_REQUEST_KEY_ID = 8,
    APS_CMD_SWITCH_KEY_ID = 9,
    APS_CMD_VERIFY_KEY_ID = 15,
    APS_CMD_CONFIRM_KEY_ID = 16,
};

typedef struct _attribute_packed_ {
    u8 cmdId;
    u8 keyType;
    addrExt_t partnerAddr;
} ss_request_key_cmd_t;

typedef struct _attribute_packed_ {
    u8 cmdId;
    u8 keyType;
    addrExt_t srcAddr;
    u8 hashVal[SEC_KEY_LEN];
} ss_verify_key_cmd_t;

typedef struct _attribute_packed_ {
    u8 cmdId;
    u8 status;
    u8 keyType;
    addrExt_t dstAddr;
} ss_confirm_key_cmd_t;

typedef struct _attribute_packed_ {
    u8 cmdId;
    u8 keySeqNum;
} ss_switch_key_cmd_t;

static inline void cmd_req_init(aps_cmd_send_req_t *req)
{
    memset(req, 0, sizeof(*req));
}

static inline u32 rd_le32(const u8 *p)
{
    return (u32)p[0] |
           ((u32)p[1] << 8) |
           ((u32)p[2] << 16) |
           ((u32)p[3] << 24);
}

static inline bool ext_addr_is_local(const addrExt_t extAddr)
{
    return memcmp(extAddr, g_zbInfo.macPib.extAddress, EXT_ADDR_LEN) == 0;
}

#if 0 /* vendor-pinned offset disabled in Zephyr port */
STATIC_ASSERT(OFFSETOF(ss_info_base_t, tcLinkKeyType) == 0x4c);
#endif

#if defined(ZB_ROUTER_ROLE)

static inline u8 ss_update_dev_mode(void)
{
    /* Use tcLinkKeyType as the update-device security-mode selector: vendor layout stores
       the mode in this field (observed in router objects). */
    return ss_ib.tcLinkKeyType;
}

_attribute_no_inline_ static void ss_apsmeUpdateDevReqSend(zb_buf_t *buf, u8 secure)
{
    ss_apsmeUpdateDeviceReq_t *req = (ss_apsmeUpdateDeviceReq_t *)buf;
    aps_cmd_send_req_t cmdReq;
    u8 *payload = tl_bufInitalloc(buf, 12);

    payload[0] = APS_CMD_UPDATE_DEVICE_ID;
    memcpy(payload + 1, req->devAddr, EXT_ADDR_LEN);
    payload[9] = LO_UINT16(req->devShortAddr);
    payload[10] = HI_UINT16(req->devShortAddr);
    payload[11] = req->status;

    cmd_req_init(&cmdReq);
    cmdReq.txBuf = buf;
    cmdReq.adu = payload;
    cmdReq.addrMode = ADDR_MODE_SHORT;
    cmdReq.aduLen = 12;
    cmdReq.secure = secure;
    cmdReq.secureNwkLayer = 1;

    aps_cmd_send(&cmdReq, APS_CMD_HANDLE_UPDATE_DEVICE);
}

static int updateDeviceSendAgain(void *arg)
{
    ss_apsmeUpdateDevReqSend((zb_buf_t *)arg, 1);

    return -1;
}
#endif

ss_info_base_t ss_ib;
ss_dev_keyPair_t g_ssDevKeyPair;

u32 ss_outgoingFrameCntGet(void) { return ss_ib.outgoingFrameCounter; }

void ss_devKeyPairSave(ss_dev_pair_set_t *keyPair)
{
    if (nv_flashWriteNew(0, NV_MODULE_KEYPAIR, NV_ITEM_SS_KEY_PAIR, sizeof(ss_dev_pair_set_t), (u8 *)keyPair) == NV_SUCC) {
        ss_ib.devKeyPairNum++;
    }
}

u8 ss_devKeyPairFind(addrExt_t extAddr, ss_dev_pair_set_t *keyPair)
{
    itemIfno_t info = {0, 0};
    nv_sts_t ret = nv_flashReadNew(0, NV_MODULE_KEYPAIR, ITEM_FIELD_IDLE, sizeof(ss_dev_pair_set_t), (u8 *)&info);
    ss_dev_pair_set_t candidate = {{0}, {0}, 0, 0, 0, 0, 0, 0};
    ss_dev_pair_set_t best = {{0}, {0}, 0, 0, 0, 0, 0, 0};
    bool haveCandidate = FALSE;

    if (ret != NV_SUCC) {
        return ret;
    }

    for (u16 i = 0; i <= info.opIndex; i++) {
        if (nv_flashReadByIndex(NV_MODULE_KEYPAIR,
                                NV_ITEM_SS_KEY_PAIR,
                                info.opSect,
                                i,
                                sizeof(ss_dev_pair_set_t),
                                (u8 *)&candidate) == NV_ITEM_NOT_FOUND) {
            continue;
        }

        if (!haveCandidate && candidate.apsLinkKeyType == 1U) {
            memcpy(&best, &candidate, sizeof(candidate));
            haveCandidate = TRUE;
            ret = NV_SUCC;
        }

        if (candidate.apsLinkKeyType != 0U) {
            continue;
        }

        if (candidate.keyAttr == SS_UNVERIFIED_KEY) {
            if (memcmp(extAddr, candidate.device_address, EXT_ADDR_LEN) == 0) {
                memcpy(&best, &candidate, sizeof(candidate));
                haveCandidate = TRUE;
                ret = NV_SUCC;
            }
        } else if (candidate.keyAttr == SS_VERIFIED_KEY) {
            if (memcmp(extAddr, candidate.device_address, EXT_ADDR_LEN) == 0) {
                memcpy(keyPair, &candidate, sizeof(candidate));
                return NV_SUCC;
            }
        }
    }

    if (haveCandidate) {
        memcpy(keyPair, &best, sizeof(best));
        return NV_SUCC;
    }

    return ret;
}

u8 ss_devKeyPairDelete(addrExt_t extAddr)
{
    itemIfno_t info = {0, 0};
    nv_sts_t ret = nv_flashReadNew(0, NV_MODULE_KEYPAIR, ITEM_FIELD_IDLE, sizeof(ss_dev_pair_set_t), (u8 *)&info);
    ss_dev_pair_set_t keyPair;

    if (ret != NV_SUCC) {
        return ret;
    }

    for (u16 i = 0; i <= info.opIndex; i++) {
        if (nv_flashReadByIndex(NV_MODULE_KEYPAIR,
                                NV_ITEM_SS_KEY_PAIR,
                                info.opSect,
                                i,
                                sizeof(ss_dev_pair_set_t),
                                (u8 *)&keyPair) != NV_SUCC) {
            continue;
        }

        if (memcmp(extAddr, keyPair.device_address, EXT_ADDR_LEN) == 0) {
            ret = nv_itemDeleteByIndex(NV_MODULE_KEYPAIR, NV_ITEM_SS_KEY_PAIR, info.opSect, i);
            ss_ib.devKeyPairNum--;
            return ret;
        }
    }

    return NV_ITEM_NOT_FOUND;
}

u16 ss_devKeyPairInfoGet(void)
{
    itemIfno_t info = {0, 0};

    ss_ib.devKeyPairNum = 0;
    if (nv_flashReadNew(0, NV_MODULE_KEYPAIR, ITEM_FIELD_IDLE, sizeof(ss_dev_pair_set_t), (u8 *)&info) != NV_SUCC) {
        return ss_ib.devKeyPairNum;
    }

    for (u16 i = 0; i <= info.opIndex; i++) {
        ss_dev_pair_set_t keyPair;

        if (nv_flashReadByIndex(NV_MODULE_KEYPAIR,
                                NV_ITEM_SS_KEY_PAIR,
                                info.opSect,
                                i,
                                sizeof(ss_dev_pair_set_t),
                                (u8 *)&keyPair) == NV_SUCC) {
            ss_ib.devKeyPairNum++;
        }
    }

    return ss_ib.devKeyPairNum;
}

u16 ss_nodeMacAddrFromdevKeyPair(u16 start_idx, u8 num, u8 *validNum, addrExt_t *nodeMacAddrList)
{
    itemIfno_t info = {0, 0};
    u8 count = 0;
    u16 seen = 0;

    *validNum = 0;
    if (num == 0U) {
        return ss_ib.devKeyPairNum;
    }

    if (nv_flashReadNew(0, NV_MODULE_KEYPAIR, ITEM_FIELD_IDLE, sizeof(ss_dev_pair_set_t), (u8 *)&info) != NV_SUCC) {
        return ss_ib.devKeyPairNum;
    }

    for (u16 i = 0; count < num && i <= info.opIndex; i++) {
        ss_dev_pair_set_t keyPair;

        if (nv_flashReadByIndex(NV_MODULE_KEYPAIR,
                                NV_ITEM_SS_KEY_PAIR,
                                info.opSect,
                                i,
                                sizeof(ss_dev_pair_set_t),
                                (u8 *)&keyPair) != NV_SUCC) {
            continue;
        }

        if (seen >= start_idx) {
            memcpy(nodeMacAddrList, keyPair.device_address, EXT_ADDR_LEN);
            nodeMacAddrList++;
            count++;
            *validNum = count;
        }

        seen++;
    }

    return ss_ib.devKeyPairNum;
}

void ss_nwkKeyGenerate(u8 *nwkKey)
{
    for (u8 i = 0; i < 16U; i++) {
        nwkKey[i] = (u8)(drv_u32Rand() >> 4);
    }

    memcpy(ss_ib.nwkSecurMaterialSet[0].key, nwkKey, SEC_KEY_LEN);
    ss_ib.reserved &= (u8)~0x30U;
    ss_ib.activeKeySeqNum = 0;
    ss_ib.nwkSecurMaterialSet[0].keySeqNum = 0;
}

void ss_nwkKeyStore(u8 *nwkKey)
{
    memcpy(ss_ib.nwkSecurMaterialSet[0].key, nwkKey, SEC_KEY_LEN);
    ss_ib.reserved &= (u8)~0x30U;
    ss_ib.activeKeySeqNum = 0;
    ss_ib.nwkSecurMaterialSet[0].keySeqNum = 0;
}

void ss_apsmeRequestKeyReq(void *arg)
{
    ss_apsmeRequestKeyReq_t *req = (ss_apsmeRequestKeyReq_t *)arg;
    aps_cmd_send_req_t cmdReq;
    u8 *payload;
    u8 payloadLen;

    if (!ss_ib.tcPolicy.updateTCLKrequired ||
        (req->keyType != SS_KEYREQ_TYPE_APPLK && req->keyType != SS_KEYREQ_TYPE_TCLK)) {
        zb_buf_free((zb_buf_t *)arg);
        return;
    }

    payloadLen = (req->keyType == SS_KEYREQ_TYPE_APPLK) ? sizeof(ss_request_key_cmd_t) : 2U;
    payload = tl_bufInitalloc((zb_buf_t *)arg, payloadLen);
    payload[0] = APS_CMD_REQUEST_KEY_ID;
    payload[1] = req->keyType;
    if (req->keyType == SS_KEYREQ_TYPE_APPLK) {
        memcpy(payload + 2, req->partnerAddr, EXT_ADDR_LEN);
    }

    cmd_req_init(&cmdReq);
    cmdReq.txBuf = (zb_buf_t *)arg;
    cmdReq.adu = payload;
    cmdReq.addrMode = req->dstAddrMode;
    cmdReq.aduLen = payloadLen;
    cmdReq.secure = 1;
    cmdReq.secureNwkLayer = 1;
    if (req->dstAddrMode == ADDR_MODE_SHORT) {
        cmdReq.dstAddr.shortAddr = req->dstAddr.shortAddr;
    } else if (req->dstAddrMode == ADDR_MODE_EXT) {
        memcpy(cmdReq.dstAddr.extAddr, req->dstAddr.extAddr, EXT_ADDR_LEN);
    }

    aps_cmd_send(&cmdReq, APS_CMD_HANDLE_REQUEST_KEY);
}

#if defined(ZB_ROUTER_ROLE)
void ss_apsmeUpdateDevReq(void *arg)
{
    u8 mode = ss_update_dev_mode();

    if (mode == 0U) {
        /*
         * NWK-secured, APS-plaintext Update-Device. Full APS-layer security
         * (secure=1) would need the TC's ext address mapped for short 0x0000
         * (tl_zbExtAddrPtrByShortAddr) to derive the APS nonce — the minimal
         * router doesn't map it, so aps_cmd_send fails with SECURITY_FAIL. The
         * native_sim host TC also only does NWK-layer crypto, so it needs the
         * APS command in the clear. Send NWK-secured only.
         */
        ss_apsmeUpdateDevReqSend((zb_buf_t *)arg, 0);
        return;
    }

    if (mode == 1U) {
        if (aps_ib.aps_updateDevice_holdApsSecurity != 0U) {
            ss_apsmeUpdateDevReqSend((zb_buf_t *)arg, 0);
            return;
        }

        zb_buf_t *retryBuf = zb_buf_allocate();

        if (retryBuf == NULL) {
            ss_apsmeUpdateDevReqSend((zb_buf_t *)arg, 0);
            return;
        }

        memcpy(retryBuf, arg, sizeof(ss_apsmeUpdateDeviceReq_t));
        ev_timer_taskPost(updateDeviceSendAgain, retryBuf, 20);
        return;
    }

    zb_buf_free((zb_buf_t *)arg);
}

void ss_apsmeRemoveDeviceReq(void *arg)
{
    ss_apsmeRemoveDeviceReq_t *req = (ss_apsmeRemoveDeviceReq_t *)arg;
    aps_cmd_send_req_t cmdReq;
    u8 *payload;
    u16 parentShortAddr;

    if (tl_zbShortAddrByExtAddr(&parentShortAddr, req->parentAddr, NULL) != RET_OK) {
        zb_buf_free((zb_buf_t *)arg);
        return;
    }

    payload = tl_bufInitalloc((zb_buf_t *)arg, 1 + EXT_ADDR_LEN);
    payload[0] = APS_CMD_REMOVE_DEVICE_ID;
    memcpy(payload + 1, req->targetExtAddr, EXT_ADDR_LEN);

    cmd_req_init(&cmdReq);
    cmdReq.txBuf = (zb_buf_t *)arg;
    cmdReq.adu = payload;
    cmdReq.dstAddr.shortAddr = parentShortAddr;
    cmdReq.addrMode = ADDR_MODE_SHORT;
    cmdReq.aduLen = (u8)(1 + EXT_ADDR_LEN);
    cmdReq.secure = 1;
    cmdReq.secureNwkLayer = 1;
    aps_cmd_send(&cmdReq, APS_CMD_HANDLE_REMOVE_DEVICE);
}

void ss_apsRemoveDeviceCmdHandle(void *arg)
{
    aps_data_ind_t *ind = (aps_data_ind_t *)arg;
    ss_apsmeRemoveDeviceInd_t *removeInd = (ss_apsmeRemoveDeviceInd_t *)arg;

    memcpy(removeInd->childExtAddr, ind->asdu + 1, EXT_ADDR_LEN);
    if (tl_zbExtAddrByShortAddr(ind->src_short_addr, removeInd->tcAddr, NULL) != RET_OK) {
        zb_buf_free((zb_buf_t *)arg);
        return;
    }

    tl_zbTaskPost(ss_zdoRemoveDeviceIndHandle, arg);
}

void ss_apsTunnelCmdHandle(void *arg)
{
    aps_data_ind_t *ind = (aps_data_ind_t *)arg;
    addrExt_t extAddr;
    tl_zb_normal_neighbor_entry_t *entry;
    nlde_data_req_t *req;
    u8 *nsdu;
    u8 nsduLen;
    u16 dstAddr;

    if (ind->asduLength < (1U + EXT_ADDR_LEN + 1U)) {
        zb_buf_free((zb_buf_t *)arg);
        return;
    }

    memcpy(extAddr, ind->asdu + 1, EXT_ADDR_LEN);
    entry = nwk_neTblGetByExtAddr(extAddr);
    if (entry == NULL ||
        (entry->relationship != NEIGHBOR_IS_UNAUTH_CHILD &&
         entry->relationship != NEIGHBOR_IS_CHILD)) {
        zb_buf_free((zb_buf_t *)arg);
        return;
    }

    dstAddr = tl_zbshortAddrByIdx(entry->addrmapIdx);
    nsduLen = (u8)(ind->asduLength - (1U + EXT_ADDR_LEN));

    {
        /*
         * The inner transport-key command sits at ind->asdu + 1 + EXT_ADDR_LEN,
         * which overlaps BOTH the nlde_data_req_t overlay we build at buf[0]
         * and the head-room the NWK layer needs to prepend its header. The
         * vendor code left nsdu pointing there and then did memset(arg, 0, 26),
         * zeroing the very bytes it was about to relay (the child received an
         * all-zero key). Copy the inner command out, then reallocate it at the
         * tail of the buffer via tl_bufInitalloc so it lands past the req
         * overlay with proper header head-room — mirrors zdo_router_minimal.c
         * af_dataSend. */
        u8 inner[64];
        u8 *relay;

        if (nsduLen == 0U || nsduLen > sizeof(inner)) {
            zb_buf_free((zb_buf_t *)arg);
            return;
        }
        memcpy(inner, ind->asdu + 1U + EXT_ADDR_LEN, nsduLen);

        memset(arg, 0, sizeof(nlde_data_req_t));
        relay = (u8 *)tl_bufInitalloc((zb_buf_t *)arg, nsduLen);
        if (relay == NULL) {
            zb_buf_free((zb_buf_t *)arg);
            return;
        }
        memcpy(relay, inner, nsduLen);
        nsdu = relay;
    }

    req = (nlde_data_req_t *)arg;
    req->dstAddr = dstAddr;
    req->radius = 1;
    /*
     * NLDE addrMode 0 = plain NWK unicast. tl_zbNwkNldeDataRequestHandler treats
     * any non-zero addrMode as group/multicast (NWK FCF bit 8 + mcast-control
     * byte); the vendor's ADDR_MODE_SHORT here would send the relayed
     * transport-key multicast-flagged and the child couldn't parse it. Match
     * zdo_router_minimal.c af_dataSend, which uses 0 for unicast.
     */
    req->addrMode = 0U;
    /*
     * NWK-UNSECURED: the joining child has no network key yet, so the relayed
     * transport-key must go on air in the clear (NWK FCF security bit 0).
     */
    req->securityEnable = 0U;
    /* Child is a direct neighbour — skip route discovery, deliver directly. */
    req->discoverRoute = 0U;
    req->unicastSkipRouting = 1U;
    req->ndsuHandle = NWK_INTERNAL_NSDU_HANDLE; /* fire-and-forget: buf freed on cnf */
    req->nsdu = nsdu;
    req->nsduLen = nsduLen;

    /*
     * Post the NLDE-DATA.request DIRECTLY, exactly as zdo_router_minimal.c
     * af_dataSend does. The vendor path routed the relay through the APS TX
     * cache (apsTxDataPost/apsTxEventPost), which re-frames the buffer with its
     * own APS header and payload pointer and discards the nlde_data_req_t we
     * built here — the child then received a correctly-addressed frame with an
     * all-zero payload (no key). nwk_fwdPacket also rejects any buffer whose
     * hdr.used flag is clear as a stale-buffer guard, so mark it active.
     */
    ((zb_buf_t *)arg)->hdr.used = 1U;

    if (tl_zbPrimitivePost(TL_Q_HIGH2NWK, NWK_NLDE_DATA_REQ, arg) != RET_OK) {
        zb_buf_free((zb_buf_t *)arg);
    }
}
#endif

void ss_apsmeTransportKeyReq(void *arg)
{
    ss_apsmeTransportKeyReq_t *req = (ss_apsmeTransportKeyReq_t *)arg;
    aps_cmd_send_req_t cmdReq;
    u8 *payload;
    u8 *p;

    if (memcmp(req->dstAddr, g_zero_addr, EXT_ADDR_LEN) == 0 &&
        req->keyType != SS_STANDARD_NETWORK_KEY) {
        zb_buf_free((zb_buf_t *)arg);
        return;
    }

    payload = tl_bufInitalloc((zb_buf_t *)arg, 1 + 1 + SEC_KEY_LEN + 1 + EXT_ADDR_LEN + EXT_ADDR_LEN);
    p = payload;

    *p++ = APS_CMD_TRANSPORT_KEY_ID;
    *p++ = req->keyType;
    memcpy(p, req->key, SEC_KEY_LEN);
    p += SEC_KEY_LEN;

    if (req->keyType == SS_STANDARD_NETWORK_KEY) {
        *p++ = req->keySeqNum;
        memcpy(p, req->dstAddr, EXT_ADDR_LEN);
        p += EXT_ADDR_LEN;
        if (ss_securityModeIsDistributed()) {
            memcpy(p, ss_ib.trust_center_address, EXT_ADDR_LEN);
        } else {
            memcpy(p, g_zbInfo.macPib.extAddress, EXT_ADDR_LEN);
        }
        p += EXT_ADDR_LEN;
    } else if (req->keyType == SS_TC_LINK_KEY) {
        memcpy(p, req->dstAddr, EXT_ADDR_LEN);
        p += EXT_ADDR_LEN;
        memcpy(p, g_zbInfo.macPib.extAddress, EXT_ADDR_LEN);
        p += EXT_ADDR_LEN;
    } else if (req->keyType == SS_APP_LINK_KEY) {
        memcpy(p, req->partnerAddr, EXT_ADDR_LEN);
        p += EXT_ADDR_LEN;
        *p++ = req->initiatorFlag;
    }

    cmd_req_init(&cmdReq);
    cmdReq.txBuf = (zb_buf_t *)arg;
    cmdReq.adu = payload;
    cmdReq.aduLen = (u8)(p - payload);
    cmdReq.secureNwkLayer = 1;
    if (ZB_IS_64BIT_ADDR_ZERO(req->dstAddr)) {
        cmdReq.addrMode = ADDR_MODE_SHORT;
        cmdReq.dstAddr.shortAddr = NWK_BROADCAST_ALL_DEVICES;
    } else {
        cmdReq.addrMode = ADDR_MODE_EXT;
        if (req->relayByParent) {
            memcpy(cmdReq.dstAddr.extAddr, req->partnerAddr, EXT_ADDR_LEN);
        } else {
            memcpy(cmdReq.dstAddr.extAddr, req->dstAddr, EXT_ADDR_LEN);
        }
    }

    if ((ss_ib.preConfiguredKeyType == SS_PRECONFIGURED_GLOBALLINKKEY) ||
        (ss_ib.preConfiguredKeyType == SS_PRECONFIGURED_UNIQUELLINKKEY)) {
        if (!req->relayByParent) {
            cmdReq.secureNwkLayer = 0;
            cmdReq.secure = 1;
        }
    }

    aps_cmd_send(&cmdReq, APS_CMD_HANDLE_TRANSPORT_KEY);
}

void ss_apsmeSwitchKeyReq(void *arg)
{
    ss_apsmeSwitchKeyReq_t *req = (ss_apsmeSwitchKeyReq_t *)arg;
    aps_cmd_send_req_t cmdReq;
    ss_switch_key_cmd_t *payload = (ss_switch_key_cmd_t *)tl_bufInitalloc((zb_buf_t *)arg, sizeof(ss_switch_key_cmd_t));
    bool broadcast;

    payload->cmdId = APS_CMD_SWITCH_KEY_ID;
    payload->keySeqNum = req->keySeqNum;

    cmd_req_init(&cmdReq);
    cmdReq.txBuf = (zb_buf_t *)arg;
    cmdReq.adu = (u8 *)payload;
    cmdReq.aduLen = sizeof(ss_switch_key_cmd_t);
    broadcast = memcmp(req->dstAddr, g_invalid_addr, EXT_ADDR_LEN) == 0;
    if (broadcast) {
        cmdReq.addrMode = ADDR_MODE_SHORT;
        cmdReq.dstAddr.shortAddr = NWK_BROADCAST_ALL_DEVICES;
    } else {
        cmdReq.addrMode = ADDR_MODE_EXT;
        memcpy(cmdReq.dstAddr.extAddr, req->dstAddr, EXT_ADDR_LEN);
        cmdReq.secureNwkLayer = 1;
    }
    cmdReq.secure = broadcast ? 0 : 1;

    aps_cmd_send(&cmdReq, APS_CMD_HANDLE_SWITCH_KEY);
}

void ss_apsmeVerifyKeyReq(void *arg)
{
    ss_apsmeVerifyKeyReq_t *req = (ss_apsmeVerifyKeyReq_t *)arg;
    aps_cmd_send_req_t cmdReq;
    ss_verify_key_cmd_t *payload;
    u8 pad = 3;

    if (memcmp(req->dstAddr, ss_ib.trust_center_address, EXT_ADDR_LEN) != 0 ||
        g_zbNwkCtx.is_tc ||
        req->keyType != SS_TC_LINK_KEY ||
        g_ssDevKeyPair.keyPair.used == 0U) {
        zb_buf_free((zb_buf_t *)arg);
        return;
    }

    payload = (ss_verify_key_cmd_t *)tl_bufInitalloc((zb_buf_t *)arg, sizeof(ss_verify_key_cmd_t));
    payload->cmdId = APS_CMD_VERIFY_KEY_ID;
    payload->keyType = req->keyType;
    memcpy(payload->srcAddr, g_zbInfo.macPib.extAddress, EXT_ADDR_LEN);
    ss_keyHash(&pad, g_ssDevKeyPair.keyPair.linkKey, payload->hashVal);

    cmd_req_init(&cmdReq);
    cmdReq.txBuf = (zb_buf_t *)arg;
    cmdReq.adu = (u8 *)payload;
    cmdReq.addrMode = ADDR_MODE_EXT;
    memcpy(cmdReq.dstAddr.extAddr, req->dstAddr, EXT_ADDR_LEN);
    cmdReq.aduLen = sizeof(ss_verify_key_cmd_t);
    cmdReq.secureNwkLayer = 1;
    cmdReq.reserved = 1;

    aps_cmd_send(&cmdReq, APS_CMD_HANDLE_VERIFY_KEY);
}

void ss_apsTransportKeyCmdHandle(void *arg)
{
    /*
     * Vendor reads payload (asdu) as a 4-byte LE int at buf+12 and
     * security_status as buf[31] — both 32-bit-pinned offsets that
     * shift on native_sim/native/64 (asdu is at offset 16 there,
     * security_status at 39). Drive through aps_data_ind_t struct
     * fields so the layout matches the producer's struct writes.
     */
    aps_data_ind_t *ind = (aps_data_ind_t *)arg;
    u8 *payload = ind->asdu;
    u8 keyType = payload[1];
    const u8 *key = payload + 2;
    const u8 *dstExtAddr;
    const u8 *srcExtAddr;
    u8 secStatus = ind->security_status;

    if (aps_ib.aps_authenticated && secStatus == 0U) {
        zb_buf_free((zb_buf_t *)arg);
        return;
    }

    if (keyType == SS_STANDARD_NETWORK_KEY) {
        u8 keySeqNum = payload[18];

        dstExtAddr = payload + 19;
        srcExtAddr = payload + 27;

        zdo_mgmt_nwk_flag &= (u8)~0x04U;
        if (zdo_af_get_use_tc_sec_on_nwk_key_rotation() &&
            ZB_IS_64BIT_ADDR_ZERO(dstExtAddr) &&
            aps_ib.aps_authenticated &&
            (secStatus & SECURITY_IN_APSLAYER) == 0U) {
            zdo_mgmt_nwk_flag |= 0x04U;
        }

        if (ext_addr_is_local(dstExtAddr) || ZB_IS_64BIT_ADDR_ZERO(dstExtAddr)) {
            ss_apsmeTransportKeyInd_t *ind = (ss_apsmeTransportKeyInd_t *)arg;

            ind->keyType = keyType;
            memcpy(ind->srcAddr, srcExtAddr, EXT_ADDR_LEN);
            memcpy(ind->key, key, SEC_KEY_LEN);
            ind->keySeqNum = keySeqNum;
            tl_zbTaskPost(ss_zdoTransportKeyIndHandle, ind);
            return;
        }
    } else if (keyType == SS_TC_LINK_KEY) {
        dstExtAddr = payload + 18;
        srcExtAddr = payload + 26;

        if (ext_addr_is_local(dstExtAddr)) {
            ss_apsmeTransportKeyInd_t *ind = (ss_apsmeTransportKeyInd_t *)arg;

            ind->keyType = keyType;
            memcpy(ind->srcAddr, srcExtAddr, EXT_ADDR_LEN);
            memcpy(ind->key, key, SEC_KEY_LEN);
            tl_zbTaskPost(ss_zdoTransportKeyIndHandle, ind);
            return;
        }
    } else {
        zb_buf_free((zb_buf_t *)arg);
        return;
    }

    if (!aps_ib.aps_authenticated && (ind->security_status & SECURITY_IN_APSLAYER) == 0U) {
        tl_zb_normal_neighbor_entry_t *entry = nwk_neTblGetByExtAddr((u8 *)dstExtAddr);

        if (entry != NULL &&
            (entry->relationship == NEIGHBOR_IS_CHILD || entry->relationship == NEIGHBOR_IS_UNAUTH_CHILD)) {
            aps_cmd_send_req_t cmdReq;

            cmd_req_init(&cmdReq);
            cmdReq.txBuf = (zb_buf_t *)arg;
            cmdReq.adu = payload;
            cmdReq.aduLen = (u8)ind->asduLength;
            cmdReq.addrMode = ADDR_MODE_SHORT;
            cmdReq.dstAddr.shortAddr = tl_zbshortAddrByIdx(entry->addrmapIdx);
            aps_cmd_send(&cmdReq, APS_CMD_HANDLE_TXKEYCMD_RELAY);
            if (entry->relationship == NEIGHBOR_IS_UNAUTH_CHILD) {
                entry->relationship = NEIGHBOR_IS_CHILD;
            }
            return;
        }
    }

    zb_buf_free((zb_buf_t *)arg);
}

void ss_apsConfirmKeyCmdHandle(void *arg)
{
    aps_data_ind_t *ind = (aps_data_ind_t *)arg;
    const ss_confirm_key_cmd_t *payload = (const ss_confirm_key_cmd_t *)ind->asdu;
    ev_timer_event_t *timeoutEvt = g_ssDevKeyPair.pTimeoutEvt;

    if (g_zbNwkCtx.is_tc ||
        ss_securityModeIsDistributed() ||
        (ind->dst_addr & 0xfff8U) == 0xfff8U ||
        memcmp(payload->dstAddr, g_zbInfo.macPib.extAddress, EXT_ADDR_LEN) != 0 ||
        payload->keyType != SS_TC_LINK_KEY ||
        g_ssDevKeyPair.keyPair.used == 0U) {
        zb_buf_free((zb_buf_t *)arg);
        return;
    }

    if (payload->status == APS_STATUS_SUCCESS) {
        g_ssDevKeyPair.keyPair.keyAttr = SS_VERIFIED_KEY;
        g_ssDevKeyPair.keyPair.outgoingFrameCounter = 0;
        g_ssDevKeyPair.keyPair.incomingFrameCounter = 0;
        (void)ss_devKeyPairDelete(g_ssDevKeyPair.keyPair.device_address);
        ss_devKeyPairSave(&g_ssDevKeyPair.keyPair);
    }

    if (timeoutEvt != NULL) {
        ev_timer_taskCancel(&g_ssDevKeyPair.pTimeoutEvt);
    }
    memset(&g_ssDevKeyPair, 0, sizeof(g_ssDevKeyPair));
    bdb_retrieveTcLinkKeyDone(payload->status);
    zb_buf_free((zb_buf_t *)arg);
}

void ss_apsSwitchKeyCmdHandle(void *arg)
{
    aps_data_ind_t *ind = (aps_data_ind_t *)arg;
    u8 *payload = ind->asdu;
    u8 keySeqNum = payload[1];

    if (keySeqNum != ss_ib.activeKeySeqNum && ss_zdoGetNwkKeyBySeqNum(keySeqNum) == NULL) {
        if ((zdo_mgmt_nwk_flag & 0x04U) != 0U) {
            tl_zbTaskPost(ss_zdoInsecureRejoin, NULL);
            zdo_mgmt_nwk_flag &= (u8)~0x04U;
        }
        zb_buf_free((zb_buf_t *)arg);
        return;
    }

    if ((zdo_mgmt_nwk_flag & 0x04U) != 0U) {
        tl_zbTaskPost(ss_zdoInsecureRejoin, NULL);
        zdo_mgmt_nwk_flag &= (u8)~0x04U;
    } else {
        ss_zdoNwkKeySwitch(keySeqNum);
    }

    zb_buf_free((zb_buf_t *)arg);
}
