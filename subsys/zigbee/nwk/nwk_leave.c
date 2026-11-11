/*
 * Copyright 2026 libzigbee
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "zb_common.h"
#include "nwk_data.h"
#include "nwk_leave.h"
#include "nwk_routing.h"
#include "zdo_nwk_manager.h"
#include "ss_security_flags.h"

static void nwk_nlmeLeaveCnf(void *arg, uint8_t status, addrExt_t extAddr)
{
	nlme_leave_cnf_t *cnf = (nlme_leave_cnf_t *)arg;

	ZB_IEEE_ADDR_COPY(cnf->deviceAddr, extAddr);
	cnf->status = status;

	zb_TaskPost(zdo_nlme_leave_confirm_cb, arg);
}

void zb_NwkSendLeaveReqCmd(zb_buf_t *buf, nwk_hdr_t *pNwkHdr, uint8_t *cmd, uint8_t handle)
{
	uint8_t *payload;

	ZB_BUF_INITIAL_ALLOC(buf, 2, payload, uint8_t *);

	payload[0] = cmd[0];
	payload[1] = cmd[4];
	buf->hdr.handle = handle;

	if (handle == NWK_INTERNAL_LEAVE_REQ_CMD_INDIRECT_HANDLE) {
		nwk_tx(buf, pNwkHdr, pNwkHdr->dstAddr, 1, payload, 2);
		return;
	}

	nwk_fwdPacket(buf, pNwkHdr, payload, 2);
}

int nwkLeaveReqSend(void *arg, nwk_hdr_t *pNwkHdr, nwkCmd_t *cmd, uint8_t handle)
{
	uint8_t *hdr = (uint8_t *)pNwkHdr;
	uint8_t security = 0;
	uint8_t depth = g_zbInfo.nwkNib.parentInfo;

	if (ss_ib_secure_all_fresh() && ss_ib_security_level_get() != 0U) {
		security = ss_keyPreconfigured();
		if (security >= 1U) {
			security--;
		}
	}

	if (depth >= 1U) {
		depth--;
	}

	/* Frame control byte 0 (hdr[4]) holds frameType/protocolVer, byte 1
	 * (hdr[5]) holds the flag bits: security (bit 1), dstIEEEAddr (bit 3),
	 * srcIEEEAddr (bit 4) and endDevInitiator (bit 5). */
	hdr[5] = (uint8_t)((hdr[5] & (uint8_t)~0x20U) | (uint8_t)(depth << 5));
	hdr[4] = (uint8_t)((hdr[4] & (uint8_t)~0x03U) | 0x01U);
	hdr[4] = (uint8_t)((hdr[4] & (uint8_t)~0x3cU) | 0x08U);
	hdr[5] = (uint8_t)((hdr[5] & (uint8_t)~0x02U) | (uint8_t)(security << 1));
	hdr[5] |= 0x10U;

	ZB_IEEE_ADDR_COPY(pNwkHdr->srcIeeeAddr, g_zbInfo.macPib.extAddress);
	pNwkHdr->srcAddr = g_zbInfo.nwkNib.nwkAddr;
	pNwkHdr->radius = 1;
	pNwkHdr->seqNum = g_zbInfo.nwkNib.seqNum;
	g_zbInfo.nwkNib.seqNum++;
	pNwkHdr->frameHdrLen = getNwkHdrSize(pNwkHdr);

	zb_NwkSendLeaveReqCmd((zb_buf_t *)arg, pNwkHdr, (uint8_t *)cmd, handle);

	return 0;
}

static int nwkLeaveReqStart(void *arg, uint16_t dstAddr, uint8_t request)
{
	nlme_leave_req_t *req = (nlme_leave_req_t *)arg;
	nwk_hdr_t nwkHdr;
	nwkCmd_t cmd;
	uint8_t *hdr = (uint8_t *)&nwkHdr;

	memset(&nwkHdr, 0, sizeof(nwkHdr));
	memset(&cmd, 0, sizeof(cmd));

	nwkHdr.dstAddr = dstAddr;

	if (!ZB_NWK_IS_ADDRESS_BROADCAST(dstAddr) &&
	    zb_address_ieee_by_short(dstAddr, nwkHdr.dstIeeeAddr) == 0U) {
		hdr[5] |= 0x08U; /* frameControl.dstIEEEAddr */
	}

	cmd.cmdId = NWK_CMD_LEAVE;
	cmd.leave.options.rejoin = req->rejoin ? 1U : 0U;
	cmd.leave.options.request = request & 0x01U;
	cmd.leave.options.removeChildren = req->removeChildren ? 1U : 0U;

	if (dstAddr == NWK_BROADCAST_RX_ON_WHEN_IDLE) {
		cmd.leave.options.request = 0;
		cmd.leave.options.removeChildren = 0;
	}

	return nwkLeaveReqSend(arg, &nwkHdr, &cmd, NWK_INTERNAL_LEAVE_REQ_CMD_HANDLE);
}

