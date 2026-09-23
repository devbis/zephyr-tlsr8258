/*
 * Copyright 2026 libzigbee
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "zb_common.h"
#include "nwk_data.h"
#include "nwk_formation.h"
#include "nwk_nlme.h"
#include "nwk_routing.h"
#include "zdo_nwk_manager.h"
#include "second_clock.h"

#if defined(ZB_ROUTER_ROLE)
enum {
	NWK_FORMATION_ED_THRESHOLD = 60,
};

STATIC_ASSERT(__builtin_offsetof(nlme_nwkFormation_req_t, scanChannels) == 0);
STATIC_ASSERT(__builtin_offsetof(nlme_nwkFormation_req_t, distributedNwkAddr) == 4);
STATIC_ASSERT(__builtin_offsetof(nlme_nwkFormation_req_t, distributedNetwork) == 6);
STATIC_ASSERT(__builtin_offsetof(nlme_nwkFormation_req_t, batteryLifeExt) == 7);
STATIC_ASSERT(__builtin_offsetof(nlme_nwkFormation_req_t, scanDuration) == 8);
STATIC_ASSERT(__builtin_offsetof(nlme_nwkFormation_req_t, beaconOrder) == 9);
STATIC_ASSERT(__builtin_offsetof(nlme_nwkFormation_req_t, superframeOrder) == 10);

STATIC_ASSERT(__builtin_offsetof(zb_mac_mlme_scan_req_t, scanChannels) == 0);
STATIC_ASSERT(__builtin_offsetof(zb_mac_mlme_scan_req_t, scanType) == 4);
STATIC_ASSERT(__builtin_offsetof(zb_mac_mlme_scan_req_t, scanDuration) == 5);

STATIC_ASSERT(__builtin_offsetof(zb_mac_mlme_scan_conf_t, status) == 0);
STATIC_ASSERT(__builtin_offsetof(zb_mac_mlme_scan_conf_t, scanType) == 1);
STATIC_ASSERT(__builtin_offsetof(zb_mac_mlme_scan_conf_t, resultList) == 8);

STATIC_ASSERT(__builtin_offsetof(zb_mac_mlme_start_req_t, panId) == 4);
STATIC_ASSERT(__builtin_offsetof(zb_mac_mlme_start_req_t, logicalChannel) == 6);
STATIC_ASSERT(__builtin_offsetof(zb_mac_mlme_start_req_t, beaconOrder) == 8);
STATIC_ASSERT(__builtin_offsetof(zb_mac_mlme_start_req_t, superframeOrder) == 9);
STATIC_ASSERT(__builtin_offsetof(zb_mac_mlme_start_req_t, panCoordinator) == 10);
STATIC_ASSERT(__builtin_offsetof(zb_mac_mlme_start_req_t, batteryLifeExt) == 11);
STATIC_ASSERT(__builtin_offsetof(zb_mac_mlme_start_req_t, coordRealignment) == 23);

static inline u8 nwk_formationScanTypeGet(u32 scanChannels)
{
	return ((scanChannels & (scanChannels - 1U)) == 0U) ? ACTIVE_SCAN : ED_SCAN;
}

static u32 nwk_formationChannelMaskFilter(const zb_mac_mlme_scan_conf_t *cnf)
{
	u32 scanChannels = g_zbNwkCtx.scanChannels;

	memcpy(g_zbNwkCtx.formationInfo.energy_detect, cnf->resultList.energyDetectList,
	       sizeof(g_zbNwkCtx.formationInfo.energy_detect));

	for (u8 i = 0; i < TL_ZB_MAC_CHANNEL_NUM; i++) {
		u32 channelMask = 1UL << (TL_ZB_MAC_CHANNEL_START + i);

		if ((scanChannels & channelMask) != 0U &&
		    cnf->resultList.energyDetectList[i] > NWK_FORMATION_ED_THRESHOLD) {
			scanChannels ^= channelMask;
		}
	}

	return scanChannels;
}

static void nwk_formationChannelPanCntUpdate(void)
{
	u8 entryNum = tl_zbAdditionNeighborTableNumGet();

	for (u8 i = 0; i < entryNum; i++) {
		tl_zb_addition_neighbor_entry_t *entry = tl_zbAdditionNeighborEntryGetFromIdx(i);

		if (entry == NULL || entry->logicChannel < TL_ZB_MAC_CHANNEL_START ||
		    entry->logicChannel > TL_ZB_MAC_CHANNEL_STOP) {
			continue;
		}

		g_zbNwkCtx.formationInfo
			.channel_pan_count[entry->logicChannel - TL_ZB_MAC_CHANNEL_START]++;
	}
}

static u16 nwk_formationPanIdChoose(u8 selectedChannel, u16 panId)
{
retry:
	if (panId == 0xffffU) {
		panId = (u16)drv_u32Rand();
		goto retry;
	}

	{
		u8 entryNum = tl_zbAdditionNeighborTableNumGet();

		for (u8 i = 0; i < entryNum; i++) {
			tl_zb_addition_neighbor_entry_t *entry =
				tl_zbAdditionNeighborEntryGetFromIdx(i);

			if (entry == NULL || entry->logicChannel != selectedChannel ||
			    entry->panId != panId) {
				continue;
			}

			panId = (u16)(panId + 1U);
			goto retry;
		}
	}

	return panId;
}

static u8 nwk_formationChannelChoose(u32 scanChannels, u16 *panId)
{
	u8 selectedChannel = 0;
	u8 bestPanCount = 0xff;
	u8 bestEnergy = 0xff;

	for (u8 channel = TL_ZB_MAC_CHANNEL_START; channel <= TL_ZB_MAC_CHANNEL_STOP; channel++) {
		u32 channelMask = 1UL << channel;
		u8 panCount;
		u8 energy;

		if ((scanChannels & channelMask) == 0U) {
			continue;
		}

		panCount = g_zbNwkCtx.formationInfo
				   .channel_pan_count[channel - TL_ZB_MAC_CHANNEL_START];
		if (panCount > bestPanCount) {
			continue;
		}

		energy = g_zbNwkCtx.formationInfo.energy_detect[channel - TL_ZB_MAC_CHANNEL_START];
		if (panCount != bestPanCount) {
			bestPanCount = panCount;
			bestEnergy = energy;
			selectedChannel = channel;

			if (panCount == 0U) {
				if (*panId == 0xffffU) {
					u16 oldPanId = *panId;

					do {
						*panId = (u16)drv_u32Rand();
					} while (*panId == oldPanId);
				}

				return selectedChannel;
			}

			continue;
		}

		if (energy <= bestEnergy) {
			bestEnergy = energy;
			selectedChannel = channel;
		}
	}

	return selectedChannel;
}

void tl_zbNwkBeaconPayloadUpdate(void)
{
	zb_mac_beacon_payload_t *payload = &g_zbMacPib.beaconPayload;

	memset(payload, 0, sizeof(*payload));
	payload->stack_profile = g_zbNIB.stackProfile & 0x0fU;
	payload->protocol_version = ZB_PROTOCOL_VERSION;
	payload->device_depth = g_zbNIB.depth & 0x0fU;
	ZB_EXTPANID_COPY(payload->extended_panid, g_zbNIB.extPANId);
	memset(payload->txoffset, 0xff, sizeof(payload->txoffset));
	payload->nwk_update_id = g_zbNIB.updateId;
	payload->router_capacity = 1;
	payload->end_device_capacity = 1;

	if (tl_zbNeighborTableChildEDNumGet() >= TL_ZB_CHILD_TABLE_SIZE) {
		payload->end_device_capacity = 0;
	}

	/* Vendor uses different effective address-map limits by role: the
	 * coordinator clears both capacity flags above 63 entries, while the
	 * router does so above 127 entries. */
