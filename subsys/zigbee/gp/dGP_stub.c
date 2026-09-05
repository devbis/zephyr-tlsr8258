#include "zb_local.h"
#include "dGP_stub.h"
#include "cGP_stub.h"
#include "gp_base.h"
#include "gp_sec.h"
#include "security_service.h"
#include "common/static_assert.h"
#include <stdint.h>

#if defined(ZB_ROUTER_ROLE)
STATIC_ASSERT(sizeof(gp_nwkHdrFrameCtrl_t) == 1);
STATIC_ASSERT(sizeof(gp_extNwkFrameCtrl_t) == 1);

extern gp_data_req_pending_t gpTxQueue;

_attribute_no_inline_ void gpDataIndSend(gp_data_ind_entry_t *pEntry);
_attribute_no_inline_ void gpDataFrameProcess(void *arg, gp_nwkHdr_t *pNwkHdr,
                                              u8 appId, u8 *pGpdAsdu,
                                              u8 gpdAsduLen, u32 mic);
gp_data_ind_entry_t *gpDataIndSecReqEntryAdd(gp_data_ind_t *pGpDataInd);

_attribute_no_inline_ gp_nwkHdrFrameCtrl_t gpNwkHdrFrameCtrlBuild(u8 frameType, bool autoComm, u8 appId,
                                                                  u8 secLevel, u8 secKey,
                                                                  bool rxAfterTx, bool direction,
                                                                  gp_extNwkFrameCtrl_t *pExtNwkFrameCtrl)
{
    gp_nwkHdrFrameCtrl_t nwkFrameCtrl;
    *((u8 *)&nwkFrameCtrl) = 0;
    nwkFrameCtrl.frameType = (u8)(frameType & 0x03U);
    nwkFrameCtrl.protocolVer = 3;
    nwkFrameCtrl.autoCommissioning = (u8)((u8)autoComm & 0x01U);

    if (pExtNwkFrameCtrl != NULL) {
        nwkFrameCtrl.nwkFrameCtrlExtension = 1;
        pExtNwkFrameCtrl->appId = (u8)(appId & 0x07U);
        pExtNwkFrameCtrl->securityLevel = (u8)(secLevel & 0x03U);
        pExtNwkFrameCtrl->securityKey = (u8)(secKey != 0U);
        pExtNwkFrameCtrl->rxAfterTx = (u8)((u8)rxAfterTx & 0x01U);
        pExtNwkFrameCtrl->direction = (u8)((u8)direction & 0x01U);
    }

    return nwkFrameCtrl;
}

u8 gpNwkHdrParse(gp_nwkHdr_t *pNwkHdr, u8 *pGpdu)
{
    u8 *p = pGpdu + 1;
    u8 nwkFrameCtrlRaw = pGpdu[0];
    u8 appId = GP_APP_ID_SRC_ID;

    *((u8 *)&pNwkHdr->nwkFrameCtrl) = nwkFrameCtrlRaw;

    if (pNwkHdr->nwkFrameCtrl.nwkFrameCtrlExtension) {
        *((u8 *)&pNwkHdr->extNwkFrameCtrl) = pGpdu[1];
        appId = pNwkHdr->extNwkFrameCtrl.appId;
        if ((appId != GP_APP_ID_SRC_ID) && (appId != GP_APP_ID_GPD)) {
            return 0;
        }

        if (pNwkHdr->extNwkFrameCtrl.direction) {
            return 0;
        }

        p++;
    }

    if ((((u8)(nwkFrameCtrlRaw & 0x03U)) | appId) == 0U) {
        memcpy(&pNwkHdr->gpdSrcId, p, 4);
        p += 4;
    } else if (((nwkFrameCtrlRaw & ~0x7cU) == 0x81U) &&
               (appId == GP_APP_ID_SRC_ID)) {
        memcpy(&pNwkHdr->gpdSrcId, p, 4);
        p += 4;
    }

    if (appId == GP_APP_ID_GPD) {
        pNwkHdr->endpoint = *p++;
    }

    if ((nwkFrameCtrlRaw & 0x80U) != 0U &&
        (pNwkHdr->extNwkFrameCtrl.securityLevel > GP_SEC_LEVEL_RESERVED)) {
        memcpy(&pNwkHdr->secFrameCnt, p, 4);
        p += 4;
    }

    return (u8)(p - pGpdu);
}

