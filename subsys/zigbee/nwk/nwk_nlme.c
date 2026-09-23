/*
 * Copyright 2026 libzigbee
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "zb_common.h"
#include "nwk_data.h"
#include "zb_nwk_core.h"
#include "nwk_formation.h"
#include "nwk_join.h"
#include "nwk_nlme.h"
#include "nwk_permit_joining.h"
#include "nwk_leave.h"
#include "zdo_nwk_manager.h"
#include "ev_timer.h"

enum {
	UNKNOWN_ENDDEV_ADDR_INIT = 0xfffe,
};

static u16 unknownEndDevAddr_8570 __asm__("unknowEndDevAddr.8570") = UNKNOWN_ENDDEV_ADDR_INIT;

#if defined(ZB_ROUTER_ROLE)
void nwk_nlmeStartRouterCnf(void *arg, u8 status)
{
	((nlme_startRouter_cnf_t *)arg)->status = status;
	g_zbNwkCtx.state = NLME_STATE_IDLE;
	tl_zbTaskPost(zdo_nlme_start_router_confirm, arg);
}

void tl_zbNwkNlmeStartRouterRequestHandler(void *arg)
{
	nlme_startRouter_req_t *req = (nlme_startRouter_req_t *)arg;
	zb_mac_mlme_start_req_t *startReq = (zb_mac_mlme_start_req_t *)arg;
	u8 stateFlags = (u8)(g_zbNwkCtx.user_state << 4);

	if (g_zbNwkCtx.state != NLME_STATE_IDLE) {
		nwk_nlmeStartRouterCnf(arg, NWK_STATUS_INVALID_REQUEST);
		return;
	}

	g_zbNwkCtx.state = NLME_STATE_ROUTER_START;
	startReq->panId = g_zbNIB.panId;
	startReq->logicalChannel = g_zbMacPib.phyChannelCur;
	startReq->channelPage = stateFlags;
	startReq->beaconOrder = req->beaconOrder;
	startReq->superframeOrder = req->superframeOrder;
#if defined(ZB_COORDINATOR_ROLE)
	startReq->panCoordinator = 1;
#else
	startReq->panCoordinator = 0;
#endif
	startReq->batteryLifeExt = req->batteryLifeExt;
	startReq->coordRealignment = stateFlags;
	tl_zbPrimitivePost(TL_Q_NWK2MAC, MAC_MLME_START_REQ, arg);
}

void nwk_startRouterCnfHandler(void *arg)
{
	u8 status = ((mac_mlme_startCnf_t *)arg)->status;

	if (status == MAC_SUCCESS) {
		u16 addrRef = 0;

		g_zbMacPib.rxOnWhenIdle = g_zbNIB.capabilityInfo.rcvOnWhenIdle;
		g_zbNIB.panId = g_zbMacPib.panId;
		ZB_IEEE_ADDR_COPY(g_zbNIB.ieeeAddr, g_zbMacPib.extAddress);
		g_zbMacPib.shortAddress = g_zbNIB.nwkAddr;

		if (ZB_EXTPANID_IS_ZERO(g_zbNIB.extPANId)) {
			ZB_EXTPANID_COPY(g_zbNIB.extPANId, g_zbMacPib.extAddress);
		}

		(void)tl_zbNwkAddrMapAdd(g_zbNIB.nwkAddr, g_zbMacPib.extAddress, &addrRef);
		tl_zbNwkBeaconPayloadUpdate();
	}

	nwk_nlmeStartRouterCnf(arg, status);
}
#endif

void tl_zbNwkNlmeEDScanRequestHandler(void *arg)
{
	nlme_edScan_req_t *req = (nlme_edScan_req_t *)arg;
	zb_mac_mlme_scan_req_t *scanReq = (zb_mac_mlme_scan_req_t *)arg;

	scanReq->scanType = ED_SCAN;
	scanReq->scanDuration = req->scanDuration;
	g_zbNwkCtx.state = NLME_STATE_ED_SCAN;

	tl_zbPrimitivePost(TL_Q_NWK2MAC, MAC_MLME_SCAN_REQ, arg);
}

void nwk_edScanCnfHandler(void *arg)
{
	g_zbNwkCtx.state = NLME_STATE_IDLE;
	tl_zbTaskPost(zdo_nlme_ed_scan_confirm, arg);
}

void tl_zbNwkNlmeResetRequestHandler(void *arg)
{
	nlme_reset_req_t *req = (nlme_reset_req_t *)arg;
	u8 state = g_zbNwkCtx.state;

	if (state != NLME_STATE_IDLE || g_zbNwkCtx.joined) {
		req->warmStart = 0;
		tl_zbTaskPost(zdo_reset_confirm_cb, arg);
		return;
	}

	if (req->warmStart) {
		tl_zbAdditionNeighborReset();
		req->warmStart = 4;
		tl_zbTaskPost(zdo_reset_confirm_cb, arg);
		return;
	}

	req->warmStart = 1;
	tl_zbPrimitivePost(TL_Q_NWK2MAC, MAC_MLME_RESET_REQ, arg);
}

void tl_zbMacMlmeResetConfirmHandler(void *arg)
{
	mac_mlme_reset_conf_t *cnf = (mac_mlme_reset_conf_t *)arg;
	u8 status = cnf->status;

	tl_zbNwkNibInit(1);
	tl_zbNeighborTableInit();
	memset(&g_zbNwkCtx, 0, 0x4f);
	g_zbNwkCtx.discoverRoute = 1;

	cnf->status = status;
	tl_zbTaskPost(zdo_reset_confirm_cb, arg);
}

#if !defined(ZB_ROUTER_ROLE)
void endDevMacDataPoll(void)
{
	tl_zb_normal_neighbor_entry_t *parent = tl_zbNeighborTableSearchForParent();

	if (parent == NULL) {
		return;
	}

	zb_buf_t *buf = zb_buf_allocate();

	if (buf == NULL) {
		return;
	}

	mac_mlme_poll_req_t *pollReq = (mac_mlme_poll_req_t *)buf->buf;

	pollReq->coordAddrMode = ADDR_MODE_SHORT;
	{
		u16 dst = tl_zbshortAddrByIdx(parent->addrmapIdx);

		pollReq->coordAddr.shortAddr = dst;
		pollReq->coordPanId = g_zbInfo.macPib.panId;
	}

	tl_zbPrimitivePost(TL_Q_NWK2MAC, MAC_MLME_POLL_REQ, buf);
}

void tl_zbNwkNlmeSyncRequestHandler(void *arg)
{
	tl_zb_normal_neighbor_entry_t *parent = tl_zbNeighborTableSearchForParent();
	nlme_sync_req_t *req = (nlme_sync_req_t *)arg;
	mac_mlme_poll_req_t *pollReq = (mac_mlme_poll_req_t *)arg;

	if (parent != NULL && req->track == 0U) {
		pollReq->coordAddrMode = ADDR_MODE_SHORT;

		{
			u16 dst = tl_zbshortAddrByIdx(parent->addrmapIdx);

			pollReq->coordAddr.shortAddr = dst;
			pollReq->coordPanId = g_zbInfo.macPib.panId;
		}

		tl_zbPrimitivePost(TL_Q_NWK2MAC, MAC_MLME_POLL_REQ, arg);
		return;
	}

	((nlme_sync_cnf_t *)arg)->status = NWK_STATUS_INVALID_PARAMETER;
	tl_zbTaskPost(zdo_nlme_sync_confirm, arg);
}
#endif

void tl_zbMacMlmeSyncLossIndicationHandler(void *arg)
{
	zb_buf_free((zb_buf_t *)arg);
}

void tl_zbNwkNlmeNwkStatusInd(void *arg, u16 nwkAddr, nwk_statusCode_t status)
{
	nlme_nwkStatus_ind_t *ind = (nlme_nwkStatus_ind_t *)arg;

	ind->nwkAddr = nwkAddr;
	ind->status = (u8)status;

	tl_zbTaskPost(zdo_nlme_status_indication, arg);
}

#if !defined(ZB_ROUTER_ROLE)
void tl_zbMacMlmePollConfirmHandler(void *arg)
{
	mac_mlme_poll_conf_t *cnf = (mac_mlme_poll_conf_t *)arg;
	u8 status = cnf->status;
	u8 state = g_zbNwkCtx.state;

	if (state == NLME_STATE_REJOIN) {
		zb_buf_free((zb_buf_t *)arg);
		return;
	}

	cnf->status = status;
	tl_zbTaskPost(zdo_nlme_sync_confirm, arg);

	{
		tl_zb_normal_neighbor_entry_t *parent = tl_zbNeighborTableSearchForParent();

		if (status == MAC_STA_NO_ACK) {
			if (parent != NULL) {
				zb_buf_t *buf = zb_buf_allocate();

				if (buf != NULL) {
					tl_zbNwkNlmeNwkStatusInd(
						buf, tl_zbshortAddrByIdx(parent->addrmapIdx),
						NWK_COMMAND_STATUS_PARENT_LINK_FAILURE);
				}
			}

			return;
		}

		if (parent != NULL) {
			parent->timeoutCnt = parent->devTimeout;
		}
	}
}
#endif

void tl_zbMacMlmePollIndicationHandler(void *arg)
{
	const mac_mlme_poll_ind_t *pollInd = (const mac_mlme_poll_ind_t *)arg;

	if (!g_zbNwkCtx.joined ||
	    (g_zbInfo.nwkNib.parentInfo & END_DEV_TIMEOUT_REQ_KEEPALIVE_BIT) == 0U) {
		zb_buf_free((zb_buf_t *)arg);
		return;
	}

	if (pollInd->addrMode != ZB_ADDR_16BIT_DEV_OR_BROADCAST) {
		zb_buf_free((zb_buf_t *)arg);
		return;
	}

	{
		u16 shortAddr = pollInd->devAddr.shortAddr;
		tl_zb_normal_neighbor_entry_t *entry = nwk_neTblGetByShortAddr(shortAddr);

		if (entry != NULL) {
			if (entry->devTimeout == 0U) {
				zb_buf_free((zb_buf_t *)arg);
				return;
			}

			entry->timeoutCnt = entry->devTimeout;
			entry->keepaliveRcvd = 1;
			zb_buf_free((zb_buf_t *)arg);
			return;
		}

		if (unknownEndDevAddr_8570 != UNKNOWN_ENDDEV_ADDR_INIT) {
			if (unknownEndDevAddr_8570 == shortAddr) {
				unknownEndDevAddr_8570 = UNKNOWN_ENDDEV_ADDR_INIT;
			}

			zb_buf_free((zb_buf_t *)arg);
			return;
		}

		unknownEndDevAddr_8570 = shortAddr;

		{
			nwk_hdr_t nwkHdr;
			nwkCmd_t cmd;
			u8 *hdr = (u8 *)&nwkHdr;

			memset(&nwkHdr, 0, sizeof(nwkHdr));
			memset(&cmd, 0, sizeof(cmd));

			nwkHdr.dstAddr = shortAddr;
			if (!ZB_NWK_IS_ADDRESS_BROADCAST(shortAddr) &&
			    zb_address_ieee_by_short(shortAddr, nwkHdr.dstIeeeAddr) == 0U) {
				hdr[5] |= 0x08U;
			}

			cmd.cmdId = NWK_CMD_LEAVE;
			cmd.leave.options.rejoin = 1;
			cmd.leave.options.request = 1;

			if (shortAddr == NWK_BROADCAST_RX_ON_WHEN_IDLE) {
				cmd.leave.options.request = 0;
			}

			nwkLeaveReqSend(arg, &nwkHdr, &cmd,
					NWK_INTERNAL_LEAVE_REQ_CMD_INDIRECT_HANDLE);
		}
	}
}