#if defined(ZB_COORDINATOR_ROLE)
	if (g_nwkAddrMap.validNum > 63U) {
#else
	if (g_nwkAddrMap.validNum > 127U) {
#endif
		payload->router_capacity = 0;
		payload->end_device_capacity = 0;
	}

	if (g_secondCnt >= LONG_UPTIME_THRESHOLD) {
		payload->long_uptime = 1;
	}

	if (nwkRoutingTabEntryDstActiveGet(g_zbNIB.managerAddr) != NULL ||
	    nwkValidNeighborToFwd(g_zbNIB.managerAddr) != NULL) {
		payload->tc_connectivity = 1;
	}
}

void nwk_nlmeNwkFormationCnf(void *arg, u8 status)
{
	((nlme_nwkFormation_cnf_t *)arg)->status = status;
	g_zbNwkCtx.state = NLME_STATE_IDLE;
	tl_zbTaskPost(zdo_network_formation_confirm, arg);
}

void tl_zbNwkNlmeNetworkFormationRequestHandler(void *arg)
{
	nlme_nwkFormation_req_t *req = (nlme_nwkFormation_req_t *)arg;
	zb_mac_mlme_scan_req_t *scanReq = (zb_mac_mlme_scan_req_t *)arg;

	if (g_zbNwkCtx.state != NLME_STATE_IDLE || g_zbNwkCtx.joined) {
		nwk_nlmeNwkFormationCnf(arg, NWK_STATUS_INVALID_REQUEST);
		return;
	}

	g_zbNwkCtx.state = NLME_STATE_FORMATION;
	g_zbNwkCtx.scanChannels = req->scanChannels;
	g_zbNwkCtx.scanDuration = req->scanDuration;

	if (req->distributedNetwork) {
		g_zbNIB.nwkAddr = req->distributedNwkAddr;
		g_zbMacPib.shortAddress = req->distributedNwkAddr;
	} else {
		g_zbNIB.nwkAddr = 0;
		g_zbMacPib.shortAddress = 0;
	}

	scanReq->scanChannels = req->scanChannels;
	scanReq->scanType = nwk_formationScanTypeGet(req->scanChannels);
	scanReq->scanDuration = g_zbNwkCtx.scanDuration;
	tl_zbPrimitivePost(TL_Q_NWK2MAC, MAC_MLME_SCAN_REQ, arg);
}

