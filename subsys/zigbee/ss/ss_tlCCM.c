/*
 * Copyright 2026 libzigbee
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "zb_common.h"
#include "ss_tlCCM.h"

enum {
	AES_BLOCK_SIZE_LOCAL = 16,
	AES_CCM_OPT_ENCRYPT = 0,
	AES_CCM_OPT_DECRYPT = 1,
	AES_CCM_L_VALUE = 1,
	AES_CCM_MIC_LEN = 4,
};

static void aes_block_xor(uint8_t *dst, const uint8_t *src)
{
	for (uint8_t i = 0; i < AES_BLOCK_SIZE_LOCAL; i++) {
		dst[i] ^= src[i];
	}
}

static inline void aes_ccm_ctr_blk_fill(uint8_t *ctr, const uint8_t *iv, uint16_t counter)
{
	ctr[0] = AES_CCM_L_VALUE;
	memcpy(ctr + 1, iv, 13);
	ctr[14] = HI_UINT16(counter);
	ctr[15] = LO_UINT16(counter);
}

/*
 * Input: an integer challenge len which is the challenge length in bytes.
 * Output: a statistically unique and unpredictable challenge string.
 */
_CODE_SS_ void ss_tlChallengeGen(uint8_t *dst, uint8_t len)
{
	for (uint8_t i = 0; i < len; i++) {
		dst[i] = (uint8_t)drv_u32Rand();
	}
}

_CODE_SS_ void zb_cryHashFunction(uint8_t *data, uint8_t len, uint8_t *result)
{
	uint8_t block[AES_BLOCK_SIZE_LOCAL];
	uint8_t pos = 0;
	uint8_t idx = 0;

	memset(result, 0, AES_BLOCK_SIZE_LOCAL);

	while (idx < len) {
		block[pos++] = data[idx++];
		if (pos == AES_BLOCK_SIZE_LOCAL) {
			drv_aes_encrypt(result, block, result);
			aes_block_xor(result, block);
			pos = 0;
		}
	}

	block[pos++] = 0x80;

	while (pos != (AES_BLOCK_SIZE_LOCAL - 2U)) {
		if (pos >= AES_BLOCK_SIZE_LOCAL) {
			drv_aes_encrypt(result, block, result);
			aes_block_xor(result, block);
			pos = 0;
		}
		block[pos++] = 0;
	}

	block[pos++] = (uint8_t)(((uint16_t)len << 3) >> 8);
	block[pos] = (uint8_t)(((uint16_t)len << 3) & 0xffU);

	drv_aes_encrypt(result, block, result);
	aes_block_xor(result, block);
}

_CODE_SS_ void ss_ttlMAC(uint8_t len, uint8_t *input, uint8_t *key, uint8_t *hashOut)
{
	uint8_t hasIn[2 * AES_BLOCK_SIZE_LOCAL];
	uint8_t *tmpBuf;

	if (len > 0x70U) {
		ZB_EXCEPTION_POST(SYS_EXCEPTTION_COMMON_PARAM_ERROR);
		return;
	}

	/* The scratch block comes from the event buffer pool, not the stack */
	tmpBuf = ev_buf_allocate(0x80);
	if (tmpBuf == NULL) {
		ZB_EXCEPTION_POST(SYS_EXCEPTTION_COMMON_BUFFER_OVERFLOWN);
		return;
	}

	for (uint8_t i = 0; i < AES_BLOCK_SIZE_LOCAL; i++) {
		hasIn[i] = key[i] ^ 0x5cU;
		tmpBuf[i] = key[i] ^ 0x36U;
	}

	for (uint8_t i = 0; i < len; i++) {
		tmpBuf[i + AES_BLOCK_SIZE_LOCAL] = input[i];
	}

	zb_cryHashFunction(tmpBuf, (uint8_t)(AES_BLOCK_SIZE_LOCAL + len), hasIn + AES_BLOCK_SIZE_LOCAL);
	zb_cryHashFunction(hasIn, 2 * AES_BLOCK_SIZE_LOCAL, hashOut);
	ev_buf_free(tmpBuf);
}

_CODE_SS_ uint8_t ss_keyHash(uint8_t *hashInput, uint8_t *key, uint8_t *hashOut)
{
	ss_ttlMAC(1, hashInput, key, hashOut);
	return RET_OK;
}

