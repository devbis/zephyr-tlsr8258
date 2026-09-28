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

#define SS_CLR_SECURITY_LEVEL(d) ((*(u8 *)(d)) &= SS_SEC_CTRL_LEVEL_CLEAR_MASK)
#define SS_SET_SECURITY_LEVEL(d, v)                                                                \
	((*(u8 *)(d)) = (u8)((*(u8 *)(d) & SS_SEC_CTRL_LEVEL_CLEAR_MASK) |                         \
			     ((v) & SS_SEC_CTRL_LEVEL_MASK)))
#define SS_AUX_NONCE_INCLUDE(d) ((d) & SS_SEC_CTRL_EXT_NONCE)

u8 g_zbDefaultLinkKeyEn = 1;

_CODE_SS_ u8 ss_apsEnAuxHdrFill(u8 *auxHdr, void *keyInfo, u8 extNonceOpt)
{
	u8 *p = auxHdr + SS_APS_AUX_HDR_LEN;
	u8 *key = (u8 *)keyInfo;

	COPY_U32TOBUFFER(auxHdr + SS_AUX_HDR_FRAME_CNT_OFFSET, ss_ib.outgoingFrameCounter);
	ss_ib.outgoingFrameCounter++;

	auxHdr[0] = (u8)((auxHdr[0] & (u8)~SS_SEC_CTRL_LEVEL_MASK) | SS_SECURITY_LEVEL_ENC_MIC_32);

	if (key != NULL) {
		auxHdr[0] |= SS_SEC_CTRL_EXT_NONCE;
		ZB_IEEE_ADDR_COPY(auxHdr + SS_AUX_HDR_SRC_ADDR_OFFSET, g_zbMacPib.extAddress);
		p = auxHdr + SS_APS_AUX_HDR_EXT_LEN;

		if (key[0] == APS_CMD_TRANSPORT_KEY_ID) {
			/*
			 * Transport Key: the key-transport key for the network key,
			 * the key-load key for a link key. Every other command
			 * uses the data key.
			 */
			if (key[1] == SS_STANDARD_NETWORK_KEY) {
				auxHdr[0] = (u8)((auxHdr[0] & (u8)~SS_SEC_CTRL_KEY_ID_MASK) |
						 SS_SEC_CTRL_KEY_ID_KEY_TRANSPORT);
			} else {
				auxHdr[0] |= SS_SEC_CTRL_KEY_ID_KEY_LOAD;
			}
		} else {
			auxHdr[0] &= (u8)~SS_SEC_CTRL_KEY_ID_MASK;
		}
	} else if ((extNonceOpt & SS_SEC_CTRL_EXT_NONCE) != 0U) {
		auxHdr[0] |= SS_SEC_CTRL_EXT_NONCE;
		ZB_IEEE_ADDR_COPY(auxHdr + SS_AUX_HDR_SRC_ADDR_OFFSET, g_zbMacPib.extAddress);
		p = auxHdr + SS_APS_AUX_HDR_EXT_LEN;
		auxHdr[0] &= (u8)~SS_SEC_CTRL_KEY_ID_MASK;
	} else {
		auxHdr[0] &= (u8)~SS_SEC_CTRL_KEY_ID_MASK;
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
	msgStartAddr[0] |= APS_FRAME_CTRL_SECURITY;
	memcpy(&aux, msgStartAddr + apsHdrLen, (u16)(apsHdrAuxLen - apsHdrLen));

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
			u8 hashInput = (aux.keyIdentifer == SS_SECUR_KEY_LOAD_KEY)
					       ? SS_KEY_HASH_INPUT_KEY_LOAD
					       : SS_KEY_HASH_INPUT_KEY_TRANSPORT;
			if (ss_keyHash(&hashInput, key, keyTemp) == RET_OK) {
				key = keyTemp;
			}
		}
	}

	nonce.frameCnt = aux.frameCnt;
	aux.securityLevel = SS_SECURITY_LEVEL_ENC_MIC_32;
	nonce.secureCtrl = *(u8 *)&aux;
	ZB_IEEE_ADDR_COPY(nonce.srcAddr, g_zbMacPib.extAddress);

	{
		u8 srcMsgLen = (u8)(nldereq->nsduLen - apsHdrAuxLen);
		u8 len = ss_ccmEncryption(key, (u8 *)&nonce, apsHdrAuxLen, msgStartAddr, srcMsgLen,
					  payloadAddr);

		if (len == 0U) {
			ss_apsSecureStatus(p, nldereq->dstAddr, APS_STATUS_SECURITY_FAIL);
			return RET_ERROR;
		}

		nldereq->nsduLen = (u8)(apsHdrAuxLen + len);
	}

	SS_CLR_SECURITY_LEVEL(msgStartAddr + apsHdrLen);
	return RET_OK;
}

