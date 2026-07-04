/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Phase-2 placeholder stubs for the experimental CONFIG_ZIGBEE_ED_LIBZIGBEE
 * build (ED on the full libzigbee stack).
 *
 * The Zephyr platform/app layer (zb_main.c, zb_bdb_bootstrap.c,
 * zb_persistence_zephyr.c, app_bdb.c) is currently wired to the hand-written
 * minimal-ED API (tl_zbNwkEdMinimal*) and the minimal ZDO sender
 * (zb_zdoActiveEpReq in zb_api_zdo_send_minimal.c). Those minimal sources are
 * NOT compiled in the libzigbee-based ED build, so these symbols are undefined.
 *
 * These stubs let the libzigbee-based ED image LINK (phase-2 milestone: the full
 * libzigbee MAC/NWK/APS/SS/ZDO core compiles and links as an ED). They are
 * intentionally inert — the ED boots but does NOT auto-join yet. Phase-3 replaces
 * the CALLERS with real libzigbee ED wiring (bdb_endDeviceStart / zb_rejoinReq /
 * endDevMacDataPoll / nwk_endDev_timeout keepalive) and routes ZDO sends through
 * the libzigbee zdp_services/zb_api path, after which this file is deleted.
 */

#include "zb_common_stub.h"
#include "zbapi/zb_api.h"
#include "zb_local.h"

/*
 * Router/parent-side RX command handlers referenced by the shared MAC/NWK/APS
 * dispatch (tl_zbMacMcpsDataIndicationHandler, af_aps_data_entry) but defined
 * only under ZB_ROUTER_ROLE. A directly-joining ED never receives these
 * (rejoin-request / network-report / mgmt-permit-join / parent-announce are
 * parent/coordinator functions; device-announce is an RX indication the ED does
 * not need to act on), so inert stubs are correct for the ED role.
 */
void tl_zbNwkRejoinReqCmdHandler(void *arg, nwk_hdr_t *pNwkHdr, nwkCmd_t *cmd)
{
	(void)arg; (void)pNwkHdr; (void)cmd;
}

void tl_zbNwkReportCmdHandler(void *arg, nwk_hdr_t *pNwkHdr, nwkCmd_t *cmd)
{
	(void)arg; (void)pNwkHdr; (void)cmd;
}

void zdo_deviceAnnounceIndicate(void *arg)
{
	(void)arg;
}

void zdo_parentAnnounceIndicate(void *arg)
{
	(void)arg;
}

void zdo_mgmtPermitJoinIndicate(void *arg)
{
	(void)arg;
}

/*
 * Router/coordinator NLME request + NWK/MAC command handlers referenced by the
 * shared primitive dispatch (tl_zbNwkTaskProc switch, MAC/NWK RX dispatch) but
 * defined only under ZB_ROUTER_ROLE. A directly-joining ED never posts the
 * corresponding primitives (formation, router-start, permit-join, direct-join,
 * route discovery) and safely ignores the router-originated RX commands
 * (route req/reply/record, link-status, network-update, coord-realignment,
 * rejoin-response, leave-as-parent), so inert stubs are correct for the ED.
 * NOTE: NLME-leave is stubbed too — an ED join does not exercise leave; wiring
 * real ED leave is follow-up work.
 */
void tl_zbNwkNlmeNetworkFormationRequestHandler(void *arg) { (void)arg; }
void tl_zbNwkNlmeStartRouterRequestHandler(void *arg) { (void)arg; }
void tl_zbNwkNlmePermitJoiningRequestHandler(void *arg) { (void)arg; }
void tl_zbNwkNlmeDirectJoinRequestHandler(void *arg) { (void)arg; }
void tl_zbNwkNlmeRouteDiscRequestHandler(void *arg) { (void)arg; }
void tl_zbNwkNlmeLeaveRequestHandler(void *arg) { (void)arg; }
void tl_zbNwkLinkStatusCmdHandler(void *arg, nwk_hdr_t *h, nwkCmd_t *c) { (void)arg; (void)h; (void)c; }
void tl_zbNwkNetworkUpdateCmdHandler(void *arg, nwk_hdr_t *h, nwkCmd_t *c) { (void)arg; (void)h; (void)c; }
void tl_zbNwkLeaveReqCmdHandler(void *arg, nwk_hdr_t *h, nwkCmd_t *c) { (void)arg; (void)h; (void)c; }
void nwkRouteReqCmdHandler(void *arg, nwk_hdr_t *h, nwkCmd_t *c) { (void)arg; (void)h; (void)c; }
void nwkRouteReplyCmdHandler(void *arg, nwk_hdr_t *h, nwkCmd_t *c) { (void)arg; (void)h; (void)c; }
void nwkRouteRecordCmdHandler(void *arg, nwk_hdr_t *h, nwkCmd_t *c) { (void)arg; (void)h; (void)c; }
void nwk_leaveCmdSendCnf(void *arg, u16 dstAddr) { (void)arg; (void)dstAddr; }
int nwkLeaveReqSend(void *arg, nwk_hdr_t *h, nwkCmd_t *c, u8 handle)
{
	(void)arg; (void)h; (void)c; (void)handle;
	return 0;
}
void tl_zbMcpsRejoinRespCnfHandler(void *arg, u8 status, u16 shortAddr)
{
	(void)arg; (void)status; (void)shortAddr;
}
u8 tl_zbMacMlmeCoordRealignmentCmdSend(u8 rxOnWhenIdle, const u8 *orphanAddr, u16 shortAddr,
				       void *arg)
{
	(void)rxOnWhenIdle; (void)orphanAddr; (void)shortAddr; (void)arg;
	return 0;
}

/*
 * ED parent short-address getter. The vendor's neighbor-table implementation of
 * this was not ported; for an End Device the parent is the node it associated
 * with, tracked in the MAC PIB coordShortAddress.
 */
u16 tl_zbNeighborParentShortAddrGet(void)
{
	return g_zbMacPib.coordShortAddress;
}

bool tl_zbNwkEdMinimalRejoinStart(u32 scanChannels, u8 scanDuration, bool withBackoff)
{
	(void)scanChannels;
	(void)scanDuration;
	(void)withBackoff;
	return false; /* TODO(phase3): bdb_endDeviceStart()/zb_rejoinReq() */
}

void tl_zbNwkEdMinimalPollRestart(u32 timeoutMs)
{
	(void)timeoutMs; /* TODO(phase3): endDevMacDataPoll() scheduling */
}

void tl_zbNwkEdMinimalOperationAbort(void)
{
	/* TODO(phase3): abort in-flight libzigbee join/rejoin */
}

void tl_zbNwkEdMinimalRuntimeReset(void)
{
	/* TODO(phase3): reset libzigbee ED runtime/NIB state */
}

void tl_zbNwkEdMinimalSetFixedJoinTarget(u8 channel, u16 panId, u16 shortAddr,
					 const u8 *extPanId, const u8 *nwkKey, const u8 *tcAddr)
{
	(void)channel;
	(void)panId;
	(void)shortAddr;
	(void)extPanId;
	(void)nwkKey;
	(void)tcAddr; /* TODO(phase3): seed libzigbee fixed-join parameters */
}

zdo_status_t zb_zdoActiveEpReq(u16 dstNwkAddr, zdo_active_ep_req_t *pReq, u8 *seqNo,
			       zdo_callback indCb)
{
	(void)dstNwkAddr;
	(void)pReq;
	(void)seqNo;
	(void)indCb;
	return ZDO_NOT_SUPPORTED; /* TODO(phase3): route via libzigbee zdp_services */
}
