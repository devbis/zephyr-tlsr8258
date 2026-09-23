/*
 * Copyright 2026 libzigbee
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "zb_common.h"
#include "ss_zdoSecurityME.h"
#include "nwk_routing.h"
#include "ss_apsSecurityME.h"
#include "zdo_nwk_manager.h"
#include <stdint.h>
#include "ev_timer.h"
#include "security_service.h"

#if UINTPTR_MAX == UINT32_MAX
STATIC_ASSERT(OFFSETOF(zdo_nwk_manager_t, authEvt) == 4);
STATIC_ASSERT(OFFSETOF(zdo_nwk_manager_t, savedBuf) == 20);
STATIC_ASSERT(sizeof(zdo_nwk_manager_t) == 38);
#endif
#if defined(ZB_ED_ROLE)
STATIC_ASSERT(OFFSETOF(zb_info_t, bdbAttr.nodeIsOnANetwork) == 170);
#endif

#if defined(ZB_ROUTER_ROLE) || defined(ZB_COORDINATOR_ROLE)
typedef struct _attribute_packed_ {
	addrExt_t srcAddr;
	addrExt_t devAddr;
	u16 devShortAddr;
	u8 useParent;
	u8 rejoinNwk;
	bool secureRejoin;
} ss_zdo_auth_req_t;

enum {
	SS_ZDO_REJOIN_UNSECURED = 0,
	SS_ZDO_REJOIN_SECURED = 3,
	SS_ZDO_REJOIN_TRUST_CENTER = 2,
};

STATIC_ASSERT(sizeof(ss_zdo_auth_req_t) == 21);
STATIC_ASSERT(sizeof(zb_addrForNeighbor_t) == 12);

static inline u8 ss_zdo_child_auth_status(const ss_zdo_auth_req_t *req)
{
	u8 state = (u8)(((req->rejoinNwk != 0U) << 1) | (req->secureRejoin != 0U));

	if (state == SS_ZDO_REJOIN_UNSECURED) {
		return SS_STANDARD_DEV_UNSECURED_JOIN;
	}
	if (state == SS_ZDO_REJOIN_SECURED) {
		return SS_STANDARD_DEV_SECURED_REJOIN;
	}
	if (state == SS_ZDO_REJOIN_TRUST_CENTER) {
		return SS_STANDARD_DEV_TC_REJOIN;
	}
	return 0xffU;
}
#endif

static inline void build_join_confirm(void *buf)
{
	nlme_join_cnf_t *cnf = (nlme_join_cnf_t *)buf;

	cnf->nwkAddr = g_zbInfo.nwkNib.nwkAddr;
	cnf->status = NWK_STATUS_SUCCESS;
	cnf->activeChannel = g_zbInfo.macPib.phyChannelCur;
	ZB_EXTPANID_COPY(cnf->extPANId, g_zbInfo.nwkNib.extPANId);
}

#if !defined(ZB_COORDINATOR_ROLE)
int ss_devKeyPairTimeoutCb(void *arg)
{
	(void)arg;

	memset(ss_ib.keyPairSetNew, 0, sizeof(ss_dev_keyPair_t));

	return -1;
}
#endif

void ss_zdoInsecureRejoin(void *arg)
{
	(void)arg;

	ZB_EXTPANID_COPY(aps_ib.aps_use_ext_panid, g_zbInfo.nwkNib.extPANId);
	aps_ib.aps_use_insecure_join = 1;
	aps_ib.aps_authenticated = 0;

	if (zdo_nwkRejoinStart(1UL << g_zbInfo.macPib.phyChannelCur,
			       zdo_cfg_attributes.config_nwk_scan_duration) ==
	    ZDO_INSUFFICIENT_SPACE) {
		tl_zbTaskPost(ss_zdoInsecureRejoin, NULL);
	}
}

bool ss_securityModeIsDistributed(void)
{
	return ZB_IEEE_ADDR_IS_INVALID(ss_ib.trust_center_address);
}

#if defined(ZB_COORDINATOR_ROLE)
static int zb_tcTransportKeyTimerCb(void *arg)
{
	tl_zbTaskPost(ss_apsmeTransportKeyReq, arg);
	return -1;
}

static int tcSwitchKeyTimerCb(void *arg)
{
	ss_apsmeSwitchKeyReq_t req;

	(void)arg;

	ZB_IEEE_ADDR_INVALID(req.dstAddr);
	req.keySeqNum = (u8)(ss_ib.activeKeySeqNum + 1U);

	return (zb_apsmeSwitchKeyReq(&req) == RET_OK) ? -1 : 0;
}

u8 ss_zdoAcceptNewDevAllow(void)
{
	return ss_ib.tcPolicy.allowJoins;
}

void ss_zdoTcInit(void)
{
	g_zbNwkCtx.is_tc = 1;
	ZB_IEEE_ADDR_COPY(ss_ib.trust_center_address, g_zbInfo.macPib.extAddress);
	aps_ib.aps_authenticated = 1;
}

void ss_tcSwitchKeyTimerStart(void)
{
	/* vendor: "6: tshftls r2,r3,#30; 8: tjpl 14" - the timer is only posted
	 * when the flag bit is set. */
	if ((zdo_mgmt_nwk_flag & 0x02U) != 0U) {
		ev_timer_taskPost(tcSwitchKeyTimerCb, NULL, 3000);
	}
}