u8 *gp_gpdfCreat(gp_data_req_t *pGpDataReq, u8 nwkFrameCtrlRaw,
                 u8 extNwkFrameCtrlRaw, u32 secFrameCounter, u32 mic,
                 u8 *pGpduLen)
{
    u8 gpdfLen = (u8)(*pGpduLen + 2U);
    u8 frameType = (u8)(nwkFrameCtrlRaw & 0x03U);
    bool hasExtNwkFrameCtrl = ((nwkFrameCtrlRaw & 0x80U) != 0U);
    u8 securityLevel = (u8)(extNwkFrameCtrlRaw & 0x18U);
    bool hasSecurityField = hasExtNwkFrameCtrl &&
                            ((securityLevel == 0x10U) || (securityLevel == 0x18U));
    u8 *pGpdu;
    u8 *p;

    if (hasExtNwkFrameCtrl) {
        gpdfLen++;
        if (hasSecurityField) {
            gpdfLen = (u8)(gpdfLen + 8U);
        }
    }

    if (frameType == GP_NWK_FRAME_TYPE_DATA) {
        if (pGpDataReq->appId == GP_APP_ID_GPD) {
            gpdfLen++;
        } else {
            gpdfLen = (u8)(gpdfLen + 4U);
        }
    }

    pGpdu = pGpDataReq->gpdAsdu - gpdfLen;
    gpdfLen = (u8)(gpdfLen + pGpDataReq->gpdAsduLen);
    *pGpduLen = gpdfLen;

    p = pGpdu;
    *p++ = nwkFrameCtrlRaw;

    if (hasExtNwkFrameCtrl) {
        *p++ = extNwkFrameCtrlRaw;
    }

    if (frameType == GP_NWK_FRAME_TYPE_DATA) {
        if (pGpDataReq->appId == GP_APP_ID_GPD) {
            *p++ = pGpDataReq->endpoint;
        } else {
            p[0] = (u8)pGpDataReq->gpdId.srcId;
            p[1] = (u8)(pGpDataReq->gpdId.srcId >> 8);
            p[2] = (u8)(pGpDataReq->gpdId.srcId >> 16);
            p[3] = (u8)(pGpDataReq->gpdId.srcId >> 24);
            p += 4;
        }
    }

    if (hasSecurityField) {
        p[0] = (u8)secFrameCounter;
        p[1] = (u8)(secFrameCounter >> 8);
        p[2] = (u8)(secFrameCounter >> 16);
        p[3] = (u8)(secFrameCounter >> 24);
        p += 4;
    }

    *p++ = pGpDataReq->gpdCmdId;
    p += pGpDataReq->gpdAsduLen;

    if (hasSecurityField) {
        p[0] = (u8)mic;
        p[1] = (u8)(mic >> 8);
        p[2] = (u8)(mic >> 16);
        p[3] = (u8)(mic >> 24);
    }

    return pGpdu;
}

