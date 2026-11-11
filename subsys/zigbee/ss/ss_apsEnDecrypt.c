/*
 * Copyright 2026 libzigbee
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "zb_common.h"
#include "aps_data.h"
#include "ss_apsEnDecrypt.h"
#include "ss_apsSecurityME.h"
#include "ss_tlCCM.h"
#include "zdo_nwk_manager.h"
#include "security_service.h"

typedef struct _attribute_packed_ {
	uint8_t securityLevel: 3;
	uint8_t keyIdentifer: 2;
	uint8_t extendedNonce: 1;
	uint8_t reserved: 2;
	uint32_t frameCnt;
} ss_apsEncryAuxCommonHdr_t;

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

typedef struct _attribute_packed_ {
	uint8_t apsHdrLen;
	uint8_t frameCtrl;
	uint8_t apsCounter;
	uint8_t srcEp;
	uint16_t srcShortAddr;
} ss_apsHdrParsed_t;

#define SS_CLR_SECURITY_LEVEL(d)    ((*(uint8_t *)(d)) &= 0xf8U)
#define SS_SET_SECURITY_LEVEL(d, v) ((*(uint8_t *)(d)) = (uint8_t)((*(uint8_t *)(d) & 0xf8U) | ((v) & 0x07U)))
#define SS_AUX_NONCE_INCLUDE(d)     ((d) & BIT(5))

uint8_t g_zbDefaultLinkKeyEn = 1;

_CODE_SS_ uint8_t ss_apsEnAuxHdrFill(uint8_t *auxHdr, void *keyInfo, uint8_t extNonceOpt)
{
	uint8_t *p = auxHdr + 5;
	uint8_t *key = (uint8_t *)keyInfo;

	COPY_U32TOBUFFER(auxHdr + 1, ss_ib.outgoingFrameCounter);
	ss_ib.outgoingFrameCounter++;

	auxHdr[0] = (uint8_t)((auxHdr[0] & (uint8_t)~0x07U) | 0x05U);

	if (key != NULL) {
		auxHdr[0] |= 0x20U;
		ZB_IEEE_ADDR_COPY(auxHdr + 5, g_zbMacPib.extAddress);
		p = auxHdr + 13;

		if (key[0] != 5U) {
			if (key[1] == 1U) {
				auxHdr[0] = (uint8_t)((auxHdr[0] & (uint8_t)~0x18U) | 0x10U);
			} else {
				auxHdr[0] |= 0x18U;
			}
		} else {
			auxHdr[0] &= (uint8_t)~0x18U;
		}
	} else if ((extNonceOpt & 0x20U) != 0U) {
		auxHdr[0] |= 0x20U;
		ZB_IEEE_ADDR_COPY(auxHdr + 5, g_zbMacPib.extAddress);
		p = auxHdr + 13;
		auxHdr[0] &= (uint8_t)~0x18U;
	} else {
		auxHdr[0] &= (uint8_t)~0x18U;
	}

	return (uint8_t)(p - auxHdr);
}

_CODE_SS_ static void ss_apsSecureStatus(void *arg, uint16_t addrShort, aps_status_t status)
{
	nlme_nwkStatus_ind_t *cmd = (nlme_nwkStatus_ind_t *)arg;

	cmd->status = (nwk_statusCode_t)status;
	cmd->nwkAddr = addrShort;
	zb_TaskPost(zdo_nlme_status_indication, arg);
}

_CODE_SS_ uint8_t ss_apsSecureFrame(void *p, uint8_t apsHdrAuxLen, uint8_t apsHdrLen, addrExt_t extAddr)
{
	nlde_data_req_t *nldereq = (nlde_data_req_t *)p;
	ss_apsNwkAuxFrameHdr_t aux;
	ss_securityCcmNonce_t nonce;
	ss_dev_pair_set_t keyPair;
	uint8_t *payloadAddr;
	uint8_t *key = NULL;
	uint8_t keyTemp[SEC_KEY_LEN];
	uint8_t *msgStartAddr = nldereq->nsdu;
	bool haveKeyPair = FALSE;

	memset(&aux, 0, sizeof(aux));
	memset(&nonce, 0, sizeof(nonce));
	memset(&keyPair, 0, sizeof(keyPair));
	memset(keyTemp, 0, sizeof(keyTemp));

	payloadAddr = msgStartAddr + apsHdrAuxLen;
	msgStartAddr[0] |= 0x20U;
	memcpy(&aux, msgStartAddr + apsHdrLen, (uint16_t)(apsHdrAuxLen - apsHdrLen));

	if (aux.keyIdentifer == SS_SECUR_NWK_KEY) {
		key = ss_ib.nwkSecurMaterialSet[ss_ib_active_secure_material_index_get()].key;
	} else {
		haveKeyPair = (ss_devKeyPairFind(extAddr, &keyPair) == NV_SUCC);

		/* The gate tests tcLinkKeyType, not preConfiguredKeyType.  A trust center
		 * configured for unique link keys can only encrypt with a stored unique pair:
		 * encryption fails when the pair is missing or when the stored pair is not a unique
		 * one.  Every other case falls through to the pair / trust-center / distributed key
		 * selection below. */
		if (ss_ib.tcLinkKeyType == SS_UNIQUE_LINK_KEY && g_zbNwkCtx.is_tc != 0U &&
		    (!haveKeyPair || keyPair.apsLinkKeyType != SS_UNIQUE_LINK_KEY)) {
			ss_apsSecureStatus(p, nldereq->dstAddr, APS_STATUS_SECURITY_FAIL);
			return RET_ERROR;
		}

		if (haveKeyPair) {
			key = keyPair.linkKey;
		} else {
			key = ss_securityModeIsDistributed() ? ss_ib.distributeLinkKey
							     : ss_ib.tcLinkKey;
		}

		if (aux.keyIdentifer != SS_SECUR_DATA_KEY) {
			uint8_t hashInput = (aux.keyIdentifer == SS_SECUR_KEY_LOAD_KEY) ? 2U : 0U;
			if (ss_keyHash(&hashInput, key, keyTemp) == RET_OK) {
				key = keyTemp;
			}
		}
	}

	nonce.frameCnt = aux.frameCnt;
	aux.securityLevel = 5;
	nonce.secureCtrl = *(uint8_t *)&aux;
	ZB_IEEE_ADDR_COPY(nonce.srcAddr, g_zbMacPib.extAddress);

	{
		uint8_t srcMsgLen = (uint8_t)(nldereq->nsduLen - apsHdrAuxLen);
		uint8_t len = ss_ccmEncryption(key, (uint8_t *)&nonce, apsHdrAuxLen, msgStartAddr, srcMsgLen,
					  payloadAddr);

		nldereq->nsduLen = (uint8_t)(apsHdrAuxLen + len);
	}

	SS_CLR_SECURITY_LEVEL(msgStartAddr + apsHdrLen);
	return RET_OK;
}

