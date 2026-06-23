/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Adapted from libzigbee/src/ss_tlCCM.c. Vendor file kept structurally
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
#include "zb_minimal_ccm.h"

enum {
    AES_BLOCK_SIZE_LOCAL = 16,
    AES_CCM_OPT_ENCRYPT = 0,
    AES_CCM_OPT_DECRYPT = 1,
    AES_CCM_L_VALUE = 1,
    AES_CCM_MIC_LEN = 4,
};

static void aes_block_xor(u8 *dst, const u8 *src)
{
    for (u8 i = 0; i < AES_BLOCK_SIZE_LOCAL; i++) {
        dst[i] ^= src[i];
    }
}

static void aes_ccm_ctr_blk_fill(u8 *ctr, const u8 *iv, u16 counter)
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
_CODE_SS_ void ss_tlChallengeGen(u8 *dst, u8 len)
{
    for (u8 i = 0; i < len; i++) {
        dst[i] = (u8)drv_u32Rand();
    }
}

_CODE_SS_ void tl_cryHashFunction(u8 *data, u8 len, u8 *result)
{
    u8 block[AES_BLOCK_SIZE_LOCAL];
    u8 pos = 0;
    u8 idx = 0;

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

    block[pos++] = (u8)(((u16)len << 3) >> 8);
    block[pos] = (u8)(((u16)len << 3) & 0xffU);

    drv_aes_encrypt(result, block, result);
    aes_block_xor(result, block);
}

_CODE_SS_ void ss_ttlMAC(u8 len, u8 *input, u8 *key, u8 *hashOut)
{
    u8 hasIn[2 * AES_BLOCK_SIZE_LOCAL];
    u8 tmpBuf[0x80];

    if (len > 0x70U) {
        ZB_EXCEPTION_POST(SYS_EXCEPTTION_COMMON_PARAM_ERROR);
        return;
    }

    for (u8 i = 0; i < AES_BLOCK_SIZE_LOCAL; i++) {
        hasIn[i] = key[i] ^ 0x5cU;
        tmpBuf[i] = key[i] ^ 0x36U;
    }

    for (u8 i = 0; i < len; i++) {
        tmpBuf[i + AES_BLOCK_SIZE_LOCAL] = input[i];
    }

    tl_cryHashFunction(tmpBuf, (u8)(AES_BLOCK_SIZE_LOCAL + len), hasIn + AES_BLOCK_SIZE_LOCAL);
    tl_cryHashFunction(hasIn, 2 * AES_BLOCK_SIZE_LOCAL, hashOut);
}

_CODE_SS_ u8 ss_keyHash(u8 *padV, u8 *key, u8 *hashOut)
{
    ss_ttlMAC(1, padV, key, hashOut);
    return RET_OK;
}

void ss_mmoHash(u8 *data, u8 len, u8 *result)
{
    tl_cryHashFunction(data, len, result);
}

/*
 * @fn      aes_ccmAuthTran
 *
 * @brief   Calculate CBC-MAC part of CCM*.
 */