void gp_gpdfGenerate(gp_data_req_pending_t *pEntry, u8 frameType)
{
    gp_data_req_t *pGpDataReq = pEntry->buf;
    cgp_data_req_t *pCgpDataReq = (cgp_data_req_t *)pGpDataReq;
    gpdId_t gpdId = {0};
    gp_nwkHdrFrameCtrl_t nwkFrameCtrl;
    gp_extNwkFrameCtrl_t extNwkFrameCtrl;
    u8 nwkFrameCtrlRaw;
    u8 extNwkFrameCtrlRaw = 0;
    u8 gpMpduLen = 0;
    u8 gpepHandle = pGpDataReq->gpepHandle;
    u8 txOptionsRaw = *((u8 *)&pGpDataReq->txOptions);
    u8 cgpTxOptions = 0;

    memset(&extNwkFrameCtrl, 0, sizeof(extNwkFrameCtrl));
    memcpy(&gpdId, &pGpDataReq->gpdId, sizeof(gpdId));

    if (frameType == GP_NWK_FRAME_TYPE_MAINTENANCE) {
        nwkFrameCtrl = gpNwkHdrFrameCtrlBuild(GP_NWK_FRAME_TYPE_MAINTENANCE,
                                              FALSE, 0, 0, 0,
                                              FALSE, TRUE, NULL);
        nwkFrameCtrlRaw = *((u8 *)&nwkFrameCtrl);
    } else {
        nwkFrameCtrl = gpNwkHdrFrameCtrlBuild(frameType, FALSE,
                                              pGpDataReq->appId,
                                              0, 0, FALSE, TRUE,
                                              &extNwkFrameCtrl);
        nwkFrameCtrlRaw = *((u8 *)&nwkFrameCtrl);
        extNwkFrameCtrlRaw = *((u8 *)&extNwkFrameCtrl);
    }

    pCgpDataReq->gpMpdu = gp_gpdfCreat(pGpDataReq, nwkFrameCtrlRaw,
                                       extNwkFrameCtrlRaw, 0, 0, &gpMpduLen);
    pCgpDataReq->gpMpduLen = gpMpduLen;
    pCgpDataReq->srcPanId = g_zbNIB.panId;
    pCgpDataReq->dstPanId = 0xffff;
    pCgpDataReq->srcAddrMode = ADDR_MODE_NONE;

    switch (extNwkFrameCtrlRaw & 0x07U) {
    case GP_APP_ID_GPD:
        pCgpDataReq->dstAddrMode = ADDR_MODE_EXT;
        memcpy(&pCgpDataReq->dstAddr, &gpdId, sizeof(gpdId));
        break;
    case GP_APP_ID_SRC_ID:
        pCgpDataReq->dstAddrMode = ADDR_MODE_SHORT;
        pCgpDataReq->dstAddr.shortAddr = 0xffff;
        break;
    default:
        break;
    }

    pCgpDataReq->gpMpduHandle = gpepHandle;
    if ((txOptionsRaw & 0x02U) != 0U) {
        cgpTxOptions |= 0x01U;
    }
    if ((txOptionsRaw & 0x04U) != 0U) {
        cgpTxOptions |= 0x02U;
    }
    *((u8 *)&pCgpDataReq->txOptions) = cgpTxOptions;

    pGpDataReq->gpepHandle = gpepHandle;
    ev_timer_taskPost(cGp_dataReq, pGpDataReq, GP_TX_OFFSET);
}

void gp_gpdfTransSchedule(u8 frameType, u8 appId, gpdId_t gpdId, u8 endpoint)
{
    gp_data_req_t *pGpDataReq;
    u8 gpdIdLen = 4;
    u8 txOptionsRaw;

    if (appId != GP_APP_ID_SRC_ID) {
        if (appId != GP_APP_ID_GPD) {
            return;
        }
        gpdIdLen = 8;
    }

    if (!gpTxQueue.used) {
        return;
    }

    pGpDataReq = gpTxQueue.buf;
    if ((pGpDataReq->appId != appId) ||
        (memcmp(&pGpDataReq->gpdId, &gpdId, gpdIdLen) != 0)) {
        return;
    }

    if ((appId == GP_APP_ID_GPD) &&
        (endpoint != GP_ENDPOINT_ALL) &&
        (pGpDataReq->endpoint != endpoint)) {
        return;
    }

    txOptionsRaw = *((u8 *)&pGpDataReq->txOptions);
    txOptionsRaw &= (u8)~0x06U;
    *((u8 *)&pGpDataReq->txOptions) = txOptionsRaw;

    gp_gpdfGenerate(&gpTxQueue, frameType);
    gpTxQueue.buf = NULL;
    gpTxQueue.gpepHandle = 0;
    gpTxQueue.used = 0;
}

gp_data_req_pending_t gpTxQueue;
gp_data_ind_entry_t g_gpDataIndSecReqTab[GP_DATA_IND_SEC_REQ_TAB_NUM];
gp_stubCb_t *gpStubCb = NULL;

static u8 dGpStubHandle = GP_HANDLE_MIN;

_attribute_no_inline_ void gpTxQueueListClear(gp_data_req_pending_t *pEntry)
{
    if (pEntry->buf != NULL) {
        zb_buf_free((zb_buf_t *)pEntry->buf);
    }

    pEntry->buf = NULL;
    pEntry->gpepHandle = 0;
    pEntry->used = 0;
}

void gpTxQueueListReplace(gp_data_req_pending_t *pEntry, gp_data_req_t *pGpDataReq)
{
    if (pEntry->used) {
        if (pEntry->buf != NULL) {
            zb_buf_free((zb_buf_t *)pEntry->buf);
        }

        pEntry->buf = pGpDataReq;
        pEntry->gpepHandle = pGpDataReq->gpepHandle;
    }
}

void gpTxQueueListAdd(gp_data_req_pending_t *pEntry, gp_data_req_t *pGpDataReq)
{
    if ((pGpDataReq != NULL) && !pEntry->used) {
        pEntry->buf = pGpDataReq;
        pEntry->gpepHandle = pGpDataReq->gpepHandle;
        pEntry->used = 1;
    }
}