/* Decrypt the saved payload again with one of the public link keys. */
static u8 ss_apsRetryDecrypt(u8 *candidate, ss_securityCcmNonce_t *nonce, nlde_data_ind_t *ind,
			     u8 *cursor, const u8 *saved, u8 len)
{
	u8 keyTemp[SEC_KEY_LEN];
	u8 hashInput = 0U;

	(void)ss_keyHash(&hashInput, candidate, keyTemp);
	memcpy(cursor, saved, len);

	return ss_ccmDecryption(keyTemp, (u8 *)nonce, (u8)(cursor - ind->nsdu), ind->nsdu, len,
				cursor);
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
	u8 *rawKey;
	u8 distributedKey = 0U;
	u8 factoryNewTransport;
	ss_dev_pair_set_t *pair;
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

	if (aux.frameCnt == SS_FRAME_CNT_INVALID || aux.keyIdentifer == SS_SECUR_NWK_KEY) {
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
	/* A trust center that uses unique link keys accepts a secured frame only
	 * from a device that has such a key. */
	if (ss_ib.tcLinkKeyType == SS_UNIQUE_LINK_KEY && g_zbNwkCtx.is_tc != 0U &&
	    (pair == NULL || pair->apsLinkKeyType != SS_UNIQUE_LINK_KEY)) {
		g_sysDiags.apsUnauthorizedKey++;
		return RET_ERROR;
	}

	key = ss_ib.tcLinkKey;

	/* A Transport Key from a neighbor that is not the coordinator is
	 * secured with the distributed-security link key. */
	if (aux.keyIdentifer == SS_SECUR_KEY_TRANSPORT_KEY) {
		neighbor = tl_zbNeighborTableSearchFromShortAddr(apsHdr->srcShortAddr,
								 neighborExtAddr, &addrMapIdx);
		if (neighbor != NULL && neighbor->deviceType != NWK_DEVICE_TYPE_COORDINATOR &&
		    ZB_IEEE_ADDR_CMP(neighborExtAddr, nonce.srcAddr)) {
			key = ss_ib.distributeLinkKey;
			distributedKey = 1U;
		}
	}

	/* The link key of a known device wins over both. */
	if (pair != NULL) {
		key = pair->linkKey;
	}

	SS_SET_SECURITY_LEVEL(auxStart, SS_SECURITY_LEVEL_ENC_MIC_32);

	if (pair != NULL && pair->apsLinkKeyType == SS_UNIQUE_LINK_KEY) {
		if (pair->incomingFrameCounter > aux.frameCnt) {
			g_sysDiags.apsFCFailure++;
			return RET_ERROR;
		}
		pair->incomingFrameCounter = aux.frameCnt + 1;
	}

	rawKey = key;

	if (aux.keyIdentifer != SS_SECUR_DATA_KEY) {
		u8 hashInput = (aux.keyIdentifer == SS_SECUR_KEY_LOAD_KEY)
				       ? SS_KEY_HASH_INPUT_KEY_LOAD
				       : SS_KEY_HASH_INPUT_KEY_TRANSPORT;

		if (ss_keyHash(&hashInput, key, keyTemp) == RET_OK) {
			key = keyTemp;
		}
	}

	nonce.frameCnt = aux.frameCnt;
	aux.securityLevel = SS_SECURITY_LEVEL_ENC_MIC_32;
	nonce.secureCtrl = *(u8 *)&aux;

	factoryNewTransport = (u8)((zb_isDeviceFactoryNew() != FALSE) &&
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

	/* A factory-new device that gets its first Transport Key tries the
	 * public link keys the application enabled when the key of the frame
	 * does not decrypt it, and remembers the key that worked, unless the
	 * sender is a device with a key pair of its own. */
	if (factoryNewTransport != 0U && zb_isDeviceFactoryNew() != FALSE) {
		u8 *winner = rawKey;

		if (ret != RET_OK && retryPayload != NULL && g_zbDefaultLinkKeyEn != 0U) {
			u8 mask = g_zbDefaultLinkKeyEn;
			u8 payloadLen = (u8)((ind->nsdu + ind->nsduLen) - cursor);

			if ((mask & LINKKEY_MASTER_KEY_EN) != 0U) {
				u8 *candidate = (distributedKey != 0U)
							? (u8 *)linkKeyDistributedMaster
							: (u8 *)tcLinkKeyCentralDefault;

				if (memcmp(rawKey, candidate, SEC_KEY_LEN) != 0) {
					ret = ss_apsRetryDecrypt(candidate, &nonce, ind, cursor,
								 retryPayload, payloadLen);
					if (ret == RET_OK) {
						winner = candidate;
					}
				}
			}

			if (ret != RET_OK && distributedKey != 0U &&
			    (mask & LINKKEY_CERTIFICATION_KEY_EN) != 0U) {
				ret = ss_apsRetryDecrypt((u8 *)linkKeyDistributedCertification,
							 &nonce, ind, cursor, retryPayload,
							 payloadLen);
				if (ret == RET_OK) {
					winner = (u8 *)linkKeyDistributedCertification;
				}
			}
		}

		if (ret == RET_OK && pair == NULL) {
			if (distributedKey != 0U) {
				ss_ib.distributeLinkKey = winner;
			} else {
				ss_ib.tcLinkKey =
					(memcmp(winner, tcLinkKeyCentralDefault, SEC_KEY_LEN) == 0)
						? (u8 *)tcLinkKeyCentralDefault
						: winner;
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
		ind->nsduLen = (u8)(ind->nsduLen - ((cursor - auxStart) + ZB_CCM_M));
		ind->nsdu = ind->nsdu + (cursor - auxStart);
	}

	return ret;
}

void ss_pubLinkKeySelect(ss_pubLinkKeyOpt_e ks)
{
	g_zbDefaultLinkKeyEn = (u8)ks;
}
