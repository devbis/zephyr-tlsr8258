/*
 * Copyright 2026 libzigbee
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "zb_common.h"
#include "nwk_endDev_timeout.h"
#include "nwk_data.h"
#include "zb_nwk_neighbor.h"
#include "zdo_nwk_manager.h"
#include "ev_timer.h"
#include "ss_security_flags.h"

typedef struct _attribute_packed_ {
	uint16_t dstAddr;
	uint16_t srcAddr;
	uint8_t frameCtrlLow;
	uint8_t frameCtrlHigh;
} nwk_hdr_frame_ctrl_bytes_t;

typedef struct _attribute_packed_ {
	uint8_t cmdId;
	nwkCmd_endDevTimeoutReq_t req;
} nwk_endDevTimeout_req_payload_t;

typedef struct _attribute_packed_ {
	uint8_t cmdId;
	nwkCmd_endDevTimeoutResp_t rsp;
} nwk_endDevTimeout_rsp_payload_t;

typedef struct _attribute_packed_ {
	uint8_t pad[9]; /* bytes 0-8 of the confirm buffer, not used here */
	uint8_t status;
	uint16_t srcAddr;
} nwk_endDevTimeout_req_cnf_t;

STATIC_ASSERT(sizeof(nwk_endDevTimeout_nv_t) == 12);
STATIC_ASSERT(sizeof(nwk_hdr_frame_ctrl_bytes_t) == 6);
STATIC_ASSERT(sizeof(nwk_endDevTimeout_req_payload_t) == 3);
STATIC_ASSERT(sizeof(nwk_endDevTimeout_rsp_payload_t) == 3);
STATIC_ASSERT(sizeof(nwk_endDevTimeout_req_cnf_t) == 12);

enum {
	NEIGHBOR_DEVICE_TYPE_STATUS_OFFSET =
			OFFSETOF(zb_normal_neighbor_entry_t, keepaliveRcvd) +
		sizeof(bool),
	NEIGHBOR_DEVICE_TYPE_MASK = 0x0eU,
	NEIGHBOR_DEVICE_TYPE_END_DEVICE = NWK_DEVICE_TYPE_ED << 1,
};

ev_timer_event_t *keepaliveMsgSendEvt = NULL;

#if !defined(ZB_ROUTER_ROLE)
int nwkEndDevWaitForTimeoutRspCb(void *arg)
{
	(void)arg;

	endDevMacDataPoll();

	return -1;
}

void nwkEndDevTimeoutReqCmdSend(zb_buf_t *buf, nwk_hdr_t *pNwkHdr, uint8_t *cmd, uint8_t handle)
{
	nwkCmd_t *nwkCmd = (nwkCmd_t *)cmd;
	nwk_endDevTimeout_req_payload_t *payload;

	ZB_BUF_INITIAL_ALLOC(buf, 3, payload, nwk_endDevTimeout_req_payload_t *);
	uint16_t nextHop = pNwkHdr->dstAddr;

	payload->cmdId = nwkCmd->cmdId;
	payload->req = nwkCmd->endDevTimeoutReq;

	buf->hdr.handle = handle;

	nwk_tx(buf, pNwkHdr, nextHop, 0, (uint8_t *)payload, 3);
}