void gpTxQueueMaintenceClear(void)
{
    gp_data_req_t *pGpDataReq = gpTxQueue.buf;

    if (gpTxQueue.used && (pGpDataReq != NULL) &&
        (pGpDataReq->appId == GP_APP_ID_SRC_ID) &&
        (pGpDataReq->gpdId.srcId != 0U)) {
        gpTxQueueListClear(&gpTxQueue);
    }
}

_attribute_no_inline_ void gpTxQueueInit(void)
{
    gpTxQueueListClear(&gpTxQueue);
}

void gpTxQueueFree(void)
{
    gpTxQueueInit();
}

_attribute_no_inline_ void gpSecReqSend(gp_sec_req_t *pGpSecReq)
{
    if ((gpStubCb == NULL) || (gpStubCb->gpSecReqCb == NULL)) {
        zb_buf_free((zb_buf_t *)pGpSecReq);
        return;
    }

    if (gpDataIndDuplicateFind(pGpSecReq->appId, pGpSecReq->gpdId,
                               pGpSecReq->gpdSecFrameCnt,
                               pGpSecReq->dgpStubHandle)) {
        gp_sec_rsp_t *pGpSecRsp = (gp_sec_rsp_t *)pGpSecReq;
        gpdId_t gpdId = pGpSecReq->gpdId;
        u32 gpdSecFrameCnt = pGpSecReq->gpdSecFrameCnt;
        u8 dgpStubHandle = pGpSecReq->dgpStubHandle;
        u8 appId = pGpSecReq->appId;
        u8 endpoint = pGpSecReq->endpoint;
        u8 gpdfSecurityLevel = pGpSecReq->gpdfSecurityLevel;

        memset(pGpSecRsp, 0, sizeof(gp_sec_rsp_t));
        pGpSecRsp->gpdId = gpdId;
        pGpSecRsp->gpdSecFrameCnt = gpdSecFrameCnt;
        pGpSecRsp->status = GP_SEC_RSP_STATUS_DROP_FRAME;
        pGpSecRsp->dgpStubHandle = dgpStubHandle;
        pGpSecRsp->appId = appId;
        pGpSecRsp->endpoint = endpoint;
        pGpSecRsp->gpdfSecurityLevel = gpdfSecurityLevel;
        tl_zbTaskPost(gpSecRsp, pGpSecRsp);
        return;
    }

    gpStubCb->gpSecReqCb(pGpSecReq);
}