_CODE_SS_ u8 aes_ccmAuthTran(u8 M, u8 *key, u8 *iv, u8 *mStr, u16 mStrLen, u8 *aStr, u8 aStrLen, u8 *result)
{
    u8 b0[AES_BLOCK_SIZE_LOCAL];
    u8 x[AES_BLOCK_SIZE_LOCAL];
    u8 *authData;
    u16 authLen = (u16)aStrLen + 2U;
    u16 totalLen;
    u16 authOffset = 0;
    u16 msgOffset = 0;
    u16 authRemain = authLen;
    u16 msgRemain = mStrLen;

    b0[0] = (u8)(AES_CCM_L_VALUE |
                 (((M - 2U) >> 1) << 3) |
                 ((aStrLen != 0U) ? 0x40U : 0U));
    memcpy(b0 + 1, iv, 13);
    b0[14] = HI_UINT16(mStrLen);
    b0[15] = LO_UINT16(mStrLen);

    authData = ev_buf_allocate(authLen);
    if (authData == NULL) {
        return RET_ERROR;
    }

    authData[0] = 0;
    authData[1] = aStrLen;
    memcpy(authData + 2, aStr, aStrLen);

    memset(x, 0, sizeof(x));

    totalLen = authLen;
    if (totalLen & 0x0fU) {
        totalLen = (u16)((totalLen & (u16)~0x0fU) + 0x10U);
    }

    totalLen = (u16)(totalLen + mStrLen);
    if (mStrLen & 0x0fU) {
        totalLen = (u16)((totalLen & (u16)~0x0fU) + 0x10U);
    }

    for (u16 processed = 0; processed < (u16)(totalLen + AES_BLOCK_SIZE_LOCAL); processed += AES_BLOCK_SIZE_LOCAL) {
        aes_block_xor(x, b0);
        drv_aes_encrypt(key, x, x);

        if (authRemain > AES_BLOCK_SIZE_LOCAL) {
            memcpy(b0, authData + authOffset, AES_BLOCK_SIZE_LOCAL);
            authOffset = (u16)(authOffset + AES_BLOCK_SIZE_LOCAL);
            authRemain = (u16)(authRemain - AES_BLOCK_SIZE_LOCAL);
        } else if (authRemain != 0U) {
            memcpy(b0, authData + authOffset, authRemain);
            memset(b0 + authRemain, 0, AES_BLOCK_SIZE_LOCAL - authRemain);
            authRemain = 0;
        } else if (msgRemain >= AES_BLOCK_SIZE_LOCAL) {
            memcpy(b0, mStr + msgOffset, AES_BLOCK_SIZE_LOCAL);
            msgOffset = (u16)(msgOffset + AES_BLOCK_SIZE_LOCAL);
            msgRemain = (u16)(msgRemain - AES_BLOCK_SIZE_LOCAL);
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

_CODE_SS_ u8 aes_ccmBaseTran(u8 M, u8 *key, u8 *iv, u8 *mStr, u16 mStrLen, u8 *aStr, u8 aStrLen, u8 *mic, u8 opt)
{
    u8 ctr[AES_BLOCK_SIZE_LOCAL];
    u8 stream[AES_BLOCK_SIZE_LOCAL];
    u16 counter = 0;
    u16 remain = mStrLen;
    u16 offset = 0;

    (void)aStr;
    (void)aStrLen;
    (void)opt;

    aes_ccm_ctr_blk_fill(ctr, iv, counter);
    drv_aes_encrypt(key, ctr, stream);

    for (u8 i = 0; i < M; i++) {
        mic[i] ^= stream[i];
    }

    counter = 1;
    while (remain != 0U) {
        u8 chunk = (remain >= AES_BLOCK_SIZE_LOCAL) ? AES_BLOCK_SIZE_LOCAL : (u8)remain;

        aes_ccm_ctr_blk_fill(ctr, iv, counter);
        drv_aes_encrypt(key, ctr, stream);

        for (u8 i = 0; i < chunk; i++) {
            mStr[offset + i] ^= stream[i];
        }

        offset = (u16)(offset + chunk);
        remain = (u16)(remain - chunk);
        counter++;
    }

    return RET_OK;
}

_CODE_SS_ u8 aes_ccmEncTran(u8 M, u8 *key, u8 *iv, u8 *mStr, u16 mStrLen, u8 *aStr, u8 aStrLen, u8 *result)
{
    return aes_ccmBaseTran(M, key, iv, mStr, mStrLen, aStr, aStrLen, result, AES_CCM_OPT_ENCRYPT);
}

/*
 * The CBC-MAC + CTR math here is byte-for-byte identical to the vendor
 * aes_ccm* routines below, but those allocate the CBC-MAC auth scratch via
 * ev_buf_allocate(). On the Zephyr port the group-1 slab has only 4 blocks
 * (BUFFER_NUM_IN_GROUP1), so under RX pressure ev_buf_allocate() returns
 * NULL and aes_ccmAuthTran() bails with RET_ERROR — indistinguishable from
 * a genuine MIC mismatch and the exact failure that blocked every inbound
 * Transport-Key during join. ss_ccmEncryption/ss_ccmDecryption now delegate
 * to the stack-only zb_minimal_ccm_* implementation (the same code path the
 * ED build runs and is proven on this hardware). The aes_ccm* functions are
 * retained for ABI/linkage but are no longer on the hot path.
 */
_CODE_SS_ u8 ss_ccmEncryption(u8 *key, u8 *nonce, u8 nwkHdrLen, u8 *nwkHdr, u8 srcMsgLen, u8 *srcMsg)
{
    u8 mic[AES_CCM_MIC_LEN];

    (void)zb_minimal_ccm_encrypt_auth(key, nonce, AES_CCM_MIC_LEN,
                                      nwkHdr, nwkHdrLen, srcMsg, srcMsgLen, mic);
    memcpy(srcMsg + srcMsgLen, mic, AES_CCM_MIC_LEN);
    return (u8)(srcMsgLen + AES_CCM_MIC_LEN);
}

_CODE_SS_ u8 aes_ccmDecTran(u8 micLen, u8 *key, u8 *iv, u8 *mStr, u16 mStrLen, u8 *aStr, u8 aStrLen, u8 *mic)
{
    return aes_ccmBaseTran(micLen, key, iv, mStr, mStrLen, aStr, aStrLen, mic, AES_CCM_OPT_DECRYPT);
}

_CODE_SS_ u8 aes_ccmDecAuthTran(u8 micLen, u8 *key, u8 *iv, u8 *mStr, u16 mStrLen, u8 *aStr, u8 aStrLen, u8 *mic)
{
    u8 tmpMic[AES_BLOCK_SIZE_LOCAL];

    if (aes_ccmAuthTran(micLen, key, iv, mStr, mStrLen, aStr, aStrLen, tmpMic) != RET_OK) {
        return RET_ERROR;
    }

    for (u8 i = 0; i < micLen; i++) {
        if (mic[i] != tmpMic[i]) {
            return RET_ERROR;
        }
    }

    return RET_OK;
}

_CODE_SS_ u8 ss_ccmDecryption(u8 *key, u8 *nonce, u8 nwkHdrLen, u8 *nwkHdr, u8 srcMsgLen, u8 *srcMsg)
{
    u8 cipherLen = (u8)(srcMsgLen - AES_CCM_MIC_LEN);
    u8 *mic = srcMsg + cipherLen;
    bool ok;

    ok = zb_minimal_ccm_decrypt_auth(key, nonce, AES_CCM_MIC_LEN, srcMsg,
                                     cipherLen, nwkHdr, nwkHdrLen, mic);
    return ok ? RET_OK : RET_ERROR;
}
