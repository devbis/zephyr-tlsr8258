/* SPDX-License-Identifier: Apache-2.0 */
/*
 * NWK leave request/indication handling.
 *
 * Adapted from libzigbee/src/nwk_leave.c (~185 LOC). Adaptations:
 *
 *   * vendor "zb_local.h" → zb_common_stub.h + nwk/includes/nwk_internal.h
 *   * Wrapped in ZB_ROUTER_ROLE; the ED build links nwk_ed_minimal.c
 *     which carries its own leave path.
 *   * Forward-declared zdo / aps helpers that other TUs still own.
 *   * tabs / Zephyr-style formatting; logic preserved verbatim.
 */

#include "zb_common_stub.h"
#include "nwk/includes/nwk.h"
#include "nwk/includes/nwk_internal.h"
#include "nwk/includes/nwk_addr_map.h"
#include "nwk/includes/nwk_neighbor.h"

#include <string.h>

#if defined(ZB_ROUTER_ROLE) && ZB_ROUTER_ROLE

extern void zdo_nlme_leave_confirm_cb(void *arg);
extern void zdo_nlme_leave_indication_cb(void *arg);

void nwk_nlmeLeaveCnf(void *arg, u8 status, addrExt_t extAddr)
{
	nlme_leave_cnf_t *cnf = (nlme_leave_cnf_t *)arg;

	memcpy(cnf->deviceAddr, extAddr, EXT_ADDR_LEN);
	cnf->status = status;

	tl_zbTaskPost(zdo_nlme_leave_confirm_cb, arg);
}

void tl_zbNwkSendLeaveReqCmd(zb_buf_t *buf, nwk_hdr_t *pNwkHdr, u8 *cmd, u8 handle)
{
	u8 *payload = tl_bufInitalloc(buf, 2);

	payload[0] = cmd[0];
	payload[1] = cmd[4];
	buf->hdr.handle = handle;

	if (handle == NWK_INTERNAL_LEAVE_REQ_CMD_INDIRECT_HANDLE) {
		nwk_tx(buf, pNwkHdr, pNwkHdr->dstAddr, 1, payload, 2);
		return;
	}

	nwk_fwdPacket(buf, pNwkHdr, payload, 2);
}

int nwkLeaveReqSend(void *arg, nwk_hdr_t *pNwkHdr, nwkCmd_t *cmd, u8 handle)
{
	u8 *hdr = (u8 *)pNwkHdr;
	u8 security = 0;
	u8 depth = g_zbInfo.nwkNib.parentInfo;

	if (ss_ib.secureAllFresh && ss_ib.securityLevel != 0U) {
		security = ss_keyPreconfigured();
		if (security >= 1U) {
			security--;
		}
	}

	if (depth >= 1U) {
		depth--;
	}

	hdr[5] = (u8)((hdr[5] & (u8)~0x20U) | (u8)(depth << 5));
	hdr[4] = (u8)((hdr[4] & (u8)~0x03U) | 0x01U);
	hdr[4] = (u8)((hdr[4] & (u8)~0x3cU) | 0x08U);
	hdr[5] = (u8)((hdr[5] & (u8)~0x02U) | (u8)(security << 1));
	hdr[5] |= 0x10U;

	memcpy(pNwkHdr->srcIeeeAddr, g_zbInfo.macPib.extAddress, EXT_ADDR_LEN);
	pNwkHdr->srcAddr = g_zbInfo.nwkNib.nwkAddr;
	pNwkHdr->radius = 1;
	pNwkHdr->seqNum = g_zbInfo.nwkNib.seqNum;
	g_zbInfo.nwkNib.seqNum++;
	pNwkHdr->frameHdrLen = getNwkHdrSize(pNwkHdr);

	tl_zbNwkSendLeaveReqCmd((zb_buf_t *)arg, pNwkHdr, (u8 *)cmd, handle);

	return 0;
}

