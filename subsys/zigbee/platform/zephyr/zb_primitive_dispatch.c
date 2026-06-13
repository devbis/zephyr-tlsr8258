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

u8 tl_zbPrimitivePost(u8 layerQ, u8 primitive, void *arg)
{
	LOG_WRN("tl_zbPrimitivePost(layerQ=%u, primitive=0x%02x, arg=%p) unhandled",
		layerQ, primitive, arg);
	ARG_UNUSED(layerQ);
	ARG_UNUSED(primitive);
	ARG_UNUSED(arg);
	return 0U;
}

/*
 * Weak stubs for the libzigbee infrastructure layer (zb_buffer.c,
 * zb_task_queue.c, second_clock.c, drv_timer.c). The router-build
 * dispatcher tables in mac.c / nwk.c reference these names so they
 * survive --gc-sections, but the runtime never enters any chain that
 * actually invokes the MAC/NWK handlers (the static-formation router
 * in nwk_router_minimal.c drives the radio directly). Real
 * implementations land later from the vendor SDK or Zephyr-native
 * shims; until then the stubs trap with a warning and a benign
 * return value.
 */
__attribute__((weak)) zb_buf_t *zb_buf_allocate(void)
{
	LOG_WRN("zb_buf_allocate stub invoked");
	return NULL;
}

__attribute__((weak)) void zb_buf_free(zb_buf_t *buf)
{
	ARG_UNUSED(buf);
}

__attribute__((weak)) void *tl_bufInitalloc(zb_buf_t *p, u8 size)
{
	ARG_UNUSED(p);
	ARG_UNUSED(size);
	return NULL;
}

__attribute__((weak)) void *tl_phyRxBufTozbBuf(u8 *rxBuf)
{
	ARG_UNUSED(rxBuf);
	return NULL;
}

__attribute__((weak)) u8 tl_zbUserTaskQNum(void)
{
	return 0U;
}

__attribute__((weak)) tl_zb_task_t *tl_zbTaskQPop(u8 idx, tl_zb_task_t *taskInfo)
{
	ARG_UNUSED(idx);
	ARG_UNUSED(taskInfo);
	return NULL;
}

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
