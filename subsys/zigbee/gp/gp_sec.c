/*
 * Copyright 2026 libzigbee
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "zb_common.h"
#include "gp_internal.h"
#include "dGP_stub.h"
#include "gp_sec.h"
#include "security_service.h"

#if defined(ZB_ROUTER_ROLE)
STATIC_ASSERT(sizeof(gp_nwkHdrFrameCtrl_t) == 1);
STATIC_ASSERT(sizeof(gp_extNwkFrameCtrl_t) == 1);

uint8_t gpCcmStar(uint8_t appId, gpdId_t gpdId, uint8_t gpdfSecKey, uint8_t gpdfSecLevel, uint8_t endpoint,
	     uint32_t secFrameCounter, uint8_t gpdAsduLen, uint8_t *gpdAsdu, bool autoComm, bool rxAfterTx,
	     uint32_t mic, uint8_t *pKey)
{
	uint8_t nonce[13];
	uint8_t *aStr;
	uint8_t *gpdAsduCpy;
	uint8_t aStrLen;
	uint8_t *pHdr;
	gp_nwkHdrFrameCtrl_t nwkFrameCtrl;
	gp_extNwkFrameCtrl_t extNwkFrameCtrl;

	aStr = NULL;
	gpdAsduCpy = NULL;
	*((uint8_t *)&nwkFrameCtrl) = 0;
	*((uint8_t *)&extNwkFrameCtrl) = 0;

	if (gpdfSecLevel == GP_SEC_LEVEL_NO_SECURITY) {
		return SUCCESS;
	}

	nonce[12] = 5;
	if (appId == GP_APP_ID_SRC_ID) {
		nonce[4] = (uint8_t)gpdId.srcId;
		nonce[5] = (uint8_t)(gpdId.srcId >> 8);
		nonce[6] = (uint8_t)(gpdId.srcId >> 16);
		nonce[7] = (uint8_t)(gpdId.srcId >> 24);
		nonce[0] = nonce[4];
		nonce[1] = nonce[5];
		nonce[2] = nonce[6];
		nonce[3] = nonce[7];
		aStrLen = 10;
	} else if (appId == GP_APP_ID_GPD) {
		memcpy(nonce, &gpdId, sizeof(gpdId_t));
		aStrLen = 7;
	} else {
		return FAILURE;
	}

	nonce[8] = (uint8_t)secFrameCounter;
	nonce[9] = (uint8_t)(secFrameCounter >> 8);
	nonce[10] = (uint8_t)(secFrameCounter >> 16);
	nonce[11] = (uint8_t)(secFrameCounter >> 24);

	nwkFrameCtrl = gpNwkHdrFrameCtrlBuild(GP_NWK_FRAME_TYPE_DATA, autoComm, appId, gpdfSecLevel,
					      gpdfSecKey, rxAfterTx, FALSE, &extNwkFrameCtrl);

	if (gpdfSecLevel == GP_SEC_LEVEL_4BFC_4BMIC_ENCRYPTION) {
		aStr = ev_buf_allocate(aStrLen);
	} else if (gpdfSecLevel == GP_SEC_LEVEL_4BFC_4BMIC) {
		aStrLen = (uint8_t)(aStrLen + gpdAsduLen);
		aStr = ev_buf_allocate(aStrLen);
	} else {
		return FAILURE;
	}

	if (aStr == NULL) {
		return FAILURE;
	}

	aStr[0] = *((uint8_t *)&nwkFrameCtrl);
	aStr[1] = *((uint8_t *)&extNwkFrameCtrl);
	pHdr = &aStr[2];

	if (appId == GP_APP_ID_SRC_ID) {
		pHdr[0] = (uint8_t)gpdId.srcId;
		pHdr[1] = (uint8_t)(gpdId.srcId >> 8);
		pHdr[2] = (uint8_t)(gpdId.srcId >> 16);
		pHdr[3] = (uint8_t)(gpdId.srcId >> 24);
		pHdr += 4;
	} else {
		*pHdr++ = endpoint;
	}

	pHdr[0] = (uint8_t)secFrameCounter;
	pHdr[1] = (uint8_t)(secFrameCounter >> 8);
	pHdr[2] = (uint8_t)(secFrameCounter >> 16);
	pHdr[3] = (uint8_t)(secFrameCounter >> 24);

	if (gpdfSecLevel == GP_SEC_LEVEL_4BFC_4BMIC) {
		memcpy(pHdr + 4, gpdAsdu, gpdAsduLen);
		aes_ccmDecTran(4, pKey, nonce, NULL, 0, aStr, aStrLen, (uint8_t *)&mic);
		gpdfSecLevel =
			aes_ccmDecAuthTran(4, pKey, nonce, NULL, 0, aStr, aStrLen, (uint8_t *)&mic);
		ev_buf_free(aStr);
		return (gpdfSecLevel == RET_OK) ? SUCCESS : FAILURE;
	}

	gpdAsduCpy = ev_buf_allocate(gpdAsduLen);
	if ((gpdAsduCpy == NULL) && (gpdAsduLen != 0U)) {
		ev_buf_free(aStr);
		return FAILURE;
	}

	if (gpdAsduCpy != NULL) {
		memcpy(gpdAsduCpy, gpdAsdu, gpdAsduLen);
	}

	aes_ccmDecTran(4, pKey, nonce, gpdAsdu, gpdAsduLen, aStr, aStrLen, (uint8_t *)&mic);
	if (aes_ccmDecAuthTran(4, pKey, nonce, gpdAsdu, gpdAsduLen, aStr, aStrLen, (uint8_t *)&mic) !=
	    RET_OK) {
		if ((gpdAsduCpy != NULL) && (gpdAsduLen != 0U)) {
			memcpy(gpdAsdu, gpdAsduCpy, gpdAsduLen);
		}
		gpdfSecKey = FAILURE;
	} else {
		gpdfSecKey = SUCCESS;
	}

	if (gpdAsduCpy != NULL) {
		ev_buf_free(gpdAsduCpy);
	}

	ev_buf_free(aStr);

	return gpdfSecKey;
}

uint8_t gpdKeyWithTCLK(uint8_t appId, gpdId_t gpdId, uint8_t dataLen, uint8_t *pData, uint32_t frameCounter, uint8_t *mic,
		  uint8_t *pKey, bool enOrDecrypt)
{
	uint8_t nonce[13] = {0};
	gpdId_t nonceId;
	uint32_t nonceCounter;
	uint32_t aStr;

	nonceCounter = 0;
	aStr = 0;

	if (appId == GP_APP_ID_SRC_ID) {
		aStr = gpdId.srcId;
		nonceCounter = frameCounter;
		if (!enOrDecrypt) {
			nonce[12] = 5;
			nonce[4] = (uint8_t)gpdId.srcId;
			nonce[5] = (uint8_t)(gpdId.srcId >> 8);
			nonce[6] = (uint8_t)(gpdId.srcId >> 16);
			nonce[7] = (uint8_t)(gpdId.srcId >> 24);
			nonce[0] = nonce[4];
			nonce[1] = nonce[5];
			nonce[2] = nonce[6];
			nonce[3] = nonce[7];
			nonceCounter = gpdId.srcId;
			COPY_U32TOBUFFER(nonce + 8, nonceCounter);

			aes_ccmDecTran(4, pKey, nonce, pData, dataLen, (uint8_t *)&aStr, 4, mic);
			return (aes_ccmDecAuthTran(4, pKey, nonce, pData, dataLen, (uint8_t *)&aStr, 4,
						   mic) == RET_OK)
				       ? SUCCESS
				       : FAILURE;
		}
	} else {
		if (appId != GP_APP_ID_GPD) {
			return FAILURE;
		}

		memcpy((uint8_t *)&aStr, &gpdId, sizeof(uint32_t));
		nonceCounter = frameCounter;
		if (!enOrDecrypt) {
			nonceCounter = gpdId.srcId;
		}
		nonceId = gpdId;
	}

	nonce[12] = 5;

	if (appId == GP_APP_ID_SRC_ID) {
		nonce[4] = (uint8_t)gpdId.srcId;
		nonce[5] = (uint8_t)(gpdId.srcId >> 8);
		nonce[6] = (uint8_t)(gpdId.srcId >> 16);
		nonce[7] = (uint8_t)(gpdId.srcId >> 24);
	} else if (appId == GP_APP_ID_GPD) {
		memcpy(nonce, &nonceId, sizeof(gpdId_t));
		if (!enOrDecrypt) {
			goto decrypt;
		}
		nonce[12] = 0xc5;
	} else {
		return FAILURE;
	}

	COPY_U32TOBUFFER(nonce + 8, nonceCounter);

	if (enOrDecrypt) {
		if (aes_ccmAuthTran(4, pKey, nonce, pData, dataLen, (uint8_t *)&aStr, 4, mic) !=
		    RET_OK) {
			return FAILURE;
		}

		aes_ccmEncTran(4, pKey, nonce, pData, dataLen, (uint8_t *)&aStr, 4, mic);
		return SUCCESS;
	}

decrypt:
	COPY_U32TOBUFFER(nonce + 8, nonceCounter);
	aes_ccmDecTran(4, pKey, nonce, pData, dataLen, (uint8_t *)&aStr, 4, mic);
	return (aes_ccmDecAuthTran(4, pKey, nonce, pData, dataLen, (uint8_t *)&aStr, 4, mic) == RET_OK)
		       ? SUCCESS
		       : FAILURE;
}
#endif
