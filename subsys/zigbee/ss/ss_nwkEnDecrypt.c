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
	uint8_t securityLevel: 3;
	uint8_t keyIdentifer: 2;
	uint8_t extendedNonce: 1;
	uint8_t reserved: 2;
	uint32_t frameCnt;
	addrExt_t srcAddr;
	uint8_t keySeqNum;
} ss_apsNwkAuxFrameHdr_t;

typedef struct _attribute_packed_ {
	addrExt_t srcAddr;
	uint32_t frameCnt;
	uint8_t secureCtrl;
} ss_securityCcmNonce_t;

#define SS_CLR_SECURITY_LEVEL(d) ((*(uint8_t *)(d)) &= 0xf8U)
#define SS_SET_SECURITY_LEVEL(d, t)                                                                \
	do {                                                                                       \
		(*(uint8_t *)(d)) = (uint8_t)((*(uint8_t *)(d) & 0xf8U) | (t));                                   \
	} while (0)
#define NWK_STATIC_PATH_COST_LOCAL 7

uint8_t T_DBG_decFrameCnt = 0;

_CODE_SS_ static inline void ss_nwkSecureStatus(void *arg, uint16_t addrShort, uint8_t status)
{
	nlme_nwkStatus_ind_t *cmd = (nlme_nwkStatus_ind_t *)arg;

	cmd->status = (nwk_statusCode_t)status;
	cmd->nwkAddr = addrShort;
	zb_TaskPost(zdo_nlme_status_indication, arg);
}

_CODE_SS_ uint8_t ss_nwkSecureFrame(zb_buf_t *src, uint8_t nwkHdrAuxLen)
{
	uint8_t ret = RET_OK;
	zb_mscp_data_req_t *req = (zb_mscp_data_req_t *)src;
	ss_apsNwkAuxFrameHdr_t aux;
	uint8_t auxLen = sizeof(ss_apsNwkAuxFrameHdr_t);
	uint8_t nwkHdrLen = (uint8_t)(nwkHdrAuxLen - auxLen);

	memset(&aux, 0, sizeof(aux));

	aux.keyIdentifer = SS_SECUR_NWK_KEY;
	aux.extendedNonce = 1;
	aux.frameCnt = ss_ib.outgoingFrameCounter++;
	aux.keySeqNum = ss_ib.activeKeySeqNum;
	ZB_IEEE_ADDR_COPY(aux.srcAddr, g_zbMacPib.extAddress);
	aux.securityLevel = 5;

	memcpy(req->msdu + nwkHdrLen, &aux, auxLen);

	{
		uint8_t *key = ss_zdoGetNwkKeyBySeqNum(aux.keySeqNum);

		if (key == NULL) {
			ret = RET_ERROR;
		} else {
			ss_securityCcmNonce_t nonce;
			uint8_t *srcMsg;
			uint8_t srcMsgLen;
			uint8_t len;

			ZB_IEEE_ADDR_COPY(nonce.srcAddr, aux.srcAddr);
			nonce.frameCnt = aux.frameCnt;
			nonce.secureCtrl = *(uint8_t *)&aux;

			srcMsg = req->msdu + nwkHdrAuxLen;
			srcMsgLen = (uint8_t)(req->msduLength - nwkHdrAuxLen);
			len = ss_ccmEncryption(key, (uint8_t *)&nonce, nwkHdrAuxLen, req->msdu,
					       srcMsgLen, srcMsg);

#if defined(ZB_ROUTER_ROLE) || defined(ZB_ED_ROLE)
			if (len == 0U) {
				ret = RET_ERROR;
			} else {
				req->msduLength = (uint8_t)(nwkHdrAuxLen + len);
				SS_CLR_SECURITY_LEVEL(req->msdu + nwkHdrLen);
			}
#else
			req->msduLength = (uint8_t)(nwkHdrAuxLen + len);
			SS_CLR_SECURITY_LEVEL(req->msdu + nwkHdrLen);
#endif

#if defined(ZB_COORDINATOR_ROLE)
			if (g_zbNwkCtx.joined != 0U && aux.frameCnt > 0x80000000UL &&
			    (zdo_mgmt_nwk_flag & BIT(2)) == 0U && zdoAppIndCbLst != NULL &&
			    zdoAppIndCbLst->ssTcFrameCntReachedCb != NULL) {
				zdoAppIndCbLst->ssTcFrameCntReachedCb();
			}
#endif
		}
	}

	return ret;
}

_CODE_SS_ uint8_t ss_nwkDecryptFrame(void *p, uint8_t nwkHdrSize, uint8_t payloadSize, uint8_t *payloadAddr,
				nwk_hdr_t *nwkHdr, uint8_t lqi)
{
	zb_buf_t *nsdu = (zb_buf_t *)p;
	zb_mscp_data_ind_t *pInd = (zb_mscp_data_ind_t *)p;
	zb_normal_neighbor_entry_t *nbe;
	ss_apsNwkAuxFrameHdr_t aux;
	ss_securityCcmNonce_t nonce;
	uint8_t auxLen = sizeof(ss_apsNwkAuxFrameHdr_t);
	uint8_t headerLen;
	uint8_t encryptedLen;
	uint8_t ret;
	uint16_t neighborAddr = pInd->srcAddr.addr.shortAddr;
	uint8_t *key;

	(void)nwkHdr;
	(void)lqi;

	headerLen = (uint8_t)(nwkHdrSize - auxLen);
	SS_SET_SECURITY_LEVEL(payloadAddr + headerLen, 5);
	memcpy(&aux, payloadAddr + headerLen, auxLen);

	encryptedLen = (uint8_t)(payloadSize - headerLen - auxLen);
	if (encryptedLen < ZB_CCM_M) {
		g_sysDiags.nwkDecryptFailures++;
		zb_buf_free(nsdu);
		return RET_ERROR;
	}

	nbe = nwk_neTblGetByExtAddr(aux.srcAddr);

	if (aux.keySeqNum == ss_ib.activeKeySeqNum && nbe != NULL) {
		nbe->receivedFrameCnt = aux.frameCnt;
		if (nbe->incomingFrameCnt > aux.frameCnt || nbe->incomingFrameCnt == (uint32_t)~0U) {
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
	nonce.secureCtrl = *(uint8_t *)&aux;
	ret = ss_ccmDecryption(key, (uint8_t *)&nonce, (uint8_t)(headerLen + auxLen), payloadAddr,
			       encryptedLen, payloadAddr + headerLen + auxLen);
	if (ret != RET_OK) {
		g_sysDiags.nwkDecryptFailures++;
		zb_buf_free(nsdu);
		return RET_ERROR;
	}

#if defined(ZB_COORDINATOR_ROLE)
	if (g_zbNwkCtx.is_tc != 0U && aux.frameCnt > 0x80000000UL && zdoAppIndCbLst != NULL &&
	    zdoAppIndCbLst->ssTcFrameCntReachedCb != NULL) {
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

	if (aux.keySeqNum != ss_ib.activeKeySeqNum && (zdo_mgmt_nwk_flag & BIT(2)) == 0U) {
		ss_zdoNwkKeySwitch(aux.keySeqNum);
	}
	return RET_OK;
}
