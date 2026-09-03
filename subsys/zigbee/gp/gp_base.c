#include "zb_local.h"
#include "gp_base.h"
#include "dGP_stub.h"

#if defined(ZB_ROUTER_ROLE)
extern void zdo_devAnnce(u16 nwkAddr, const addrExt_t ieeeAddr, u8 capability);
extern u8 zclGpAttr_gpSharedSecKeyType;
extern u8 zclGpAttr_gpSharedSecKey[];

gp_base_t g_gpBaseCtx = {NULL, 0, 2};

static int gpTransmitChannelTimeoutCb(void *arg)
{
    (void)arg;

    if (g_gpBaseCtx.gpOperationalChannel != rf_getChannel()) {
        tl_zbMacChannelSet(g_gpBaseCtx.gpOperationalChannel);
    }

    gpTxQueueMaintenceClear();
    g_gpBaseCtx.transmitChannelTimeoutEvt = NULL;

    return -1;
}

void gpTransmitChannelTimeoutStop(void)
{
    if (g_gpBaseCtx.gpOperationalChannel != rf_getChannel()) {
        tl_zbMacChannelSet(g_gpBaseCtx.gpOperationalChannel);
    }

    if (g_gpBaseCtx.transmitChannelTimeoutEvt != NULL) {
        ev_timer_taskCancel(&g_gpBaseCtx.transmitChannelTimeoutEvt);
    }
}

void gpSwitchToTransmitChannel(u8 operationChannel, u8 tempMasterTxChannel)
{
    u8 gpChannel = (u8)(operationChannel + 11U);
    u8 txChannel = (u8)(tempMasterTxChannel + 11U);

    if (g_gpBaseCtx.gpOperationalChannel != gpChannel) {
        g_gpBaseCtx.gpOperationalChannel = gpChannel;
    }

    if (txChannel != rf_getChannel()) {
        tl_zbMacChannelSet(txChannel);
    }

    if (g_gpBaseCtx.transmitChannelTimeoutEvt != NULL) {
        ev_timer_taskCancel(&g_gpBaseCtx.transmitChannelTimeoutEvt);
    }

    g_gpBaseCtx.transmitChannelTimeoutEvt = ev_timer_taskPost(gpTransmitChannelTimeoutCb, NULL, 5000);
}

u16 gpAliasSrcAddrDerived(u8 appId, gpdId_t gpdId)
{
    u16 alias;
    u16 folded;

    if (appId != 0U && appId != 2U) {
        return 7;
    }

    alias = (u16)gpdId.srcId;
    folded = (u16)(gpdId.srcId >> 16);
    if (alias != 0U && alias <= 0xfff6U) {
        return alias;
    }

    folded ^= alias;
    if (folded != 0U && folded <= 0xfff6U) {
        return folded;
    }

    return alias != 0U ? (u16)(alias - 8U) : 7U;
}

u8 gppTunnelingDelayGet(bool rxAfterTx, u8 lqi, bool firstToForward, bool noRoute)
{
    u8 delay = rxAfterTx ? 32U : 5U;
    u8 extra = lqi ? 32U : 96U;

    if (noRoute) {
        return (u8)(delay + 100U);
    }

    if (!firstToForward) {
        delay = (u8)(delay + extra);
    }

    return delay;
}

void gpTranimitGPDF(gp_data_req_t *pGpDataReq)
{
    zb_buf_t *buf = zb_buf_allocate();

    if (buf == NULL) {
        return;
    }

    if (pGpDataReq->gpdCmdId == 0xf3U) {
        pGpDataReq->gpepHandle = 0xbeU;
    } else {
        pGpDataReq->gpepHandle = dGpStubHandleGet();
    }

    memcpy(buf, pGpDataReq, sizeof(*pGpDataReq));

    if (pGpDataReq->gpdAsduLen != 0U) {
        u8 *payload = tl_bufInitalloc(buf, pGpDataReq->gpdAsduLen);
        gp_data_req_t *saved = (gp_data_req_t *)buf;

        saved->gpdAsdu = payload;
        memcpy(payload, pGpDataReq->gpdAsdu, pGpDataReq->gpdAsduLen);
    }

    tl_zbTaskPost(gpDataReq, buf);
}

int gpDataIndDuplicatePeriodic(void *arg)
{
    (void)arg;

    for (u8 i = 0; i < GP_DATA_IND_SEC_REQ_TAB_NUM; i++) {
        gp_data_ind_entry_t *entry = &g_gpDataIndSecReqTab[i];

        if (!entry->used || entry->timeout == 0xffU) {
            continue;
        }

        if (entry->timeout == 0U) {
            gpDataIndSecReqEntryClear(entry);
        } else {
            entry->timeout--;
        }
    }

    return 0;
}

