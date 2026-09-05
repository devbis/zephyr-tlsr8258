/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Layer-queue primitive dispatch shim and AF callback bridge.
 *
 * The Zigbee SDK / libzigbee runtime moves MLME / NLDE / NLME
 * primitives between layers via tl_zbPrimitivePost(layerQ, primitive,
 * arg). The vendor implementation (libzigbee/src/zb_task_queue.c)
 * funnels everything through a static per-layer queue (g_zbTaskQ[])
 * polled by ev_main() / tl_zbTaskProcedure().
 *
 * The Zephyr port uses tl_zbTaskPost() (a single Zephyr work queue)
 * for callbacks. This TU provides the platform dispatch boundary for
 * primitives that are not represented by a dedicated Zephyr work item,
 * while the vendor-derived MAC/NWK/APS/ZDO handlers remain authoritative.
 *
 * ZDP request dispatch is kept in zdo/zdo_zephyr_glue.c, where the vendor
 * request table and restricted-mode status contract are preserved. This file
 * hosts only the AF dispatch bridge needed by the Zephyr worker.
 */

#include <zephyr/zigbee/zb_bootstrap.h>

#include "zb_common_stub.h"
#include "mac/includes/mac_internal.h"

#include "af/zb_af.h"
#include "../../zcl/zcl_include.h"

/*
 * GreenPower hook — zdp_services.c::zdo_deviceAnnounceIndicate consults
 * this callback to suppress dev_annce for proxy-table entries that GP
 * has already claimed. Our router build doesn't include the GP subsystem,
 * so provide a NULL fallback (zdp_services already null-checks it).
 */
typedef bool (*gpDeviceAnnounceCheckCb_t)(u16 nwkAddr, const u8 *ieeeAddr);
gpDeviceAnnounceCheckCb_t g_gpDeviceAnnounceCheckCb __attribute__((weak)) = NULL;

/*
 * tl_zbPrimitivePost / tl_zbTaskQPop / tl_zbUserTaskQNum live in
 * platform/zephyr/zb_task_queue_router.c backed by
 * a per-layer k_msgq.
 *
 * zb_buf_allocate / zb_buf_free / tl_bufInitalloc / zb_buf_clear /
 * is_zb_buf / tl_phyRxBufTozbBuf live in
 * platform/zephyr/zb_buffer_zephyr.c backed by a
 * K_MEM_SLAB_DEFINE_STATIC pool of zb_buf_t.
 *
 * The remaining definitions in this file are platform bindings for symbols
 * that are not part of a libzigbee functional translation unit. Functional
 * NWK, buffer, AF and ZDO entry points are implemented by their respective
 * vendor-derived sources or by the Zephyr adapter below.
 */

/* AF confirm and fragmentation entry points mirror libzigbee/src/zb_af_data.c.
 * The carrier for a confirmation is an event-buffer allocation, while the
 * carrier for an APS indication is a Zephyr zb_buf_t.
 */
void af_dataCnfHandler(void *arg)
{
	apsdeDataConf_t *cnf = (apsdeDataConf_t *)arg;
	af_endpoint_descriptor_t *zdo_ep = af_zdoSimpleDescriptorGet();
	af_endpoint_descriptor_t *ep_list = af_epDescriptorGet();
	u8 ep_num = af_availableEpNumGet();

	if (cnf == NULL) {
		return;
	}
	if (zdo_ep != NULL && zdo_ep->cb_cnf != NULL &&
	    cnf->srcEndpoint == zdo_ep->ep) {
		zdo_ep->cb_cnf(arg);
		return;
	}
	for (u8 i = 0; i < ep_num; i++) {
		if (ep_list[i].cb_cnf != NULL && ep_list[i].ep == cnf->srcEndpoint) {
			ep_list[i].cb_cnf(arg);
			return;
		}
	}
	ev_buf_free((u8 *)arg);
}