_CODE_SS_ uint8_t ss_apsDecryptFrame(void *arg)
{
	nlde_data_ind_t *ind = (nlde_data_ind_t *)arg;
	ss_apsHdrParsed_t *apsHdr = (ss_apsHdrParsed_t *)((uint8_t *)arg + sizeof(nlde_data_ind_t));
	ss_apsEncryAuxCommonHdr_t aux;
	ss_securityCcmNonce_t nonce;
	ss_dev_pair_set_t keyPair;
	uint8_t keyTemp[SEC_KEY_LEN];
	uint8_t *auxStart;
	uint8_t *cursor;
	uint8_t *key = NULL;
	uint8_t *retryPayload = NULL;
	uint8_t *retryKey;
	uint8_t retryKeyIndex;
	uint8_t retryKeyCount;
	uint8_t publicKeyMask;
	uint8_t factoryNewTransport;
	ss_dev_pair_set_t *pair;
	uint16_t addrMapIdx;
	addrExt_t neighborExtAddr;
	zb_normal_neighbor_entry_t *neighbor;
	uint8_t ret;

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

	if (zb_ExtAddrByShortAddr(apsHdr->srcShortAddr, nonce.srcAddr, &addrMapIdx) ==
		    ZB_RETURN_INVALID &&
	    aps_ib.aps_authenticated) {
		return RET_ERROR;
	}

	if (SS_AUX_NONCE_INCLUDE(*auxStart) != 0U) {
		ZB_IEEE_ADDR_COPY(nonce.srcAddr, cursor);
		cursor += EXT_ADDR_LEN;
	}

#if !defined(ZB_COORDINATOR_ROLE)
	/* ED and router "98:".."b6:": the trust center link key that still waits
	 * for its Confirm-Key lives in ss_ib.keyPairSetNew, not in NV, and the
	 * Confirm-Key is secured with it.  When it is in use and "246:".."256:"
	 * matches its address, the NV lookup is skipped.  The coordinator object
	 * has no such branch.
	 */
	pair = &((ss_dev_keyPair_t *)ss_ib.keyPairSetNew)->keyPair;
	if (pair->used == 0U || memcmp(pair->device_address, nonce.srcAddr, EXT_ADDR_LEN) != 0) {
		pair = (ss_devKeyPairFind(nonce.srcAddr, &keyPair) == NV_SUCC) ? &keyPair : NULL;
	}
#else
	pair = (ss_devKeyPairFind(nonce.srcAddr, &keyPair) == NV_SUCC) ? &keyPair : NULL;
#endif
	if (pair != NULL) {
		key = pair->linkKey;
	} else {
		if (ss_ib.preConfiguredKeyType == SS_PRECONFIGURED_UNIQUELLINKKEY) {
			return RET_ERROR;
		}
		key = ss_ib.tcLinkKey;
	}

	if (aux.keyIdentifer == SS_SECUR_KEY_TRANSPORT_KEY) {
		neighbor = zb_NeighborTableSearchFromShortAddr(apsHdr->srcShortAddr,
								 neighborExtAddr, &addrMapIdx);
		if (neighbor != NULL && neighbor->deviceType != NWK_DEVICE_TYPE_COORDINATOR &&
		    ZB_IEEE_ADDR_CMP(neighborExtAddr, nonce.srcAddr)) {
			key = ss_ib.tcLinkKey;
		}
	}

	SS_SET_SECURITY_LEVEL(auxStart, 5);

	if (pair != NULL && pair->apsLinkKeyType == SS_UNIQUE_LINK_KEY) {
		if (pair->incomingFrameCounter > aux.frameCnt) {
			return RET_ERROR;
		}
		pair->incomingFrameCounter = aux.frameCnt + 1;
	}

	if (aux.keyIdentifer != SS_SECUR_DATA_KEY) {
		uint8_t hashInput = (aux.keyIdentifer == SS_SECUR_KEY_LOAD_KEY) ? 2U : 0U;

		if (ss_keyHash(&hashInput, key, keyTemp) == RET_OK) {
			key = keyTemp;
		}
	}

	nonce.frameCnt = aux.frameCnt;
	aux.securityLevel = 5;
	nonce.secureCtrl = *(uint8_t *)&aux;

	factoryNewTransport =
		(uint8_t)((zb_isDeviceFactoryNew() != FALSE) && (g_zbNwkCtx.is_factory_new != 0U) &&
		     (aux.keyIdentifer == SS_SECUR_KEY_TRANSPORT_KEY));
	if (factoryNewTransport != 0U) {
		uint8_t payloadLen = (uint8_t)((ind->nsdu + ind->nsduLen) - cursor);

		retryPayload = ev_buf_allocate(payloadLen);
		if (retryPayload != NULL) {
			memcpy(retryPayload, cursor, payloadLen);
		}
	}

	ret = ss_ccmDecryption(key, (uint8_t *)&nonce, (uint8_t)(cursor - ind->nsdu), ind->nsdu,
			       (uint8_t)((ind->nsdu + ind->nsduLen) - cursor), cursor);

	if (factoryNewTransport != 0U && zb_isDeviceFactoryNew() != FALSE && ret != RET_OK &&
	    retryPayload != NULL && pair == NULL) {
		uint8_t payloadLen = (uint8_t)((ind->nsdu + ind->nsduLen) - cursor);

		publicKeyMask = (uint8_t)(g_zbDefaultLinkKeyEn &
				     (LINKKEY_MASTER_KEY_EN | LINKKEY_CERTIFICATION_KEY_EN));
		retryKeyCount = 3;
		for (retryKeyIndex = 0U; retryKeyIndex < retryKeyCount; retryKeyIndex++) {
			if (retryKeyIndex == 0U) {
				if ((publicKeyMask & LINKKEY_MASTER_KEY_EN) != 0U) {
					retryKey = (uint8_t *)linkKeyDistributedMaster;
				} else if ((publicKeyMask & LINKKEY_CERTIFICATION_KEY_EN) != 0U) {
					retryKey = (uint8_t *)linkKeyDistributedCertification;
				} else {
					retryKey = (uint8_t *)tcLinkKeyCentralDefault;
				}
			} else if (retryKeyIndex == 1U) {
				if ((publicKeyMask & LINKKEY_CERTIFICATION_KEY_EN) != 0U) {
					retryKey = (uint8_t *)linkKeyDistributedCertification;
				} else if ((publicKeyMask & LINKKEY_MASTER_KEY_EN) != 0U) {
					retryKey = (uint8_t *)linkKeyDistributedMaster;
				} else {
					retryKey = (uint8_t *)tcLinkKeyCentralDefault;
				}
			} else {
				retryKey = (uint8_t *)tcLinkKeyCentralDefault;
			}

			if (memcmp(retryKey, key, SEC_KEY_LEN) == 0) {
				continue;
			}

			memcpy(cursor, retryPayload, payloadLen);
			key = retryKey;
			ret = ss_ccmDecryption(key, (uint8_t *)&nonce, (uint8_t)(cursor - ind->nsdu),
					       ind->nsdu, (uint8_t)((ind->nsdu + ind->nsduLen) - cursor),
					       cursor);
			if (ret == RET_OK) {
				break;
			}
		}
	}

	if (retryPayload != NULL) {
		ev_buf_free(retryPayload);
	}

	if (ret != RET_OK) {
		g_sysDiags.apsDecryptFailures++;
	}

	if (ret == RET_OK) {
		ind->nsduLen = (uint8_t)(ind->nsduLen - ((cursor - auxStart) + 4));
		ind->nsdu = ind->nsdu + (cursor - auxStart);
	}

	return ret;
}

void ss_pubLinkKeySelect(ss_pubLinkKeyOpt_e ks)
{
	g_zbDefaultLinkKeyEn = (uint8_t)ks;
}
