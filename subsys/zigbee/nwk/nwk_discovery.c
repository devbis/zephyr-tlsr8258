/* SPDX-License-Identifier: Apache-2.0 */
/*
 * NLME-NETWORK-DISCOVERY request + beacon-notify indication handler.
 *
 * Adapted from libzigbee/src/nwk_discovery.c (~135 LOC). Vendor file
 * is kept structurally one-for-one; only the include layout changes
 * (vendor zb_local.h → zb_common_stub.h + nwk/includes + mac/includes).
 *
 * Active for both ED and router roles in the libzigbee source — the
 * #if !defined(ZB_COORDINATOR_ROLE) guard is implicit in the way the
 * vendor file is included into the libzigbee build. Keep the same
 * shape here: only the network-formation-state branches actually
 * exercise the table updates that need router-only globals, so this
 * TU is safe to compile in both roles. ZB_ROUTER_ROLE gating is
 * applied solely around tl_zbAdditionNeighborTableUpdate, since the
 * addition-neighbor table itself only exists when the router build
 * pulls in zb_config.c's storage.
 */

#include "zb_common_stub.h"
#include "mac/includes/tl_zb_mac.h"
#include "nwk/includes/nwk.h"
#include "nwk/includes/nwk_neighbor.h"
#include "nwk/includes/nwk_internal.h"

#include <string.h>

extern void zdo_nlme_network_discovery_confirm_cb(void *arg);

enum {
	NWK_CTX_STATE_OFFSET = 47,
	NWK_CTX_DISC_MASK = 0x0f,
	NWK_CTX_DISC_STATE = 0x30,
};

static bool nwk_beacon_payload_is_valid(const zb_mac_beacon_payload_t *payload)
{
	return payload != NULL &&
	       payload->protocol_id == 0U &&
	       payload->protocol_version == ZB_PROTOCOL_VERSION &&
	       payload->txoffset[0] == 0xffU &&
	       payload->txoffset[1] == 0xffU &&
	       payload->txoffset[2] == 0xffU;
}

void nwk_nlmeNwkDiscCnf(void *arg, u8 status)
{
	((u8 *)arg)[0x80] = status;
	g_zbNwkCtx.state = NLME_STATE_IDLE;
	tl_zbTaskPost(zdo_nlme_network_discovery_confirm_cb, arg);
}

void tl_zbNwkNlmeNwkDiscRequestHandler(void *arg)
{
	nlme_nwkDisc_req_t *req = (nlme_nwkDisc_req_t *)arg;

	if (g_zbNwkCtx.state != NLME_STATE_IDLE) {
		nwk_nlmeNwkDiscCnf(arg, 0xc2);
		return;
	}

	if ((req->scanChannels & ZB_TRANSCEIVER_ALL_CHANNELS_MASK) == 0U) {
		nwk_nlmeNwkDiscCnf(arg, 0xc1);
		return;
	}

	g_zbNwkCtx.state = NLME_STATE_DISC;
	((u8 *)arg)[4] = 1;
	((u8 *)arg)[5] = req->scanDuration;
	tl_zbPrimitivePost(TL_Q_NWK2MAC, MAC_MLME_SCAN_REQ, arg);
}

void nwk_discoveryScanCnfHandler(void *arg)
{
	if (((u8 *)arg)[1] != 1U) {
		zb_buf_free((zb_buf_t *)arg);
		return;
	}

	nwk_nlmeNwkDiscCnf(arg, ((u8 *)arg)[0]);
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
	} else if (state != NLME_STATE_FORMATION) {
		zb_buf_free((zb_buf_t *)arg);
		return;
	}

#if defined(ZB_ROUTER_ROLE) && ZB_ROUTER_ROLE
	{
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
			memcpy(entry.extAddr, ind->panDesc.coordAddr.addr.extAddr,
			       sizeof(entry.extAddr));
		} else {
			entry.shortAddr = ind->panDesc.coordAddr.addr.shortAddr;
		}

		entry.potentialParent = 0;
		if (entry.stackProfile == g_zbNIB.stackProfile && entry.edCapacity) {
			entry.potentialParent = 1;
		}

		if (state == NLME_STATE_REJOIN &&
		    g_zbInfo.macPib.coordShortAddress ==
			    ind->panDesc.coordAddr.addr.shortAddr) {
			entry.potentialParent = 1;
		}

		entry.potentialParent = entry.permitJoining && entry.potentialParent;
		entry.deviceType = ((ind->panDesc.superframeSpec & 0x4000U) != 0U)
					   ? NWK_DEVICE_TYPE_COORDINATOR
					   : NWK_DEVICE_TYPE_ROUTER;

		(void)tl_zbAdditionNeighborTableUpdate(&entry);
	}
#endif
	zb_buf_free((zb_buf_t *)arg);
}
