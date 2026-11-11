/*
 * Copyright 2026 libzigbee
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "zb_common.h"
#include "gp_internal.h"
#include "ss_security_flags.h"
#include "zdo_nwk_manager.h"
#include "zdp_services.h"
#include "gp_base.h"
#include "dGP_stub.h"

#if defined(ZB_ROUTER_ROLE)
gp_base_t g_gpBaseCtx = {NULL, 0, 2};

static int gpTransmitChannelTimeoutCb(void *arg)
{
	(void)arg;

	if (g_gpBaseCtx.gpOperationalChannel != rf_getChannel()) {
		zb_MacChannelSet(g_gpBaseCtx.gpOperationalChannel);
	}

	gpTxQueueMaintenceClear();
	g_gpBaseCtx.transmitChannelTimeoutEvt = NULL;

	return -1;
}

void gpTransmitChannelTimeoutStop(void)
{
	if (g_gpBaseCtx.gpOperationalChannel != rf_getChannel()) {
		zb_MacChannelSet(g_gpBaseCtx.gpOperationalChannel);
	}

	if (g_gpBaseCtx.transmitChannelTimeoutEvt != NULL) {
		ev_timer_taskCancel(&g_gpBaseCtx.transmitChannelTimeoutEvt);
	}
}

void gpSwitchToTransmitChannel(uint8_t operationChannel, uint8_t tempMasterTxChannel)
{
	uint8_t gpChannel = (uint8_t)(operationChannel + 11U);
	uint8_t txChannel = (uint8_t)(tempMasterTxChannel + 11U);

	if (g_gpBaseCtx.gpOperationalChannel != gpChannel) {
		g_gpBaseCtx.gpOperationalChannel = gpChannel;
	}

	if (txChannel != rf_getChannel()) {
		zb_MacChannelSet(txChannel);
	}

	if (g_gpBaseCtx.transmitChannelTimeoutEvt != NULL) {
		ev_timer_taskCancel(&g_gpBaseCtx.transmitChannelTimeoutEvt);
	}

	g_gpBaseCtx.transmitChannelTimeoutEvt =
		ev_timer_taskPost(gpTransmitChannelTimeoutCb, NULL, 5000);
}

uint16_t gpAliasSrcAddrDerived(uint8_t appId, gpdId_t gpdId)
{
	uint16_t alias;
	uint16_t folded;

	if (appId != GP_APP_ID_SRC_ID && appId != GP_APP_ID_GPD) {
		return 7;
	}

	alias = (uint16_t)gpdId.srcId;
	folded = (uint16_t)(gpdId.srcId >> 16);
	if (alias != 0U && alias <= 0xfff6U) {
		return alias;
	}

	folded ^= alias;
	if (folded != 0U && folded <= 0xfff6U) {
		return folded;
	}

	return alias != 0U ? (uint16_t)(alias - 8U) : 7U;
}

uint8_t gppTunnelingDelayGet(bool rxAfterTx, uint8_t lqi, bool firstToForward, bool noRoute)
{
	uint8_t delay = rxAfterTx ? 32U : 5U;
	uint8_t extra = lqi ? 32U : 96U;

	if (noRoute) {
		return (uint8_t)(delay + 100U);
	}

	if (!firstToForward) {
		delay = (uint8_t)(delay + extra);
	}

	return delay;
}

void gpTranimitGPDF(gp_data_req_t *pGpDataReq)
{
	zb_buf_t *buf = zb_buf_allocate();
	gp_data_req_t *saved;

	if (buf == NULL) {
		return;
	}

	if (pGpDataReq->gpdCmdId == 0xf3U) {
		pGpDataReq->gpepHandle = GP_HANDLE_CHANNEL_CONFIGURATION;
	} else {
		pGpDataReq->gpepHandle = dGpStubHandleGet();
	}

	memcpy(buf, pGpDataReq, sizeof(*pGpDataReq));
	saved = (gp_data_req_t *)buf;
	saved->gpdAsdu = NULL;

	if (saved->gpdAsduLen != 0U) {
		uint8_t *payload;

		ZB_BUF_INITIAL_ALLOC(buf, saved->gpdAsduLen, payload, uint8_t *);

		saved->gpdAsdu = payload;
		memcpy(payload, pGpDataReq->gpdAsdu, saved->gpdAsduLen);
	}

	zb_TaskPost(gpDataReq, buf);
}

int gpDataIndDuplicatePeriodic(void *arg)
{
	(void)arg;

	for (uint8_t i = 0; i < GP_DATA_IND_SEC_REQ_TAB_NUM; i++) {
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

bool gpDataIndDuplicateFind(uint8_t appId, gpdId_t gpdId, uint32_t secFrameCounter, uint8_t handle)
{
	for (uint8_t i = 0; i < GP_DATA_IND_SEC_REQ_TAB_NUM; i++) {
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

bool gpDataTunneledDuplicateCheck(uint8_t appId, gpdId_t gpdId, uint32_t secFrameCounter)
{
	gp_data_ind_entry_t *entry;

	if (gpDataIndDuplicateFind(appId, gpdId, secFrameCounter, GP_HANDLE_TUNNELED_GPD_CMD)) {
		return TRUE;
	}

	entry = gpDataIndEntryFreeGet();
	if (entry == NULL) {
		return TRUE;
	}

	entry->appId = (uint8_t)(appId & 0x07U);
	memcpy(&entry->gpdId, &gpdId, sizeof(gpdId_t));
	entry->frameCounter = secFrameCounter;
	entry->dGpStubHandle = GP_HANDLE_TUNNELED_GPD_CMD;
	entry->timeout = g_gpBaseCtx.gpDuplicateTimeout;

	return FALSE;
}

bool gpSecKeyTypeMappingChk(uint8_t secKeyTypeInProxy, uint8_t secKeyTypeFromGPDF)
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

	return (uint8_t)(secKeyTypeInProxy - 5U) <= 1U;
}

gpSecRsp_status_t gpKeyRecovery(uint8_t gpdfKeyType, uint8_t entryKeyType, uint8_t *entryKey,
				gpSecRsp_status_t status, uint8_t *pKeyType, uint8_t *pKey)
{
	if (gpdfKeyType != 0U) {
		if ((entryKeyType != GP_SEC_KEY_TYPE_OUT_OF_THE_BOX_GPD_KEY) &&
		    (entryKeyType != GP_SEC_KEY_TYPE_DERIVED_INDIVIDUAL_GPD_KEY)) {
			return GP_SEC_RSP_STATUS_DROP_FRAME;
		}

		if (ZB_IS_16BYTE_SECURITY_KEY_ZERO(entryKey)) {
			return GP_SEC_RSP_STATUS_DROP_FRAME;
		}

		memcpy(pKey, entryKey, SEC_KEY_LEN);
		*pKeyType = entryKeyType;
		return status;
	}

	switch (entryKeyType) {
	case GP_SEC_KEY_TYPE_NWK_KEY:
		if (!ZB_IS_16BYTE_SECURITY_KEY_ZERO(entryKey)) {
			memcpy(pKey, entryKey, SEC_KEY_LEN);
		} else if (zclGpAttr_gpSharedSecKeyType == GP_SEC_KEY_TYPE_NWK_KEY) {
			memcpy(pKey, zclGpAttr_gpSharedSecKey, SEC_KEY_LEN);
		} else if (!ZB_IS_16BYTE_SECURITY_KEY_ZERO(
				   ss_ib.nwkSecurMaterialSet
					   [ss_ib_active_secure_material_index_get()]
						   .key)) {
			memcpy(pKey,
			       ss_ib.nwkSecurMaterialSet[ss_ib_active_secure_material_index_get()]
				       .key,
			       SEC_KEY_LEN);
		} else {
			return GP_SEC_RSP_STATUS_DROP_FRAME;
		}

		*pKeyType = GP_SEC_KEY_TYPE_NWK_KEY;
		return status;
	case GP_SEC_KEY_TYPE_GPD_GROUP_KEY:
	case GP_SEC_KEY_TYPE_NWK_KEY_DERIVED_GPD_GROUP_KEY:
		if (!ZB_IS_16BYTE_SECURITY_KEY_ZERO(entryKey)) {
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

void gpDevAnnceAliasSend(uint16_t aliasNwkAddr)
{
	addrExt_t ieeeAddr;

	ZB_IEEE_ADDR_INVALID(ieeeAddr);
	zdo_devAnnce(aliasNwkAddr, ieeeAddr, 0);
}
#endif
