/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Layer-queue primitive dispatch shim.
 *
 * The Zigbee SDK / libzigbee runtime moves MLME / NLDE / NLME
 * primitives between layers via tl_zbPrimitivePost(layerQ, primitive,
 * arg). The vendor implementation (libzigbee/src/zb_task_queue.c)
 * funnels everything through a static per-layer queue (g_zbTaskQ[])
 * polled by ev_main() / tl_zbTaskProcedure().
 *
 * The Zephyr port uses tl_zbTaskPost() (a single Zephyr work queue)
 * for callbacks. To keep the SDK / libzigbee macros (tl_zbMacScanRequest,
 * tl_zbMacStartRequest, …) link-clean while the MAC MLME path is
 * being ported, this TU provides a *minimal* primitive dispatcher
 * that just logs unhandled primitives and frees the carrier buffer.
 *
 * Once individual primitives have a real handler (e.g. a MAC scan
 * service) the dispatch table below grows entries that hand the
 * arg to the handler instead of dropping it.
 *
 * Also hosts the two globals the SDK runtime exposes that don't fit
 * anywhere else: g_zero_addr (used as an "all-zero IEEE" reference)
 * and g_secondCnt (uptime-in-seconds counter consumed by the formation
 * beacon-payload long_uptime flag).
 */

#include "zb_common_stub.h"
#include "mac/includes/mac_internal.h"
#include <zephyr/zigbee/zb_bootstrap.h>

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(zigbee_primitive_dispatch, CONFIG_ZIGBEE_LOG_LEVEL);

/* g_zero_addr and g_secondCnt live in common/zb_initialize.c +
 * common/second_clock.c (libzigbee-derived) when those TUs are
 * compiled; provide weak fallbacks here so the ED build that
 * doesn't pull those in still links.
 */
const u8 g_zero_addr[8] __attribute__((weak)) = {0};
u32 g_secondCnt __attribute__((weak));

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
 * platform/zephyr/zb_task_queue_router.c (router build) backed by
 * a per-layer k_msgq.
 *
 * zb_buf_allocate / zb_buf_free / tl_bufInitalloc / zb_buf_clear /
 * is_zb_buf / tl_phyRxBufTozbBuf live in
 * platform/zephyr/zb_buffer_zephyr.c (router build) backed by a
 * K_MEM_SLAB_DEFINE_STATIC pool of zb_buf_t.
 *
 * The weak stubs below remain because they fill in symbols that the
 * dispatcher chain reaches but the Zephyr port hasn't bound to a
 * real implementation yet (ED-only helpers reachable from shared
 * code, exception posting, hardware timer driver, vendor flash
 * address constant).
 */

__attribute__((weak)) u8 sys_exceptionPost(u16 line, u8 evt)
{
	LOG_WRN("sys_exceptionPost line=%u evt=0x%02x", line, evt);
	return 0U;
}

__attribute__((weak)) int drv_hwTmr_set(u8 tmrIdx, u32 t_us, timerCb_t func, void *arg)
{
	ARG_UNUSED(tmrIdx);
	ARG_UNUSED(t_us);
	ARG_UNUSED(func);
	ARG_UNUSED(arg);
	return -1;
}

/* g_u32MacFlashAddr / zb_post_tk_trace / ZB_TASKQ_USERUSE_SIZE — vendor
 * runtime symbols referenced by the dispatcher chain.
 */
u32 g_u32MacFlashAddr __attribute__((weak));
volatile u32 zb_post_tk_trace[16] __attribute__((weak));
u8 ZB_TASKQ_USERUSE_SIZE __attribute__((weak));

/* Dummy anchor for the ZB_BUF_FROM_REF / ZB_REF_FROM_BUF macros in
 * aps_internal.h. The runtime never enters the aps group queue path
 * that consumes the ref index — but the dispatcher table reachability
 * forces the symbol to be present.
 */
zb_buf_t g_zb_buf_ref_dummy __attribute__((weak));