void ss_mmoHash(uint8_t *data, uint8_t len, uint8_t *result)
{
	zb_cryHashFunction(data, len, result);
}

/*
 * @fn      aes_ccmAuthTran
 *
 * @brief   Calculate CBC-MAC part of CCM*.
 */
_CODE_SS_ uint8_t aes_ccmAuthTran(uint8_t M, uint8_t *key, uint8_t *iv, uint8_t *mStr, uint16_t mStrLen, uint8_t *aStr, uint8_t aStrLen,
			     uint8_t *result)
{
	uint8_t b0[AES_BLOCK_SIZE_LOCAL];
	uint8_t x[AES_BLOCK_SIZE_LOCAL];
	uint8_t *authData;
	uint16_t authLen = (uint16_t)aStrLen + 2U;
	uint16_t totalLen;
	uint16_t authOffset = 0;
	uint16_t msgOffset = 0;
	uint16_t authRemain = authLen;
	uint16_t msgRemain = mStrLen;

	b0[0] = (uint8_t)(AES_CCM_L_VALUE | (((M - 2U) >> 1) << 3) | ((aStrLen != 0U) ? 0x40U : 0U));
	memcpy(b0 + 1, iv, 13);
	b0[14] = HI_UINT16(mStrLen);
	b0[15] = LO_UINT16(mStrLen);

	authData = ev_buf_allocate(authLen);
	if (authData == NULL) {
		return 0xffU;
	}

	authData[0] = 0;
	authData[1] = aStrLen;
	memcpy(authData + 2, aStr, aStrLen);

	memset(x, 0, sizeof(x));

	totalLen = authLen;
	if (totalLen & 0x0fU) {
		totalLen = (uint16_t)((totalLen & (uint16_t)~0x0fU) + 0x10U);
	}

	totalLen = (uint16_t)(totalLen + mStrLen);
	if (mStrLen & 0x0fU) {
		totalLen = (uint16_t)((totalLen & (uint16_t)~0x0fU) + 0x10U);
	}

	for (uint16_t processed = 0; processed < (uint16_t)(totalLen + AES_BLOCK_SIZE_LOCAL);
	     processed += AES_BLOCK_SIZE_LOCAL) {
		aes_block_xor(x, b0);
		drv_aes_encrypt(key, x, x);

		if (authRemain > AES_BLOCK_SIZE_LOCAL) {
			memcpy(b0, authData + authOffset, AES_BLOCK_SIZE_LOCAL);
			authOffset = (uint16_t)(authOffset + AES_BLOCK_SIZE_LOCAL);
			authRemain = (uint16_t)(authRemain - AES_BLOCK_SIZE_LOCAL);
		} else if (authRemain != 0U) {
			memcpy(b0, authData + authOffset, authRemain);
			memset(b0 + authRemain, 0, AES_BLOCK_SIZE_LOCAL - authRemain);
			authRemain = 0;
		} else if (msgRemain >= AES_BLOCK_SIZE_LOCAL) {
			memcpy(b0, mStr + msgOffset, AES_BLOCK_SIZE_LOCAL);
			msgOffset = (uint16_t)(msgOffset + AES_BLOCK_SIZE_LOCAL);
			msgRemain = (uint16_t)(msgRemain - AES_BLOCK_SIZE_LOCAL);
		} else {
			memcpy(b0, mStr + msgOffset, msgRemain);
			memset(b0 + msgRemain, 0, AES_BLOCK_SIZE_LOCAL - msgRemain);
			msgRemain = 0;
		}
	}

	memcpy(result, x, M);
	ev_buf_free(authData);
	return RET_OK;
}

