/*
 * Copyright 2026 libzigbee
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "zb_common.h"
#include "nwk_discovery.h"
#include "nwk_panid_conflict.h"
#include "zdo_nwk_manager.h"

enum {
	NWK_CTX_DISC_MASK = 0x0f,
	NWK_CTX_DISC_STATE = 0x30,
};

static bool nwk_beacon_payload_is_valid(const zb_mac_beacon_payload_t *payload)
{
	return payload != NULL && payload->protocol_id == 0U &&
	       payload->protocol_version == ZB_PROTOCOL_VERSION && payload->txoffset[0] == 0xffU &&
	       payload->txoffset[1] == 0xffU && payload->txoffset[2] == 0xffU;
}

static void nwk_nlmeNwkDiscCnf(void *arg, u8 status)
{
	((nlme_nwkDisc_cnf_t *)arg)->status = status;
	g_zbNwkCtx.state = NLME_STATE_IDLE;
	tl_zbTaskPost(zdo_nlme_network_discovery_confirm_cb, arg);
}

void tl_zbNwkNlmeNwkDiscRequestHandler(void *arg)
{
	nlme_nwkDisc_req_t *req = (nlme_nwkDisc_req_t *)arg;
	if (g_zbNwkCtx.state != NLME_STATE_IDLE) {
		nwk_nlmeNwkDiscCnf(arg, NWK_STATUS_INVALID_REQUEST);
		return;
	}

	if ((req->scanChannels & ZB_TRANSCEIVER_ALL_CHANNELS_MASK) == 0U) {
		nwk_nlmeNwkDiscCnf(arg, NWK_STATUS_INVALID_PARAMETER);
		return;
	}

	g_zbNwkCtx.state = NLME_STATE_DISC;
	((zb_mac_mlme_scan_req_t *)arg)->scanType = ACTIVE_SCAN;
	((zb_mac_mlme_scan_req_t *)arg)->scanDuration = req->scanDuration;
	tl_zbPrimitivePost(TL_Q_NWK2MAC, MAC_MLME_SCAN_REQ, arg);
}

void nwk_discoveryScanCnfHandler(void *arg)
{
	zb_mac_mlme_scan_conf_t *scanCnf = (zb_mac_mlme_scan_conf_t *)arg;

	if (scanCnf->scanType != ACTIVE_SCAN) {
		zb_buf_free((zb_buf_t *)arg);
		return;
	}

	nwk_nlmeNwkDiscCnf(arg, scanCnf->status);
}

void tl_zbMacMlmeBeaconNotifyIndicationHandler(void *arg)
{
	zb_mlme_beacon_notify_ind_t *ind = (zb_mlme_beacon_notify_ind_t *)arg;
	zb_mac_beacon_payload_t *payload = (zb_mac_beacon_payload_t *)ind->psdu;
	nlme_state_t state = g_zbNwkCtx.state;

	if (!nwk_beacon_payload_is_valid(payload)) {
		zb_buf_free((zb_buf_t *)arg);
		return;
	}

	if (state == NLME_STATE_DISC) {
		if (TL_ZB_ASSOCJOIN_PERMIT_PANID != MAC_INVALID_PANID &&
		    TL_ZB_ASSOCJOIN_PERMIT_PANID != ind->panDesc.coordPanId) {
			zb_buf_free((zb_buf_t *)arg);
			return;
		}

		if (TL_ZB_ASSOCJOIN_FILTER_PANID != MAC_INVALID_PANID &&
		    TL_ZB_ASSOCJOIN_FILTER_PANID == ind->panDesc.coordPanId) {
			zb_buf_free((zb_buf_t *)arg);
			return;
		}

		if ((((u8)(ind->panDesc.superframeSpec >> 8)) & 0x7fU) == 0U) {
			zb_buf_free((zb_buf_t *)arg);
			return;
		}
	} else if (state == NLME_STATE_REJOIN) {
		if (!ZB_EXTPANID_CMP(payload->extended_panid, g_zbInfo.nwkNib.extPANId)) {
			zb_buf_free((zb_buf_t *)arg);
			return;
		}
	}
#if defined(ZB_ROUTER_ROLE) || defined(ZB_COORDINATOR_ROLE)
	else if (state != NLME_STATE_FORMATION && state != NLME_STATE_IDLE &&
		 state != NLME_STATE_ROUTER_START) {
		zb_buf_free((zb_buf_t *)arg);
		return;
	}

	if (state == NLME_STATE_IDLE || state == NLME_STATE_ROUTER_START) {
		if (g_zbNwkCtx.joined == 0U || g_zbNwkCtx.joined_pro != 0U) {
			zb_buf_free((zb_buf_t *)arg);
			return;
		}

		if (tl_zbNwkPanidConflictDetect(ind->panDesc.coordPanId, payload->extended_panid)) {
			g_sysDiags.panIdConflict++;
			tl_zbNwkPanidConflictProcess(arg);
			return;
		}

		if (ind->panDesc.coordAddr.addrMode == ADDR_MODE_SHORT &&
		    nwk_neTblGetByShortAddr(ind->panDesc.coordAddr.addr.shortAddr) == NULL) {
			zb_buf_free((zb_buf_t *)arg);
			return;
		}
	}
#else
	else if (state != NLME_STATE_FORMATION && state != NLME_STATE_DISC &&
		 state != NLME_STATE_REJOIN) {
		zb_buf_free((zb_buf_t *)arg);
		return;
	}
#endif

	tl_zb_addition_neighbor_entry_t entry;
	memset(&entry, 0, sizeof(entry));

	memcpy(entry.extPanId, payload->extended_panid, sizeof(entry.extPanId));
	entry.panId = ind->panDesc.coordPanId;
	entry.addrMode = ind->panDesc.coordAddr.addrMode;
	entry.logicChannel = ind->panDesc.logicalChannel;
	entry.depth = payload->device_depth;
	entry.beaconOrder = (u8)(ind->panDesc.superframeSpec & 0x0fU);
	entry.permitJoining = (u8)((ind->panDesc.superframeSpec >> 15) & 0x01U);
	entry.routerCapacity = payload->router_capacity;
	entry.edCapacity = payload->end_device_capacity;
	entry.stackProfile = payload->stack_profile;
	entry.superframeOrder = (u8)((ind->panDesc.superframeSpec >> 4) & 0x0fU);
	entry.lqi = ind->panDesc.linkQuality;
	entry.nwkUpdateId = payload->nwk_update_id;

	if (entry.addrMode == ADDR_MODE_EXT) {
		memcpy(entry.extAddr, ind->panDesc.coordAddr.addr.extAddr, sizeof(entry.extAddr));
	} else {
		entry.shortAddr = ind->panDesc.coordAddr.addr.shortAddr;
	}

	entry.potentialParent = 0;
	if (entry.stackProfile == g_zbNIB.stackProfile && entry.edCapacity) {
		entry.potentialParent = 1;
	}

	if (state == NLME_STATE_REJOIN &&
	    g_zbInfo.macPib.coordShortAddress == ind->panDesc.coordAddr.addr.shortAddr) {
		entry.potentialParent = 1;
	}

	entry.potentialParent = entry.permitJoining && entry.potentialParent;
	entry.deviceType = ((ind->panDesc.superframeSpec & 0x4000U) != 0U)
				   ? NWK_DEVICE_TYPE_COORDINATOR
				   : NWK_DEVICE_TYPE_ROUTER;

	(void)tl_zbAdditionNeighborTableUpdate(&entry);
	zb_buf_free((zb_buf_t *)arg);
}
