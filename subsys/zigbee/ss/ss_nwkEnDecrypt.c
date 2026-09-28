/*
 * Copyright 2026 libzigbee
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "zb_common.h"
#include "ss_nwkEnDecrypt.h"
#include "ss_tlCCM.h"
#include "ss_zdoSecurityME.h"
#include "zdo_nwk_manager.h"
#include "security_service.h"
#include "nwk_neighbor.h"

typedef struct _attribute_packed_ {
	u8 securityLevel: 3;
	u8 keyIdentifer: 2;
	u8 extendedNonce: 1;
	u8 reserved: 2;
	u32 frameCnt;
	addrExt_t srcAddr;
	u8 keySeqNum;
} ss_apsNwkAuxFrameHdr_t;

typedef struct _attribute_packed_ {
	addrExt_t srcAddr;
	u32 frameCnt;
	u8 secureCtrl;
} ss_securityCcmNonce_t;

#define SS_CLR_SECURITY_LEVEL(d) ((*(u8 *)(d)) &= SS_SEC_CTRL_LEVEL_CLEAR_MASK)
#define SS_SET_SECURITY_LEVEL(d, t)                                                                \
	do {                                                                                       \
		(*(u8 *)(d)) = (u8)((*(u8 *)(d) & SS_SEC_CTRL_LEVEL_CLEAR_MASK) | (t));            \
	} while (0)
#define NWK_STATIC_PATH_COST_LOCAL 7

u8 T_DBG_decFrameCnt = 0;

_CODE_SS_ static inline void ss_nwkSecureStatus(void *arg, u16 addrShort, u8 status)
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
	ZB_IEEE_ADDR_COPY(aux.srcAddr, g_zbMacPib.extAddress);
	aux.securityLevel = SS_SECURITY_LEVEL_ENC_MIC_32;

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

			ZB_IEEE_ADDR_COPY(nonce.srcAddr, aux.srcAddr);
			nonce.frameCnt = aux.frameCnt;
			nonce.secureCtrl = *(u8 *)&aux;

			srcMsg = req->msdu + nwkHdrAuxLen;
			srcMsgLen = (u8)(req->msduLength - nwkHdrAuxLen);
			len = ss_ccmEncryption(key, (u8 *)&nonce, nwkHdrAuxLen, req->msdu,
					       srcMsgLen, srcMsg);

			if (len == 0U) {
				ret = RET_ERROR;
			} else {
				req->msduLength = (u8)(nwkHdrAuxLen + len);
				SS_CLR_SECURITY_LEVEL(req->msdu + nwkHdrLen);
			}

#if defined(ZB_COORDINATOR_ROLE)
			/* The vendor tells the trust center that the counter is running
			 * out after the increment, not for a frame that was not built. */
			if (len != 0U && g_zbNwkCtx.is_tc != 0U &&
			    ss_ib.outgoingFrameCounter > SS_FRAME_CNT_HIGH_MARK &&
			    zdoAppIndCbLst != NULL &&
			    zdoAppIndCbLst->ssTcFrameCntReachedCb != NULL &&
			    (zdo_mgmt_nwk_flag & SS_MGMT_NWK_FLAG_TC_KEY_UPDATE) == 0U) {
				zdoAppIndCbLst->ssTcFrameCntReachedCb();
			}
#endif
		}
	}

	return ret;
}

