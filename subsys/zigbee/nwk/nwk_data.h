/* Copyright 2026 libzigbee
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef _NWK_DATA_H
#define _NWK_DATA_H

#include "zb_common.h"

extern u8 quickDataPollCnt;
extern u8 g_edBrcSkipParent;
extern ev_timer_event_t *quickDataPollTimerEvt;
extern nwkDataIndCb_t g_nwkDataIndCb;

void nwk_tx(zb_buf_t *buf, nwk_hdr_t *pNwkHdr, u16 nextHop, u8 indirect, u8 *payload,
	    u8 payloadLen);
void nwk_fwdPacket(zb_buf_t *buf, nwk_hdr_t *pNwkHdr, u8 *payload, u8 payloadLen);
void tl_zbNwkNetworkUpdateCmdHandler(void *arg, nwk_hdr_t *pNwkHdr, nwkCmd_t *cmd);
void tl_zbNwkRejoinReqCmdHandler(void *arg, nwk_hdr_t *pNwkHdr, nwkCmd_t *cmd);
void tl_zbNwkRejoinRespCmdHandler(void *arg, nwk_hdr_t *pNwkHdr, nwkCmd_t *cmd);
void tl_zbMcpsRejoinRespCnfHandler(void *arg, u8 status, u16 shortAddr);
void tl_zbNwkLeaveReqCmdHandler(void *arg, nwk_hdr_t *pNwkHdr, nwkCmd_t *cmd);
void tl_zbNwkStatusCmdHandler(void *arg, nwk_hdr_t *pNwkHdr, nwkCmd_t *cmd);
void tl_zbNwkLinkStatusCmdHandler(void *arg, nwk_hdr_t *pNwkHdr, nwkCmd_t *cmd);
void tl_zbNwkReportCmdHandler(void *arg, nwk_hdr_t *pNwkHdr, nwkCmd_t *cmd);
void nwkReportCmdHandler(void *arg, nwkCmd_t *cmd);
u8 tl_zbNwkInterPanDataReq(void *arg);
void nwkNldeDataCnf(void *arg, u8 status, u8 nsduHandle);
void tl_zbNwkBeaconPayloadUpdate(void);
void endDevMacDataPoll(void);
void nwkRouteReqCmdHandler(void *arg, nwk_hdr_t *pNwkHdr, nwkCmd_t *cmd);
void nwkRouteReplyCmdHandler(void *arg, nwk_hdr_t *pNwkHdr, nwkCmd_t *cmd);
void nwkRouteRecordInitiation(u16 srcAddr, u16 dstAddr);
void nwkRouteRecordCmdHandler(void *arg, nwk_hdr_t *pNwkHdr, nwkCmd_t *cmd);
tl_zb_normal_neighbor_entry_t *nwkValidNeighborToFwd(u16 shortAddr);
u8 tl_nwkGetAverageLqi(u8 oldLqi, u8 newLqi);
void tl_zbMacMcpsDataConfirmHandler(void *arg);
void tl_zbMacMcpsDataIndicationHandler(void *arg);

#endif
