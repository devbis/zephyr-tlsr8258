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

__attribute__((weak)) tl_zb_normal_neighbor_entry_t *tl_zbNeighborTableUpdate(
	tl_zb_normal_neighbor_entry_t *entry, u8 delete_flag)
{
	ARG_UNUSED(entry);
	ARG_UNUSED(delete_flag);
	return NULL;
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

__attribute__((weak)) void af_aps_data_entry(void *arg)
{
	ARG_UNUSED(arg);
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
