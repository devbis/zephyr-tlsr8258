#include "zb_local.h"
#include "dGP_stub.h"
#include "gp_sec.h"
#include "common/static_assert.h"
#include "security_service.h"

#if defined(ZB_ROUTER_ROLE)
STATIC_ASSERT(sizeof(gp_nwkHdrFrameCtrl_t) == 1);
STATIC_ASSERT(sizeof(gp_extNwkFrameCtrl_t) == 1);

u8 gpCcmStar(u8 appId, gpdId_t gpdId,
             u8 gpdfSecKey, u8 gpdfSecLevel,
             u8 endpoint, u32 secFrameCounter,
             u8 gpdAsduLen, u8 *gpdAsdu,
             bool autoComm, bool rxAfterTx,
             u32 mic, u8 *pKey)
{
    u8 nonce[13];
    u8 *aStr;
    u8 *gpdAsduCpy;
    u8 aStrLen;
    u8 *pHdr;
    gp_nwkHdrFrameCtrl_t nwkFrameCtrl;
    gp_extNwkFrameCtrl_t extNwkFrameCtrl;

    aStr = NULL;
    gpdAsduCpy = NULL;
    *((u8 *)&nwkFrameCtrl) = 0;
    *((u8 *)&extNwkFrameCtrl) = 0;

    if (gpdfSecLevel == GP_SEC_LEVEL_NO_SECURITY) {
        return SUCCESS;
    }

    nonce[12] = 5;
    if (appId == GP_APP_ID_SRC_ID) {
        nonce[4] = (u8)gpdId.srcId;
        nonce[5] = (u8)(gpdId.srcId >> 8);
        nonce[6] = (u8)(gpdId.srcId >> 16);
        nonce[7] = (u8)(gpdId.srcId >> 24);
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

    nonce[8] = (u8)secFrameCounter;
    nonce[9] = (u8)(secFrameCounter >> 8);
    nonce[10] = (u8)(secFrameCounter >> 16);
    nonce[11] = (u8)(secFrameCounter >> 24);

    nwkFrameCtrl = gpNwkHdrFrameCtrlBuild(GP_NWK_FRAME_TYPE_DATA, autoComm, appId,
                                          gpdfSecLevel, gpdfSecKey,
                                          rxAfterTx, FALSE, &extNwkFrameCtrl);

    if (gpdfSecLevel == GP_SEC_LEVEL_4BFC_4BMIC_ENCRYPTION) {
        aStr = ev_buf_allocate(aStrLen);
    } else if (gpdfSecLevel == GP_SEC_LEVEL_4BFC_4BMIC) {
        aStrLen = (u8)(aStrLen + gpdAsduLen);
        aStr = ev_buf_allocate(aStrLen);
    } else {
        return FAILURE;
    }

    if (aStr == NULL) {
        return FAILURE;
    }

    aStr[0] = *((u8 *)&nwkFrameCtrl);
    aStr[1] = *((u8 *)&extNwkFrameCtrl);
    pHdr = &aStr[2];

    if (appId == GP_APP_ID_SRC_ID) {
        pHdr[0] = (u8)gpdId.srcId;
        pHdr[1] = (u8)(gpdId.srcId >> 8);
        pHdr[2] = (u8)(gpdId.srcId >> 16);
        pHdr[3] = (u8)(gpdId.srcId >> 24);
        pHdr += 4;
    } else {
        *pHdr++ = endpoint;
    }

    pHdr[0] = (u8)secFrameCounter;
    pHdr[1] = (u8)(secFrameCounter >> 8);
    pHdr[2] = (u8)(secFrameCounter >> 16);
    pHdr[3] = (u8)(secFrameCounter >> 24);

    if (gpdfSecLevel == GP_SEC_LEVEL_4BFC_4BMIC) {
        memcpy(pHdr + 4, gpdAsdu, gpdAsduLen);
        aes_ccmDecTran(4, pKey, nonce, NULL, 0, aStr, aStrLen, (u8 *)&mic);
        gpdfSecLevel = aes_ccmDecAuthTran(4, pKey, nonce, NULL, 0, aStr, aStrLen, (u8 *)&mic);
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

    aes_ccmDecTran(4, pKey, nonce, gpdAsdu, gpdAsduLen, aStr, aStrLen, (u8 *)&mic);
    if (aes_ccmDecAuthTran(4, pKey, nonce, gpdAsdu, gpdAsduLen, aStr, aStrLen, (u8 *)&mic) != RET_OK) {
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

u8 gpdKeyWithTCLK(u8 appId, gpdId_t gpdId,
                  u8 dataLen, u8 *pData,
                  u32 frameCounter, u8 *mic,
                  u8 *pKey, bool enOrDecrypt)
{
    u8 nonce[13] = {0};
    gpdId_t nonceId;
    u32 nonceCounter;
    u32 aStr;

    nonceCounter = 0;
    aStr = 0;

    if (appId == GP_APP_ID_SRC_ID) {
        aStr = gpdId.srcId;
        nonceCounter = frameCounter;
        if (!enOrDecrypt) {
            nonce[12] = 5;
            nonce[4] = (u8)gpdId.srcId;
            nonce[5] = (u8)(gpdId.srcId >> 8);
            nonce[6] = (u8)(gpdId.srcId >> 16);
            nonce[7] = (u8)(gpdId.srcId >> 24);
            nonce[0] = nonce[4];
            nonce[1] = nonce[5];
            nonce[2] = nonce[6];
            nonce[3] = nonce[7];
            nonceCounter = gpdId.srcId;
            *((u32 *)&nonce[8]) = nonceCounter;

            aes_ccmDecTran(4, pKey, nonce, pData, dataLen, (u8 *)&aStr, 4, mic);
            return (aes_ccmDecAuthTran(4, pKey, nonce, pData, dataLen, (u8 *)&aStr, 4, mic) == RET_OK) ? SUCCESS : FAILURE;
        }
    } else {
        if (appId != GP_APP_ID_GPD) {
            return FAILURE;
        }

        memcpy((u8 *)&aStr, &gpdId, sizeof(u32));
        nonceCounter = frameCounter;
        if (!enOrDecrypt) {
            nonceCounter = gpdId.srcId;
        }
        nonceId = gpdId;
    }

    nonce[12] = 5;

    if (appId == GP_APP_ID_SRC_ID) {
        nonce[4] = (u8)gpdId.srcId;
        nonce[5] = (u8)(gpdId.srcId >> 8);
        nonce[6] = (u8)(gpdId.srcId >> 16);
        nonce[7] = (u8)(gpdId.srcId >> 24);
    } else if (appId == GP_APP_ID_GPD) {
        memcpy(nonce, &nonceId, sizeof(gpdId_t));
        if (!enOrDecrypt) {
            goto decrypt;
        }
        nonce[12] = 0xc5;
    } else {
        return FAILURE;
    }

    *((u32 *)&nonce[8]) = nonceCounter;

    if (enOrDecrypt) {
        if (aes_ccmAuthTran(4, pKey, nonce, pData, dataLen, (u8 *)&aStr, 4, mic) != RET_OK) {
            return FAILURE;
        }

        aes_ccmEncTran(4, pKey, nonce, pData, dataLen, (u8 *)&aStr, 4, mic);
        return SUCCESS;
    }

decrypt:
    *((u32 *)&nonce[8]) = nonceCounter;
    aes_ccmDecTran(4, pKey, nonce, pData, dataLen, (u8 *)&aStr, 4, mic);
    return (aes_ccmDecAuthTran(4, pKey, nonce, pData, dataLen, (u8 *)&aStr, 4, mic) == RET_OK) ? SUCCESS : FAILURE;
}
#endif