void gpSecRsp(void *arg)
{
    gp_sec_rsp_t *pGpSecRsp = (gp_sec_rsp_t *)arg;
    gp_data_ind_entry_t *pEntry = NULL;

    for (u8 i = 0; i < GP_DATA_IND_SEC_REQ_TAB_NUM; i++) {
        if (g_gpDataIndSecReqTab[i].used &&
            (g_gpDataIndSecReqTab[i].dGpStubHandle == pGpSecRsp->dgpStubHandle)) {
            pEntry = &g_gpDataIndSecReqTab[i];
            break;
        }
    }

    if (pEntry == NULL) {
        zb_buf_free((zb_buf_t *)pGpSecRsp);
        return;
    }

    gp_data_ind_t *pGpDataInd = pEntry->buf;
    gpdId_t gpdId = {0};
    u8 appId = pGpDataInd->appId;
    u8 endpoint = pGpDataInd->endpoint;
    u8 rxAfterTx = pGpDataInd->rxAfterTx;

    pGpDataInd->gpdfKeyType = pGpSecRsp->gpdfKeyType;
    if (appId == GP_APP_ID_GPD) {
        memcpy(&gpdId, &pGpDataInd->srcAddr, sizeof(gpdId));
    } else {
        gpdId.srcId = pGpDataInd->srcId;
    }

    switch (pGpSecRsp->status) {
    case GP_SEC_RSP_STATUS_DROP_FRAME:
        zb_buf_free((zb_buf_t *)pGpSecRsp);
        gpDataIndSecReqEntryClear(pEntry);
        return;
    case GP_SEC_RSP_STATUS_MATCH:
    case GP_SEC_RSP_STATUS_TX_THEN_DROP:
        if (gpCcmStar(pGpDataInd->appId, gpdId,
                      pEntry->gpdfSecKey, pEntry->gpdfSecLevel,
                      pGpDataInd->endpoint, pGpDataInd->gpdSecFrameCnt,
                      pGpDataInd->gpdAsduLen, pGpDataInd->gpdAsdu,
                      pGpDataInd->autoCommissioning, pGpDataInd->rxAfterTx,
                      pGpDataInd->mic, pGpSecRsp->gpdKey) != SUCCESS) {
            pGpDataInd->status = GP_DATA_IND_STATUS_AUTH_FAILURE;
            tl_zbTaskPost((tl_zb_callback_t)gpDataIndSend, pEntry);
            break;
        }

        pGpDataInd->gpdCmdId = pGpDataInd->gpdAsdu[0];
        if (GPD_CMD_ID_IS_INVALID(pGpDataInd->gpdCmdId)) {
            zb_buf_free((zb_buf_t *)pGpSecRsp);
            gpDataIndSecReqEntryClear(pEntry);
            return;
        }

        if (pGpSecRsp->status == GP_SEC_RSP_STATUS_TX_THEN_DROP) {
            gpDataIndSecReqEntryClear(pEntry);
            break;
        }

        pGpDataInd->status = (pGpDataInd->gpdfSecurityLevel == GP_SEC_LEVEL_NO_SECURITY)
                                 ? GP_DATA_IND_STATUS_NO_SECURITY
                                 : GP_DATA_IND_STATUS_SEC_SUCCESS;
        tl_zbTaskPost((tl_zb_callback_t)gpDataIndSend, pEntry);
        break;
    case GP_SEC_RSP_STATUS_PASS_UNPROCESSED:
        pGpDataInd->status = GP_DATA_IND_STATUS_UNPROCESSED;
        tl_zbTaskPost((tl_zb_callback_t)gpDataIndSend, pEntry);
        break;
    default:
        break;
    }

    zb_buf_free((zb_buf_t *)pGpSecRsp);
    if ((pEntry->buf != NULL) && rxAfterTx) {
        gp_gpdfTransSchedule(GP_NWK_FRAME_TYPE_DATA, appId, gpdId, endpoint);
    }
}

_attribute_no_inline_ void gpDataIndSecReqEntryClear(gp_data_ind_entry_t *pEntry)
{
    if (pEntry->buf != NULL) {
        zb_buf_free((zb_buf_t *)pEntry->buf);
    }

    memset(pEntry, 0, sizeof(gp_data_ind_entry_t));
}

_attribute_no_inline_ void gpDataIndSend(gp_data_ind_entry_t *pEntry)
{
    if ((gpStubCb == NULL) || (gpStubCb->gpDataIndCb == NULL)) {
        gpDataIndSecReqEntryClear(pEntry);
        return;
    }

    pEntry->timeout = g_gpBaseCtx.gpDuplicateTimeout;
    if ((g_gpBaseCtx.transmitChannelTimeoutEvt != NULL) &&
        (g_gpBaseCtx.gpOperationalChannel != rf_getChannel())) {
        return;
    }

    gpStubCb->gpDataIndCb(pEntry->buf);
}