/* Application/ZDP entrypoints that the dispatcher tables reach but
 * the Zephyr port hasn't bound yet. Weak so a real implementation
 * can override at link time.
 */
__attribute__((weak)) void zdp_init(void)
{
}

__attribute__((weak)) u8 zb_zdoSendDevAnnance(void)
{
	return 0U;
}

/* ED-only helper invoked from shared bdb.c — gated out of the
 * libzigbee build path; provide a weak no-op for the router build.
 */
__attribute__((weak)) void tl_zbNwkEdMinimalInterviewPollStart(u8 count, u32 intervalMs)
{
	ARG_UNUSED(count);
	ARG_UNUSED(intervalMs);
}

/* Neighbor-table operations that depend on the address-map NV layout
 * we haven't ported yet. Weak so the router build links; the static-
 * formation path doesn't use them.
 */
#include "nwk/includes/nwk_neighbor.h"

/*
 * The full libzigbee `tl_zbNeighborTableUpdate` (~75 LOC, calls
 * tl_zbNeighborTableChildEDNumGet / tl_zbNeighborTableDeleteAuto /
 * tl_nebListAdd / neighbor_active_count_update, plus diagnostic
 * counters) hasn't landed in the Zephyr port yet. The vendor returns
 * NULL on table-full / low-LQI, otherwise a pointer to the stored
 * entry. The Zephyr stub used to always return NULL, which made
 * `tl_zbMacMlmeAssociateConfirmHandler` short-circuit with
 * NWK_STATUS_NEIGHBOR_TABLE_FULL right after a successful AssocResp
 * — the joiner never enters NLME_JOINING, the TRANSPORT_KEY frame is
 * dropped by `tl_zbMacMcpsDataIndicationHandler`'s
 * `!joined && state != NLME_JOINING` guard, and BDB cycles forever.
 *
 * As a pragmatic stop-gap until the real table lands, stash a single
 * "parent neighbor" entry locally and return it. Multiple neighbors
 * collapse into one slot (the most recent caller wins) — adequate
 * for the router-as-joiner path which only needs the coord parent
 * present so the join state machine reaches "joined".
 */
static tl_zb_normal_neighbor_entry_t zb_neighbor_stub_slot;

__attribute__((weak)) tl_zb_normal_neighbor_entry_t *tl_zbNeighborTableUpdate(
	tl_zb_normal_neighbor_entry_t *entry, u8 delete_flag)
{
	ARG_UNUSED(delete_flag);
	if (entry == NULL) {
		return NULL;
	}
	zb_neighbor_stub_slot = *entry;
	zb_neighbor_stub_slot.used = 1U;
	return &zb_neighbor_stub_slot;
}

__attribute__((weak)) void tl_zbNeighborTableDelete(tl_zb_normal_neighbor_entry_t *entry)
{
	ARG_UNUSED(entry);
}

__attribute__((weak)) tl_zb_normal_neighbor_entry_t *
tl_zbNeighborTableSearchFromExtAddr(u16 *shortAddr, addrExt_t extAddr, u16 *idx)
{
	ARG_UNUSED(shortAddr);
	ARG_UNUSED(extAddr);
	ARG_UNUSED(idx);
	return NULL;
}

__attribute__((weak)) tl_zb_normal_neighbor_entry_t *
tl_zbNeighborTableSearchFromShortAddr(u16 shortAddr, addrExt_t extAddr, u16 *idx)
{
	ARG_UNUSED(shortAddr);
	ARG_UNUSED(extAddr);
	ARG_UNUSED(idx);
	return NULL;
}

__attribute__((weak)) void zb_buf_clear(zb_buf_t *p)
{
	ARG_UNUSED(p);
}

/* af / aps_data confirm + interpan entry-points from libzigbee
 * zb_af_data.c (not ported — Zephyr af/zb_af.c handles AF on its own
 * path). Weak stubs let dispatcher tables link.
 */
