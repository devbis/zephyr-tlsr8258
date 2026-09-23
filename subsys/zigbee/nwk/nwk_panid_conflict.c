/*
 * Copyright 2026 libzigbee
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "zb_common.h"
#include "ev_timer.h"
#include "zb_nwk_core.h"
#include "nwk_brc.h"
#include "nwk_data.h"
#include "nwk_panid_conflict.h"
#include "nwk_routing.h"
#include "mac_scan.h"
#include "zdo_nwk_manager.h"
#include "ss_security_flags.h"

#if defined(ZB_ROUTER_ROLE)
typedef struct {
	ev_timer_event_t *evt;
	extPANId_t epid;
	u8 cnt;
} nwk_panidConflictDetect_t;

nwk_panidConflictDetect_t g_panIdConflictDected = {NULL, {0}, 0};
ev_timer_event_t *panidConflictTimerEvt = NULL;

static inline bool nwk_panid_security_enabled(void)
{
	return ss_ib_secure_all_fresh() && (ss_ib_security_level_get() != 0U) &&
	       ss_keyPreconfigured();
}

static inline u8 nwk_panid_report_count(const nwkCmd_nwkReport_t *report)
{
	return (u8)(*(u8 *)&report->options & 0x1fU);
}

static bool nwk_panid_in_list(const u16 *panIds, u8 count, u16 panId)
{
	for (u8 i = 0; i < count; i++) {
		if (panIds[i] == panId) {
			return TRUE;
		}
	}

	return FALSE;
}

static inline void nwk_panid_hdr_init(nwk_hdr_t *hdr, u16 dstAddr, bool security)
{
	memset(hdr, 0, sizeof(*hdr));

	hdr->dstAddr = dstAddr;
	hdr->srcAddr = g_zbNIB.nwkAddr;
	hdr->radius = (u8)(g_zbNIB.maxDepth << 1);
	hdr->seqNum = g_zbNIB.seqNum++;
	hdr->frameControl.frameType = FRAME_TYPE_COMMAND;
	hdr->frameControl.protocolVer = ZB_PROTOCOL_VERSION;
	hdr->frameControl.discRoute = 1;
	hdr->frameControl.srcIEEEAddr = 1;
	hdr->frameControl.security = security ? 1U : 0U;
	ZB_IEEE_ADDR_COPY(hdr->srcIeeeAddr, g_zbMacPib.extAddress); /* g_zbInfo+12 */
	hdr->frameHdrLen = getNwkHdrSize(hdr);
}

void nwkReportCmdSend(zb_buf_t *buf, nwk_hdr_t *pNwkHdr, nwkCmd_t *cmd, u8 handle)
{
	u8 count = nwk_panid_report_count(&cmd->nwkReport);
	u8 payloadLen = (u8)(10U + (u8)(count << 1));
	u8 *payload;

	TL_BUF_INITIAL_ALLOC(buf, payloadLen, payload, u8 *);

	payload[0] = cmd->cmdId;
	payload[1] = *(u8 *)&cmd->nwkReport.options;
	ZB_EXTPANID_COPY(payload + 2, cmd->nwkReport.epid);
	memcpy(payload + 10, cmd->nwkReport.panIds, (u8)(count << 1));
	buf->hdr.handle = handle;
	nwk_fwdPacket(buf, pNwkHdr, payload, payloadLen);
}

void nwkUpdateCmdSend(zb_buf_t *buf, nwk_hdr_t *pNwkHdr, nwkCmd_t *cmd, u8 handle)
{
	u8 *payload;

	TL_BUF_INITIAL_ALLOC(buf, 13, payload, u8 *);

	payload[0] = cmd->cmdId;
	payload[1] = *(u8 *)&cmd->nwkUpdate.options;
	ZB_EXTPANID_COPY(payload + 2, cmd->nwkUpdate.epid);
	payload[10] = cmd->nwkUpdate.updateId;
	memcpy(payload + 11, &cmd->nwkUpdate.newPanId, sizeof(cmd->nwkUpdate.newPanId));
	buf->hdr.handle = handle;
	nwk_fwdPacket(buf, pNwkHdr, payload, 13);
}