_attribute_no_inline_ void gpDataFrameProcess(void *arg, gp_nwkHdr_t *pNwkHdr,
                                              u8 appId, u8 *pGpdAsdu,
                                              u8 gpdAsduLen, u32 mic)
{
    dgp_data_ind_t saved;
    gp_data_ind_t *pGpDataInd = (gp_data_ind_t *)arg;
    gp_data_ind_entry_t *pEntry;
    gp_sec_req_t *pGpSecReq;

    memcpy(&saved, pGpDataInd, sizeof(saved));
    memset(pGpDataInd, 0, sizeof(gp_data_ind_t));

    pGpDataInd->status = GP_DATA_IND_STATUS_NO_SECURITY;
    pGpDataInd->rssi = saved.rssi;
    pGpDataInd->lqi = saved.lqi;
    pGpDataInd->seqNum = saved.seqNum;
    pGpDataInd->srcAddrMode = saved.srcAddrMode;
    pGpDataInd->srcPanId = saved.srcPanId;
    memcpy(&pGpDataInd->srcAddr, &saved.srcAddr, sizeof(saved.srcAddr));
    pGpDataInd->appId = appId;
    pGpDataInd->gpdfSecurityLevel = pNwkHdr->nwkFrameCtrl.nwkFrameCtrlExtension
                                        ? pNwkHdr->extNwkFrameCtrl.securityLevel
                                        : GP_SEC_LEVEL_NO_SECURITY;
    pGpDataInd->gpdfKeyType = GP_SEC_SHARED_KEY;
    pGpDataInd->autoCommissioning = pNwkHdr->nwkFrameCtrl.autoCommissioning;
    pGpDataInd->rxAfterTx = pNwkHdr->nwkFrameCtrl.nwkFrameCtrlExtension
                                ? pNwkHdr->extNwkFrameCtrl.rxAfterTx
                                : FALSE;
    pGpDataInd->srcId = pNwkHdr->gpdSrcId;
    pGpDataInd->endpoint = pNwkHdr->endpoint;
    pGpDataInd->gpdSecFrameCnt = saved.seqNum;
    if (pGpDataInd->gpdfSecurityLevel != GP_SEC_LEVEL_NO_SECURITY) {
        pGpDataInd->gpdSecFrameCnt = pNwkHdr->secFrameCnt;
    }
    pGpDataInd->mic = mic;
    pGpDataInd->gpdAsdu = pGpdAsdu;
    pGpDataInd->gpdCmdId = pGpdAsdu[0];
    pGpDataInd->gpdAsduLen = gpdAsduLen;
    pGpDataInd->frameType = pNwkHdr->nwkFrameCtrl.frameType;

    pGpSecReq = (gp_sec_req_t *)zb_buf_allocate();
    if (pGpSecReq == NULL) {
        zb_buf_free((zb_buf_t *)pGpDataInd);
        return;
    }

    pEntry = gpDataIndSecReqEntryAdd(pGpDataInd);
    if (pEntry == NULL) {
        zb_buf_free((zb_buf_t *)pGpSecReq);
        zb_buf_free((zb_buf_t *)pGpDataInd);
        return;
    }

    pEntry->gpdfSecKey = pNwkHdr->nwkFrameCtrl.nwkFrameCtrlExtension
                             ? pNwkHdr->extNwkFrameCtrl.securityKey
                             : 0;
    pEntry->gpdfSecLevel = pGpDataInd->gpdfSecurityLevel;
    pEntry->frameCounter = pGpDataInd->gpdSecFrameCnt;

    memset(pGpSecReq, 0, sizeof(gp_sec_req_t));
    pGpSecReq->appId = appId;
    if (appId == GP_APP_ID_GPD) {
        memcpy(&pGpSecReq->gpdId, &saved.srcAddr, sizeof(saved.srcAddr));
        pGpSecReq->endpoint = pGpDataInd->endpoint;
    } else {
        pGpSecReq->gpdId.srcId = pGpDataInd->srcId;
    }
    pGpSecReq->gpdfSecurityLevel = pEntry->gpdfSecLevel;
    pGpSecReq->gpdfKeyType = pEntry->gpdfSecKey;
    pGpSecReq->gpdSecFrameCnt = pEntry->frameCounter;
    pGpSecReq->dgpStubHandle = pEntry->dGpStubHandle;

    tl_zbTaskPost((tl_zb_callback_t)gpSecReqSend, pGpSecReq);
}

gp_data_ind_entry_t *gpDataIndEntryFreeGet(void)
{
    for (u8 i = 0; i < GP_DATA_IND_SEC_REQ_TAB_NUM; i++) {
        if (!g_gpDataIndSecReqTab[i].used) {
            return &g_gpDataIndSecReqTab[i];
        }
    }

    return NULL;
}

gp_data_ind_entry_t *gpDataIndSecReqEntryAdd(gp_data_ind_t *pGpDataInd)
{
    gp_data_ind_entry_t *pEntry = gpDataIndEntryFreeGet();

    if (pEntry == NULL) {
        return NULL;
    }

    pEntry->used = 1;
    pEntry->buf = pGpDataInd;
    pEntry->appId = (u8)(pGpDataInd->appId & 0x07U);

    if (pEntry->appId == GP_APP_ID_GPD) {
        memcpy(&pEntry->gpdId, &pGpDataInd->srcAddr, sizeof(tl_zb_addr_t));
    } else {
        pEntry->gpdId.srcId = pGpDataInd->srcId;
        pEntry->frameCounter = 0xffffffff;
        pEntry->timeout = 0xff;
    }

    dGpStubHandle++;
    if (dGpStubHandle > GP_HANDLE_MAX) {
        dGpStubHandle = GP_HANDLE_MIN;
    }

    pEntry->dGpStubHandle = dGpStubHandle;

    return pEntry;
}