_CODE_SS_ uint8_t aes_ccmBaseTran(uint8_t M, uint8_t *key, uint8_t *iv, uint8_t *mStr, uint16_t mStrLen, uint8_t *aStr, uint8_t aStrLen,
			     uint8_t *mic, uint8_t opt)
{
	uint8_t ctr[AES_BLOCK_SIZE_LOCAL];
	uint8_t stream[AES_BLOCK_SIZE_LOCAL];
	uint16_t counter = 0;
	uint16_t remain = mStrLen;
	uint16_t offset = 0;

	(void)aStr;
	(void)aStrLen;
	(void)opt;

	aes_ccm_ctr_blk_fill(ctr, iv, counter);
	drv_aes_encrypt(key, ctr, stream);

	for (uint8_t i = 0; i < M; i++) {
		mic[i] ^= stream[i];
	}

	counter = 1;
	while (remain != 0U) {
		uint8_t chunk = (remain >= AES_BLOCK_SIZE_LOCAL) ? AES_BLOCK_SIZE_LOCAL : (uint8_t)remain;

		aes_ccm_ctr_blk_fill(ctr, iv, counter);
		drv_aes_encrypt(key, ctr, stream);

		for (uint8_t i = 0; i < chunk; i++) {
			mStr[offset + i] ^= stream[i];
		}

		offset = (uint16_t)(offset + chunk);
		remain = (uint16_t)(remain - chunk);
		counter++;
	}

	return RET_OK;
}

_CODE_SS_ uint8_t aes_ccmEncTran(uint8_t M, uint8_t *key, uint8_t *iv, uint8_t *mStr, uint16_t mStrLen, uint8_t *aStr, uint8_t aStrLen,
			    uint8_t *result)
{
	return aes_ccmBaseTran(M, key, iv, mStr, mStrLen, aStr, aStrLen, result,
			       AES_CCM_OPT_ENCRYPT);
}

_CODE_SS_ uint8_t ss_ccmEncryption(uint8_t *key, uint8_t *nonce, uint8_t nwkHdrLen, uint8_t *nwkHdr, uint8_t srcMsgLen,
			      uint8_t *srcMsg)
{
	uint8_t mic[AES_CCM_MIC_LEN];

	if (aes_ccmAuthTran(AES_CCM_MIC_LEN, key, nonce, srcMsg, srcMsgLen, nwkHdr, nwkHdrLen,
			    mic) != RET_OK) {
		return 0;
	}

	aes_ccmEncTran(AES_CCM_MIC_LEN, key, nonce, srcMsg, srcMsgLen, nwkHdr, nwkHdrLen, mic);
	memcpy(srcMsg + srcMsgLen, mic, AES_CCM_MIC_LEN);
	return (uint8_t)(srcMsgLen + AES_CCM_MIC_LEN);
}

_CODE_SS_ uint8_t aes_ccmDecTran(uint8_t micLen, uint8_t *key, uint8_t *iv, uint8_t *mStr, uint16_t mStrLen, uint8_t *aStr, uint8_t aStrLen,
			    uint8_t *mic)
{
	return aes_ccmBaseTran(micLen, key, iv, mStr, mStrLen, aStr, aStrLen, mic,
			       AES_CCM_OPT_DECRYPT);
}

_CODE_SS_ uint8_t aes_ccmDecAuthTran(uint8_t micLen, uint8_t *key, uint8_t *iv, uint8_t *mStr, uint16_t mStrLen, uint8_t *aStr,
				uint8_t aStrLen, uint8_t *mic)
{
	uint8_t tmpMic[AES_BLOCK_SIZE_LOCAL];

	/* The vendor library returns 0xff, not RET_ERROR, from every failure path
	 * here. */
	if (aes_ccmAuthTran(micLen, key, iv, mStr, mStrLen, aStr, aStrLen, tmpMic) != RET_OK) {
		return 0xffU;
	}

	for (uint8_t i = 0; i < micLen; i++) {
		if (mic[i] != tmpMic[i]) {
			return 0xffU;
		}
	}

	return RET_OK;
}

_CODE_SS_ uint8_t ss_ccmDecryption(uint8_t *key, uint8_t *nonce, uint8_t nwkHdrLen, uint8_t *nwkHdr, uint8_t srcMsgLen,
			      uint8_t *srcMsg)
{
	uint8_t *mic = srcMsg + srcMsgLen - AES_CCM_MIC_LEN;

	srcMsgLen -= AES_CCM_MIC_LEN;
	aes_ccmDecTran(AES_CCM_MIC_LEN, key, nonce, srcMsg, srcMsgLen, nwkHdr, nwkHdrLen, mic);
	return aes_ccmDecAuthTran(AES_CCM_MIC_LEN, key, nonce, srcMsg, srcMsgLen, nwkHdr, nwkHdrLen,
				  mic);
}
