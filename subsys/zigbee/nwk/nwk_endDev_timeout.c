/* SPDX-License-Identifier: Apache-2.0 */
/*
 * NWK end-device-timeout request/response + keepalive driver.
 *
 * Adapted from libzigbee/src/nwk_endDev_timeout.c (~350 LOC). Adaptations:
 *
 *   * vendor "zb_local.h" / "ev_timer.h" → zb_common_stub.h +
 *     nwk/includes/nwk_internal.h + os/ev_timer.h
 *   * Whole TU wrapped in ZB_ROUTER_ROLE. Vendor file kept the
 *     keepalive/request path outside the ROUTER guard so an ED build
 *     could reuse it; the Zephyr ED build instead uses nwk_ed_minimal.c
 *     and this file is only linked into the router build.
 *   * Forward-declared cross-TU helpers (endDevMacDataPoll, zdo*,
 *     nv_* persistence, ev_buf_*) that still live in other TUs or are
 *     stubbed by the Zephyr platform layer.
 *   * tabs / Zephyr-style formatting; logic preserved verbatim.
 */

#include "zb_common_stub.h"
#include "common/static_assert.h"
#include "nwk/includes/nwk.h"
#include "nwk/includes/nwk_internal.h"
#include "nwk/includes/nwk_neighbor.h"
#include "os/ev_timer.h"

#include <string.h>

#if defined(ZB_ROUTER_ROLE) && ZB_ROUTER_ROLE

extern void endDevMacDataPoll(void);
extern void tl_zbNwkBeaconPayloadUpdate(void);
extern void zb_buf_clear(zb_buf_t *buf);
/* zdo_nwkRejoinStart / zdo_cfg_attributes come from zdo/zdo_api.h
 * (pulled in via zb_common_stub.h). NV helpers come from drv_nv.h, and
 * ev_buf_* come from os/ev_buffer.h.
 */

enum {
	NWK_CTX_FLAGS0_OFFSET = 45,
};

typedef struct {
	u32 timeout;
	addrExt_t extAddr;
} nwk_endDevTimeout_nv_t;

typedef struct _attribute_packed_ {
	u16 dstAddr;
	u16 srcAddr;
	u8 frameCtrlLow;
	u8 frameCtrlHigh;
} nwk_hdr_frame_ctrl_bytes_t;

typedef struct _attribute_packed_ {
	u8 cmdId;
	nwkCmd_endDevTimeoutReq_t req;
} nwk_endDevTimeout_req_payload_t;

typedef struct _attribute_packed_ {
	u8 cmdId;
	nwkCmd_endDevTimeoutResp_t rsp;
} nwk_endDevTimeout_rsp_payload_t;

typedef struct _attribute_packed_ {
	u8 reserved0[9];
	u8 status;
	u16 srcAddr;
} nwk_endDevTimeout_req_cnf_t;

STATIC_ASSERT(sizeof(nwk_endDevTimeout_nv_t) == 12);
/* Vendor layout asserts — naturally aligned in the Zephyr build matches
 * the packed sizes here (all small u8/u16 fields), keep them on. */
STATIC_ASSERT(sizeof(nwk_hdr_frame_ctrl_bytes_t) == 6);
STATIC_ASSERT(sizeof(nwk_endDevTimeout_req_payload_t) == 3);
STATIC_ASSERT(sizeof(nwk_endDevTimeout_rsp_payload_t) == 3);
STATIC_ASSERT(sizeof(nwk_endDevTimeout_req_cnf_t) == 12);


ev_timer_event_t *keepaliveMsgSendEvt = NULL;

int nwkEndDevWaitForTimeoutRspCb(void *arg)
{
	ARG_UNUSED(arg);

	endDevMacDataPoll();

	return -1;
}

void nwkEndDevTimeoutReqCmdSend(zb_buf_t *buf, nwk_hdr_t *pNwkHdr, u8 *cmd, u8 handle)
{
	nwkCmd_t *nwkCmd = (nwkCmd_t *)cmd;
	nwk_endDevTimeout_req_payload_t *payload =
		(nwk_endDevTimeout_req_payload_t *)tl_bufInitalloc(buf, 3);
	u16 nextHop = pNwkHdr->dstAddr;

	payload->cmdId = nwkCmd->cmdId;
	payload->req = nwkCmd->endDevTimeoutReq;

	buf->hdr.handle = handle;

	nwk_tx(buf, pNwkHdr, nextHop, 0, (u8 *)payload, 3);
}

