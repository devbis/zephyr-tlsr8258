/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Zephyr ZDO/AF platform glue for the full libzigbee stack.
 *
 * zdo_cfg_attributes itself is declared in subsys/zigbee/zdo/zdo.c (the
 * libzigbee port). zdo.c only declares it — it leaves the struct
 * zero-initialized, which makes zdo_nwkDiscoveryStart() return
 * ZDO_NOT_PERMITTED because config_nwk_scan_attempts == 0.
 *
 * Populate the attributes here via SYS_INIT() so every full-stack role gets
 * non-zero defaults without conflicting with zdo.c's tentative definition.
 */

#include <zephyr/init.h>
#include <zephyr/kernel.h>

#include "zb_common_stub.h"
#include "zdo_api.h"
#include "af/zb_af.h"
#include "aps/aps_api.h"
#include "nwk/includes/nwk.h"
#include "mac/includes/mac_internal.h"
#include "ss/ss_internal.h"
#include "zdo/zdp.h"

/* ZDP endpoint metadata and response handoff from libzigbee/src/zdp.c. */
static const u16 zdo_in_clusters[] = {
	NWK_ADDR_RSP_CLID,
	IEEE_ADDR_RSP_CLID,
	NODE_DESC_RSP_CLID,
	POWER_DESC_RSP_CLID,
	SIMPLE_DESC_RSP_CLID,
	ACTIVE_EP_RSP_CLID,
	MATCH_DESC_RSP_CLID,
};

static const u16 zdo_out_clusters[] = {
	NWK_ADDR_REQ_CLID,
	IEEE_ADDR_REQ_CLID,
	NODE_DESC_REQ_CLID,
	POWER_DESC_REQ_CLID,
	SIMPLE_DESC_REQ_CLID,
	ACTIVE_EP_REQ_CLID,
	MATCH_DESC_REQ_CLID,
	DEVICE_ANNCE_CLID,
};

static const af_simple_descriptor_t zdo_simple_desc = {
	.app_profile_id = ZDO_PROFILE_ID,
	.app_dev_id = 0U,
	.endpoint = ZDO_EP,
	.app_dev_ver = 0U,
	.reserved = 0U,
	.app_in_cluster_count = ARRAY_SIZE(zdo_in_clusters),
	.app_out_cluster_count = ARRAY_SIZE(zdo_out_clusters),
	.app_in_cluster_lst = (u16 *)zdo_in_clusters,
	.app_out_cluster_lst = (u16 *)zdo_out_clusters,
};

static void zdo_zdp_data_indication(void *arg)
{
	aps_data_ind_t *aps_ind = (aps_data_ind_t *)arg;
	zdo_zdpDataInd_t *zdp_ind = (zdo_zdpDataInd_t *)arg;

	if (aps_ind == NULL || aps_ind->asdu == NULL || aps_ind->asduLength < 2U) {
		zb_buf_free((zb_buf_t *)arg);
		return;
	}

	/* zdp_cb_process() consumes the vendor-shaped zdo_zdpDataInd_t overlay
	 * synchronously, so the RX carrier can be released immediately after it. */
	zdp_ind->zpdu = aps_ind->asdu;
	zdp_ind->src_addr = aps_ind->src_short_addr;
	zdp_ind->clusterId = aps_ind->cluster_id;
	zdp_ind->seq_num = aps_ind->asdu[0];
	zdp_ind->status = aps_ind->asdu[1];
	zdp_ind->length = aps_ind->asduLength;
	zdp_cb_process(zdp_ind->seq_num, arg);
	zb_buf_free((zb_buf_t *)arg);
}

void zdp_init(void)
{
	(void)af_endpointRegister(ZDO_EP, (af_simple_descriptor_t *)&zdo_simple_desc,
				 zdo_zdp_data_indication, NULL);
}

/*
 * APS/ZDO data-send bridge.
 *
 * Routes application data through the full stack: build the 8-byte APS data
 * header, then submit an NLDE-DATA.request (NWK_NLDE_DATA_REQ) which the
 * libzigbee nwk_data.c port carries down through nwk_tx -> MAC -> radio with
 * proper NWK security and (for unicast) routing.
 *
 * Only short-addressed (unicast/broadcast) frames with an endpoint are
 * supported — the only modes zdo_send_req()/ZCL actually use on this build.
 */
#define AF_APS_DATA_HDR_LEN  8U

static u8 zdo_announce_seq;

/*
 * The full ZDO service calls this API after a normal join, but a restored
 * session may reach the application without that callback. Keep the
 * platform implementation here so fresh joins and restored sessions use
 * the same APS/NWK transmit bridge.
 */
u8 zb_zdoSendDevAnnance(void)
{
	u8 payload[1U + sizeof(zdo_device_annce_req_t)];
	epInfo_t dst;
	u8 capability = 0U;
	u8 status;

	/* Give the coordinator time to finish the association exchange. */
	k_sleep(K_MSEC(40));

	payload[0] = zdo_announce_seq++;
	COPY_U16TOBUFFER(&payload[1], g_zbNIB.nwkAddr);
	memcpy(&payload[3], g_zbMacPib.extAddress, sizeof(addrExt_t));

	capability |= g_zbNIB.capabilityInfo.altPanCoord ? BIT(0) : 0U;
	capability |= g_zbNIB.capabilityInfo.devType ? BIT(1) : 0U;
	capability |= g_zbNIB.capabilityInfo.powerSrc ? BIT(2) : 0U;
	capability |= g_zbMacPib.rxOnWhenIdle ? BIT(3) : 0U;
	capability |= g_zbNIB.capabilityInfo.secuCapability ? BIT(6) : 0U;
	capability |= BIT(7);
	payload[11] = capability;

	memset(&dst, 0, sizeof(dst));
	dst.dstAddrMode = APS_SHORT_DSTADDR_WITHEP;
	dst.dstAddr.shortAddr = NWK_BROADCAST_RX_ON_WHEN_IDLE;
	dst.dstEp = ZDO_EP;
	dst.profileId = ZDO_PROFILE_ID;
	dst.radius = 30U;

	status = af_dataSend(ZDO_EP, &dst, DEVICE_ANNCE_CLID,
			     sizeof(payload), payload, NULL);
	return status;
}

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

	/* APS data-frame header follows the libzigbee APS data request layout. */
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
	/* Leave response completion is the handoff point for the subsequent local
	 * leave. Other internal sends remain fire-and-forget. */
	req->ndsuHandle = (clusterId == MGMT_LEAVE_RSP_CLID) ?
		NWK_INTERNAL_MGMT_LEAVE_RSP_HANDLE : NWK_INTERNAL_NSDU_HANDLE;
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

static int zdo_platform_attr_init(void)
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

SYS_INIT(zdo_platform_attr_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