_CODE_SS_ u8 ss_nwkDecryptFrame(void *p, u8 nwkHdrSize, u8 payloadSize, u8 *payloadAddr,
				nwk_hdr_t *nwkHdr, u8 lqi)
{
	zb_buf_t *nsdu = (zb_buf_t *)p;
	zb_mscp_data_ind_t *pInd = (zb_mscp_data_ind_t *)p;
	tl_zb_normal_neighbor_entry_t *nbe;
	ss_apsNwkAuxFrameHdr_t aux;
	ss_securityCcmNonce_t nonce;
	u8 auxLen = sizeof(ss_apsNwkAuxFrameHdr_t);
	u8 headerLen;
	u8 encryptedLen;
	u8 ret;
	u16 neighborAddr = pInd->srcAddr.addr.shortAddr;
	u8 *key;

	(void)nwkHdr;
	(void)lqi;

	headerLen = (u8)(nwkHdrSize - auxLen);
	SS_SET_SECURITY_LEVEL(payloadAddr + headerLen, SS_SECURITY_LEVEL_ENC_MIC_32);
	memcpy(&aux, payloadAddr + headerLen, auxLen);

	encryptedLen = (u8)(payloadSize - headerLen - auxLen);
	/* The vendor compares the length as a signed byte. */
	if ((s8)encryptedLen <= (s8)(ZB_CCM_M - 1)) {
		g_sysDiags.nwkDecryptFailures++;
		zb_buf_free(nsdu);
		return RET_ERROR;
	}

	nbe = nwk_neTblGetByExtAddr(aux.srcAddr);

	if (aux.keySeqNum == ss_ib.activeKeySeqNum && nbe != NULL) {
		nbe->receivedFrameCnt = aux.frameCnt;
		if (aux.frameCnt == SS_FRAME_CNT_INVALID || aux.frameCnt < nbe->incomingFrameCnt) {
			g_sysDiags.nwkFCFailure++;
			nbe->frameCounterFailCnt++;
			ss_nwkSecureStatus(nsdu, neighborAddr,
					   NWK_COMMAND_STATUS_BAD_FRAME_COUNTER);
			return RET_ERROR;
		}
	}

	key = ss_zdoGetNwkKeyBySeqNum(aux.keySeqNum);
	if (key == NULL || aux.keySeqNum < ss_ib.activeKeySeqNum) {
		ss_nwkSecureStatus(nsdu, neighborAddr, NWK_COMMAND_STATUS_BAD_KEY_SEQUENCE_NUMBER);
		return RET_ERROR;
	}

	ZB_IEEE_ADDR_COPY(nonce.srcAddr, aux.srcAddr);
	nonce.frameCnt = aux.frameCnt;
	nonce.secureCtrl = *(u8 *)&aux;
	ret = ss_ccmDecryption(key, (u8 *)&nonce, (u8)(headerLen + auxLen), payloadAddr,
			       encryptedLen, payloadAddr + headerLen + auxLen);
	if (ret != RET_OK) {
		g_sysDiags.nwkDecryptFailures++;
		zb_buf_free(nsdu);
		return RET_ERROR;
	}

#if defined(ZB_COORDINATOR_ROLE)
	if (g_zbNwkCtx.is_tc != 0U && aux.frameCnt > SS_FRAME_CNT_HIGH_MARK &&
	    zdoAppIndCbLst != NULL && zdoAppIndCbLst->ssTcFrameCntReachedCb != NULL &&
	    (zdo_mgmt_nwk_flag & SS_MGMT_NWK_FLAG_TC_KEY_UPDATE) == 0U) {
		zdoAppIndCbLst->ssTcFrameCntReachedCb();
	}
#endif

	if (nbe != NULL) {
		nbe->age = 0;
		nbe->incomingFrameCnt = aux.frameCnt + 1U;
		nbe->frameCounterFailCnt = 0;

#if defined(ZB_ROUTER_ROLE) || defined(ZB_COORDINATOR_ROLE)
		if (nbe->relationship == NEIGHBOR_IS_UNAUTH_CHILD) {
			if (nbe->deviceType == NWK_DEVICE_TYPE_ROUTER) {
				nbe->relationship = NEIGHBOR_IS_SIBLING;
			} else if (nbe->deviceType == NWK_DEVICE_TYPE_ED) {
				nbe->relationship = NEIGHBOR_IS_CHILD;
				ss_zdoChildTableStore(nbe);
			}
		}
#endif
	}

	if (aux.keySeqNum != ss_ib.activeKeySeqNum &&
	    (zdo_mgmt_nwk_flag & SS_MGMT_NWK_FLAG_KEY_REJOIN) == 0U) {
		ss_zdoNwkKeySwitch(aux.keySeqNum);
	}
	return RET_OK;
}
