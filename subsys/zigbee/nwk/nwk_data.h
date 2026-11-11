/* Copyright 2026 libzigbee
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef _NWK_DATA_H
#define _NWK_DATA_H

#include "zb_common.h"

extern uint8_t quickDataPollCnt;
extern uint8_t g_edBrcSkipParent;
extern ev_timer_event_t *quickDataPollTimerEvt;
extern nwkDataIndCb_t g_nwkDataIndCb;

void nwk_tx(zb_buf_t *buf, nwk_hdr_t *pNwkHdr, uint16_t nextHop, uint8_t indirect, uint8_t *payload,
	    uint8_t payloadLen);
void nwk_fwdPacket(zb_buf_t *buf, nwk_hdr_t *pNwkHdr, uint8_t *payload, uint8_t payloadLen);
void zb_NwkNetworkUpdateCmdHandler(void *arg, nwk_hdr_t *pNwkHdr, nwkCmd_t *cmd);
void zb_NwkRejoinReqCmdHandler(void *arg, nwk_hdr_t *pNwkHdr, nwkCmd_t *cmd);
void zb_NwkRejoinRespCmdHandler(void *arg, nwk_hdr_t *pNwkHdr, nwkCmd_t *cmd);
void zb_McpsRejoinRespCnfHandler(void *arg, uint8_t status, uint16_t shortAddr);
void zb_NwkLeaveReqCmdHandler(void *arg, nwk_hdr_t *pNwkHdr, nwkCmd_t *cmd);
void zb_NwkStatusCmdHandler(void *arg, nwk_hdr_t *pNwkHdr, nwkCmd_t *cmd);
void zb_NwkLinkStatusCmdHandler(void *arg, nwk_hdr_t *pNwkHdr, nwkCmd_t *cmd);
void zb_NwkReportCmdHandler(void *arg, nwk_hdr_t *pNwkHdr, nwkCmd_t *cmd);
void nwkReportCmdHandler(void *arg, nwkCmd_t *cmd);
uint8_t zb_NwkInterPanDataReq(void *arg);
void nwkNldeDataCnf(void *arg, uint8_t status, uint8_t nsduHandle);
void zb_NwkBeaconPayloadUpdate(void);
void endDevMacDataPoll(void);
void nwkRouteReqCmdHandler(void *arg, nwk_hdr_t *pNwkHdr, nwkCmd_t *cmd);
void nwkRouteReplyCmdHandler(void *arg, nwk_hdr_t *pNwkHdr, nwkCmd_t *cmd);
void nwkRouteRecordInitiation(uint16_t srcAddr, uint16_t dstAddr);
void nwkRouteRecordCmdHandler(void *arg, nwk_hdr_t *pNwkHdr, nwkCmd_t *cmd);
zb_normal_neighbor_entry_t *nwkValidNeighborToFwd(uint16_t shortAddr);
uint8_t zb_nwkGetAverageLqi(uint8_t oldLqi, uint8_t newLqi);
void zb_MacMcpsDataConfirmHandler(void *arg);
void zb_MacMcpsDataIndicationHandler(void *arg);

#endif