void zb_NwkNlmeLeaveRequestHandler(void *arg)
{
	nlme_leave_req_t *req = (nlme_leave_req_t *)arg;
	addrExt_t extAddr;

	ZB_IEEE_ADDR_COPY(extAddr, req->deviceAddr);

	if (ZB_IEEE_ADDR_IS_ZERO(req->deviceAddr) ||
	    ZB_IEEE_ADDR_CMP(req->deviceAddr, g_zbInfo.macPib.extAddress)) {
		((zb_buf_t *)arg)->hdr.leaveRejoin = req->rejoin ? 1U : 0U;
		nwkLeaveReqStart(arg, NWK_BROADCAST_RX_ON_WHEN_IDLE, 0);
		return;
	}

	if (!g_zbNwkCtx.joined) {
		nwk_nlmeLeaveCnf(arg, NWK_STATUS_INVALID_REQUEST, extAddr);
		return;
	}

	{
		zb_normal_neighbor_entry_t *entry = nwk_neTblGetByExtAddr(extAddr);

		if (entry == NULL) {
			nwk_nlmeLeaveCnf(arg, NWK_STATUS_UNKNOWN_DEVICE, extAddr);
			return;
		}

		if (entry->relationship == NEIGHBOR_IS_UNAUTH_CHILD) {
			nwk_nlmeLeaveCnf(arg, NWK_STATUS_SUCCESS, extAddr);
			return;
		}

		nwkLeaveReqStart(arg, zb_shortAddrByIdx(entry->addrmapIdx), 1);
	}
}

void nwk_leaveCmdSendCnf(void *arg, uint16_t dstAddr)
{
	addrExt_t extAddr;

	nwk_leaveCnfAddr(extAddr, dstAddr);

	nwk_nlmeLeaveCnf(arg, ((uint8_t *)arg)[9], extAddr);
}

void zb_NwkLeaveReqCmdHandler(void *arg, nwk_hdr_t *pNwkHdr, nwkCmd_t *cmd)
{
	nlme_leave_req_t *req = (nlme_leave_req_t *)arg;
	nlme_leave_ind_t *ind = (nlme_leave_ind_t *)arg;
	addrExt_t srcExtAddr;
	uint16_t srcAddr = pNwkHdr->srcAddr;
	zb_normal_neighbor_entry_t *entry;
#if defined(ZB_ROUTER_ROLE) || defined(ZB_COORDINATOR_ROLE)
	uint16_t srcAddrMapIdx;
#endif

	if (!g_zbNwkCtx.joined) {
		zb_buf_free((zb_buf_t *)arg);
		return;
	}

	if (zb_ExtAddrByShortAddr(srcAddr, srcExtAddr,
#if defined(ZB_ROUTER_ROLE) || defined(ZB_COORDINATOR_ROLE)
				    &srcAddrMapIdx
#else
				    NULL
#endif
				    ) == 0xffU) {
		zb_buf_free((zb_buf_t *)arg);
		return;
	}

	entry = nwk_neTblGetByShortAddr(srcAddr);

	if (cmd->leave.options.request != 0U && !ZB_NWK_IS_ADDRESS_BROADCAST(pNwkHdr->dstAddr) &&
	    entry != NULL &&
	    (entry->relationship == NEIGHBOR_IS_PARENT ||
	     cmd->leave.options.removeChildren != 0U)) {
		ZB_IEEE_ADDR_ZERO(req->deviceAddr);
		req->removeChildren = cmd->leave.options.removeChildren ? TRUE : FALSE;
		req->rejoin = cmd->leave.options.rejoin ? TRUE : FALSE;
		zb_TaskPost(zb_NwkNlmeLeaveRequestHandler, arg);
		return;
	}

	if (cmd->leave.options.request == 0U || ZB_NWK_IS_ADDRESS_BROADCAST(pNwkHdr->dstAddr)) {
		ZB_IEEE_ADDR_COPY(ind->deviceAddr, srcExtAddr);
		ind->rejoin = cmd->leave.options.rejoin ? TRUE : FALSE;
		zb_TaskPost(zdo_nlme_leave_indication_cb, arg);
		return;
	}

#if defined(ZB_ROUTER_ROLE) && !defined(ZB_COORDINATOR_ROLE)
	if (g_zbInfo.nwkNib.leaveReqAllowed != 0U && pNwkHdr->dstAddr == g_zbInfo.nwkNib.nwkAddr) {
		ZB_IEEE_ADDR_ZERO(req->deviceAddr);
		req->removeChildren = cmd->leave.options.removeChildren ? TRUE : FALSE;
		req->rejoin = cmd->leave.options.rejoin ? TRUE : FALSE;
		zb_TaskPost(zb_NwkNlmeLeaveRequestHandler, arg);
		return;
	}
#endif

	zb_buf_free((zb_buf_t *)arg);
}