bool gpDataIndDuplicateFind(u8 appId, gpdId_t gpdId, u32 secFrameCounter, u8 handle)
{
    for (u8 i = 0; i < GP_DATA_IND_SEC_REQ_TAB_NUM; i++) {
        gp_data_ind_entry_t *entry = &g_gpDataIndSecReqTab[i];

        if (!entry->used || entry->appId != (appId & 0x07U)) {
            continue;
        }

        if (memcmp(&entry->gpdId, &gpdId, sizeof(gpdId_t)) != 0 ||
            entry->frameCounter != secFrameCounter) {
            continue;
        }

        if (entry->dGpStubHandle != handle) {
            return TRUE;
        }
    }

    return FALSE;
}

bool gpDataTunneledDuplicateCheck(u8 appId, gpdId_t gpdId, u32 secFrameCounter)
{
    gp_data_ind_entry_t *entry;

    if (gpDataIndDuplicateFind(appId, gpdId, secFrameCounter, GP_HANDLE_TUNNELED_GPD_CMD)) {
        return TRUE;
    }

    entry = gpDataIndEntryFreeGet();
    if (entry == NULL) {
        return TRUE;
    }

    entry->appId = (u8)(appId & 0x07U);
    memcpy(&entry->gpdId, &gpdId, sizeof(gpdId_t));
    entry->frameCounter = secFrameCounter;
    entry->dGpStubHandle = GP_HANDLE_TUNNELED_GPD_CMD;
    entry->timeout = g_gpBaseCtx.gpDuplicateTimeout;

    return FALSE;
}

bool gpSecKeyTypeMappingChk(u8 secKeyTypeInProxy, u8 secKeyTypeFromGPDF)
{
    if (secKeyTypeFromGPDF == 1U) {
        if (secKeyTypeInProxy <= 3U) {
            return TRUE;
        }
    } else if (secKeyTypeFromGPDF == 0U) {
        if (secKeyTypeInProxy == GP_SEC_KEY_TYPE_OUT_OF_THE_BOX_GPD_KEY ||
            secKeyTypeInProxy == GP_SEC_KEY_TYPE_DERIVED_INDIVIDUAL_GPD_KEY) {
            return TRUE;
        }
    }

    return (u8)(secKeyTypeInProxy - 5U) <= 1U;
}

gpSecRsp_status_t gpKeyRecovery(u8 gpdfKeyType, u8 entryKeyType, u8 *entryKey,
                                gpSecRsp_status_t status, u8 *pKeyType, u8 *pKey)
{
    if (gpdfKeyType != 0U) {
        if ((entryKeyType != GP_SEC_KEY_TYPE_OUT_OF_THE_BOX_GPD_KEY) &&
            (entryKeyType != GP_SEC_KEY_TYPE_DERIVED_INDIVIDUAL_GPD_KEY)) {
            return GP_SEC_RSP_STATUS_DROP_FRAME;
        }

        if (memcmp(entryKey, g_null_securityKey, SEC_KEY_LEN) == 0) {
            return GP_SEC_RSP_STATUS_DROP_FRAME;
        }

        memcpy(pKey, entryKey, SEC_KEY_LEN);
        *pKeyType = entryKeyType;
        return status;
    }

    switch (entryKeyType) {
    case GP_SEC_KEY_TYPE_NWK_KEY:
        if (memcmp(entryKey, g_null_securityKey, SEC_KEY_LEN) != 0) {
            memcpy(pKey, entryKey, SEC_KEY_LEN);
        } else if (zclGpAttr_gpSharedSecKeyType == GP_SEC_KEY_TYPE_NWK_KEY) {
            memcpy(pKey, zclGpAttr_gpSharedSecKey, SEC_KEY_LEN);
        } else if (memcmp(ss_ib.nwkSecurMaterialSet[ss_ib.activeSecureMaterialIndex].key,
                          g_null_securityKey,
                          SEC_KEY_LEN) != 0) {
            memcpy(pKey,
                   ss_ib.nwkSecurMaterialSet[ss_ib.activeSecureMaterialIndex].key,
                   SEC_KEY_LEN);
        } else {
            return GP_SEC_RSP_STATUS_DROP_FRAME;
        }

        *pKeyType = GP_SEC_KEY_TYPE_NWK_KEY;
        return status;
    case GP_SEC_KEY_TYPE_GPD_GROUP_KEY:
    case GP_SEC_KEY_TYPE_NWK_KEY_DERIVED_GPD_GROUP_KEY:
        if (memcmp(entryKey, g_null_securityKey, SEC_KEY_LEN) != 0) {
            memcpy(pKey, entryKey, SEC_KEY_LEN);
        } else if (zclGpAttr_gpSharedSecKeyType == entryKeyType) {
            memcpy(pKey, zclGpAttr_gpSharedSecKey, SEC_KEY_LEN);
        } else {
            return GP_SEC_RSP_STATUS_DROP_FRAME;
        }

        *pKeyType = entryKeyType;
        return status;
    default:
        return GP_SEC_RSP_STATUS_DROP_FRAME;
    }
}

void gpDevAnnceAliasSend(u16 aliasNwkAddr)
{
    addrExt_t ieeeAddr;

    memcpy(ieeeAddr, g_invalid_addr, sizeof(ieeeAddr));
    zdo_devAnnce(aliasNwkAddr, ieeeAddr, 0);
}
#endif