gp_data_ind_entry_t *gpDataIndGet(u8 handle)
{
    for (u8 i = 0; i < GP_DATA_IND_SEC_REQ_TAB_NUM; i++) {
        if (g_gpDataIndSecReqTab[i].used &&
            (g_gpDataIndSecReqTab[i].dGpStubHandle == handle)) {
            return &g_gpDataIndSecReqTab[i];
        }
    }

    return NULL;
}

u8 dGpStubHandleGet(void)
{
    dGpStubHandle++;
    if (dGpStubHandle > GP_HANDLE_MAX) {
        dGpStubHandle = GP_HANDLE_MIN;
    }

    return dGpStubHandle;
}

_attribute_no_inline_ void gpDataIndSecReqTabInit(void)
{
    for (u8 i = 0; i < GP_DATA_IND_SEC_REQ_TAB_NUM; i++) {
        gpDataIndSecReqEntryClear(&g_gpDataIndSecReqTab[i]);
    }
}

void gpStubCbInit(gp_stubCb_t *cb)
{
    gpTxQueueInit();
    gpDataIndSecReqTabInit();
    gpStubCb = cb;
}

_attribute_no_inline_ void gpDataCnfDeliver(gp_data_cnf_t *pGpDataCnf)
{
    if ((gpStubCb != NULL) && (gpStubCb->gpDataCnfCb != NULL)) {
        if ((pGpDataCnf->status == GP_DATA_CNF_STATUS_GPDF_SENDING_FINALIZED) &&
            (pGpDataCnf->gpepHandle == GP_HANDLE_CHANNEL_CONFIGURATION)) {
            gpTransmitChannelTimeoutStop();
        }

        gpStubCb->gpDataCnfCb(pGpDataCnf);
    }
}

_attribute_no_inline_ void gpTxQueueCheck(gp_data_req_t *pGpDataReq)
{
    gp_data_cnf_t gpDataCnf = {0, 0};
    gp_data_req_t *pQueuedReq = gpTxQueue.buf;
    u8 appId = pGpDataReq->appId;
    u8 gpdIdLen = (appId == GP_APP_ID_GPD) ? sizeof(gpdId_t) : sizeof(u32);
    u8 txOptionsRaw = *((u8 *)&pGpDataReq->txOptions);

    if (gpTxQueue.used &&
        (pQueuedReq->appId == appId) &&
        (memcmp(&pQueuedReq->gpdId, &pGpDataReq->gpdId, gpdIdLen) == 0)) {
        if ((appId != GP_APP_ID_GPD) ||
            ((txOptionsRaw & 0x20U) == 0U) ||
            (pGpDataReq->endpoint == pQueuedReq->endpoint)) {
            gpDataCnf.gpepHandle = pQueuedReq->gpepHandle;
            if (pGpDataReq->action) {
                gpTxQueueListReplace(&gpTxQueue, pGpDataReq);
                gpDataCnf.status = GP_DATA_CNF_STATUS_ENTRY_REPLACED;
            } else {
                zb_buf_free((zb_buf_t *)pGpDataReq);
                gpTxQueueListClear(&gpTxQueue);
                gpDataCnf.status = GP_DATA_CNF_STATUS_ENTRY_REMOVED;
            }
            gpDataCnfDeliver(&gpDataCnf);
            return;
        }
    }

    gpDataCnf.gpepHandle = pGpDataReq->gpepHandle;
    if (!pGpDataReq->action) {
        zb_buf_free((zb_buf_t *)pGpDataReq);
        gpDataCnf.status = GP_DATA_CNF_STATUS_ENTRY_REMOVED;
        gpDataCnfDeliver(&gpDataCnf);
        return;
    }

    if (!gpTxQueue.used) {
        gpTxQueueListAdd(&gpTxQueue, pGpDataReq);
        gpDataCnf.status = GP_DATA_CNF_STATUS_ENTRY_ADDED;
    } else {
        zb_buf_free((zb_buf_t *)pGpDataReq);
        gpDataCnf.status = GP_DATA_CNF_STATUS_TX_QUEUE_FULL;
    }

    gpDataCnfDeliver(&gpDataCnf);
}

void gpDataReq(void *arg)
{
    gp_data_req_t *req = (gp_data_req_t *)arg;

    if (req->action) {
        if (req->appId == GP_APP_ID_SRC_ID) {
            *((u8 *)&req->txOptions) &= (u8)~0x20U;
        } else if (req->appId != GP_APP_ID_GPD) {
            zb_buf_free((zb_buf_t *)arg);
            return;
        }

        req->gpTxQueueEntryLifetime = 0x00ffffffUL;
        gpTxQueueCheck(req);
        return;
    }

    zb_buf_free((zb_buf_t *)arg);
}

