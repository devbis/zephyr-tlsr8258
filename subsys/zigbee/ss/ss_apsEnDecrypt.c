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
	u8 securityLevel: 3;
	u8 keyIdentifer: 2;
	u8 extendedNonce: 1;
	u8 reserved: 2;
	u32 frameCnt;
} ss_apsEncryAuxCommonHdr_t;

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

typedef struct _attribute_packed_ {
	u8 apsHdrLen;
	u8 frameCtrl;
	u8 apsCounter;
	u8 srcEp;
	u16 srcShortAddr;
} ss_apsHdrParsed_t;

#define SS_CLR_SECURITY_LEVEL(d)    ((*(u8 *)(d)) &= 0xf8U)
#define SS_SET_SECURITY_LEVEL(d, v) ((*(u8 *)(d)) = (u8)((*(u8 *)(d) & 0xf8U) | ((v) & 0x07U)))
#define SS_AUX_NONCE_INCLUDE(d)     ((d) & BIT(5))

u8 g_zbDefaultLinkKeyEn = 1;

_CODE_SS_ u8 ss_apsEnAuxHdrFill(u8 *auxHdr, void *keyInfo, u8 extNonceOpt)
{
	u8 *p = auxHdr + 5;
	u8 *key = (u8 *)keyInfo;

	COPY_U32TOBUFFER(auxHdr + 1, ss_ib.outgoingFrameCounter);
	ss_ib.outgoingFrameCounter++;

	auxHdr[0] = (u8)((auxHdr[0] & (u8)~0x07U) | 0x05U);

	if (key != NULL) {
		auxHdr[0] |= 0x20U;
		ZB_IEEE_ADDR_COPY(auxHdr + 5, g_zbMacPib.extAddress);
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
		ZB_IEEE_ADDR_COPY(auxHdr + 5, g_zbMacPib.extAddress);
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
		key = ss_ib.nwkSecurMaterialSet[ss_ib_active_secure_material_index_get()].key;
	} else {
		haveKeyPair = (ss_devKeyPairFind(extAddr, &keyPair) == NV_SUCC);

		/* "82: tmovs r2,#80; 84: tloadrb r2,[r1,r2]" - the gate reads
		 * ss_ib+80, which is tcLinkKeyType, not preConfiguredKeyType
		 * (ss_ib+64).  A trust center configured for unique link keys can
		 * only encrypt with a stored unique pair: "12e" fails when the pair
		 * is missing and "132: tloadrb r2,[r3,#25]" fails when the stored
		 * pair is not a unique one.  Every other case falls through to the
		 * pair / trust-center / distributed key selection below. */
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
			u8 pad = (aux.keyIdentifer == SS_SECUR_KEY_LOAD_KEY) ? 2U : 0U;
			if (ss_keyHash(&pad, key, keyTemp) == RET_OK) {
				key = keyTemp;
			}
		}
	}

	nonce.frameCnt = aux.frameCnt;
	aux.securityLevel = 5;
	nonce.secureCtrl = *(u8 *)&aux;
	ZB_IEEE_ADDR_COPY(nonce.srcAddr, g_zbMacPib.extAddress);

	{
		u8 srcMsgLen = (u8)(nldereq->nsduLen - apsHdrAuxLen);
		u8 len = ss_ccmEncryption(key, (u8 *)&nonce, apsHdrAuxLen, msgStartAddr, srcMsgLen,
					  payloadAddr);

		nldereq->nsduLen = (u8)(apsHdrAuxLen + len);
	}

	SS_CLR_SECURITY_LEVEL(msgStartAddr + apsHdrLen);
	return RET_OK;
}

