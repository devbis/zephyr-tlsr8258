/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Router-mode ZDO helpers. Symmetric counterpart to zdo_ed_minimal.c.
 *
 * zdo_cfg_attributes itself is declared in subsys/zigbee/zdo/zdo.c (the
 * libzigbee port). zdo.c only declares it — it leaves the struct
 * zero-initialized, which makes zdo_nwkDiscoveryStart() return
 * ZDO_NOT_PERMITTED because config_nwk_scan_attempts == 0.
 *
 * Populate the attributes here via a SYS_INIT() so the router build
 * gets non-zero defaults without conflicting with zdo.c's tentative
 * definition. ED build keeps its own static initializer in
 * zdo_ed_minimal.c (not linked for router).
 */

#include <zephyr/init.h>

#include "zb_common_stub.h"
#include "zdo_api.h"
#include "af/zb_af.h"
#include "aps/aps_api.h"
#include "nwk/includes/nwk.h"
#include "mac/includes/mac_internal.h"
#include "ss/ss_internal.h"

/*
 * Router APS/ZDO data-send bridge.
 *
 * The router build links the WEAK no-op af_dataSend stub from
 * zb_api_bdb_ed_compat.c (which just returns APS_STATUS_NOT_SUPPORTED) —
 * the real implementation lives in zb_api_zdo_send_minimal.c, which is NOT
 * compiled for the router (CMakeLists comment: "superseded by the libzigbee
 * zdo/zdp_services.c ports"), yet no replacement was ever provided. The net
 * effect: every outbound ZDO/ZCL APS frame on the router (Device Announce,
 * Node-Descriptor / Active-EP / Simple-Descriptor responses, etc.) was
 * silently dropped, so a joined router transmitted nothing at the NWK layer
 * and the coordinator could never interview it (Z2M: "can not get node
 * descriptor"). See zephyr-docs/router-aps-join-fixes-handoff-2026-06-23.md.
 *
 * This strong definition overrides the weak stub for the router build and
 * routes application data through the full stack: build the 8-byte APS data
 * header, then submit an NLDE-DATA.request (NWK_NLDE_DATA_REQ) which the
 * libzigbee nwk_data.c port carries down through nwk_tx -> MAC -> radio with
 * proper NWK security and (for unicast) routing.
 *
 * Only short-addressed (unicast/broadcast) frames with an endpoint are
 * supported — the only modes zdo_send_req()/ZCL actually use on this build.
 */
#define AF_APS_DATA_HDR_LEN  8U