__attribute__((weak)) void af_dataCnfHandler(void *arg)
{
	ARG_UNUSED(arg);
}

/*
 * APS data indication dispatcher. The vendor af_aps_data_entry weak stub
 * (left below for callers that prefer to override) drops every frame on
 * the floor, which means Z2M's ZDP-interview queries (Node Descriptor,
 * Active Endpoint, Simple Descriptor, etc.) addressed to ZDO endpoint 0
 * are silently discarded by the joined router. Replace it with a routing
 * function:
 *
 *   profile_id == ZDO_PROFILE_ID  → dispatch by cluster_id to the
 *                                   zdp_services.c indicate handlers
 *   else                          → invoke the endpoint's cb_rx
 *                                   (zcl_rx_handler for app endpoints)
 *
 * Each indicate handler frees the buf when it's done; if no handler
 * matches we free the buf ourselves so the slab pool doesn't leak.
 *
 * Cluster IDs are from subsys/zigbee/zdo/zdp.h. Responses (bit 15 set)
 * are not dispatched — we're the responder, not the client.
 */
#include "zdo/zdp.h"

extern af_endpoint_descriptor_t *af_epDescriptorGet(void);
extern u8 af_availableEpNumGet(void);
extern af_endpoint_descriptor_t *af_zdoEpDescriptorGet(void);

