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

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(zigbee_primitive_dispatch, CONFIG_ZIGBEE_LOG_LEVEL);

const u8 g_zero_addr[8] = {0};
u32 g_secondCnt;

u8 tl_zbPrimitivePost(u8 layerQ, u8 primitive, void *arg)
{
	LOG_WRN("tl_zbPrimitivePost(layerQ=%u, primitive=0x%02x, arg=%p) unhandled",
		layerQ, primitive, arg);
	ARG_UNUSED(layerQ);
	ARG_UNUSED(primitive);
	ARG_UNUSED(arg);
	return 0U;
}