u8 af_dataSend(u8 srcEp, epInfo_t *pDstEpInfo, u16 clusterId, u16 cmdPldLen,
	       u8 *cmdPld, u8 *apsCnt)
{
	zb_buf_t *buf;
	nlde_data_req_t *req;
	u8 *aps;
	u16 dstShort;
	bool broadcast;
	u8 apsCounter;

	if (srcEp > 240U) {
		return APS_STATUS_NOT_SUPPORTED;
	}
	if (pDstEpInfo == NULL || (cmdPld == NULL && cmdPldLen != 0U)) {
		return APS_STATUS_INVALID_PARAMETER;
	}
	if (pDstEpInfo->dstAddrMode != APS_SHORT_DSTADDR_WITHEP) {
		/* Group/IEEE-addressed sends are not used by this build's ZDO. */
		return APS_STATUS_NOT_SUPPORTED;
	}
	if ((u16)(AF_APS_DATA_HDR_LEN + cmdPldLen) > ZB_BUF_SIZE) {
		return APS_STATUS_ASDU_TOO_LONG;
	}

	buf = zb_buf_allocate();
	if (buf == NULL) {
		return APS_STATUS_INTERNAL_BUF_FULL;
	}

	/* Reserve the APS frame at the buffer tail; nwk_tx/MAC prepend their
	 * headers in the space ahead of it (see tl_bufInitalloc). */
	aps = (u8 *)tl_bufInitalloc(buf, (u8)(AF_APS_DATA_HDR_LEN + cmdPldLen));
	if (aps == NULL) {
		zb_buf_free(buf);
		return APS_STATUS_ASDU_TOO_LONG;
	}

	dstShort = pDstEpInfo->dstAddr.shortAddr;
	broadcast = ZB_NWK_IS_ADDRESS_BROADCAST(dstShort);
	apsCounter = aps_get_counter_value();

	/* APS data-frame header (mirrors zb_minimal_build_aps_header). */
	aps[0] = (u8)((broadcast ? 0x02U : 0x00U) << 2);
	if (!broadcast && (pDstEpInfo->txOptions & APS_TX_OPT_ACK_TX) != 0U) {
		aps[0] |= BIT(6);
	}
	aps[1] = pDstEpInfo->dstEp;
	COPY_U16TOBUFFER(&aps[2], clusterId);
	COPY_U16TOBUFFER(&aps[4], pDstEpInfo->profileId);
	aps[6] = srcEp;
	aps[7] = apsCounter;
	if (cmdPldLen != 0U) {
		memcpy(&aps[AF_APS_DATA_HDR_LEN], cmdPld, cmdPldLen);
	}

	/* NLDE-DATA.request. nlde_data_req_t overlays buf->buf[0]; nsdu points
	 * at the tail region just built. buf->hdr lives past buf[] so it is
	 * untouched by this memset. */
	req = (nlde_data_req_t *)buf;
	memset(req, 0, sizeof(*req));
	req->dstAddr = dstShort;
	req->addrMode = 0U;        /* not a group cast */
	req->radius = (pDstEpInfo->radius != 0U) ? pDstEpInfo->radius : 30U;
	req->discoverRoute = broadcast ? 0U : 1U;
	req->securityEnable = (ss_ib.securityLevel != 0U) ? 1U : 0U;
	req->ndsuHandle = NWK_INTERNAL_NSDU_HANDLE; /* fire-and-forget: buf freed on cnf */
	req->nsdu = aps;
	req->nsduLen = (u8)(AF_APS_DATA_HDR_LEN + cmdPldLen);
	req->useAlias = pDstEpInfo->useAlias;
	req->aliasSrcAddr = pDstEpInfo->aliasSrcAddr;
	req->aliasSeqNum = pDstEpInfo->aliasSeqNum;
	req->unicastSkipRouting =
		(!broadcast && dstShort == g_zbInfo.macPib.coordShortAddress) ? 1U : 0U;
	if (req->unicastSkipRouting) {
		req->discoverRoute = 0U;
	}

	/*
	 * nwk_fwdPacket() (the unicast/broadcast NLDE-DATA route path) rejects
	 * any buffer whose hdr.used flag is clear as a "stale buffer" guard.
	 * zb_buf_allocate() zero-inits the whole buffer (used=0), so without
	 * this every ZDO/announce frame routed via nwk_fwdPacket was silently
	 * dropped (Device Announce never went on air). Mark the carrier active.
	 */
	((zb_buf_t *)buf)->hdr.used = 1U;

	if (apsCnt != NULL) {
		*apsCnt = apsCounter;
	}

	if (tl_zbPrimitivePost(TL_Q_HIGH2NWK, NWK_NLDE_DATA_REQ, buf) != RET_OK) {
		zb_buf_free(buf);
		return APS_STATUS_INTERNAL_BUF_FULL;
	}
	return APS_STATUS_SUCCESS;
}

static int zdo_router_minimal_attr_init(void)
{
	zdo_cfg_attributes.config_nwk_indirectPollRate = 1000U;
	zdo_cfg_attributes.config_nwk_time_btwn_scans = 100U;
	/* Match libzigbee BDB: only one attempt is needed because the
	 * BDB layer drives the retry loop itself. Higher values cause
	 * zdo_nlme_network_discovery_confirm_cb to queue more rounds
	 * via zdo_nwkDiscReqTimerCb instead of returning to BDB, which
	 * then never reaches bdb_nwkDiscCnfCb -> zb_assocJoinReq.
	 */
	zdo_cfg_attributes.config_nwk_scan_attempts = 1U;
	zdo_cfg_attributes.config_permit_join_duration = 0U;
	zdo_cfg_attributes.config_parent_link_retry_threshold = 3U;
	zdo_cfg_attributes.config_accept_nwk_update_pan_id = 0xFFFFU;
	zdo_cfg_attributes.config_accept_nwk_update_channel = 0xFFU;
	zdo_cfg_attributes.config_nwk_scan_duration = 5U;
	return 0;
}

SYS_INIT(zdo_router_minimal_attr_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