void dGp_dataInd(void *arg)
{
    dgp_data_ind_t *pDgpDataInd = (dgp_data_ind_t *)arg;
    gp_nwkHdr_t nwkHdr;
    gpdId_t gpdId = {0};
    u8 nwkHdrLen;
    u8 appId = GP_APP_ID_SRC_ID;
    u8 *pGpdAsdu;
    u8 gpdAsduLen;
    u8 cmdId;
    u32 mic = 0;

    memset(&nwkHdr, 0, sizeof(nwkHdr));
    nwkHdrLen = gpNwkHdrParse(&nwkHdr, pDgpDataInd->gpMpdu);
    if ((nwkHdrLen == 0U) || (nwkHdrLen >= pDgpDataInd->gpMpduLen)) {
        zb_buf_free((zb_buf_t *)arg);
        return;
    }

    if (nwkHdr.nwkFrameCtrl.nwkFrameCtrlExtension) {
        appId = nwkHdr.extNwkFrameCtrl.appId;
        if (appId == GP_APP_ID_GPD) {
            if (pDgpDataInd->srcAddrMode != ADDR_MODE_EXT) {
                zb_buf_free((zb_buf_t *)arg);
                return;
            }

            memcpy(&gpdId, &pDgpDataInd->srcAddr, sizeof(gpdId));
        } else {
            gpdId.srcId = nwkHdr.gpdSrcId;
        }
    } else {
        gpdId.srcId = nwkHdr.gpdSrcId;
    }

    pGpdAsdu = pDgpDataInd->gpMpdu + nwkHdrLen;
    if (nwkHdr.nwkFrameCtrl.nwkFrameCtrlExtension &&
        (appId != GP_APP_ID_SRC_ID) &&
        (nwkHdr.extNwkFrameCtrl.securityLevel > GP_SEC_LEVEL_RESERVED)) {
        gpdAsduLen = (u8)(pDgpDataInd->gpMpduLen - nwkHdrLen - 4U);
        mic = (u32)pGpdAsdu[gpdAsduLen] |
              ((u32)pGpdAsdu[gpdAsduLen + 1] << 8) |
              ((u32)pGpdAsdu[gpdAsduLen + 2] << 16) |
              ((u32)pGpdAsdu[gpdAsduLen + 3] << 24);
    } else {
        gpdAsduLen = (u8)(pDgpDataInd->gpMpduLen - nwkHdrLen);
    }

    cmdId = pGpdAsdu[0];
    if (nwkHdr.nwkFrameCtrl.frameType == GP_NWK_FRAME_TYPE_MAINTENANCE) {
        if (GPD_CMD_ID_IS_INVALID(cmdId)) {
            zb_buf_free((zb_buf_t *)arg);
            return;
        }

        if (!nwkHdr.nwkFrameCtrl.autoCommissioning) {
            memset(&gpdId, 0, sizeof(gpdId));
            gp_gpdfTransSchedule(nwkHdr.nwkFrameCtrl.frameType,
                                 GP_APP_ID_SRC_ID, gpdId, 0);
            appId = GP_APP_ID_SRC_ID;
        }
    }

    if (nwkHdr.nwkFrameCtrl.nwkFrameCtrlExtension) {
        u8 securityLevel = (u8)(*((u8 *)&nwkHdr.extNwkFrameCtrl) & 0x18U);

        if (securityLevel == 0x08U) {
            zb_buf_free((zb_buf_t *)arg);
            return;
        }

        if (((securityLevel == 0x00U) || (securityLevel == 0x10U)) &&
            GPD_CMD_ID_IS_INVALID(cmdId)) {
            zb_buf_free((zb_buf_t *)arg);
            return;
        }
    }

    gpDataFrameProcess(arg, &nwkHdr, appId, pGpdAsdu, gpdAsduLen, mic);
}

void cGpDataCnfHandler(void *arg)
{
    u8 *buf = (u8 *)arg;
    gp_data_cnf_t gpDataCnf = {
        .status = GP_DATA_CNF_STATUS_GPDF_SENDING_FINALIZED,
        .gpepHandle = buf[1],
    };

    gpDataCnfDeliver(&gpDataCnf);
    zb_buf_free((zb_buf_t *)arg);
}
#else
/* Empty translation unit: the original end-device object exported no symbols. */
#endif