void nwkEndDevTimeoutReqSend(reqTimeoutEnum_t reqTimeoutEnum, uint8_t endDevCfg)
{
	zb_normal_neighbor_entry_t *parent = zb_NeighborTableSearchForParent();

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
	uint8_t parentInfo = g_zbNIB.parentInfo;
	uint8_t security = 0;
	memset(&nwkHdr, 0, sizeof(nwkHdr));
	memset(&cmd, 0, sizeof(cmd));

	if (ss_ib_secure_all_fresh()) {
		if (ss_ib_security_level_get() != 0U) {
			security = ss_keyPreconfigured();
			if (security >= 1U) {
				security--;
			}
		}
	}

	if (parentInfo >= 1U) {
		parentInfo--;
	}

	hdr->frameCtrlHigh = (uint8_t)((hdr->frameCtrlHigh & (uint8_t)~0x20U) | (uint8_t)(parentInfo << 5));
	hdr->frameCtrlLow = (uint8_t)((hdr->frameCtrlLow & (uint8_t)~0x3cU) | 0x08U);
	hdr->frameCtrlLow = (uint8_t)((hdr->frameCtrlLow & (uint8_t)~0x03U) | 0x01U);
	hdr->frameCtrlHigh = (uint8_t)((hdr->frameCtrlHigh & (uint8_t)~0x02U) | (uint8_t)(security << 1));
	hdr->frameCtrlHigh |= 0x10U;
	hdr->frameCtrlHigh |= 0x3cU;

	ZB_IEEE_ADDR_COPY(nwkHdr.srcIeeeAddr, g_zbMacPib.extAddress);
	zb_ExtAddrByIdx(parent->addrmapIdx, nwkHdr.dstIeeeAddr);

	nwkHdr.srcAddr = g_zbNIB.nwkAddr;
	nwkHdr.dstAddr = zb_shortAddrByIdx(parent->addrmapIdx);
	nwkHdr.seqNum = g_zbNIB.seqNum;
	g_zbNIB.seqNum++;
	nwkHdr.radius = 1;
	nwkHdr.frameHdrLen = getNwkHdrSize(&nwkHdr);

	cmd.cmdId = NWK_CMD_ENDDEVTIMEOUT_REQUEST;
	cmd.endDevTimeoutReq.reqTimeoutEnum = reqTimeoutEnum;
	cmd.endDevTimeoutReq.endDevCfg = endDevCfg;

	nwkEndDevTimeoutReqCmdSend(buf, &nwkHdr, (uint8_t *)&cmd,
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
	(void)arg;

	if (g_zbNwkCtx.joined) {
		keepaliveMsgSend();
		return 0;
	}

	keepaliveMsgSendEvt = NULL;

	return -1;
}

void keepaliveMsgSendStart(void)
{
	uint32_t timeout = (REQUESTED_TIMEOUT_VALUE_GET(g_zbNIB.endDevTimeoutDefault) * 1000U) / 3U;

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

	zb_normal_neighbor_entry_t *entry = nwk_neTblGetByShortAddr(pNwkHdr->srcAddr);

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

	if (buf->status == TIMEOUT_RSP_STATUS_SUCCESS) {
		zb_buf_free((zb_buf_t *)arg);

		if (g_zbInfo.macPib.rxOnWhenIdle != 0U) {
			return;
		}

		ev_timer_taskPost(nwkEndDevWaitForTimeoutRspCb, NULL,
				  ((((uint32_t)g_zbInfo.macPib.respWaitTime << 4) -
				    (uint32_t)g_zbInfo.macPib.respWaitTime)
				   << 10) /
					  1000U);
		return;
	}

	zb_NwkNlmeNwkStatusInd(arg, buf->srcAddr, NWK_COMMAND_STATUS_PARENT_LINK_FAILURE);
}

void nwkEndDevTimeoutRejoin(void)
{
	zdo_nwkRejoinStart(1UL << g_zbInfo.macPib.phyChannelCur,
			   zdo_cfg_attributes.config_nwk_scan_duration);
}
#endif

#if defined(ZB_ROUTER_ROLE)
void nwkEndDevTimeoutInfoNVStore(void *arg)
{
	nwk_endDevTimeout_nv_t *info = (nwk_endDevTimeout_nv_t *)arg;
	nwk_endDevTimeout_nv_t timeoutInfo;
	itemIfno_t itemInfo;

	memset(&itemInfo, 0, sizeof(itemInfo));

	/* The vendor library keeps the end-device timeout item in module 1
	 * (NV_MODULE_ADDRESS_TABLE), not NV_MODULE_ZB_INFO: every NV call in this
	 * function uses that module. */
	if (nv_flashReadNew(0, NV_MODULE_ADDRESS_TABLE, ITEM_FIELD_IDLE, sizeof(timeoutInfo),
			    (uint8_t *)&itemInfo) == NV_SUCC) {
		for (uint16_t i = 0; i <= itemInfo.opIndex; i++) {
			if (nv_flashReadByIndex(NV_MODULE_ADDRESS_TABLE, NV_ITEM_ED_TIMEOUT,
						itemInfo.opSect, i, sizeof(timeoutInfo),
						(uint8_t *)&timeoutInfo) != NV_SUCC) {
				continue;
			}

			if (!ZB_IEEE_ADDR_CMP(info->extAddr, timeoutInfo.extAddr)) {
				continue;
			}

			if (info->timeout == timeoutInfo.timeout) {
				ev_buf_free((uint8_t *)info);
				return;
			}

			(void)nv_itemDeleteByIndex(NV_MODULE_ADDRESS_TABLE, NV_ITEM_ED_TIMEOUT,
						   itemInfo.opSect, i);
		}
	}

	(void)nv_flashWriteNew(0, NV_MODULE_ADDRESS_TABLE, NV_ITEM_ED_TIMEOUT, sizeof(*info),
			       (uint8_t *)info);
	ev_buf_free((uint8_t *)info);
}

void nwkEndDevKeepaliveSupport(uint8_t keepaliveSupport)
{
	g_zbNIB.parentInfo = keepaliveSupport;
}

void nwkEndDevTimeoutRspCmdSend(zb_buf_t *buf, nwk_hdr_t *pNwkHdr, uint8_t *cmd, uint8_t handle)
{
	nwkCmd_t *nwkCmd = (nwkCmd_t *)cmd;
	nwk_endDevTimeout_rsp_payload_t *payload;

	ZB_BUF_INITIAL_ALLOC(buf, 3, payload, nwk_endDevTimeout_rsp_payload_t *);

	payload->cmdId = nwkCmd->cmdId;
	payload->rsp = nwkCmd->endDevTimeoutRsp;

	buf->hdr.handle = handle;

	nwk_fwdPacket(buf, pNwkHdr, (uint8_t *)payload, 3);
}

void nwkEndDevTimeoutInfoStore(zb_normal_neighbor_entry_t *entry)
{
	nwk_endDevTimeout_nv_t *info =
		(nwk_endDevTimeout_nv_t *)ev_buf_allocate(sizeof(nwk_endDevTimeout_nv_t));

	if (info == NULL) {
		return;
	}

	zb_ExtAddrByIdx(entry->addrmapIdx, info->extAddr);
	info->timeout = entry->devTimeout;

	zb_TaskPost(nwkEndDevTimeoutInfoNVStore, info);
}

void nwkEndDevTimeoutReqCmdHandler(void *arg, nwk_hdr_t *pNwkHdr, nwkCmd_t *cmd)
{
	zb_buf_t *buf = (zb_buf_t *)arg;
	nwkCmd_t rsp;
	nwk_hdr_t nwkHdr;
	nwk_hdr_frame_ctrl_bytes_t *hdr = (nwk_hdr_frame_ctrl_bytes_t *)&nwkHdr;
	uint8_t security = 0;

	memset(&rsp, 0, sizeof(rsp));
	memset(&nwkHdr, 0, sizeof(nwkHdr));

	rsp.cmdId = NWK_CMD_ENDDEVTIMEOUT_RESPONSE;

	if (cmd->endDevTimeoutReq.reqTimeoutEnum > REQTIMEOUTENUM_16384_MINUTES ||
	    cmd->endDevTimeoutReq.endDevCfg != 0U) {
		rsp.endDevTimeoutRsp.status = TIMEOUT_RSP_STATUS_INCORRECT_VALUE;
	} else {
		zb_normal_neighbor_entry_t *entry = nwk_neTblGetByShortAddr(pNwkHdr->srcAddr);

		/* The SDK bitfield declaration does not reproduce the vendor status
		 * byte layout with this compiler.  The vendor checks deviceType in
		 * bits 1..3; derive its offset from the named preceding field instead
		 * of assuming a 32-bit pointer size. */
		if (entry == NULL ||
		    (((uint8_t *)entry)[NEIGHBOR_DEVICE_TYPE_STATUS_OFFSET] &
		     NEIGHBOR_DEVICE_TYPE_MASK) != NEIGHBOR_DEVICE_TYPE_END_DEVICE) {
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

	if (ss_ib_secure_all_fresh()) {
		if (ss_ib_security_level_get() != 0U) {
			security = ss_keyPreconfigured();
			if (security >= 1U) {
				security--;
			}
		}
	}

	hdr->frameCtrlLow = (uint8_t)((hdr->frameCtrlLow & (uint8_t)~0x3cU) | 0x08U);
	hdr->frameCtrlLow = (uint8_t)((hdr->frameCtrlLow & (uint8_t)~0x03U) | 0x01U);
	hdr->frameCtrlHigh = (uint8_t)((hdr->frameCtrlHigh & (uint8_t)~0x02U) | (uint8_t)(security << 1));
	hdr->frameCtrlHigh |= 0x10U;
	hdr->frameCtrlHigh |= 0x08U;

	ZB_IEEE_ADDR_COPY(nwkHdr.srcIeeeAddr, g_zbMacPib.extAddress);
	ZB_IEEE_ADDR_COPY(nwkHdr.dstIeeeAddr, pNwkHdr->srcIeeeAddr);

	nwkHdr.srcAddr = g_zbNIB.nwkAddr;
	nwkHdr.dstAddr = pNwkHdr->srcAddr;
	nwkHdr.seqNum = g_zbNIB.seqNum;
	g_zbNIB.seqNum++;
	nwkHdr.radius = 1;
	nwkHdr.frameHdrLen = getNwkHdrSize(&nwkHdr);

	nwkEndDevTimeoutRspCmdSend(buf, &nwkHdr, (uint8_t *)&rsp,
				   NWK_INTERNAL_ENDDEVTIMEOUT_RSP_CMD_HANDLE);
}

void nwkEndDevTimeoutRspCnfHandler(void *arg)
{
	zb_buf_free((zb_buf_t *)arg);
}
#endif
