/* SPDX-License-Identifier: Apache-2.0 */
/*
 * NWK PAN-ID conflict detection / network-report / network-update.
 *
 * Adapted from libzigbee/src/nwk_panid_conflict.c (~400 LOC). Adaptations:
 *
 *   * vendor "zb_local.h" / "zdo_internal.h" → zb_common_stub.h +
 *     nwk/includes/nwk_internal.h
 *   * Whole router-mode body wrapped in ZB_ROUTER_ROLE; the vendor file
 *     ships a smaller ED fallback at the bottom which is preserved
 *     untouched so the router build only links the router path.
 *   * forward-declared zdo/aps/MAC helpers that other TUs still own.
 *   * tabs / Zephyr-style formatting; logic preserved verbatim.
 */

#include "zb_common_stub.h"
#include "nwk/includes/nwk.h"
#include "nwk/includes/nwk_internal.h"
#include "mac/includes/tl_zb_mac.h"
#include "os/ev_timer.h"

#include <string.h>

extern void zb_buf_free(zb_buf_t *buf);
extern u8 tl_zbMacMlmeBeaconRequestCmdSend(void);
extern void tl_zbNwkBeaconPayloadUpdate(void);
extern u16 zdo_af_get_accept_nwk_update_pan_id(void);
/* af_nodeDevTypeGet / zdoAppIndCbLst come from af/zb_af.h and
 * zdo/zdo_api.h respectively (pulled in via zb_common_stub.h).
 */

#if defined(ZB_ROUTER_ROLE) && ZB_ROUTER_ROLE

typedef struct {
	ev_timer_event_t *evt;
	extPANId_t epid;
	u8 cnt;
} nwk_panidConflictDetect_t;

nwk_panidConflictDetect_t g_panIdConflictDected = { NULL, {0}, 0 };
ev_timer_event_t *panidConflictTimerEvt = NULL;

static inline bool nwk_panid_security_enabled(void)
{
	return (ss_ib.secureAllFresh != 0U) &&
	       (ss_ib.securityLevel != 0U) &&
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

static void nwk_panid_hdr_init(nwk_hdr_t *hdr, u16 dstAddr, bool security)
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
	memcpy(hdr->srcIeeeAddr, g_zbNIB.ieeeAddr, EXT_ADDR_LEN);
	hdr->frameHdrLen = getNwkHdrSize(hdr);
}

void nwkReportCmdSend(zb_buf_t *buf, nwk_hdr_t *pNwkHdr, nwkCmd_t *cmd, u8 handle)
{
	u8 count = nwk_panid_report_count(&cmd->nwkReport);
	u8 payloadLen = (u8)(10U + (u8)(count << 1));
	u8 *payload = tl_bufInitalloc(buf, payloadLen);

	payload[0] = cmd->cmdId;
	payload[1] = *(u8 *)&cmd->nwkReport.options;
	memcpy(payload + 2, cmd->nwkReport.epid, EXT_ADDR_LEN);
	memcpy(payload + 10, cmd->nwkReport.panIds, (u8)(count << 1));
	buf->hdr.handle = handle;
	nwk_fwdPacket(buf, pNwkHdr, payload, payloadLen);
}

void nwkUpdateCmdSend(zb_buf_t *buf, nwk_hdr_t *pNwkHdr, nwkCmd_t *cmd, u8 handle)
{
	u8 *payload = tl_bufInitalloc(buf, 13);

	payload[0] = cmd->cmdId;
	payload[1] = *(u8 *)&cmd->nwkUpdate.options;
	memcpy(payload + 2, cmd->nwkUpdate.epid, EXT_ADDR_LEN);
	payload[10] = cmd->nwkUpdate.updateId;
	memcpy(payload + 11, &cmd->nwkUpdate.newPanId, sizeof(cmd->nwkUpdate.newPanId));
	buf->hdr.handle = handle;
	nwk_fwdPacket(buf, pNwkHdr, payload, 13);
}