/*
 * APS data indication dispatcher. ZDP requests are routed to the vendor-
 * derived indicate handlers and ZDP responses to zdp_cb_process(); application
 * frames are delivered to their endpoint callback. This preserves Z2M's
 * ZDP-interview queries (Node Descriptor, Active Endpoint, Simple Descriptor,
 * etc.) addressed to ZDO endpoint 0.
 *
 *   profile_id == ZDO_PROFILE_ID  → dispatch by cluster_id to the
 *                                   zdp_services.c indicate handlers
 *   else                          → invoke the endpoint's cb_rx
 *                                   (zcl_rx_handler for app endpoints)
 *
 * Each indicate handler owns the buffer when it is done; if no handler
 * matches we free the buffer ourselves so the slab pool does not leak.
 *
 * Cluster IDs are from subsys/zigbee/zdo/zdp.h. Responses (bit 15 set)
 * are handed to the registered ZDP response callback.
 */
#include "zdo/zdp.h"

extern af_endpoint_descriptor_t *af_epDescriptorGet(void);
extern u8 af_availableEpNumGet(void);
extern af_endpoint_descriptor_t *af_zdoSimpleDescriptorGet(void);

static u16 zb_dispatch_basic_read_attr(u8 *rsp, u16 pos, u16 max_len, u16 attr_id)
{
	const char *str = NULL;
	u8 type = 0U;
	u8 value = 0U;

	if (rsp == NULL || pos + 3U > max_len) {
		return pos;
	}

	COPY_U16TOBUFFER(&rsp[pos], attr_id);
	pos += 2U;

	switch (attr_id) {
	case ZCL_ATTRID_BASIC_ZCL_VER:
		type = ZCL_DATA_TYPE_UINT8;
		value = 3U;
		break;
	case ZCL_ATTRID_BASIC_APP_VER:
	case ZCL_ATTRID_BASIC_STACK_VER:
	case ZCL_ATTRID_BASIC_HW_VER:
		type = ZCL_DATA_TYPE_UINT8;
		value = 1U;
		break;
	case ZCL_ATTRID_BASIC_MFR_NAME:
		type = ZCL_DATA_TYPE_CHAR_STR;
		str = zb_platform_app_basic_mfr_name();
		break;
	case ZCL_ATTRID_BASIC_MODEL_ID:
		type = ZCL_DATA_TYPE_CHAR_STR;
		str = zb_platform_app_basic_model_id();
		break;
	case ZCL_ATTRID_BASIC_POWER_SOURCE:
		type = ZCL_DATA_TYPE_ENUM8;
		value = POWER_SOURCE_BATTERY;
		break;
	case ZCL_ATTRID_GLOBAL_CLUSTER_REVISION:
		if (pos + 1U + 1U + 2U > max_len) {
			return pos - 2U;
		}
		rsp[pos++] = ZCL_STA_SUCCESS;
		rsp[pos++] = ZCL_DATA_TYPE_UINT16;
		COPY_U16TOBUFFER(&rsp[pos], ZCL_ATTR_GLOBAL_CLUSTER_REVISION_DEFAULT);
		return pos + 2U;
	default:
		rsp[pos++] = ZCL_STA_UNSUPPORTED_ATTRIBUTE;
		return pos;
	}

	rsp[pos++] = ZCL_STA_SUCCESS;
	rsp[pos++] = type;
	if (str != NULL) {
		u8 len = (u8)strlen(str);

		if (pos + 1U + len > max_len) {
			return pos - 2U;
		}
		rsp[pos++] = len;
		memcpy(&rsp[pos], str, len);
		return pos + len;
	}

	if (pos + 1U > max_len) {
		return pos - 2U;
	}
	rsp[pos++] = value;
	return pos;
}