int tl_zbNwkPanidConflictSetPanidCb(void *arg)
{
	(void)arg;

	zb_buf_t *buf = zb_buf_allocate();

	if (buf != NULL) {
		zb_mac_mlme_start_req_t *req = (zb_mac_mlme_start_req_t *)buf;

		memset(req, 0, sizeof(*req));
		req->panId = g_zbNwkCtx.new_panid;
		req->logicalChannel = g_zbMacCtx.curChannel;
		req->beaconOrder = 15;
		req->superframeOrder = 15;
		req->panCoordinator = af_nodeDevTypeGet();
		req->batteryLifeExt = 0;
		g_zbNwkCtx.state = NLME_STATE_PANID_CONFLICT;
		tl_zbPrimitivePost(TL_Q_NWK2MAC, MAC_MLME_START_REQ, buf);
		g_sysDiags.panIdModified++;
	}

	panidConflictTimerEvt = NULL;

	return -1;
}

void tl_zbNwkPanidConflictSetPanidStart(void)
{
	if (panidConflictTimerEvt == NULL) {
		panidConflictTimerEvt = ev_timer_taskPost(tl_zbNwkPanidConflictSetPanidCb, NULL,
							  g_zbNIB.nwkBroadcastDeliveryTime);
	}
}

int tl_zbNwkPanidConflictDoubleCheck(void *arg)
{
	(void)arg;

	(void)tl_zbMacMlmeBeaconRequestCmdSend();

	/* "12: tshftls r2,r2,#24; 14: tcmp r2,#0; 16: tjle 1e" - the counter is
	 * tested as a signed byte, so a zero counter expires immediately instead
	 * of wrapping to 255. */
	if ((s8)(--g_panIdConflictDected.cnt) > 0) {
		return 2000;
	}

	g_panIdConflictDected.evt = NULL;

	return -1;
}

bool tl_zbNwkPanidConflictDetect(u16 panId, extPANId_t epid)
{
	if (g_zbNIB.panId != panId) {
		return FALSE;
	}

	/* A conflict is a matching short PAN ID with a *different* extended PAN ID.
	 * The vendor library only bails out when the EPID is non-zero and matches
	 * ours: "86: tjl memcmp; 8a: tcmp r0,#0; 8c: tjne 2e" continues the check
	 * when the EPIDs differ. */
	if (!ZB_EXTPANID_IS_ZERO(epid) && ZB_EXTPANID_CMP(epid, g_zbNIB.extPANId)) {
		return FALSE;
	}

	g_sysDiags.panIdConflictCheck++;

	if (g_panIdConflictDected.evt != NULL) {
		if (!ZB_EXTPANID_CMP(g_panIdConflictDected.epid, epid)) {
			return FALSE;
		}

		ev_timer_taskCancel(&g_panIdConflictDected.evt);
		return TRUE;
	}

	ZB_EXTPANID_COPY(g_panIdConflictDected.epid, epid);
	g_panIdConflictDected.cnt = 2;
	g_panIdConflictDected.evt = ev_timer_taskPost(tl_zbNwkPanidConflictDoubleCheck, NULL, 20);

	return FALSE;
}

void tl_zbNwkReportForPanidConflict(zb_buf_t *buf)
{
	nwk_hdr_t hdr;
	nwkCmd_t cmd;
	u16 managerAddr = g_zbNIB.managerAddr;
	u16 managerAddrMapIdx;

	nwk_panid_hdr_init(&hdr, managerAddr, nwk_panid_security_enabled());
	if (tl_zbExtAddrByShortAddr(managerAddr, hdr.dstIeeeAddr, &managerAddrMapIdx) == 0U) {
		hdr.frameControl.dstIEEEAddr = 1;
	}

	memset(&cmd, 0, sizeof(cmd));
	cmd.cmdId = NWK_CMD_NETWORK_REPORT;
	cmd.nwkReport.options.reportInfoCnt = g_zbNwkCtx.known_panids_cnt;
	cmd.nwkReport.options.reportCmdId = 0;
	ZB_EXTPANID_COPY(cmd.nwkReport.epid, g_zbNIB.extPANId);
	cmd.nwkReport.panIds = (u8 *)g_zbNwkCtx.known_panids;
	nwkReportCmdSend(buf, &hdr, &cmd, NWK_INTERNAL_NETWORK_REPORT_CMD_HANDLE);
}