void nwkEndDevTimeoutReqSend(reqTimeoutEnum_t reqTimeoutEnum, u8 endDevCfg)
{
	tl_zb_normal_neighbor_entry_t *parent = tl_zbNeighborTableSearchForParent();

	if (parent == NULL) {
		return;
	}

	zb_buf_t *buf = zb_buf_allocate();

	if (buf == NULL) {
		return;
	}

	nwk_hdr_t nwkHdr;
	nwkCmd_t cmd;
	nwk_hdr_frame_ctrl_bytes_t *hdr = (nwk_hdr_frame_ctrl_bytes_t *)&nwkHdr;
	u8 parentInfo = g_zbNIB.parentInfo;
	u8 security = 0;
	memset(&nwkHdr, 0, sizeof(nwkHdr));
	memset(&cmd, 0, sizeof(cmd));

	if (ss_ib.secureAllFresh) {
		if (ss_ib.securityLevel != 0U) {
			security = ss_keyPreconfigured();
			if (security >= 1U) {
				security--;
			}
		}
	}

	if (parentInfo >= 1U) {
		parentInfo--;
	}

	hdr->frameCtrlHigh =
		(u8)((hdr->frameCtrlHigh & (u8)~0x20U) | (u8)(parentInfo << 5));
	hdr->frameCtrlLow =
		(u8)((hdr->frameCtrlLow & (u8)~0x3cU) | 0x08U);
	hdr->frameCtrlLow =
		(u8)((hdr->frameCtrlLow & (u8)~0x03U) | 0x01U);
	hdr->frameCtrlHigh =
		(u8)((hdr->frameCtrlHigh & (u8)~0x02U) | (u8)(security << 1));
	hdr->frameCtrlHigh |= 0x10U;
	hdr->frameCtrlHigh |= 0x3cU;

	memcpy(nwkHdr.srcIeeeAddr, g_zbMacPib.extAddress, EXT_ADDR_LEN);
	tl_zbExtAddrByIdx(parent->addrmapIdx, nwkHdr.dstIeeeAddr);

	nwkHdr.srcAddr = g_zbNIB.nwkAddr;
	nwkHdr.dstAddr = tl_zbshortAddrByIdx(parent->addrmapIdx);
	nwkHdr.seqNum = g_zbNIB.seqNum;
	g_zbNIB.seqNum++;
	nwkHdr.radius = 1;
	nwkHdr.frameHdrLen = getNwkHdrSize(&nwkHdr);

	cmd.cmdId = NWK_CMD_ENDDEVTIMEOUT_REQUEST;
	cmd.endDevTimeoutReq.reqTimeoutEnum = reqTimeoutEnum;
	cmd.endDevTimeoutReq.endDevCfg = endDevCfg;

	nwkEndDevTimeoutReqCmdSend(buf, &nwkHdr, (u8 *)&cmd,
				   NWK_INTERNAL_ENDDEVTIMEOUT_REQ_CMD_HANDLE);
}

void keepaliveMsgSend(void)
{
	if (g_zbNIB.parentInfo & MAC_DATA_POLL_KEEPALIVE_BIT) {
		endDevMacDataPoll();
		return;
	}

	if (g_zbNIB.parentInfo & END_DEV_TIMEOUT_REQ_KEEPALIVE_BIT) {
		nwkEndDevTimeoutReqSend((reqTimeoutEnum_t)g_zbNIB.endDevTimeoutDefault, 0);
	}
}

int keepaliveMsgSendTimerCb(void *arg)
{
	ARG_UNUSED(arg);

	if (g_zbNwkCtx.joined) {
		keepaliveMsgSend();
		return 0;
	}

	keepaliveMsgSendEvt = NULL;

	return -1;
}

void keepaliveMsgSendStart(void)
{
	u32 timeout = (REQUESTED_TIMEOUT_VALUE_GET(g_zbNIB.endDevTimeoutDefault) * 1000U) / 3U;

	if (keepaliveMsgSendEvt != NULL) {
		ev_timer_taskCancel(&keepaliveMsgSendEvt);
	}

	keepaliveMsgSendEvt = ev_timer_taskPost(keepaliveMsgSendTimerCb, NULL, timeout);
}

void keepaliveMsgSendStop(void)
{
	if (keepaliveMsgSendEvt != NULL) {
		ev_timer_taskCancel(&keepaliveMsgSendEvt);
	}
}

