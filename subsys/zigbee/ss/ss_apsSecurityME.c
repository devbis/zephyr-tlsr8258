/*
 * Copyright 2026 libzigbee
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "zb_common.h"
#include "ss_apsSecurityME.h"
#include "ss_zdoSecurityME.h"
#include "ss_tlCCM.h"
#include "aps_data.h"
#include "zdo_nwk_manager.h"
#include "gp_internal.h"
#include "zdo.h"
#include <stdint.h>
#include "ev_timer.h"
#include "security_service.h"

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

static inline bool ext_addr_is_local(const addrExt_t extAddr)
{
	return ZB_IEEE_ADDR_CMP(extAddr, g_zbInfo.macPib.extAddress);
}

#if UINTPTR_MAX == UINT32_MAX
#if defined(ZB_COORDINATOR_ROLE)
STATIC_ASSERT(OFFSETOF(ss_info_base_t, tcLinkKeyType) == 0x50);
#else
STATIC_ASSERT(OFFSETOF(ss_info_base_t, tcLinkKeyType) == 0x4c);
#endif
#else
#if defined(ZB_COORDINATOR_ROLE)
STATIC_ASSERT(OFFSETOF(ss_info_base_t, tcLinkKeyType) == 0x5c);
#else
STATIC_ASSERT(OFFSETOF(ss_info_base_t, tcLinkKeyType) == 0x58);
#endif
#endif

#if defined(ZB_ROUTER_ROLE) || defined(ZB_COORDINATOR_ROLE)

_attribute_no_inline_ static void ss_apsmeUpdateDevReqSend(zb_buf_t *buf, u8 secure)
{
	ss_apsmeUpdateDeviceReq_t *req = (ss_apsmeUpdateDeviceReq_t *)buf;
	aps_cmd_send_req_t cmdReq;
	u8 *payload;

	TL_BUF_INITIAL_ALLOC(buf, 12, payload, u8 *);

	payload[0] = APS_CMD_UPDATE_DEVICE_ID;
	ZB_IEEE_ADDR_COPY(payload + 1, req->devAddr);
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
#if defined(ZB_COORDINATOR_ROLE)
ss_tc_keyPair_t g_ssTcKeyPair[SS_TC_KEY_PAIR_CACHE_NUM];
#else
ss_dev_keyPair_t g_ssDevKeyPair;
#endif

u32 ss_outgoingFrameCntGet(void)
{
	return ss_ib.outgoingFrameCounter;
}

void ss_devKeyPairSave(ss_dev_pair_set_t *keyPair)
{
	if (nv_flashWriteNew(0, NV_MODULE_KEYPAIR, NV_ITEM_SS_KEY_PAIR, sizeof(ss_dev_pair_set_t),
			     (u8 *)keyPair) == NV_SUCC) {
		ss_ib.devKeyPairNum++;
	}
}

u8 ss_devKeyPairFind(addrExt_t extAddr, ss_dev_pair_set_t *keyPair)
{
	itemIfno_t info = {0, 0};
	nv_sts_t ret = nv_flashReadNew(0, NV_MODULE_KEYPAIR, ITEM_FIELD_IDLE,
				       sizeof(ss_dev_pair_set_t), (u8 *)&info);
	ss_dev_pair_set_t candidate = {{0}, {0}, 0, 0, 0, 0, 0, 0};
	ss_dev_pair_set_t best = {{0}, {0}, 0, 0, 0, 0, 0, 0};
	bool haveCandidate = FALSE;
	bool fallbackLocked = FALSE;

	if (ret != NV_SUCC) {
		return ret;
	}

	/* A successful metadata read does not mean that a matching key exists. */
	ret = NV_ITEM_NOT_FOUND;

	for (u16 i = 0; i <= info.opIndex; i++) {
		if (nv_flashReadByIndex(NV_MODULE_KEYPAIR, NV_ITEM_SS_KEY_PAIR, info.opSect, i,
					sizeof(ss_dev_pair_set_t),
					(u8 *)&candidate) == NV_ITEM_NOT_FOUND) {
			continue;
		}

		if (candidate.apsLinkKeyType == SS_GLOBAL_LINK_KEY) {
			if (!fallbackLocked) {
				memcpy(&best, &candidate, sizeof(candidate));
				haveCandidate = TRUE;
				fallbackLocked = TRUE;
				ret = NV_SUCC;
			}
			continue;
		}

		if (candidate.apsLinkKeyType != SS_UNIQUE_LINK_KEY) {
			continue;
		}

		if (!ZB_IEEE_ADDR_CMP(extAddr, candidate.device_address)) {
			continue;
		}

		if (candidate.keyAttr == SS_VERIFIED_KEY) {
			memcpy(keyPair, &candidate, sizeof(candidate));
			return NV_SUCC;
		}

		if (candidate.keyAttr == SS_UNVERIFIED_KEY) {
			memcpy(&best, &candidate, sizeof(candidate));
			haveCandidate = TRUE;
			ret = NV_SUCC;
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
	nv_sts_t ret = nv_flashReadNew(0, NV_MODULE_KEYPAIR, ITEM_FIELD_IDLE,
				       sizeof(ss_dev_pair_set_t), (u8 *)&info);
	ss_dev_pair_set_t keyPair;

	if (ret != NV_SUCC) {
		return ret;
	}

	for (u16 i = 0; i <= info.opIndex; i++) {
		if (nv_flashReadByIndex(NV_MODULE_KEYPAIR, NV_ITEM_SS_KEY_PAIR, info.opSect, i,
					sizeof(ss_dev_pair_set_t), (u8 *)&keyPair) != NV_SUCC) {
			continue;
		}

		if (ZB_IEEE_ADDR_CMP(extAddr, keyPair.device_address)) {
			ret = nv_itemDeleteByIndex(NV_MODULE_KEYPAIR, NV_ITEM_SS_KEY_PAIR,
						   info.opSect, i);
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
	if (nv_flashReadNew(0, NV_MODULE_KEYPAIR, ITEM_FIELD_IDLE, sizeof(ss_dev_pair_set_t),
			    (u8 *)&info) != NV_SUCC) {
		return ss_ib.devKeyPairNum;
	}

	for (u16 i = 0; i <= info.opIndex; i++) {
		ss_dev_pair_set_t keyPair;

		if (nv_flashReadByIndex(NV_MODULE_KEYPAIR, NV_ITEM_SS_KEY_PAIR, info.opSect, i,
					sizeof(ss_dev_pair_set_t), (u8 *)&keyPair) == NV_SUCC) {
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

	if (nv_flashReadNew(0, NV_MODULE_KEYPAIR, ITEM_FIELD_IDLE, sizeof(ss_dev_pair_set_t),
			    (u8 *)&info) != NV_SUCC) {
		return ss_ib.devKeyPairNum;
	}

	for (u16 i = 0; count < num && i <= info.opIndex; i++) {
		ss_dev_pair_set_t keyPair;

		if (nv_flashReadByIndex(NV_MODULE_KEYPAIR, NV_ITEM_SS_KEY_PAIR, info.opSect, i,
					sizeof(ss_dev_pair_set_t), (u8 *)&keyPair) != NV_SUCC) {
			continue;
		}

		if (seen >= start_idx) {
			ZB_IEEE_ADDR_COPY(nodeMacAddrList, keyPair.device_address);
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
	ss_ib_active_secure_material_index_set(0);
	ss_ib.activeKeySeqNum = 0;
	ss_ib.nwkSecurMaterialSet[0].keySeqNum = 0;
}

void ss_nwkKeyStore(u8 *nwkKey)
{
	memcpy(ss_ib.nwkSecurMaterialSet[0].key, nwkKey, SEC_KEY_LEN);
	ss_ib_active_secure_material_index_set(0);
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
	    (req->dstAddrMode != ADDR_MODE_SHORT && req->dstAddrMode != ADDR_MODE_EXT)) {
		zb_buf_free((zb_buf_t *)arg);
		return;
	}

	payloadLen = (req->keyType == SS_KEYREQ_TYPE_APPLK) ? sizeof(ss_request_key_cmd_t) : 2U;
	TL_BUF_INITIAL_ALLOC((zb_buf_t *)arg, payloadLen, payload, u8 *);
	payload[0] = APS_CMD_REQUEST_KEY_ID;
	payload[1] = req->keyType;
	if (req->keyType == SS_KEYREQ_TYPE_APPLK) {
		ZB_IEEE_ADDR_COPY(payload + 2, req->partnerAddr);
	}

	cmd_req_init(&cmdReq);
	cmdReq.txBuf = (zb_buf_t *)arg;
	cmdReq.adu = payload;
	cmdReq.addrMode = req->dstAddrMode;
	cmdReq.aduLen = payloadLen;
	if (req->dstAddrMode == ADDR_MODE_SHORT) {
		cmdReq.dstAddr.shortAddr = req->dstAddr.shortAddr;
	} else if (req->dstAddrMode == ADDR_MODE_EXT) {
		ZB_IEEE_ADDR_COPY(cmdReq.dstAddr.extAddr, req->dstAddr.extAddr);
	}

	aps_cmd_send(&cmdReq, APS_CMD_HANDLE_REQUEST_KEY);
}

#if defined(ZB_COORDINATOR_ROLE)
ss_tc_keyPair_t *ss_tcKeyPairFind(addrExt_t extAddr)
{
	for (u8 i = 0; i < SS_TC_KEY_PAIR_CACHE_NUM; i++) {
		if (g_ssTcKeyPair[i].keyPair.used &&
		    ZB_IEEE_ADDR_CMP(g_ssTcKeyPair[i].keyPair.device_address, extAddr)) {
			return &g_ssTcKeyPair[i];
		}
	}

	return NULL;
}

ss_tc_keyPair_t *ss_tcKeyPairFreeGet(void)
{
	for (u8 i = 0; i < SS_TC_KEY_PAIR_CACHE_NUM; i++) {
		if (!g_ssTcKeyPair[i].keyPair.used) {
			return &g_ssTcKeyPair[i];
		}
	}

	return NULL;
}

void ss_tcKeyPairClear(ss_tc_keyPair_t *keyPair)
{
	memset(keyPair, 0, sizeof(*keyPair));
}

void ss_tcKeyPairPeriodic(void)
{
	for (u8 i = 0; i < SS_TC_KEY_PAIR_CACHE_NUM; i++) {
		if (!g_ssTcKeyPair[i].keyPair.used) {
			continue;
		}

		if (g_ssTcKeyPair[i].timeout != 0U) {
			g_ssTcKeyPair[i].timeout--;
			if (g_ssTcKeyPair[i].timeout == 0U) {
				ss_tcKeyPairClear(&g_ssTcKeyPair[i]);
			}
		}
	}
}
#endif

#if defined(ZB_ROUTER_ROLE) || defined(ZB_COORDINATOR_ROLE)
void ss_apsmeUpdateDevReq(void *arg)
{
	u8 mode = ss_ib.tcLinkKeyType;

	if (mode == 0U) {
		ss_apsmeUpdateDevReqSend((zb_buf_t *)arg, 1);
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
	u16 parentAddrMapIdx;

	if (tl_zbShortAddrByExtAddr(&parentShortAddr, req->parentAddr, &parentAddrMapIdx) ==
	    0xffU) {
		zb_buf_free((zb_buf_t *)arg);
		return;
	}

	TL_BUF_INITIAL_ALLOC((zb_buf_t *)arg, 1 + EXT_ADDR_LEN, payload, u8 *);
	payload[0] = APS_CMD_REMOVE_DEVICE_ID;
	ZB_IEEE_ADDR_COPY(payload + 1, req->targetExtAddr);

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
	u16 tcAddrMapIdx;

	ZB_IEEE_ADDR_COPY(removeInd->childExtAddr, ind->asdu + 1);
	if (tl_zbExtAddrByShortAddr(ind->src_short_addr, removeInd->tcAddr, &tcAddrMapIdx) ==
	    0xffU) {
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
	aps_tx_cache_list_t *cache;
	apsdeDataConf_t cnf;
	u8 *nsdu;
	u8 nsduLen;
	u16 dstAddr;

	ZB_IEEE_ADDR_COPY(extAddr, ind->asdu + 1);
	entry = nwk_neTblGetByExtAddr(extAddr);
	if (entry == NULL || (entry->relationship != NEIGHBOR_IS_UNAUTH_CHILD &&
			      entry->relationship != NEIGHBOR_IS_CHILD)) {
		zb_buf_free((zb_buf_t *)arg);
		return;
	}

	dstAddr = tl_zbshortAddrByIdx(entry->addrmapIdx);
	nsdu = ind->asdu + 1U + EXT_ADDR_LEN;
	nsduLen = (u8)(ind->asduLength - (1U + EXT_ADDR_LEN));

	memset(arg, 0, sizeof(nlde_data_req_t));
	req = (nlde_data_req_t *)arg;
	req->dstAddr = dstAddr;
	req->radius = 1;
	req->addrMode = ADDR_MODE_SHORT;
	req->ndsuHandle = APS_CMD_HANDLE_TXKEYCMD_RELAY;
	req->nsdu = nsdu;
	req->nsduLen = nsduLen;

	memset(&cnf, 0, sizeof(cnf));
	cnf.dstAddr.addr_short = dstAddr;
	cnf.dstAddrMode = ADDR_MODE_SHORT;
	cnf.handle = APS_CMD_HANDLE_TXKEYCMD_RELAY;
	cnf.apsCnt = nsdu[1];

	cache = apsTxDataPost((nsdu[0] & APS_FRAME_CTRL_ACK_REQUEST) ? 1U : 0U, 0, 0, arg, &cnf);
	if (cache == NULL) {
		zb_buf_free((zb_buf_t *)arg);
		return;
	}

	apsTxEventPost(cache, APS_TX_EVENT_TX_TODO, APS_STATUS_SUCCESS);
}
#endif

#if defined(ZB_ROUTER_ROLE) || defined(ZB_COORDINATOR_ROLE)
void ss_apsmeTransportKeyReq(void *arg)
{
	ss_apsmeTransportKeyReq_t *req = (ss_apsmeTransportKeyReq_t *)arg;
	aps_cmd_send_req_t cmdReq;
	u8 *payload;
	u8 *p;
	u8 handle = APS_CMD_HANDLE_TRANSPORT_KEY;

	if (ZB_IEEE_ADDR_IS_ZERO(req->dstAddr) && req->keyType != SS_STANDARD_NETWORK_KEY) {
		zb_buf_free((zb_buf_t *)arg);
		return;
	}

	TL_BUF_INITIAL_ALLOC((zb_buf_t *)arg, 39, payload, u8 *);
	p = payload;

	*p++ = APS_CMD_TRANSPORT_KEY_ID;
	*p++ = req->keyType;
	memcpy(p, req->key, SEC_KEY_LEN);
	p += SEC_KEY_LEN;

	if (req->keyType == SS_STANDARD_NETWORK_KEY) {
		handle = req->relayByParent ? APS_CMD_HANDLE_CMD_TUNNEL : APS_CMD_HANDLE_NWK_KEY;
		*p++ = req->keySeqNum;
		ZB_IEEE_ADDR_COPY(p, req->dstAddr);
		p += EXT_ADDR_LEN;
		if (ss_securityModeIsDistributed()) {
			ZB_IEEE_ADDR_COPY(p, ss_ib.trust_center_address);
		} else {
			ZB_IEEE_ADDR_COPY(p, g_zbInfo.macPib.extAddress);
		}
		p += EXT_ADDR_LEN;
	} else if (req->keyType == SS_TC_LINK_KEY) {
		ZB_IEEE_ADDR_COPY(p, req->dstAddr);
		p += EXT_ADDR_LEN;
		ZB_IEEE_ADDR_COPY(p, g_zbInfo.macPib.extAddress);
		p += EXT_ADDR_LEN;
	} else if (req->keyType == SS_APP_LINK_KEY) {
		ZB_IEEE_ADDR_COPY(p, req->partnerAddr);
		p += EXT_ADDR_LEN;
		*p++ = req->initiatorFlag;
	}

	cmd_req_init(&cmdReq);
	cmdReq.txBuf = (zb_buf_t *)arg;
	cmdReq.adu = payload;
	cmdReq.aduLen = (u8)(p - payload);
	/* "5a: tmovs r3,#1; 5c: tstorerb r3,[r5,#19]" - network-layer security is
	 * the default and only the two tests below turn it off.  APS security is
	 * decided by the destination alone ("8c"/"90"): a broadcast key and a key
	 * a parent relays go out unencrypted, a direct unicast key is encrypted
	 * with the link key.  The vendor never reads preConfiguredKeyType here. */
	cmdReq.secureNwkLayer = 1;
	if (ZB_IEEE_ADDR_IS_ZERO(req->dstAddr)) {
		cmdReq.addrMode = ADDR_MODE_SHORT;
		cmdReq.dstAddr.shortAddr = NWK_BROADCAST_ALL_DEVICES;
		cmdReq.secure = 0;
	} else {
		cmdReq.addrMode = ADDR_MODE_EXT;
		if (req->relayByParent) {
			ZB_IEEE_ADDR_COPY(cmdReq.dstAddr.extAddr, req->partnerAddr);
			cmdReq.secure = 0;
		} else {
			ZB_IEEE_ADDR_COPY(cmdReq.dstAddr.extAddr, req->dstAddr);
			cmdReq.secure = 1;
		}
	}

	if (req->nwkSecurity == 0U && !req->relayByParent) {
		cmdReq.secureNwkLayer = 0;
	}

	aps_cmd_send(&cmdReq, handle);
}

void ss_apsmeSwitchKeyReq(void *arg)
{
	ss_apsmeSwitchKeyReq_t *req = (ss_apsmeSwitchKeyReq_t *)arg;
	aps_cmd_send_req_t cmdReq;
	ss_switch_key_cmd_t *payload;

	TL_BUF_INITIAL_ALLOC((zb_buf_t *)arg, sizeof(ss_switch_key_cmd_t), payload,
			     ss_switch_key_cmd_t *);
	bool broadcast;

	payload->cmdId = APS_CMD_SWITCH_KEY_ID;
	payload->keySeqNum = req->keySeqNum;

	cmd_req_init(&cmdReq);
	cmdReq.txBuf = (zb_buf_t *)arg;
	cmdReq.adu = (u8 *)payload;
	cmdReq.aduLen = sizeof(ss_switch_key_cmd_t);
	broadcast = ZB_IEEE_ADDR_IS_INVALID(req->dstAddr);
	if (broadcast) {
		cmdReq.addrMode = ADDR_MODE_SHORT;
		cmdReq.dstAddr.shortAddr = NWK_BROADCAST_ALL_DEVICES;
	} else {
		cmdReq.addrMode = ADDR_MODE_EXT;
		ZB_IEEE_ADDR_COPY(cmdReq.dstAddr.extAddr, req->dstAddr);
		cmdReq.secureNwkLayer = 1;
	}
	cmdReq.secure = broadcast ? 0 : 1;

	aps_cmd_send(&cmdReq, APS_CMD_HANDLE_SWITCH_KEY);
}
#endif

#if defined(ZB_COORDINATOR_ROLE)
void ss_apsUpdateDeviceCmdHandle(void *arg)
{
	aps_data_ind_t *ind = (aps_data_ind_t *)arg;
	ss_apsmeUpdateDeviceInd_t *updateInd = (ss_apsmeUpdateDeviceInd_t *)arg;
	/* The indication is built over the buffer that carries the incoming
	 * frame, and the ASDU pointer and the source address it reads through
	 * are themselves fields of that buffer.  Take everything out of the
	 * frame before writing the first field of the indication.
	 */
	const u8 *asdu = ind->asdu;
	u16 srcShortAddr = ind->src_short_addr;
	u8 securityStatus = ind->security_status;
	u16 devShortAddr = (u16)asdu[9] | ((u16)asdu[10] << 8);
	u8 status = asdu[11];
	addrExt_t devAddr;
	ss_dev_pair_set_t keyPair;
	bool haveKeyPair;
	u16 srcAddrMapIdx;

	ZB_IEEE_ADDR_COPY(devAddr, asdu + 1);

	if (tl_zbExtAddrByShortAddr(srcShortAddr, updateInd->srcAddr, &srcAddrMapIdx) == 0xffU) {
		zb_buf_free((zb_buf_t *)arg);
		return;
	}

	ZB_IEEE_ADDR_COPY(updateInd->devAddr, devAddr);
	updateInd->devShortAddr = devShortAddr;
	updateInd->status = status;

	haveKeyPair = (ss_devKeyPairFind(updateInd->srcAddr, &keyPair) == NV_SUCC);
	if (!haveKeyPair) {
		if (ss_ib.tcLinkKeyType == 0U && securityStatus == APS_STATUS_SUCCESS) {
			zb_buf_free((zb_buf_t *)arg);
			return;
		}
	} else if (securityStatus == APS_STATUS_SUCCESS && keyPair.used == 0U) {
		zb_buf_free((zb_buf_t *)arg);
		return;
	}

	tl_zbTaskPost(ss_zdoUpdateDeviceIndHandle, arg);
	if (g_gpDeviceAnnounceCheckCb != NULL) {
		(void)g_gpDeviceAnnounceCheckCb(devShortAddr, devAddr);
	}
}

u8 ss_apsVerifyKeyCmdValid(void *keyPair, u8 keyType)
{
	if (!g_zbNwkCtx.is_tc) {
		return APS_STATUS_ILLEGAL_REQUEST;
	}

	if (keyType != SS_TC_LINK_KEY) {
		return APS_STATUS_NOT_SUPPORTED;
	}

	if (ss_securityModeIsDistributed()) {
		return APS_STATUS_NOT_SUPPORTED;
	}

	if (keyPair == NULL ||
	    ((ss_tc_keyPair_t *)keyPair)->keyPair.keyAttr == SS_PROVISIONAL_KEY) {
		return APS_STATUS_SECURITY_FAIL;
	}

	return APS_STATUS_SUCCESS;
}

void ss_apsVerifyKeyCmdHandle(void *arg)
{
	aps_data_ind_t *ind = (aps_data_ind_t *)arg;
	ss_verify_key_cmd_t verifyCmd;
	ss_tc_keyPair_t *keyPair;
	aps_cmd_send_req_t cmdReq;
	u8 *payload;
	u8 hashPad = 3;
	u8 hashVal[SEC_KEY_LEN];
	u8 status;

	if (ZB_NWK_IS_ADDRESS_BROADCAST(ind->dst_addr)) {
		zb_buf_free((zb_buf_t *)arg);
		return;
	}

	memcpy(&verifyCmd, ind->asdu, sizeof(verifyCmd));
	keyPair = ss_tcKeyPairFind(verifyCmd.srcAddr);
	status = ss_apsVerifyKeyCmdValid(keyPair, verifyCmd.keyType);

	if (status == APS_STATUS_SUCCESS) {
		ss_keyHash(&hashPad, keyPair->keyPair.linkKey, hashVal);
		if (memcmp(hashVal, verifyCmd.hashVal, SEC_KEY_LEN) != 0) {
			status = APS_STATUS_SECURITY_FAIL;
		} else {
			keyPair->keyPair.keyAttr = SS_VERIFIED_KEY;
			(void)ss_devKeyPairDelete(keyPair->keyPair.device_address);
			ss_devKeyPairSave(&keyPair->keyPair);
		}
	}

	if (keyPair != NULL) {
		ss_tcKeyPairClear(keyPair);
	}

	cmd_req_init(&cmdReq);
	TL_BUF_INITIAL_ALLOC((zb_buf_t *)arg, 11, payload, u8 *);
	payload[0] = APS_CMD_CONFIRM_KEY_ID;
	payload[1] = status;
	payload[2] = verifyCmd.keyType;
	ZB_IEEE_ADDR_COPY(payload + 3, verifyCmd.srcAddr);

	cmdReq.txBuf = (zb_buf_t *)arg;
	cmdReq.adu = payload;
	cmdReq.addrMode = ADDR_MODE_SHORT;
	cmdReq.dstAddr.shortAddr = ind->src_short_addr;
	cmdReq.aduLen = 11;
	cmdReq.secure = (status == APS_STATUS_SUCCESS);
	cmdReq.secureNwkLayer = 1;
	cmdReq.reserved = 1;
	aps_cmd_send(&cmdReq, APS_CMD_HANDLE_VERIFY_KEY);
}

void ss_apsRequestKeyCmdHandle(void *arg)
{
	aps_data_ind_t *ind = (aps_data_ind_t *)arg;
	ss_tc_keyPair_t *keyPair;
	addrExt_t srcExtAddr;
	aps_cmd_send_req_t cmdReq;
	u8 *payload;
	u16 srcAddrMapIdx;

	if (!g_zbNwkCtx.is_tc || ind->asdu[1] != SS_TC_LINK_KEY) {
		zb_buf_free((zb_buf_t *)arg);
		return;
	}

	if (tl_zbExtAddrByShortAddr(ind->src_short_addr, srcExtAddr, &srcAddrMapIdx) != RET_OK) {
		zb_buf_free((zb_buf_t *)arg);
		return;
	}

	if (ss_ib.tcPolicy.allowTCLKrequest != 1U ||
	    (int)ss_ib.devKeyPairNum >= (int)(TL_ZB_NWK_ADDR_MAP_SIZE - 1U)) {
		zb_buf_free((zb_buf_t *)arg);
		return;
	}

	keyPair = ss_tcKeyPairFind(srcExtAddr);
	if (keyPair == NULL) {
		keyPair = ss_tcKeyPairFreeGet();
		if (keyPair == NULL) {
			zb_buf_free((zb_buf_t *)arg);
			return;
		}
		keyPair->keyPair.used = 1;
	}

	ZB_IEEE_ADDR_COPY(keyPair->keyPair.device_address, srcExtAddr);
	drv_generateRandomData(keyPair->keyPair.linkKey, SEC_KEY_LEN);
	keyPair->keyPair.keyAttr = SS_UNVERIFIED_KEY;
	keyPair->keyPair.apsLinkKeyType = SS_UNIQUE_LINK_KEY;
	keyPair->keyPair.rsv = 0;
	keyPair->keyPair.outgoingFrameCounter = 0;
	keyPair->keyPair.incomingFrameCounter = 0;
	keyPair->timeout = ss_ib.ssTimeoutPeriod / 1000U;

	cmd_req_init(&cmdReq);
	TL_BUF_INITIAL_ALLOC((zb_buf_t *)arg, 39, payload, u8 *);
	payload[0] = APS_CMD_TRANSPORT_KEY_ID;
	payload[1] = SS_TC_LINK_KEY;
	memcpy(payload + 2, keyPair->keyPair.linkKey, SEC_KEY_LEN);
	ZB_IEEE_ADDR_COPY(payload + 18, srcExtAddr);
	ZB_IEEE_ADDR_COPY(payload + 26, g_zbInfo.macPib.extAddress);

	cmdReq.txBuf = (zb_buf_t *)arg;
	cmdReq.adu = payload;
	cmdReq.dstAddr.shortAddr = ind->src_short_addr;
	cmdReq.addrMode = ADDR_MODE_SHORT;
	cmdReq.aduLen = 34;
	cmdReq.secure = 1;
	cmdReq.secureNwkLayer = 1;
	aps_cmd_send(&cmdReq, APS_CMD_HANDLE_TRANSPORT_KEY);
}
#endif

#if !defined(ZB_COORDINATOR_ROLE)
void ss_apsmeVerifyKeyReq(void *arg)
{
	ss_apsmeVerifyKeyReq_t *req = (ss_apsmeVerifyKeyReq_t *)arg;
	aps_cmd_send_req_t cmdReq;
	ss_verify_key_cmd_t *payload;
	u8 pad = 3;

	if (!ZB_IEEE_ADDR_CMP(req->dstAddr, ss_ib.trust_center_address) || g_zbNwkCtx.is_tc ||
	    req->keyType != SS_TC_LINK_KEY || g_ssDevKeyPair.keyPair.used == 0U) {
		zb_buf_free((zb_buf_t *)arg);
		return;
	}

	TL_BUF_INITIAL_ALLOC((zb_buf_t *)arg, sizeof(ss_verify_key_cmd_t), payload,
			     ss_verify_key_cmd_t *);
	payload->cmdId = APS_CMD_VERIFY_KEY_ID;
	payload->keyType = req->keyType;
	ZB_IEEE_ADDR_COPY(payload->srcAddr, g_zbInfo.macPib.extAddress);
	ss_keyHash(&pad, g_ssDevKeyPair.keyPair.linkKey, payload->hashVal);

	cmd_req_init(&cmdReq);
	cmdReq.txBuf = (zb_buf_t *)arg;
	cmdReq.adu = (u8 *)payload;
	cmdReq.addrMode = ADDR_MODE_EXT;
	ZB_IEEE_ADDR_COPY(cmdReq.dstAddr.extAddr, req->dstAddr);
	cmdReq.aduLen = sizeof(ss_verify_key_cmd_t);
	cmdReq.secureNwkLayer = 1;
	cmdReq.reserved = 1;

	aps_cmd_send(&cmdReq, APS_CMD_HANDLE_VERIFY_KEY);
}

void ss_apsTransportKeyCmdHandle(void *arg)
{
	aps_data_ind_t *ind = (aps_data_ind_t *)arg;
	u8 *payload = ind->asdu;
	u8 keyType = payload[1];
	const u8 *key = payload + 2;
	const u8 *dstExtAddr;
	const u8 *srcExtAddr;

	if (aps_ib.aps_authenticated && ind->security_status == 0U) {
		zb_buf_free((zb_buf_t *)arg);
		return;
	}

	if (keyType == SS_STANDARD_NETWORK_KEY) {
		u8 keySeqNum = payload[18];

		dstExtAddr = payload + 19;
		srcExtAddr = payload + 27;

		zdo_mgmt_nwk_flag &= (u8)~0x04U;
		if (zdo_af_get_use_tc_sec_on_nwk_key_rotation() &&
		    ZB_IEEE_ADDR_IS_ZERO(dstExtAddr) && aps_ib.aps_authenticated &&
		    (ind->security_status & SECURITY_IN_APSLAYER) == 0U) {
			zdo_mgmt_nwk_flag |= 0x04U;
		}

		if (ext_addr_is_local(dstExtAddr) || ZB_IEEE_ADDR_IS_ZERO(dstExtAddr)) {
			if (!aps_ib.aps_authenticated) {
				tl_zb_normal_neighbor_entry_t *entry;

				if (ind->src_addr_mode == ADDR_MODE_SHORT) {
					entry = nwk_neTblGetByShortAddr(ind->src_short_addr);
				} else {
					entry = nwk_neTblGetByExtAddr(ind->src_ext_addr);
				}

				if (entry == NULL || entry->relationship != NEIGHBOR_IS_PARENT) {
					zb_buf_free((zb_buf_t *)arg);
					return;
				}

				{
					addrExt_t parentExtAddr;

					tl_zbExtAddrByIdx(entry->addrmapIdx, parentExtAddr);
					if (entry->deviceType != NWK_DEVICE_TYPE_COORDINATOR &&
					    ZB_IEEE_ADDR_CMP(srcExtAddr, parentExtAddr)) {
						ss_securityModeSet(SS_SEMODE_DISTRIBUTED);
					} else {
						ss_securityModeSet(SS_SEMODE_CENTRALIZED);
					}
				}
			}

			ss_apsmeTransportKeyInd_t *transportInd = (ss_apsmeTransportKeyInd_t *)arg;

			transportInd->keyType = keyType;
			ZB_IEEE_ADDR_COPY(transportInd->srcAddr, srcExtAddr);
			memcpy(transportInd->key, key, SEC_KEY_LEN);
			transportInd->keySeqNum = keySeqNum;
			tl_zbTaskPost(ss_zdoTransportKeyIndHandle, transportInd);
			return;
		}
	} else if (keyType == SS_TC_LINK_KEY) {
		dstExtAddr = payload + 18;
		srcExtAddr = payload + 26;

		if (ext_addr_is_local(dstExtAddr)) {
			ss_apsmeTransportKeyInd_t *transportInd = (ss_apsmeTransportKeyInd_t *)arg;

			zdo_mgmt_nwk_flag &= (u8)~0x04U;

			/* Keep the sender mapping when the command arrived with a short
			 * source address. */
			if (ind->src_addr_mode == ADDR_MODE_SHORT) {
				u16 addrRef;

				(void)tl_zbNwkAddrMapAdd(ind->src_short_addr, (u8 *)srcExtAddr,
							 &addrRef);
			}

			transportInd->keyType = keyType;
			ZB_IEEE_ADDR_COPY(transportInd->srcAddr, srcExtAddr);
			memcpy(transportInd->key, key, SEC_KEY_LEN);
			tl_zbTaskPost(ss_zdoTransportKeyIndHandle, transportInd);
			return;
		}
	} else {
		zb_buf_free((zb_buf_t *)arg);
		return;
	}

#if defined(ZB_ROUTER_ROLE)
	/* "60: tcmp r0,#0; 62: tjne ba" - a key that is not for this device goes
	 * straight to the child lookup, whatever this router's own state. */
	{
		tl_zb_normal_neighbor_entry_t *entry = nwk_neTblGetByExtAddr((u8 *)dstExtAddr);

		if (entry != NULL && (entry->relationship == NEIGHBOR_IS_CHILD ||
				      entry->relationship == NEIGHBOR_IS_UNAUTH_CHILD)) {
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
#endif

	zb_buf_free((zb_buf_t *)arg);
}

void ss_apsConfirmKeyCmdHandle(void *arg)
{
	aps_data_ind_t *ind = (aps_data_ind_t *)arg;
	const ss_confirm_key_cmd_t *payload = (const ss_confirm_key_cmd_t *)ind->asdu;
	ev_timer_event_t *timeoutEvt = g_ssDevKeyPair.pTimeoutEvt;

	if (g_zbNwkCtx.is_tc || ss_securityModeIsDistributed() ||
	    ZB_NWK_IS_ADDRESS_BROADCAST(ind->dst_addr) ||
	    !ZB_IEEE_ADDR_CMP(payload->dstAddr, g_zbInfo.macPib.extAddress) ||
	    payload->keyType != SS_TC_LINK_KEY || g_ssDevKeyPair.keyPair.used == 0U) {
		zb_buf_free((zb_buf_t *)arg);
		return;
	}

	if (payload->status == APS_STATUS_SUCCESS) {
		g_ssDevKeyPair.keyPair.keyAttr = SS_VERIFIED_KEY;
		g_ssDevKeyPair.keyPair.incomingFrameCounter = 0;
		(void)ss_devKeyPairDelete(g_ssDevKeyPair.keyPair.device_address);
		ss_devKeyPairSave(&g_ssDevKeyPair.keyPair);
	}

	if (timeoutEvt != NULL) {
		ev_timer_taskCancel(&g_ssDevKeyPair.pTimeoutEvt);
	}
	memset(&g_ssDevKeyPair.keyPair, 0, sizeof(g_ssDevKeyPair.keyPair));
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
#endif