void tl_zbNwkUpdateForPanidConflict(zb_buf_t *buf, u16 newPanId)
{
	nwk_hdr_t hdr;
	nwkCmd_t cmd;

	g_zbNwkCtx.new_panid = newPanId;

	nwk_panid_hdr_init(&hdr, NWK_BROADCAST_ALL_DEVICES, nwk_panid_security_enabled());

	memset(&cmd, 0, sizeof(cmd));
	cmd.cmdId = NWK_CMD_NETWORK_UPDATE;
	cmd.nwkUpdate.options.updateInfoCnt = 1;
	cmd.nwkUpdate.options.updateCmdId = 0;
	ZB_EXTPANID_COPY(cmd.nwkUpdate.epid, g_zbNIB.extPANId);
	cmd.nwkUpdate.updateId = g_zbNIB.updateId;
	cmd.nwkUpdate.newPanId = newPanId;

	if (zdoAppIndCbLst != NULL && zdoAppIndCbLst->zdpNwkUpdateIndCb != NULL &&
	    !zdoAppIndCbLst->zdpNwkUpdateIndCb(&cmd.nwkUpdate)) {
		zb_buf_free(buf);
		return;
	}

	nwkUpdateCmdSend(buf, &hdr, &cmd, NWK_INTERNAL_NETWORK_UPDATE_CMD_PAN_ID_UPDATE_HANDLE);
}

void tl_zbNwkPanidConflictProcess(void *arg)
{
	zb_buf_t *buf = (zb_buf_t *)arg;

	/* Only the network manager picks the replacement PAN ID; everyone else
	 * reports the conflict to it.  The vendor library compares g_zbInfo+104/105
	 * (managerAddr) with g_zbInfo+102/103 (nwkAddr). */
	if (g_zbNIB.managerAddr != g_zbNIB.nwkAddr) {
		tl_zbNwkReportForPanidConflict(buf);
		return;
	}

	for (;;) {
		u16 newPanId = (u16)drv_u32Rand();

		/* The vendor checks new_panid only when the known-PAN list is non-empty
		 * ("4a: ... known_panids_cnt", then "4e: ... new_panid"). */
		if ((g_zbNwkCtx.known_panids_cnt != 0U) && (newPanId == g_zbNwkCtx.new_panid)) {
			continue;
		}

		if (nwk_panid_in_list(g_zbNwkCtx.known_panids, g_zbNwkCtx.known_panids_cnt,
				      newPanId)) {
			continue;
		}

		if (newPanId == 0U) {
			continue;
		}

		g_zbNIB.updateId++;
		tl_zbNwkUpdateForPanidConflict(buf, newPanId);
		return;
	}
}

void nwkReportCmdHandler(void *arg, nwkCmd_t *cmd)
{
	if (g_zbNwkCtx.panIdConflict || cmd->nwkReport.options.reportCmdId != 0U) {
		zb_buf_free((zb_buf_t *)arg);
		return;
	}

	g_zbNwkCtx.panIdConflict = 1;

	for (;;) {
		u16 newPanId = (u16)drv_u32Rand();
		u8 reportCnt = nwk_panid_report_count(&cmd->nwkReport);

		if (newPanId == 0U) {
			continue;
		}

		if (newPanId == g_zbNwkCtx.new_panid) {
			continue;
		}

		if (nwk_panid_in_list(g_zbNwkCtx.known_panids, g_zbNwkCtx.known_panids_cnt,
				      newPanId)) {
			continue;
		}

		if (reportCnt != 0U &&
		    nwk_panid_in_list((const u16 *)cmd->nwkReport.panIds, reportCnt, newPanId)) {
			continue;
		}

		g_zbNIB.updateId++;
		tl_zbNwkUpdateForPanidConflict((zb_buf_t *)arg, newPanId);
		return;
	}
}

void nwkUpdateCmdHandler(void *arg, nwk_hdr_t *pNwkHdr, nwkCmd_t *cmd)
{
	u8 options = *(u8 *)&cmd->nwkUpdate.options;

	if (g_zbNwkCtx.panidUpdateRecv) {
		zb_buf_free((zb_buf_t *)arg);
		return;
	}

	if ((options & (u8)~0x1fU) != 0U) {
		zb_buf_free((zb_buf_t *)arg);
		return;
	}

	if (!ZB_EXTPANID_CMP(cmd->nwkUpdate.epid, g_zbNIB.extPANId)) {
		zb_buf_free((zb_buf_t *)arg);
		return;
	}

	{
		u16 acceptPanId = zdo_af_get_accept_nwk_update_pan_id();

		if (acceptPanId != 0U && acceptPanId != MAC_PAN_ID_BROADCAST &&
		    acceptPanId != cmd->nwkUpdate.newPanId) {
			zb_buf_free((zb_buf_t *)arg);
			return;
		}
	}

	g_zbNwkCtx.panidUpdateRecv = 1;
	g_zbNwkCtx.panIdConflict = 1;
	g_zbNwkCtx.new_panid = cmd->nwkUpdate.newPanId;
	tl_zbNwkPanidConflictSetPanidStart();

	if (pNwkHdr->srcAddr == g_zbNIB.managerAddr) {
		g_zbNIB.updateId = cmd->nwkUpdate.updateId;
	}

	zb_buf_free((zb_buf_t *)arg);
}

