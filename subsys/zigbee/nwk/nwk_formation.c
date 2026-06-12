/* SPDX-License-Identifier: Apache-2.0 */
/*
 * NLME-NETWORK-FORMATION request/scan/start state machine.
 *
 * Adapted from libzigbee/src/nwk_formation.c (~310 LOC). Kept
 * structurally one-for-one with the vendor. Adaptations:
 *
 *   * vendor "zb_local.h" → zb_common_stub.h + nwk/includes + mac/includes
 *   * STATIC_ASSERTs preserved
 *   * tl_zbPrimitivePost() is provided by
 *     platform/zephyr/zb_primitive_dispatch.c as a logging stub until
 *     the MAC MLME-SCAN / MLME-START layers are wired. Once those
 *     primitives have real handlers, the state machine below drives
 *     a real channel/PAN scan + start sequence.
 *   * Externs g_secondCnt / g_zero_addr / ZB_PROTOCOL_VERSION are
 *     declared in zb_common_stub.h (the Zephyr aggregator).
 *
 * Until the MAC primitives are real, calling
 * tl_zbNwkNlmeNetworkFormationRequestHandler() posts a SCAN_REQ that
 * is logged and dropped — formation never confirms. The static
 * formation path in nwk_router_minimal.c remains the working router
 * commissioning route.
 */

#include "zb_common_stub.h"
#include "common/static_assert.h"
#include "mac/includes/tl_zb_mac.h"
#include "nwk/includes/nwk.h"
#include "nwk/includes/nwk_addr_map.h"
#include "nwk/includes/nwk_neighbor.h"
#include "nwk/includes/nwk_nib.h"

#include <string.h>

#if defined(ZB_ROUTER_ROLE) && ZB_ROUTER_ROLE

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

extern void zdo_network_formation_confirm(void *arg);
extern nwk_routingTabEntry_t *nwkRoutingTabEntryDstActiveGet(u16 dstAddr);
extern tl_zb_normal_neighbor_entry_t *nwkValidNeighborToFwd(u16 shortAddr);
extern void zb_buf_free(zb_buf_t *buf);

static inline u8 nwk_formationScanTypeGet(u32 scanChannels)
{
	return ((scanChannels & (scanChannels - 1U)) == 0U) ? ACTIVE_SCAN : ED_SCAN;
}

static u32 nwk_formationChannelMaskFilter(const zb_mac_mlme_scan_conf_t *cnf)
{
	u32 scanChannels = g_zbNwkCtx.scanChannels;

	memcpy(g_zbNwkCtx.formationInfo.energy_detect,
	       cnf->resultList.energyDetectList,
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

		if (entry == NULL ||
		    entry->logicChannel < TL_ZB_MAC_CHANNEL_START ||
		    entry->logicChannel > TL_ZB_MAC_CHANNEL_STOP) {
			continue;
		}

		g_zbNwkCtx.formationInfo.channel_pan_count
			[entry->logicChannel - TL_ZB_MAC_CHANNEL_START]++;
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

	for (u8 channel = TL_ZB_MAC_CHANNEL_START;
	     channel <= TL_ZB_MAC_CHANNEL_STOP; channel++) {
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

		energy = g_zbNwkCtx.formationInfo
				 .energy_detect[channel - TL_ZB_MAC_CHANNEL_START];
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
	memcpy(payload->extended_panid, g_zbNIB.extPANId, EXT_ADDR_LEN);
	memset(payload->txoffset, 0xff, sizeof(payload->txoffset));
	payload->nwk_update_id = g_zbNIB.updateId;
	payload->router_capacity = 1;
	payload->end_device_capacity = 1;

	if (tl_zbNeighborTableChildEDNumGet() >= TL_ZB_CHILD_TABLE_SIZE) {
		payload->end_device_capacity = 0;
	}

	if (g_nwkAddrMap.validNum >= TL_ZB_NWK_ADDR_MAP_NUM) {
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
	((u8 *)arg)[0] = status;
	g_zbNwkCtx.state = NLME_STATE_IDLE;
	tl_zbTaskPost(zdo_network_formation_confirm, arg);
}

void tl_zbNwkNlmeNetworkFormationRequestHandler(void *arg)
{
	nlme_nwkFormation_req_t *req = (nlme_nwkFormation_req_t *)arg;
	u8 *raw = (u8 *)arg;

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

	COPY_U32TOBUFFER(raw, req->scanChannels);
	raw[4] = nwk_formationScanTypeGet(req->scanChannels);
	raw[5] = g_zbNwkCtx.scanDuration;
	tl_zbPrimitivePost(TL_Q_NWK2MAC, MAC_MLME_SCAN_REQ, arg);
}

void nwk_formationScanCnfHandler(void *arg)
{
	zb_mac_mlme_scan_conf_t *cnf = (zb_mac_mlme_scan_conf_t *)arg;

	if (cnf->scanType == ED_SCAN) {
		u32 scanChannels;
		u8 *raw = (u8 *)arg;

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

		COPY_U32TOBUFFER(raw, scanChannels);
		raw[4] = ACTIVE_SCAN;
		raw[5] = g_zbNwkCtx.scanDuration;
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
		u8 selectedChannel =
			nwk_formationChannelChoose(g_zbNwkCtx.scanChannels, &panId);
		u8 *raw = (u8 *)arg;

		if (selectedChannel < TL_ZB_MAC_CHANNEL_START ||
		    selectedChannel > TL_ZB_MAC_CHANNEL_STOP) {
			nwk_nlmeNwkFormationCnf(arg, NWK_STATUS_STARTUP_FAILURE);
			return;
		}

		if (g_zbNwkCtx.formationInfo
			    .channel_pan_count[selectedChannel - TL_ZB_MAC_CHANNEL_START] != 0U) {
			panId = nwk_formationPanIdChoose(selectedChannel, panId);
		}

		COPY_U16TOBUFFER(raw + 4, panId);
		raw[6] = selectedChannel;
		raw[7] = 0;
		raw[8] = 15;
		raw[9] = 15;
		raw[10] = 0;
		raw[11] = 0;
		raw[23] = 0;
		tl_zbPrimitivePost(TL_Q_NWK2MAC, MAC_MLME_START_REQ, arg);
	}
}

void nwk_formationStartCnfHandler(void *arg)
{
	if (((u8 *)arg)[0] == MAC_SUCCESS) {
		u16 selfRef = 0;

		g_zbMacPib.rxOnWhenIdle = g_zbNIB.capabilityInfo.rcvOnWhenIdle;
		g_zbNIB.panId = g_zbMacPib.panId;
		memcpy(g_zbNIB.ieeeAddr, g_zbMacPib.extAddress, EXT_ADDR_LEN);
		g_zbMacPib.shortAddress = g_zbNIB.nwkAddr;

		if (memcmp(g_zbNIB.extPANId, g_zero_addr, EXT_ADDR_LEN) == 0) {
			memcpy(g_zbNIB.extPANId, g_zbMacPib.extAddress, EXT_ADDR_LEN);
		}

		(void)tl_zbNwkAddrMapAdd(g_zbNIB.nwkAddr, g_zbMacPib.extAddress, &selfRef);
		tl_zbNwkBeaconPayloadUpdate();
	}

	nwk_nlmeNwkFormationCnf(arg, ((u8 *)arg)[0]);
}

#endif /* ZB_ROUTER_ROLE */