void ss_tcTransportKeyTimerStart(void *arg)
{
	ss_apsmeTransportKeyReq_t *req = (ss_apsmeTransportKeyReq_t *)arg;
	u8 idx = (u8)((ss_ib_active_secure_material_index_get() + 1U) & 0x01U);

	zdo_mgmt_nwk_flag |= 0x02U;
	memcpy(ss_ib.nwkSecurMaterialSet[idx].key, req->key, SEC_KEY_LEN);
	ss_ib.nwkSecurMaterialSet[idx].keySeqNum = req->keySeqNum;
	ss_ib.nwkSecurMaterialSet[idx].keyType = req->keyType;
	ev_timer_taskPost(zb_tcTransportKeyTimerCb, arg, 1000);
}
#endif

#if !defined(ZB_COORDINATOR_ROLE)
void ss_zdoTransportKeyIndHandle(void *arg)
{
	ss_apsmeTransportKeyInd_t *ind = (ss_apsmeTransportKeyInd_t *)arg;

	if (ind->keyType == SS_STANDARD_NETWORK_KEY) {
		if (ZB_IEEE_ADDR_IS_ZERO(ss_ib.trust_center_address)) {
			u16 shortAddr = 0;
			ZB_IEEE_ADDR_COPY(ss_ib.trust_center_address, ind->srcAddr);
			(void)tl_zbNwkAddrMapAdd(0, ss_ib.trust_center_address, &shortAddr);
		}

		if (ss_ib.preConfiguredKeyType != SS_PRECONFIGURED_NWKKEY &&
		    !ZB_IS_16BYTE_SECURITY_KEY_ZERO(ind->key)) {
			bool duplicate = FALSE;
			u8 idx = (u8)((ss_ib_active_secure_material_index_get() + ind->keySeqNum -
				       ss_ib.activeKeySeqNum) &
				      0x01U);

			for (u8 i = 0; i < SECUR_N_SECUR_MATERIAL; i++) {
				if (memcmp(ss_ib.nwkSecurMaterialSet[i].key, ind->key,
					   SEC_KEY_LEN) == 0 &&
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
					ss_ib_active_secure_material_index_set(idx);
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

		/* "10c: tloadrb r0,[r6,#23] ... 114: tjl zb_buf_free" - the buffer
		 * released here is the retained join confirmation, not the
		 * indication that carried the key. */
		if (zdo_nwk_mngr()->savedBuf != NULL) {
			zb_buf_free((zb_buf_t *)zdo_nwk_mngr()->savedBuf);
			zdo_nwk_mngr()->savedBuf = NULL;
		}

		ev_timer_taskCancel(&zdo_nwk_mngr()->authEvt);

		if (ss_ib.preConfiguredKeyType != SS_PRECONFIGURED_NWKKEY &&
		    !ZB_IS_16BYTE_SECURITY_KEY_ZERO(ind->key)) {
			ss_ib_active_secure_material_index_set(
				ss_ib_active_secure_material_index_get());
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

#if !defined(ZB_COORDINATOR_ROLE)
	ss_dev_keyPair_t *pendingKeyPair = (ss_dev_keyPair_t *)ss_ib.keyPairSetNew;

	if (pendingKeyPair->pTimeoutEvt != NULL) {
		ev_timer_taskCancel(&pendingKeyPair->pTimeoutEvt);
	}
	memset(&pendingKeyPair->pTimeoutEvt, 0, sizeof(pendingKeyPair->pTimeoutEvt));

	ZB_IEEE_ADDR_COPY(pendingKeyPair->keyPair.device_address, ind->srcAddr);
	memcpy(pendingKeyPair->keyPair.linkKey, ind->key, SEC_KEY_LEN);
	pendingKeyPair->keyPair.keyAttr = SS_UNVERIFIED_KEY;
	pendingKeyPair->keyPair.apsLinkKeyType = SS_UNIQUE_LINK_KEY;
	pendingKeyPair->keyPair.used = 1;
	pendingKeyPair->keyPair.rsv = 0;
	pendingKeyPair->keyPair.outgoingFrameCounter = 0;
	pendingKeyPair->keyPair.incomingFrameCounter = 0;
	ss_ib.tcLinkKeyType = 0;

	tl_zbTaskPost(ss_apsmeVerifyKeyReq, arg);
	pendingKeyPair->pTimeoutEvt =
		ev_timer_taskPost(ss_devKeyPairTimeoutCb, NULL, ss_ib.ssTimeoutPeriod);
#else
	zb_buf_free((zb_buf_t *)arg);
#endif
}
#endif

#if defined(ZB_ROUTER_ROLE) || defined(ZB_COORDINATOR_ROLE)
void ss_zdoChildTableStore(void *arg)
{
	tl_zb_normal_neighbor_entry_t *entry = (tl_zb_normal_neighbor_entry_t *)arg;
	zb_addrForNeighbor_t *info;

	info = (zb_addrForNeighbor_t *)ev_buf_allocate(sizeof(zb_addrForNeighbor_t));
	if (info == NULL) {
		return;
	}

	info->shortAddr = tl_zbshortAddrByIdx(entry->addrmapIdx);
	tl_zbExtAddrByIdx(entry->addrmapIdx, info->extAddr);
	info->depth = entry->depth;
	info->rxOnWhileIdle = entry->rxOnWhileIdle;
	info->deviceType = entry->deviceType;
	info->relationship = entry->relationship;
	tl_zbTaskPost(nwk_nodeAddrInfoStore, info);
}

void ss_zdoChildAuthStart(void *arg)
{
	ss_zdo_auth_req_t req;
	tl_zb_normal_neighbor_entry_t *entry;
	u8 status;

	memcpy(&req, arg, sizeof(req));
	status = ss_zdo_child_auth_status(&req);
	entry = nwk_neTblGetByExtAddr(req.devAddr);

	/* The vendor selects the neighbor path from the secure-all-fresh flag. */
	if (!ss_ib_secure_all_fresh()) {
		if (entry == NULL || entry->relationship != NEIGHBOR_IS_UNAUTH_CHILD) {
			zb_buf_free((zb_buf_t *)arg);
			return;
		}

		if (entry->deviceType == NWK_DEVICE_TYPE_ED) {
			entry->relationship = NEIGHBOR_IS_CHILD;
			ss_zdoChildTableStore(entry);
		} else {
			entry->relationship = NEIGHBOR_IS_SIBLING;
			zb_buf_free((zb_buf_t *)arg);
			return;
		}
	} else if ((status == SS_STANDARD_DEV_SECURED_REJOIN) && entry != NULL &&
		   entry->relationship == NEIGHBOR_IS_CHILD) {
		ss_zdoChildTableStore(entry);
	}

#if defined(ZB_COORDINATOR_ROLE)
	if (g_zbNwkCtx.is_tc != 0U) {
		ss_dev_pair_set_t keyPair;
		u8 keyPairStatus;

		if (status == SS_STANDARD_DEV_SECURED_REJOIN) {
			zb_buf_free((zb_buf_t *)arg);
			return;
		}

		keyPairStatus = ss_devKeyPairFind(req.devAddr, &keyPair);

		/* A verified unique key makes an unsecured join restart with an
		 * insecure rejoin.  This is the coordinator vendor's key-cache
		 * recovery path. */
		if (status == SS_STANDARD_DEV_UNSECURED_JOIN && keyPairStatus == NV_SUCC &&
		    keyPair.apsLinkKeyType == SS_UNIQUE_LINK_KEY &&
		    keyPair.keyAttr == SS_VERIFIED_KEY) {
			(void)ss_devKeyPairDelete(req.devAddr);
			ss_zdoInsecureRejoin(NULL);
			return;
		}

		/* A known default global key is not used to complete a TC rejoin. */
		if (status == SS_STANDARD_DEV_TC_REJOIN && g_zbNwkCtx.is_factory_new == 0U &&
		    keyPairStatus == NV_SUCC && keyPair.apsLinkKeyType == SS_GLOBAL_LINK_KEY &&
		    memcmp(keyPair.linkKey, tcLinkKeyCentralDefault, SEC_KEY_LEN) == 0) {
			zb_buf_free((zb_buf_t *)arg);
			return;
		}

		{
			ss_apsmeTransportKeyReq_t *tx = (ss_apsmeTransportKeyReq_t *)arg;

			ZB_IEEE_ADDR_COPY(tx->dstAddr, req.devAddr);
			tx->keyType = SS_STANDARD_NETWORK_KEY;
			if (ss_ib.preConfiguredKeyType == SS_PRECONFIGURED_NWKKEY) {
				memset(tx->key, 0, SEC_KEY_LEN);
				tx->keySeqNum = 0;
			} else {
				memcpy(tx->key,
				       ss_ib.nwkSecurMaterialSet
					       [ss_ib_active_secure_material_index_get()]
						       .key,
				       SEC_KEY_LEN);
				tx->keySeqNum = ss_ib.activeKeySeqNum;
			}
			tx->relayByParent = req.useParent;
			if (tx->relayByParent != 0U) {
				ZB_IEEE_ADDR_COPY(tx->partnerAddr, req.srcAddr);
			}
			tx->nwkSecurity = 0;

			if (zdoAppIndCbLst != NULL && zdoAppIndCbLst->zdoTcJoinIndCb != NULL) {
				zdo_tc_join_ind_t joinInd;

				ZB_IEEE_ADDR_COPY(joinInd.parentIeeeAddr, req.srcAddr);
				ZB_IEEE_ADDR_COPY(joinInd.devIeeeAddr, req.devAddr);
				joinInd.devShortAddr = req.devShortAddr;
				joinInd.sta = status;
				if (!zdoAppIndCbLst->zdoTcJoinIndCb(&joinInd)) {
					zb_buf_free((zb_buf_t *)arg);
					return;
				}
			}

			tl_zbTaskPost(ss_apsmeTransportKeyReq, tx);
		}
		return;
	}
#endif

	/* Router operation, including a coordinator-disabled router build. */
	if (ZB_IEEE_ADDR_IS_INVALID(ss_ib.trust_center_address)) {
		/* Match the vendor's distributed-security branch: only a TC rejoin
		 * proceeds through this path; other failed authentication attempts
		 * release the original buffer. */
		if (status != SS_STANDARD_DEV_TC_REJOIN) {
			zb_buf_free((zb_buf_t *)arg);
			return;
		}

		{
			ss_apsmeTransportKeyReq_t *tx = (ss_apsmeTransportKeyReq_t *)arg;

			ZB_IEEE_ADDR_COPY(tx->dstAddr, req.devAddr);
			tx->keyType = SS_STANDARD_NETWORK_KEY;
			if (ss_ib.preConfiguredKeyType == SS_PRECONFIGURED_NWKKEY) {
				memset(tx->key, 0, SEC_KEY_LEN);
				tx->keySeqNum = 0;
			} else {
				memcpy(tx->key,
				       ss_ib.nwkSecurMaterialSet
					       [ss_ib_active_secure_material_index_get()]
						       .key,
				       SEC_KEY_LEN);
				tx->keySeqNum = ss_ib.activeKeySeqNum;
			}
			tx->relayByParent = 0;
			tx->nwkSecurity = 0;
			tl_zbTaskPost(ss_apsmeTransportKeyReq, tx);
		}
		return;
	}

	{
		ss_apsmeUpdateDeviceReq_t *update = (ss_apsmeUpdateDeviceReq_t *)arg;

		ZB_IEEE_ADDR_COPY(update->dstAddr, ss_ib.trust_center_address);
		ZB_IEEE_ADDR_COPY(update->devAddr, req.devAddr);
		update->devShortAddr = req.devShortAddr;
		update->status = status;
		tl_zbTaskPost(ss_apsmeUpdateDevReq, update);
	}
}

void ss_zdoUpdateDeviceIndHandle(void *arg)
{
	ss_apsmeUpdateDeviceInd_t *ind = (ss_apsmeUpdateDeviceInd_t *)arg;
	u16 addrMapIdx = 0;
	u8 status = ind->status;
	addrExt_t resolvedExtAddr;

#if !defined(ZB_COORDINATOR_ROLE)
	if (status != SS_DEV_LEFT) {
		zb_buf_free((zb_buf_t *)arg);
		return;
	}
#else
	if (status == SS_STANDARD_DEV_SECURED_REJOIN) {
		zb_buf_free((zb_buf_t *)arg);
		return;
	}
#endif

	if (status == SS_DEV_LEFT) {
		if (tl_idxByExtAddr(&addrMapIdx, ind->devAddr) != RET_OK &&
		    tl_idxByShortAddr(&addrMapIdx, ind->devShortAddr) != RET_OK) {
			zb_buf_free((zb_buf_t *)arg);
			return;
		}

		if (zdoAppIndCbLst != NULL && zdoAppIndCbLst->zdpLeaveIndCb != NULL) {
			nlme_leave_ind_t leaveInd;

			memset(&leaveInd, 0, sizeof(leaveInd));
			tl_zbExtAddrByIdx(addrMapIdx, leaveInd.deviceAddr);
			zdoAppIndCbLst->zdpLeaveIndCb(&leaveInd);
		}

		tl_zbExtAddrByIdx(addrMapIdx, resolvedExtAddr);
		nwkRoutingTabEntryDstDel(tl_zbshortAddrByIdx(addrMapIdx));
#if defined(ZB_COORDINATOR_ROLE)
		nwkRouteRecTabEntryDstDel(tl_zbshortAddrByIdx(addrMapIdx));
		(void)ss_devKeyPairDelete(resolvedExtAddr);
#endif
		aps_bindingTblEntryDelByDstExtAddr(resolvedExtAddr);
		tl_nwkNeighborDeleteByAddrmapIdx(addrMapIdx);
		tl_zbNwkAddrMapDelete(addrMapIdx);
		zb_buf_free((zb_buf_t *)arg);
		return;
	}

#if defined(ZB_COORDINATOR_ROLE)
	if (status == SS_STANDARD_DEV_UNSECURED_JOIN && ss_ib.tcPolicy.allowJoins == 0U) {
		zb_buf_free((zb_buf_t *)arg);
		return;
	}

	if (status == SS_STANDARD_DEV_TC_REJOIN && ss_securityModeIsDistributed()) {
		zb_buf_free((zb_buf_t *)arg);
		return;
	}
#endif

	(void)tl_zbNwkAddrMapAdd(ind->devShortAddr, ind->devAddr, &addrMapIdx);

	{
		ss_zdo_auth_req_t *req = (ss_zdo_auth_req_t *)arg;

		req->useParent = 1;
		req->rejoinNwk = (status == SS_STANDARD_DEV_TC_REJOIN) ? 1U : 0U;
		req->secureRejoin = 0;
	}

	ss_zdoChildAuthStart(arg);
}

void ss_zdoRemoveDeviceIndHandle(void *arg)
{
	ss_apsmeRemoveDeviceInd_t *ind = (ss_apsmeRemoveDeviceInd_t *)arg;
	nlme_leave_req_t *req = (nlme_leave_req_t *)arg;

	if (!ZB_IEEE_ADDR_CMP(ind->tcAddr, ss_ib.trust_center_address)) {
		zb_buf_free((zb_buf_t *)arg);
		return;
	}

	if (ZB_IEEE_ADDR_CMP(ind->childExtAddr, g_zbInfo.macPib.extAddress)) {
		ZB_IEEE_ADDR_ZERO(req->deviceAddr);
	} else {
		ZB_IEEE_ADDR_COPY(req->deviceAddr, ind->childExtAddr);
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

	ss_ib_active_secure_material_index_set(idx);
	ss_ib.activeKeySeqNum = keySeqNum;

	if (ss_ib.outgoingFrameCounter > 0x80000000UL) {
		ss_ib.outgoingFrameCounter = 0;
	}

	tl_neighborFrameCntReset();
	nv_nwkFrameCountSaveToFlash(ss_ib.outgoingFrameCounter);
	zdo_ssInfoSaveToFlash();
}

#if defined(ZB_COORDINATOR_ROLE)
void ss_tcSwitchKey(u8 keySeqNum)
{
	ss_zdoNwkKeySwitch(keySeqNum);
	if ((zdo_mgmt_nwk_flag & 0x02U) != 0U) {
		zdo_mgmt_nwk_flag &= (u8)~0x02U;
	}
}
#endif

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
	ZB_IEEE_ADDR_COPY(keyPair.device_address, extAddr);
	ss_devKeyPairSave(&keyPair);
}

void ss_zdoUseKey(u8 keySeqNum)
{
	if (keySeqNum > 1U) {
		ZB_EXCEPTION_POST(SYS_EXCEPTTION_ZB_SS_KEY_INDEX);
		return;
	}

	ss_ib.activeKeySeqNum = keySeqNum;
	ss_ib_active_secure_material_index_set(keySeqNum);
}
u8 ss_keyIsEmpty(const u8 *key)
{
	const u8 *ptr = key;

	for (u8 i = 0; i < SEC_KEY_LEN; i++) {
		if (*ptr++ != 0U) {
			return 0;
		}
	}
	return 1;
}

bool ss_keyPreconfigured(void)
{
	return ss_keyIsEmpty(
		       ss_ib.nwkSecurMaterialSet[ss_ib_active_secure_material_index_get()].key)
		       ? FALSE
		       : TRUE;
}

void *ss_zdoGetNwkKeyBySeqNum(u8 seqNum)
{
	for (u8 i = 0; i < SECUR_N_SECUR_MATERIAL; i++) {
		u8 idx = (u8)((ss_ib_active_secure_material_index_get() + i) & 0x01U);

		if (ss_ib.nwkSecurMaterialSet[idx].keySeqNum == seqNum) {
			return ss_ib.nwkSecurMaterialSet[idx].key;
		}
	}

	return NULL;
}

/*
 * Called from the tail of the task loop, so the guard is what sets the flash
 * write rate. Persist one counter value every 1024 frames, which is what
 * libzb_router.a does: it tests `counter << 22` against zero, i.e. the low ten
 * bits. The increment inside keeps the counter off the multiple once the value
 * has been stored, so a quiet node cannot rewrite the same value every pass.
 */
#define SS_FRAME_COUNTER_SAVE_MASK 0x3FFU

void zdo_ssInfoUpdate(void)
{
	if ((ss_ib.outgoingFrameCounter & SS_FRAME_COUNTER_SAVE_MASK) == 0U &&
	    g_bdbAttrs.nodeIsOnANetwork != 0U &&
	    /* "28: tmovs r2,#39" is g_bdbCtx.state, read as a whole byte - not
	     * bit 3 of the bitfield at +43 (forceJoin), which is what this used
	     * to test.  The open question from be21c7e was not a g_bdbCtx layout
	     * problem, just the wrong field. */
	    g_bdbCtx.state == BDB_STATE_IDLE) {
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

		if (!ZB_IEEE_ADDR_IS_ZERO(ss_ib.trust_center_address) &&
		    !ZB_IEEE_ADDR_IS_INVALID(ss_ib.trust_center_address)) {
			u16 shortAddr = 0;
			tl_zbNwkAddrMapAdd(0, ss_ib.trust_center_address, &shortAddr);
		}
		return;
	}

	ss_ib.ssTimeoutPeriod = 5000;
	ss_ib_security_level_set(0);

	if (enSecurity) {
		ss_ib_security_level_set(5);
		ss_ib_secure_all_fresh_set(TRUE);
		ss_ib.preConfiguredKeyType = 0;
		ss_ib.devKeyPairNum = 0;
	}

#if defined(ZB_COORDINATOR_ROLE)
	/* Both paths of the vendor routine join at "22: tmovs r5,#0" and run the
	 * trust-center policy from there: "40: tstorerb r2,[r4,r3]" with r3=69 is
	 * allowRejoins and "46: tstorerb r2,[r4,r3]" with r3=68 and r2=1 is
	 * allowJoins.  Setting the policy only for an unsecured start left a
	 * secured trust center refusing every device that joins through a router.
	 */
	{
		u8 stackRevision = af_nodeDescStackRevisionGet();

		ss_ib.tcPolicy.allowRejoins = (stackRevision <= 20U);
		ss_ib.tcPolicy.allowJoins = 1;
	}
#endif

	if (nv_nwkFrameCountFromFlash(&frameCounter) == 0U) {
		ss_ib.outgoingFrameCounter = frameCounter;
	} else {
		ss_ib.outgoingFrameCounter = 0;
	}
}

void ss_securityModeSet(ss_securityMode_e m)
{
	if (aps_ib.aps_authenticated && !ZB_IEEE_ADDR_IS_ZERO(ss_ib.trust_center_address) &&
	    !ZB_IEEE_ADDR_IS_INVALID(ss_ib.trust_center_address)) {
		ZB_EXCEPTION_POST(SYS_EXCEPTTION_ZB_NWK_NEIGHBOR_TABLE); /* vendor: evt 0x33 */
		return;
	}

	if (m == SS_SEMODE_DISTRIBUTED) {
		ss_ib.tcPolicy.updateTCLKrequired = 0;
		ZB_IEEE_ADDR_INVALID(ss_ib.trust_center_address);
	} else if (m == SS_SEMODE_CENTRALIZED) {
		ss_ib.tcPolicy.updateTCLKrequired = 1;
		ZB_IEEE_ADDR_ZERO(ss_ib.trust_center_address);
	}
}