void tl_zbNwkNetworkUpdateCmdHandler(void *arg, nwk_hdr_t *pNwkHdr, nwkCmd_t *cmd)
{
	zb_buf_t *buf = (zb_buf_t *)arg;
	zb_mscp_data_ind_t *macInd = (zb_mscp_data_ind_t *)arg;

	if (ZB_NWK_IS_ADDRESS_BROADCAST(pNwkHdr->dstAddr) && pNwkHdr->radius != 0U) {
		nwkBrcTransJitterSet(NWK_BRC_JITTER);

		if (nwkBrcTimerStart(buf, pNwkHdr, macInd->msdu + pNwkHdr->frameHdrLen,
				     (u8)(macInd->msduLength - pNwkHdr->frameHdrLen)) !=
		    NWK_STATUS_SUCCESS) {
			nwk_brcTransRecordEntry_t *record =
				nwkBrcTransEntryFind(pNwkHdr->srcAddr, pNwkHdr->seqNum);

			if (record != NULL) {
				nwkBrcTransTabEntryClear(record);
			}

			zb_buf_free(buf);
			return;
		}
	}

	nwkUpdateCmdHandler(arg, pNwkHdr, cmd);
}

void nwk_panIdConflictCnfHandler(void *arg)
{
	g_zbNIB.panId = g_zbMacPib.panId;

	tl_zbNwkBeaconPayloadUpdate();

	g_zbNwkCtx.state = NLME_STATE_IDLE;
	g_zbNwkCtx.panIdConflict = 0;
	g_zbNwkCtx.panidUpdateRecv = 0;
	g_zbNwkCtx.known_panids_cnt = 0;
	memset(g_zbNwkCtx.known_panids, 0, sizeof(g_zbNwkCtx.known_panids));

	tl_zbNwkNlmeNwkStatusInd(arg, g_zbNIB.managerAddr,
				 NWK_COMMAND_STATUS_PAN_IDENTIFIER_UPDATE);
}

#else

void nwkUpdateCmdHandler(void *arg, nwk_hdr_t *pNwkHdr, nwkCmd_t *cmd)
{
	u8 options = *(u8 *)&cmd->nwkUpdate.options;

	if (g_zbNwkCtx.panidUpdateRecv) {
		zb_buf_free((zb_buf_t *)arg);
		return;
	}

	if ((options & (u8)~0x1fU) != 0U) {
		zb_buf_free((zb_buf_t *)arg);
		return;
	}

	if (!ZB_EXTPANID_CMP(cmd->nwkUpdate.epid, g_zbInfo.nwkNib.extPANId)) {
		zb_buf_free((zb_buf_t *)arg);
		return;
	}

	{
		u16 acceptPanId = zdo_af_get_accept_nwk_update_pan_id();

		if (acceptPanId != 0U && acceptPanId != MAC_PAN_ID_BROADCAST &&
		    acceptPanId != cmd->nwkUpdate.newPanId) {
			zb_buf_free((zb_buf_t *)arg);
			return;
		}
	}

	g_zbNwkCtx.panidUpdateRecv = 1;
	g_zbNwkCtx.panIdConflict = 1;

	if (pNwkHdr->srcAddr == g_zbInfo.nwkNib.managerAddr) {
		g_zbInfo.nwkNib.updateId = cmd->nwkUpdate.updateId;
	}

	zb_buf_free((zb_buf_t *)arg);
}

void tl_zbNwkNetworkUpdateCmdHandler(void *arg, nwk_hdr_t *pNwkHdr, nwkCmd_t *cmd)
{
	nwkUpdateCmdHandler(arg, pNwkHdr, cmd);
}

void nwk_panIdConflictCnfHandler(void *arg)
{
	g_zbInfo.nwkNib.panId = g_zbInfo.macPib.panId;

	tl_zbNwkBeaconPayloadUpdate();

	g_zbNwkCtx.state = NLME_STATE_IDLE;
	g_zbNwkCtx.panIdConflict = 0;
	g_zbNwkCtx.panidUpdateRecv = 0;
	g_zbNwkCtx.known_panids_cnt = 0;
	memset(g_zbNwkCtx.known_panids, 0, sizeof(g_zbNwkCtx.known_panids));

	tl_zbNwkNlmeNwkStatusInd(arg, g_zbNIB.managerAddr,
				 NWK_COMMAND_STATUS_PAN_IDENTIFIER_UPDATE);
}

#endif