void nwkEndDevTimeoutRspCmdHandler(void *arg, nwk_hdr_t *pNwkHdr, nwkCmd_t *cmd)
{
	if (cmd->endDevTimeoutRsp.status != TIMEOUT_RSP_STATUS_SUCCESS) {
		zb_buf_free((zb_buf_t *)arg);
		return;
	}

	g_zbNIB.parentInfo = cmd->endDevTimeoutRsp.parentInfo;
	keepaliveMsgSendStart();

	tl_zb_normal_neighbor_entry_t *entry = nwk_neTblGetByShortAddr(pNwkHdr->srcAddr);

	if (entry == NULL) {
		zb_buf_free((zb_buf_t *)arg);
		return;
	}

	if (entry->relationship != NEIGHBOR_IS_PARENT) {
		zb_buf_free((zb_buf_t *)arg);
		return;
	}

	entry->timeoutCnt = REQUESTED_TIMEOUT_VALUE_GET(g_zbNIB.endDevTimeoutDefault);
	entry->devTimeout = entry->timeoutCnt;

	zb_buf_free((zb_buf_t *)arg);
}

void nwkEndDevTimeoutReqCnfHandler(void *arg)
{
	nwk_endDevTimeout_req_cnf_t *buf = (nwk_endDevTimeout_req_cnf_t *)arg;

	if (buf->status == 0U) {
		zb_buf_free((zb_buf_t *)arg);

		if (g_zbInfo.macPib.rxOnWhenIdle != 0U) {
			return;
		}

		ev_timer_taskPost(nwkEndDevWaitForTimeoutRspCb, NULL,
				  ((((u32)g_zbInfo.macPib.respWaitTime << 4) -
				    (u32)g_zbInfo.macPib.respWaitTime) << 10) /
					  1000U);
		return;
	}

	tl_zbNwkNlmeNwkStatusInd(arg,
				 buf->srcAddr,
				 NWK_COMMAND_STATUS_PARENT_LINK_FAILURE);
}

void nwkEndDevTimeoutRejoin(void)
{
	zdo_nwkRejoinStart(1UL << g_zbInfo.macPib.phyChannelCur,
			   zdo_cfg_attributes.config_nwk_scan_duration);
}

void nwkEndDevTimeoutInfoNVStore(void *arg)
{
	nwk_endDevTimeout_nv_t *info = (nwk_endDevTimeout_nv_t *)arg;
	nwk_endDevTimeout_nv_t timeoutInfo;
	itemIfno_t itemInfo;

	memset(&itemInfo, 0, sizeof(itemInfo));

	if (nv_flashReadNew(0, NV_MODULE_ZB_INFO, ITEM_FIELD_IDLE, sizeof(timeoutInfo),
			    (u8 *)&itemInfo) == NV_SUCC) {
		for (u16 i = 0; i <= itemInfo.opIndex; i++) {
			if (nv_flashReadByIndex(NV_MODULE_ZB_INFO, NV_ITEM_ED_TIMEOUT,
						itemInfo.opSect, i, sizeof(timeoutInfo),
						(u8 *)&timeoutInfo) != NV_SUCC) {
				continue;
			}

			if (memcmp(info->extAddr, timeoutInfo.extAddr, EXT_ADDR_LEN) != 0) {
				continue;
			}

			if (info->timeout == timeoutInfo.timeout) {
				ev_buf_free((u8 *)info);
				return;
			}

			(void)nv_itemDeleteByIndex(NV_MODULE_ZB_INFO, NV_ITEM_ED_TIMEOUT,
						   itemInfo.opSect, i);
		}
	}

	(void)nv_flashWriteNew(0, NV_MODULE_ZB_INFO, NV_ITEM_ED_TIMEOUT, sizeof(*info),
			       (u8 *)info);
	ev_buf_free((u8 *)info);
}

void nwkEndDevKeepaliveSupport(u8 keepaliveSupport)
{
	g_zbNIB.parentInfo = keepaliveSupport;
}

void nwkEndDevTimeoutRspCmdSend(zb_buf_t *buf, nwk_hdr_t *pNwkHdr, u8 *cmd, u8 handle)
{
	nwkCmd_t *nwkCmd = (nwkCmd_t *)cmd;
	nwk_endDevTimeout_rsp_payload_t *payload =
		(nwk_endDevTimeout_rsp_payload_t *)tl_bufInitalloc(buf, 3);

	payload->cmdId = nwkCmd->cmdId;
	payload->rsp = nwkCmd->endDevTimeoutRsp;

	buf->hdr.handle = handle;

	nwk_fwdPacket(buf, pNwkHdr, (u8 *)payload, 3);
}