_CODE_SS_ u8 ss_apsDecryptFrame(void *arg)
{
	nlde_data_ind_t *ind = (nlde_data_ind_t *)arg;
	ss_apsHdrParsed_t *apsHdr = (ss_apsHdrParsed_t *)((u8 *)arg + sizeof(nlde_data_ind_t));
	ss_apsEncryAuxCommonHdr_t aux;
	ss_securityCcmNonce_t nonce;
	ss_dev_pair_set_t keyPair;
	u8 keyTemp[SEC_KEY_LEN];
	u8 *auxStart;
	u8 *cursor;
	u8 *key = NULL;
	u8 *retryPayload = NULL;
	u8 *retryKey;
	u8 retryKeyIndex;
	u8 retryKeyCount;
	u8 publicKeyMask;
	u8 factoryNewTransport;
	u8 haveKeyPair;
	u16 addrMapIdx;
	addrExt_t neighborExtAddr;
	tl_zb_normal_neighbor_entry_t *neighbor;
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

	if (tl_zbExtAddrByShortAddr(apsHdr->srcShortAddr, nonce.srcAddr, &addrMapIdx) ==
		    TL_RETURN_INVALID &&
	    aps_ib.aps_authenticated) {
		return RET_ERROR;
	}

	if (SS_AUX_NONCE_INCLUDE(*auxStart) != 0U) {
		ZB_IEEE_ADDR_COPY(nonce.srcAddr, cursor);
		cursor += EXT_ADDR_LEN;
	}

	haveKeyPair = (ss_devKeyPairFind(nonce.srcAddr, &keyPair) == NV_SUCC);
	if (haveKeyPair != 0U) {
		key = keyPair.linkKey;
	} else {
		if (ss_ib.preConfiguredKeyType == SS_PRECONFIGURED_UNIQUELLINKKEY) {
			return RET_ERROR;
		}
		key = ss_ib.tcLinkKey;
	}

	if (aux.keyIdentifer == SS_SECUR_KEY_TRANSPORT_KEY) {
		neighbor = tl_zbNeighborTableSearchFromShortAddr(apsHdr->srcShortAddr,
								 neighborExtAddr, &addrMapIdx);
		if (neighbor != NULL && neighbor->deviceType != NWK_DEVICE_TYPE_COORDINATOR &&
		    ZB_IEEE_ADDR_CMP(neighborExtAddr, nonce.srcAddr)) {
			key = ss_ib.tcLinkKey;
		}
	}

	SS_SET_SECURITY_LEVEL(auxStart, 5);

	if (haveKeyPair != 0U && keyPair.apsLinkKeyType == SS_UNIQUE_LINK_KEY) {
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

	factoryNewTransport =
		(u8)((zb_isDeviceFactoryNew() != FALSE) && (g_zbNwkCtx.is_factory_new != 0U) &&
		     (aux.keyIdentifer == SS_SECUR_KEY_TRANSPORT_KEY));
	if (factoryNewTransport != 0U) {
		u8 payloadLen = (u8)((ind->nsdu + ind->nsduLen) - cursor);

		retryPayload = ev_buf_allocate(payloadLen);
		if (retryPayload != NULL) {
			memcpy(retryPayload, cursor, payloadLen);
		}
	}

	ret = ss_ccmDecryption(key, (u8 *)&nonce, (u8)(cursor - ind->nsdu), ind->nsdu,
			       (u8)((ind->nsdu + ind->nsduLen) - cursor), cursor);

	if (factoryNewTransport != 0U && zb_isDeviceFactoryNew() != FALSE && ret != RET_OK &&
	    retryPayload != NULL && haveKeyPair == 0U) {
		u8 payloadLen = (u8)((ind->nsdu + ind->nsduLen) - cursor);

		publicKeyMask = (u8)(g_zbDefaultLinkKeyEn &
				     (LINKKEY_MASTER_KEY_EN | LINKKEY_CERTIFICATION_KEY_EN));
		retryKeyCount = 3;
		for (retryKeyIndex = 0U; retryKeyIndex < retryKeyCount; retryKeyIndex++) {
			if (retryKeyIndex == 0U) {
				if ((publicKeyMask & LINKKEY_MASTER_KEY_EN) != 0U) {
					retryKey = (u8 *)linkKeyDistributedMaster;
				} else if ((publicKeyMask & LINKKEY_CERTIFICATION_KEY_EN) != 0U) {
					retryKey = (u8 *)linkKeyDistributedCertification;
				} else {
					retryKey = (u8 *)tcLinkKeyCentralDefault;
				}
			} else if (retryKeyIndex == 1U) {
				if ((publicKeyMask & LINKKEY_CERTIFICATION_KEY_EN) != 0U) {
					retryKey = (u8 *)linkKeyDistributedCertification;
				} else if ((publicKeyMask & LINKKEY_MASTER_KEY_EN) != 0U) {
					retryKey = (u8 *)linkKeyDistributedMaster;
				} else {
					retryKey = (u8 *)tcLinkKeyCentralDefault;
				}
			} else {
				retryKey = (u8 *)tcLinkKeyCentralDefault;
			}

			if (memcmp(retryKey, key, SEC_KEY_LEN) == 0) {
				continue;
			}

			memcpy(cursor, retryPayload, payloadLen);
			key = retryKey;
			ret = ss_ccmDecryption(key, (u8 *)&nonce, (u8)(cursor - ind->nsdu),
					       ind->nsdu, (u8)((ind->nsdu + ind->nsduLen) - cursor),
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
		ind->nsduLen = (u8)(ind->nsduLen - ((cursor - auxStart) + 4));
		ind->nsdu = ind->nsdu + (cursor - auxStart);
	}

	return ret;
}

void ss_pubLinkKeySelect(ss_pubLinkKeyOpt_e ks)
{
	g_zbDefaultLinkKeyEn = (u8)ks;
}
