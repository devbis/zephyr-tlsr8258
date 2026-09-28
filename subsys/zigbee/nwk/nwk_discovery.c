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
	       payload->protocol_version == ZB_PROTOCOL_VERSION &&
	       payload->txoffset[0] == NWK_BEACON_TX_OFFSET_UNKNOWN &&
	       payload->txoffset[1] == NWK_BEACON_TX_OFFSET_UNKNOWN &&
	       payload->txoffset[2] == NWK_BEACON_TX_OFFSET_UNKNOWN;
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
	g_zbNwkCtx.scanChannels = req->scanChannels;
	g_zbNwkCtx.scanDuration = req->scanDuration;
	((zb_mac_mlme_scan_req_t *)arg)->scanType = ACTIVE_SCAN;
	((zb_mac_mlme_scan_req_t *)arg)->scanChannels = g_zbNwkCtx.scanChannels;
	((zb_mac_mlme_scan_req_t *)arg)->scanDuration = g_zbNwkCtx.scanDuration;
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
	bool addEntry = FALSE;

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

		if ((ind->panDesc.superframeSpec & NWK_SUPERFRAME_ASSOC_PERMIT) != 0U) {
			addEntry = TRUE;
		}
	} else if (state == NLME_STATE_FORMATION) {
		addEntry = TRUE;
	} else if (state == NLME_STATE_REJOIN) {
		if (ZB_EXTPANID_CMP(payload->extended_panid, g_zbInfo.nwkNib.extPANId)) {
			addEntry = TRUE;
		}
	}

	if (!addEntry) {
#if defined(ZB_ROUTER_ROLE) || defined(ZB_COORDINATOR_ROLE)
		u16 panId = ind->panDesc.coordPanId;

		if (g_zbNwkCtx.joined == 0U || g_zbNwkCtx.joined_pro != 0U) {
			zb_buf_free((zb_buf_t *)arg);
			return;
		}

		if (g_zbNwkCtx.known_panids_cnt < PANID_TABLE_SIZE) {
			for (u8 i = 0; i < PANID_TABLE_SIZE; i++) {
				if (g_zbNwkCtx.known_panids[i] == panId) {
					break;
				}
				if (g_zbNwkCtx.known_panids[i] == 0U) {
					g_zbNwkCtx.known_panids[i] = panId;
					g_zbNwkCtx.known_panids_cnt++;
					break;
				}
			}

			if (g_zbNwkCtx.panIdConflict != 0U) {
				zb_buf_free((zb_buf_t *)arg);
				return;
			}

			if (tl_zbNwkPanidConflictDetect(panId, payload->extended_panid)) {
				g_zbNwkCtx.panIdConflict = 1;
				g_sysDiags.panIdConflict++;
				tl_zbNwkPanidConflictProcess(arg);
				return;
			}
		}

		if (ind->panDesc.coordAddr.addrMode == ADDR_MODE_SHORT) {
			tl_zb_normal_neighbor_entry_t *nbr =
				nwk_neTblGetByShortAddr(ind->panDesc.coordAddr.addr.shortAddr);

			if (nbr != NULL && ind->panDesc.coordPanId == g_zbInfo.nwkNib.panId &&
			    ZB_EXTPANID_CMP(payload->extended_panid, g_zbInfo.nwkNib.extPANId)) {
				nbr->depth = payload->device_depth;
			}
		}
#endif
		zb_buf_free((zb_buf_t *)arg);
		return;
	}

	tl_zb_addition_neighbor_entry_t entry;
	memset(&entry, 0, sizeof(entry));

	entry.lqi = ind->panDesc.linkQuality;
	entry.logicChannel = ind->panDesc.logicalChannel;
	entry.depth = payload->device_depth;
	entry.beaconOrder = (u8)(ind->panDesc.superframeSpec & NWK_SUPERFRAME_BEACON_ORDER_MASK);
	entry.permitJoining =
		(u8)((ind->panDesc.superframeSpec >> NWK_SUPERFRAME_ASSOC_PERMIT_SHIFT) & 0x01U);
	entry.stackProfile = payload->stack_profile;
	entry.superframeOrder = (u8)((ind->panDesc.superframeSpec >> NWK_SUPERFRAME_ORDER_SHIFT) &
				     NWK_SUPERFRAME_BEACON_ORDER_MASK);
	entry.routerCapacity = payload->router_capacity;
	entry.edCapacity = payload->end_device_capacity;
	entry.nwkUpdateId = payload->nwk_update_id;

	/* Routers look for router capacity, end devices for end device capacity. */
#if defined(ZB_ROUTER_ROLE) || defined(ZB_COORDINATOR_ROLE)
	entry.potentialParent =
		(entry.stackProfile == g_zbNIB.stackProfile && entry.routerCapacity) ? 1 : 0;
#else
	entry.potentialParent =
		(entry.stackProfile == g_zbNIB.stackProfile && entry.edCapacity) ? 1 : 0;

	/* entry.shortAddr is still zero here, as in the vendor object. */
	if (state == NLME_STATE_REJOIN && g_zbInfo.macPib.coordShortAddress == entry.shortAddr) {
		entry.potentialParent = 1;
	}
#endif

	memcpy(entry.extPanId, payload->extended_panid, sizeof(entry.extPanId));
	entry.panId = ind->panDesc.coordPanId;
	entry.addrMode = ind->panDesc.coordAddr.addrMode;
	if (entry.addrMode == ADDR_MODE_SHORT) {
		memcpy(&entry.shortAddr, &ind->panDesc.coordAddr.addr.shortAddr,
		       sizeof(entry.shortAddr));
	} else if (entry.addrMode == ADDR_MODE_EXT) {
		memcpy(entry.extAddr, ind->panDesc.coordAddr.addr.extAddr, sizeof(entry.extAddr));
	}

	entry.deviceType = ((ind->panDesc.superframeSpec & NWK_SUPERFRAME_PAN_COORDINATOR) != 0U)
				   ? NWK_DEVICE_TYPE_COORDINATOR
				   : NWK_DEVICE_TYPE_ROUTER;

	(void)tl_zbAdditionNeighborTableUpdate(&entry);
	zb_buf_free((zb_buf_t *)arg);
}