void nwk_formationScanCnfHandler(void *arg)
{
	zb_mac_mlme_scan_conf_t *cnf = (zb_mac_mlme_scan_conf_t *)arg;

	if (cnf->scanType == ED_SCAN) {
		u32 scanChannels;
		zb_mac_mlme_scan_req_t *scanReq = (zb_mac_mlme_scan_req_t *)arg;

		if (cnf->status != MAC_SUCCESS) {
			nwk_nlmeNwkFormationCnf(arg, NWK_STATUS_STARTUP_FAILURE);
			return;
		}

		scanChannels = nwk_formationChannelMaskFilter(cnf);
		g_zbNwkCtx.scanChannels = scanChannels;
		if (scanChannels == 0U) {
			nwk_nlmeNwkFormationCnf(arg, NWK_STATUS_STARTUP_FAILURE);
			return;
		}

		scanReq->scanChannels = scanChannels;
		scanReq->scanType = ACTIVE_SCAN;
		scanReq->scanDuration = g_zbNwkCtx.scanDuration;
		tl_zbPrimitivePost(TL_Q_NWK2MAC, MAC_MLME_SCAN_REQ, arg);
		return;
	}

	if (cnf->scanType != ACTIVE_SCAN) {
		zb_buf_free((zb_buf_t *)arg);
		return;
	}

	if (cnf->status != MAC_SUCCESS && cnf->status != MAC_STA_NO_BEACON) {
		nwk_nlmeNwkFormationCnf(arg, NWK_STATUS_STARTUP_FAILURE);
		return;
	}

	nwk_formationChannelPanCntUpdate();

	{
		u16 panId = g_zbNIB.panId;
		u8 selectedChannel = nwk_formationChannelChoose(g_zbNwkCtx.scanChannels, &panId);
		zb_mac_mlme_start_req_t *startReq = (zb_mac_mlme_start_req_t *)arg;

		if (selectedChannel < TL_ZB_MAC_CHANNEL_START ||
		    selectedChannel > TL_ZB_MAC_CHANNEL_STOP) {
			nwk_nlmeNwkFormationCnf(arg, NWK_STATUS_STARTUP_FAILURE);
			return;
		}

		if (g_zbNwkCtx.formationInfo
			    .channel_pan_count[selectedChannel - TL_ZB_MAC_CHANNEL_START] != 0U) {
			panId = nwk_formationPanIdChoose(selectedChannel, panId);
		}

		startReq->panId = panId;
		startReq->logicalChannel = selectedChannel;
		startReq->channelPage = 0;
		startReq->beaconOrder = 15;
		startReq->superframeOrder = 15;
		startReq->panCoordinator = 0;
		startReq->batteryLifeExt = 0;
		startReq->coordRealignment = 0;
		tl_zbPrimitivePost(TL_Q_NWK2MAC, MAC_MLME_START_REQ, arg);
	}
}

void nwk_formationStartCnfHandler(void *arg)
{
	mac_mlme_startCnf_t *cnf = (mac_mlme_startCnf_t *)arg;

	if (cnf->status == MAC_SUCCESS) {
		u16 selfRef = 0;

		g_zbMacPib.rxOnWhenIdle = g_zbNIB.capabilityInfo.rcvOnWhenIdle;
		g_zbNIB.panId = g_zbMacPib.panId;
		ZB_IEEE_ADDR_COPY(g_zbNIB.ieeeAddr, g_zbMacPib.extAddress);
		g_zbMacPib.shortAddress = g_zbNIB.nwkAddr;

		if (ZB_EXTPANID_IS_ZERO(g_zbNIB.extPANId)) {
			ZB_EXTPANID_COPY(g_zbNIB.extPANId, g_zbMacPib.extAddress);
		}

		(void)tl_zbNwkAddrMapAdd(g_zbNIB.nwkAddr, g_zbMacPib.extAddress, &selfRef);
		tl_zbNwkBeaconPayloadUpdate();
	}

	nwk_nlmeNwkFormationCnf(arg, cnf->status);
}

#endif
