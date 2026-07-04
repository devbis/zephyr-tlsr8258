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
