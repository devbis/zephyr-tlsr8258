/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * Layer-queue primitive dispatch shim and AF callback bridge.
 *
 * The Zigbee SDK / libzigbee runtime moves MLME / NLDE / NLME
 * primitives between layers via zb_PrimitivePost(layerQ, primitive,
 * arg). The vendor implementation (libzigbee/src/zb_task_queue.c)
 * funnels everything through a static per-layer queue (g_zbTaskQ[])
 * polled by ev_main() / zb_TaskProcedure().
 *
 * The Zephyr port uses zb_TaskPost() (a single Zephyr work queue)
 * for callbacks. This TU provides the platform dispatch boundary for
 * primitives that are not represented by a dedicated Zephyr work item,
 * while the vendor-derived MAC/NWK/APS/ZDO handlers remain authoritative.
 *
 * ZDP request dispatch is kept in zdo/zdo_zephyr_glue.c, where the vendor
 * request table and restricted-mode status contract are preserved. This file
 * hosts only the AF dispatch bridge needed by the Zephyr worker.
 */

#include <zephyr/zigbee/zb_bootstrap.h>

#include "zb_common.h"
#include "zb_buffer.h"
#include "mac/mac.h"
#include "mac/mac_trx.h"
#include "mac/mac_data.h"

#include "af/zb_af.h"
#include "../../zcl/zcl_include.h"

/*
 * GreenPower hook — zdp_services.c::zdo_deviceAnnounceIndicate consults
 * this callback to suppress dev_annce for proxy-table entries that GP
 * has already claimed. gpDeviceAnnounceCheckCb_t comes from
 * gp/dGP_stub.h (pulled in unconditionally via zb_common.h); this
 * weak NULL fallback covers builds where gp_sinkTab.c never overrides it
 * (CONFIG_ZIGBEE_GP=n, or GP without the sink role) — zdp_services.c
 * null-checks it before calling.
 */
gpDeviceAnnounceCheckCb_t g_gpDeviceAnnounceCheckCb __attribute__((weak)) = NULL;

/*
 * zb_PrimitivePost / zb_TaskQPop / zb_UserTaskQNum live in
 * platform/zephyr/zb_task_queue_router.c backed by
 * a per-layer k_msgq.
 *
 * zb_buf_allocate / zb_buf_free / zb_bufInitalloc / zb_buf_clear /
 * is_zb_buf / zb_phyRxBufTozbBuf live in
 * platform/zephyr/zb_buffer_zephyr.c backed by a
 * K_MEM_SLAB_DEFINE_STATIC pool of zb_buf_t.
 *
 * The remaining definitions in this file are platform bindings for symbols
 * that are not part of a libzigbee functional translation unit. Functional
 * NWK, buffer, AF and ZDO entry points are implemented by their respective
 * vendor-derived sources or by the Zephyr adapter below.
 */

/*
 * AF confirm and fragmentation entry points mirror libzigbee/src/zb_af_data.c.
 * The carrier for a confirmation is an event-buffer allocation, while the
 * carrier for an APS indication is a Zephyr zb_buf_t.
 */
void af_dataCnfHandler(void *arg)
{
	apsdeDataConf_t *cnf = (apsdeDataConf_t *)arg;
	af_endpoint_descriptor_t *zdo_ep = af_zdoSimpleDescriptorGet();
	af_endpoint_descriptor_t *ep_list = af_epDescriptorGet();
	uint8_t ep_num = af_availableEpNumGet();

	if (cnf == NULL) {
		return;
	}
	if (zdo_ep != NULL && zdo_ep->cb_cnf != NULL && cnf->srcEndpoint == zdo_ep->ep) {
		zdo_ep->cb_cnf(arg);
		return;
	}
	for (uint8_t i = 0; i < ep_num; i++) {
		if (ep_list[i].cb_cnf != NULL && ep_list[i].ep == cnf->srcEndpoint) {
			ep_list[i].cb_cnf(arg);
			return;
		}
	}
	ev_buf_free((uint8_t *)arg);
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
extern uint8_t af_availableEpNumGet(void);
extern af_endpoint_descriptor_t *af_zdoSimpleDescriptorGet(void);

void af_aps_data_entry(void *arg)
{
	zb_buf_t *buf = (zb_buf_t *)arg;
	aps_data_ind_t *ad = (aps_data_ind_t *)arg;

	if (arg == NULL) {
		return;
	}
	if (ad->profile_id == ZDO_PROFILE_ID && ad->dst_ep == ZDO_EP) {
		if (ad->asdu != NULL && ad->asduLength > 0U) {
			zb_platform_app_zdo_rx(ad->src_short_addr,
					       ZB_NWK_IS_ADDRESS_BROADCAST(ad->dst_addr),
					       ad->cluster_id, ad->asdu, ad->asduLength);
		}
		/* ZDP owns the complete vendor request/response dispatch table. */
		af_endpoint_descriptor_t *zdo_ep = af_zdoSimpleDescriptorGet();

		if (zdo_ep != NULL && zdo_ep->cb_rx != NULL) {
			zdo_ep->cb_rx(arg);
		} else {
			zb_buf_free(buf);
		}
		return;
	}

	/* Application endpoint — find descriptor by ep, invoke cb_rx. */
	af_endpoint_descriptor_t *epList = af_epDescriptorGet();
	uint8_t epNum = af_availableEpNumGet();

	for (uint8_t i = 0; i < epNum; i++) {
		apsdeDataInd_t *ind;

		if (epList[i].ep != ad->dst_ep || epList[i].cb_rx == NULL) {
			continue;
		}

		/*
		 * Application receive callbacks such as zcl_rx_handler()
		 * take an apsdeDataInd_t in an event buffer, with the ASDU
		 * behind the indication, and release it themselves.
		 */
		ind = (apsdeDataInd_t *)ev_buf_allocate(
			(uint16_t)(sizeof(*ind) + ad->asduLength));
		if (ind == NULL) {
			break;
		}
		ind->indInfo = *ad;
		ind->asduLen = ad->asduLength;
		memcpy(ind->asdu, ad->asdu, ad->asduLength);
		ind->indInfo.asdu = ind->asdu;
		zb_buf_free(buf);
		epList[i].cb_rx(ind);
		return;
	}

	zb_buf_free(buf);
}

void af_aps_data_fragment_entry(void *arg)
{
	af_aps_data_entry(arg);
}