void af_aps_data_entry(void *arg)
{
	zb_buf_t *buf = (zb_buf_t *)arg;
	aps_data_ind_t *ad = (aps_data_ind_t *)arg;

	if (arg == NULL) {
		return;
	}
	if (ad->profile_id == ZDO_PROFILE_ID && ad->dst_ep == ZDO_EP) {
		/* ZDP owns the complete vendor request/response dispatch table. */
		af_endpoint_descriptor_t *zdo_ep = af_zdoSimpleDescriptorGet();

		if (zdo_ep != NULL && zdo_ep->cb_rx != NULL) {
			zdo_ep->cb_rx(arg);
		} else {
			zb_buf_free(buf);
		}
		return;
	} else {
		if (ad->profile_id == HA_PROFILE_ID && ad->cluster_id == ZCL_CLUSTER_GEN_BASIC &&
		    ad->dst_ep != ZDO_EP && ad->asdu != NULL && ad->asduLength >= 5U &&
		    (ad->asdu[0] & (ZCL_FRAME_CONTROL_TYPE | ZCL_FRAME_CONTROL_MANU_SPECIFIC)) ==
			ZCL_FRAME_TYPE_PROFILE_CMD && ad->asdu[2] == ZCL_CMD_READ) {
			u8 rsp[96];
			epInfo_t dst;
			u16 rsp_len = 3U;
			u8 aps_cnt = 0U;

			rsp[0] = ZCL_FRAME_TYPE_PROFILE_CMD |
				 ZCL_FRAME_CONTROL_DIRECTION |
				 ZCL_FRAME_CONTROL_DISABLE_DEFAULT_RSP;
			rsp[1] = ad->asdu[1];
			rsp[2] = ZCL_CMD_READ_RSP;
			for (u16 in_pos = 3U; in_pos + 1U < ad->asduLength; in_pos += 2U) {
				rsp_len = zb_dispatch_basic_read_attr(rsp, rsp_len, sizeof(rsp),
								      (u16)ad->asdu[in_pos] |
								      ((u16)ad->asdu[in_pos + 1U] << 8));
			}

			TL_SETSTRUCTCONTENT(dst, 0);
			/* Snapshot every field used after releasing the RX carrier.  `ad`
			 * aliases `buf`; reading it after zb_buf_free() is a use-after-free
			 * and can feed the TX request with recycled slab metadata. */
			u8 tx_src_ep = ad->dst_ep;
			u8 tx_dst_ep = ad->src_ep;
			u16 tx_profile = ad->profile_id;
			u16 tx_cluster = ad->cluster_id;
			u16 tx_dst_short = ad->src_short_addr;

			dst.profileId = tx_profile;
			dst.dstEp = tx_dst_ep;
			dst.radius = 30U;
			dst.txOptions = APS_TX_OPT_ACK_TX;
			dst.dstAddrMode = APS_SHORT_DSTADDR_WITHEP;
			dst.dstAddr.shortAddr = tx_dst_short;

			/* Match the vendor zdo_send_req() ownership order: the RX
			 * indication carrier is no longer needed after all request fields
			 * and the response payload have been copied.  af_dataSend() needs a
			 * separate TX carrier, and keeping this one live can exhaust the
			 * shared slab at the exact post-idle read boundary. */
			zb_buf_free(buf);
			{
				u8 tx_status = af_dataSend(tx_src_ep, &dst, tx_cluster, rsp_len,
								 rsp, &aps_cnt);
			}
			return;
		}

		/* Application endpoint — find descriptor by ep, invoke cb_rx. */
		af_endpoint_descriptor_t *epList = af_epDescriptorGet();
		u8 epNum = af_availableEpNumGet();

		for (u8 i = 0; i < epNum; i++) {
			if (epList[i].ep == ad->dst_ep && epList[i].cb_rx != NULL) {
				epList[i].cb_rx(arg);
				return;
			}
		}
	}

	zb_buf_free(buf);
}

void af_aps_data_fragment_entry(void *arg)
{
	af_aps_data_entry(arg);
}