int tl_zbNwkPanidConflictSetPanidCb(void *arg)
{
	ARG_UNUSED(arg);

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
	ARG_UNUSED(arg);

	(void)tl_zbMacMlmeBeaconRequestCmdSend();

	if (--g_panIdConflictDected.cnt != 0U) {
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

	if (memcmp(epid, g_zero_addr, EXT_ADDR_LEN) != 0 &&
	    memcmp(epid, g_zbNIB.extPANId, EXT_ADDR_LEN) != 0) {
		return FALSE;
	}

	g_sysDiags.panIdConflictCheck++;

	if (g_panIdConflictDected.evt != NULL) {
		if (memcmp(g_panIdConflictDected.epid, epid, EXT_ADDR_LEN) != 0) {
			return FALSE;
		}

		ev_timer_taskCancel(&g_panIdConflictDected.evt);
		return TRUE;
	}

	memcpy(g_panIdConflictDected.epid, epid, EXT_ADDR_LEN);
	g_panIdConflictDected.cnt = 2;
	g_panIdConflictDected.evt =
		ev_timer_taskPost(tl_zbNwkPanidConflictDoubleCheck, NULL, 20);

	return FALSE;
}

void tl_zbNwkReportForPanidConflict(zb_buf_t *buf)
{
	nwk_hdr_t hdr;
	nwkCmd_t cmd;
	u16 managerAddr = g_zbNIB.managerAddr;

	nwk_panid_hdr_init(&hdr, managerAddr, nwk_panid_security_enabled());
	if (tl_zbExtAddrByShortAddr(managerAddr, hdr.dstIeeeAddr, NULL) == 0U) {
		hdr.frameControl.dstIEEEAddr = 1;
	}

	memset(&cmd, 0, sizeof(cmd));
	cmd.cmdId = NWK_CMD_NETWORK_REPORT;
	cmd.nwkReport.options.reportInfoCnt = g_zbNwkCtx.known_panids_cnt;
	cmd.nwkReport.options.reportCmdId = 0;
	memcpy(cmd.nwkReport.epid, g_zbNIB.extPANId, EXT_ADDR_LEN);
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
	memcpy(cmd.nwkUpdate.epid, g_zbNIB.extPANId, EXT_ADDR_LEN);
	cmd.nwkUpdate.updateId = g_zbNIB.updateId;
	cmd.nwkUpdate.newPanId = newPanId;

	if (zdoAppIndCbLst != NULL &&
	    zdoAppIndCbLst->zdpNwkUpdateIndCb != NULL &&
	    !zdoAppIndCbLst->zdpNwkUpdateIndCb(&cmd.nwkUpdate)) {
		zb_buf_free(buf);
		return;
	}

	nwkUpdateCmdSend(buf, &hdr, &cmd,
			 NWK_INTERNAL_NETWORK_UPDATE_CMD_PAN_ID_UPDATE_HANDLE);
}

void tl_zbNwkPanidConflictProcess(void *arg)
{
	zb_buf_t *buf = (zb_buf_t *)arg;

	if (g_zbNIB.panId != g_zbNwkCtx.new_panid) {
		tl_zbNwkReportForPanidConflict(buf);
		return;
	}

	for (;;) {
		u16 newPanId = (u16)drv_u32Rand();

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

		g_zbNIB.updateId++;
		tl_zbNwkUpdateForPanidConflict(buf, newPanId);
		return;
	}
}

void nwkReportCmdHandler(void *arg, nwkCmd_t *cmd)
{
	if (g_zbNwkCtx.panIdConflict ||
	    cmd->nwkReport.options.reportCmdId != 0U) {
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

	if (memcmp(cmd->nwkUpdate.epid, g_zbNIB.extPANId, EXT_ADDR_LEN) != 0) {
		zb_buf_free((zb_buf_t *)arg);
		return;
	}

	{
		u16 acceptPanId = zdo_af_get_accept_nwk_update_pan_id();

		if (acceptPanId != 0U &&
		    acceptPanId != MAC_PAN_ID_BROADCAST &&
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

#endif /* ZB_ROUTER_ROLE */