int nwkLeaveReqStart(void *arg, u16 dstAddr, u8 request)
{
	nlme_leave_req_t *req = (nlme_leave_req_t *)arg;
	nwk_hdr_t nwkHdr;
	nwkCmd_t cmd;
	u8 *hdr = (u8 *)&nwkHdr;

	memset(&nwkHdr, 0, sizeof(nwkHdr));
	memset(&cmd, 0, sizeof(cmd));

	nwkHdr.dstAddr = dstAddr;

	if (!ZB_NWK_IS_ADDRESS_BROADCAST(dstAddr) &&
	    zb_address_ieee_by_short(dstAddr, nwkHdr.dstIeeeAddr) == 0U) {
		hdr[5] |= 0x08U;
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

void tl_zbNwkNlmeLeaveRequestHandler(void *arg)
{
	nlme_leave_req_t *req = (nlme_leave_req_t *)arg;
	addrExt_t extAddr;

	memcpy(extAddr, req->deviceAddr, EXT_ADDR_LEN);

	if (memcmp(req->deviceAddr, g_zero_addr, EXT_ADDR_LEN) == 0 ||
	    memcmp(req->deviceAddr, g_zbInfo.macPib.extAddress, EXT_ADDR_LEN) == 0) {
		((u8 *)arg)[OFFSETOF(zb_buf_t, hdr) + 3] =
			(u8)((((u8 *)arg)[OFFSETOF(zb_buf_t, hdr) + 3] & (u8)~0x04U) |
			     ((req->rejoin ? 1U : 0U) << 2));
		nwkLeaveReqStart(arg, NWK_BROADCAST_RX_ON_WHEN_IDLE, 0);
		return;
	}

	if (!g_zbNwkCtx.joined) {
		nwk_nlmeLeaveCnf(arg, NWK_STATUS_INVALID_REQUEST, extAddr);
		return;
	}

	{
		tl_zb_normal_neighbor_entry_t *entry = nwk_neTblGetByExtAddr(extAddr);

		if (entry == NULL) {
			nwk_nlmeLeaveCnf(arg, NWK_STATUS_UNKNOWN_DEVICE, extAddr);
			return;
		}

		if (entry->relationship == NEIGHBOR_IS_UNAUTH_CHILD) {
			nwk_nlmeLeaveCnf(arg, NWK_STATUS_SUCCESS, extAddr);
			return;
		}

		nwkLeaveReqStart(arg, tl_zbshortAddrByIdx(entry->addrmapIdx), 1);
	}
}

void nwk_leaveCmdSendCnf(void *arg, u16 dstAddr)
{
	addrExt_t extAddr;

	memcpy(extAddr, g_invalid_addr, EXT_ADDR_LEN);
	if (!ZB_NWK_IS_ADDRESS_BROADCAST(dstAddr)) {
		(void)zb_address_ieee_by_short(dstAddr, extAddr);
	}

	nwk_nlmeLeaveCnf(arg, ((u8 *)arg)[9], extAddr);
}

void tl_zbNwkLeaveReqCmdHandler(void *arg, nwk_hdr_t *pNwkHdr, nwkCmd_t *cmd)
{
	nlme_leave_req_t *req = (nlme_leave_req_t *)arg;
	nlme_leave_ind_t *ind = (nlme_leave_ind_t *)arg;
	addrExt_t srcExtAddr;
	u16 srcAddr = pNwkHdr->srcAddr;
	tl_zb_normal_neighbor_entry_t *entry;

	if (!g_zbNwkCtx.joined) {
		zb_buf_free((zb_buf_t *)arg);
		return;
	}

	if (tl_zbExtAddrByShortAddr(srcAddr, srcExtAddr, NULL) == 0xffU) {
		zb_buf_free((zb_buf_t *)arg);
		return;
	}

	entry = nwk_neTblGetByShortAddr(srcAddr);

	if (cmd->leave.options.request != 0U &&
	    !ZB_NWK_IS_ADDRESS_BROADCAST(pNwkHdr->dstAddr) &&
	    entry != NULL &&
	    (entry->relationship == NEIGHBOR_IS_PARENT ||
	     cmd->leave.options.removeChildren != 0U)) {
		memset(req->deviceAddr, 0, EXT_ADDR_LEN);
		req->removeChildren = cmd->leave.options.removeChildren ? TRUE : FALSE;
		req->rejoin = cmd->leave.options.rejoin ? TRUE : FALSE;
		tl_zbTaskPost(tl_zbNwkNlmeLeaveRequestHandler, arg);
		return;
	}

	if (cmd->leave.options.request == 0U || ZB_NWK_IS_ADDRESS_BROADCAST(srcAddr)) {
		memcpy(ind->deviceAddr, srcExtAddr, EXT_ADDR_LEN);
		ind->rejoin = cmd->leave.options.rejoin ? TRUE : FALSE;
		tl_zbTaskPost(zdo_nlme_leave_indication_cb, arg);
		return;
	}

	zb_buf_free((zb_buf_t *)arg);
}

#endif /* ZB_ROUTER_ROLE */