void nwkEndDevTimeoutInfoStore(tl_zb_normal_neighbor_entry_t *entry)
{
	nwk_endDevTimeout_nv_t *info =
		(nwk_endDevTimeout_nv_t *)ev_buf_allocate(sizeof(nwk_endDevTimeout_nv_t));

	if (info == NULL) {
		return;
	}

	tl_zbExtAddrByIdx(entry->addrmapIdx, info->extAddr);
	info->timeout = entry->devTimeout;

	tl_zbTaskPost(nwkEndDevTimeoutInfoNVStore, info);
}

void nwkEndDevTimeoutReqCmdHandler(void *arg, nwk_hdr_t *pNwkHdr, nwkCmd_t *cmd)
{
	zb_buf_t *buf = (zb_buf_t *)arg;
	nwkCmd_t rsp;
	nwk_hdr_t nwkHdr;
	nwk_hdr_frame_ctrl_bytes_t *hdr = (nwk_hdr_frame_ctrl_bytes_t *)&nwkHdr;
	u8 security = 0;

	memset(&rsp, 0, sizeof(rsp));
	memset(&nwkHdr, 0, sizeof(nwkHdr));

	rsp.cmdId = NWK_CMD_ENDDEVTIMEOUT_RESPONSE;

	if (cmd->endDevTimeoutReq.reqTimeoutEnum > REQTIMEOUTENUM_16384_MINUTES ||
	    cmd->endDevTimeoutReq.endDevCfg != 0U) {
		rsp.endDevTimeoutRsp.status = TIMEOUT_RSP_STATUS_INCORRECT_VALUE;
	} else {
		tl_zb_normal_neighbor_entry_t *entry =
			nwk_neTblGetByShortAddr(pNwkHdr->srcAddr);

		if (entry == NULL || entry->relationship != NEIGHBOR_IS_CHILD) {
			zb_buf_free(buf);
			return;
		}

		entry->keepaliveRcvd = 1;
		entry->timeoutCnt =
			REQUESTED_TIMEOUT_VALUE_GET(cmd->endDevTimeoutReq.reqTimeoutEnum);
		entry->devTimeout = entry->timeoutCnt;
		entry->endDevCfg = cmd->endDevTimeoutReq.endDevCfg;
		nwkEndDevTimeoutInfoStore(entry);

		rsp.endDevTimeoutRsp.status = TIMEOUT_RSP_STATUS_SUCCESS;
		if (g_zbNIB.parentInfo & END_DEV_TIMEOUT_REQ_KEEPALIVE_BIT) {
			rsp.endDevTimeoutRsp.parentInfo = g_zbNIB.parentInfo;
		} else {
			rsp.endDevTimeoutRsp.parentInfo = MAC_DATA_POLL_KEEPALIVE_BIT;
		}
	}

	zb_buf_clear(buf);

	if (ss_ib.secureAllFresh) {
		if (ss_ib.securityLevel != 0U) {
			security = ss_keyPreconfigured();
			if (security >= 1U) {
				security--;
			}
		}
	}

	hdr->frameCtrlLow =
		(u8)((hdr->frameCtrlLow & (u8)~0x3cU) | 0x08U);
	hdr->frameCtrlLow =
		(u8)((hdr->frameCtrlLow & (u8)~0x03U) | 0x01U);
	hdr->frameCtrlHigh =
		(u8)((hdr->frameCtrlHigh & (u8)~0x02U) | (u8)(security << 1));
	hdr->frameCtrlHigh |= 0x10U;
	hdr->frameCtrlHigh |= 0x08U;

	memcpy(nwkHdr.srcIeeeAddr, g_zbMacPib.extAddress, EXT_ADDR_LEN);
	memcpy(nwkHdr.dstIeeeAddr, pNwkHdr->srcIeeeAddr, EXT_ADDR_LEN);

	nwkHdr.srcAddr = g_zbNIB.nwkAddr;
	nwkHdr.dstAddr = pNwkHdr->srcAddr;
	nwkHdr.seqNum = g_zbNIB.seqNum;
	g_zbNIB.seqNum++;
	nwkHdr.radius = 1;
	nwkHdr.frameHdrLen = getNwkHdrSize(&nwkHdr);

	nwkEndDevTimeoutRspCmdSend(buf, &nwkHdr, (u8 *)&rsp,
				   NWK_INTERNAL_ENDDEVTIMEOUT_RSP_CMD_HANDLE);
}

void nwkEndDevTimeoutRspCnfHandler(void *arg)
{
	zb_buf_free((zb_buf_t *)arg);
}

#endif /* ZB_ROUTER_ROLE */