__attribute__((weak)) void af_aps_data_entry(void *arg)
{
	zb_buf_t *buf = (zb_buf_t *)arg;
	aps_data_ind_t *ad = (aps_data_ind_t *)arg;
	extern volatile u32 zb_nwk_ed_trace[];

	if (arg == NULL) {
		return;
	}

	/*
	 * slot[45]: low 16 = af_aps_data_entry call count,
	 * bits 16-23 = last profile_id low byte,
	 * bits 24-31 = last cluster_id low byte.
	 * Single u32 write at entry — safe per the instrumentation
	 * regression notes.
	 */
	{
		u32 prev = zb_nwk_ed_trace[45];
		zb_nwk_ed_trace[45] = ((u32)(ad->cluster_id & 0xffU) << 24) |
				       ((u32)(ad->profile_id & 0xffU) << 16) |
				       (((prev & 0xffffU) + 1U) & 0xffffU);
	}
	printk("zb_af_entry: profile=0x%04x cluster=0x%04x dst_ep=%u src_ep=%u dst=0x%04x asdu_len=%u\n",
	       ad->profile_id, ad->cluster_id, ad->dst_ep, ad->src_ep, ad->dst_addr,
	       ad->asduLength);

	if (ad->profile_id == ZDO_PROFILE_ID && ad->dst_ep == ZDO_EP) {
		/* ZDP request — only handle non-response cluster IDs (bit 15 clear). */
		if ((ad->cluster_id & 0x8000U) != 0U) {
			printk("zb_af_drop: zdo response cluster=0x%04x\n", ad->cluster_id);
			zb_buf_free(buf);
			return;
		}
		switch (ad->cluster_id) {
		case NODE_DESC_REQ_CLID:
		case POWER_DESC_REQ_CLID:
		case SIMPLE_DESC_REQ_CLID:
			zdo_descriptorsIndicate(arg);
			return;
		case ACTIVE_EP_REQ_CLID:
			zdo_activeEpIndicate(arg);
			return;
		case MATCH_DESC_REQ_CLID:
			zdo_matchDescriptorIndicate(arg);
			return;
		case NWK_ADDR_REQ_CLID:
			zdo_nwkAddrIndicate(arg);
			return;
		case IEEE_ADDR_REQ_CLID:
			zdo_ieeeAddrIndicate(arg);
			return;
		case DEVICE_ANNCE_CLID:
			zdo_deviceAnnounceIndicate(arg);
			return;
		case MGMT_PERMIT_JOINING_REQ_CLID:
			zdo_mgmtPermitJoinIndicate(arg);
			return;
		case MGMT_LQI_REQ_CLID:
			zdo_mgmtLqiIndicate(arg);
			return;
		case MGMT_BIND_REQ_CLID:
			zdo_mgmtBindIndicate(arg);
			return;
		case MGMT_NWK_UPDATE_REQ_CLID:
			zdo_mgmtNwkUpdateIndicate(arg);
			return;
		case BIND_REQ_CLID:
		case UNBIND_REQ_CLID:
			zdo_bindOrUnbindIndicate(arg);
			return;
		case SYSTEM_SERVER_DISCOVERY_REQ_CLID:
			zdo_SysServerDiscoveryIndicate(arg);
			return;
		case PARENT_ANNCE_CLID:
			zdo_parentAnnounceIndicate(arg);
			return;
		default:
			printk("zb_af_drop: unknown zdo cluster=0x%04x\n", ad->cluster_id);
			break;
		}
	} else {
		if (ad->profile_id == 0x0104U && ad->cluster_id == 0x0000U &&
		    ad->dst_ep != ZDO_EP && ad->asdu != NULL && ad->asduLength >= 5U &&
		    (ad->asdu[0] & 0x07U) == 0U && ad->asdu[2] == 0x00U) {
			u8 rsp[40];
			epInfo_t dst;
			const char *model_id = zb_platform_app_basic_model_id();
			u8 model_len = (u8)strlen(model_id);
			u8 aps_cnt = 0U;

			rsp[0] = 0x18U;
			rsp[1] = ad->asdu[1];
			rsp[2] = 0x01U;
			COPY_U16TOBUFFER(&rsp[3], 0x0005U);
			rsp[5] = 0x00U;
			rsp[6] = 0x42U;
			rsp[7] = model_len;
			memcpy(&rsp[8], model_id, model_len);

			TL_SETSTRUCTCONTENT(dst, 0);
			dst.profileId = ad->profile_id;
			dst.dstEp = ad->src_ep;
			dst.radius = 30U;
			dst.txOptions = APS_TX_OPT_ACK_TX;
			dst.dstAddrMode = APS_SHORT_DSTADDR_WITHEP;
			dst.dstAddr.shortAddr = ad->src_short_addr;

			if (af_dataSend(ad->dst_ep, &dst, ad->cluster_id, (u16)(8U + model_len),
					rsp, &aps_cnt) == APS_STATUS_SUCCESS) {
				zb_buf_free(buf);
				return;
			}
		}

		/* Application endpoint — find descriptor by ep, invoke cb_rx. */
		af_endpoint_descriptor_t *epList = af_epDescriptorGet();
		u8 epNum = af_availableEpNumGet();

		for (u8 i = 0; i < epNum; i++) {
			if (epList[i].ep == ad->dst_ep && epList[i].cb_rx != NULL) {
				printk("zb_af_route: app ep=%u\n", ad->dst_ep);
				epList[i].cb_rx(arg);
				return;
			}
		}
		printk("zb_af_drop: no app ep=%u profile=0x%04x\n", ad->dst_ep, ad->profile_id);
	}

	zb_buf_free(buf);
}

__attribute__((weak)) void af_aps_data_fragment_entry(void *arg)
{
	ARG_UNUSED(arg);
}

/* zb_zdoNwkAddrReq is declared in zbapi/zb_api.h with the
 * (dstNwkAddr, pReq, seqNo, indCb) shape; provide a weak stub here
 * that returns 0 (ZDO_SUCCESS).
 */
#include "zbapi/zb_api.h"
__attribute__((weak)) zdo_status_t zb_zdoNwkAddrReq(u16 dstNwkAddr,
						    zdo_nwk_addr_req_t *pReq,
						    u8 *seqNo, zdo_callback indCb)
{
	ARG_UNUSED(dstNwkAddr);
	ARG_UNUSED(pReq);
	ARG_UNUSED(seqNo);
	ARG_UNUSED(indCb);
	return 0;
}
