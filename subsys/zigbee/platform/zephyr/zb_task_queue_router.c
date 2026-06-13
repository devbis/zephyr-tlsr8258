/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Zephyr-native replacement for libzigbee zb_task_queue.c, used
 * exclusively by the router build (CONFIG_ZIGBEE_ROUTER=y). The ED
 * build still uses zb_task_queue_zephyr.c which owns tl_zbTaskPost
 * for the lightweight nwk_ed_minimal path.
 *
 * The libzigbee runtime exchanges layer-to-layer primitives through
 * tl_zbPrimitivePost(layerQ, primitive, buf). Each post stamps the
 * primitive opcode into buf->hdr.id and enqueues onto a per-layer
 * ring. The receiver — tl_zbNwkTaskProc() in nwk.c — pops the next
 * entry, reads buf->hdr.id, and dispatches to the matching
 * tl_zbMac… / tl_zbNwk… handler.
 *
 * The vendor backing store is g_zbTaskQ[]: a static array of
 * tl_zb_task_t with wptr/rptr cursors. The Zephyr port replaces it
 * with one k_msgq per layer; semantics match (FIFO, fixed depth,
 * non-blocking try-put / try-get).
 */

#include <zephyr/kernel.h>

#include "zb_common_stub.h"

extern void zb_buf_free(zb_buf_t *buf);

/*
 * One queue per layer-id from the TL_Q_* enum in zb_common_stub.h.
 * Depth was 8 in vendor builds (TL_Q_DEPTH not exposed publicly);
 * the router-side dispatcher drains each tick so a single-digit
 * depth is enough as long as the producer doesn't outrun the
 * consumer.
 */
#define ZB_TASKQ_DEPTH 12

K_MSGQ_DEFINE(zb_taskq_ev_task,  sizeof(tl_zb_task_t), ZB_TASKQ_DEPTH, 4);
K_MSGQ_DEFINE(zb_taskq_mac2nwk,  sizeof(tl_zb_task_t), ZB_TASKQ_DEPTH, 4);
K_MSGQ_DEFINE(zb_taskq_nwk2mac,  sizeof(tl_zb_task_t), ZB_TASKQ_DEPTH, 4);
K_MSGQ_DEFINE(zb_taskq_high2nwk, sizeof(tl_zb_task_t), ZB_TASKQ_DEPTH, 4);
K_MSGQ_DEFINE(zb_taskq_nwk2high, sizeof(tl_zb_task_t), ZB_TASKQ_DEPTH, 4);

static struct k_msgq *const zb_taskq_per_layer[TL_Q_TYPE_MAX] = {
	[TL_Q_EV_TASK]  = &zb_taskq_ev_task,
	[TL_Q_MAC2NWK]  = &zb_taskq_mac2nwk,
	[TL_Q_NWK2MAC]  = &zb_taskq_nwk2mac,
	[TL_Q_HIGH2NWK] = &zb_taskq_high2nwk,
	[TL_Q_NWK2HIGH] = &zb_taskq_nwk2high,
};

u8 tl_zbPrimitivePost(u8 layerQ, u8 primitive, void *arg)
{
	tl_zb_task_t task = {
		.tlCb = NULL,
		.data = arg,
	};

	if (layerQ >= TL_Q_TYPE_MAX || zb_taskq_per_layer[layerQ] == NULL) {
		return 0xff;
	}

	if (arg == NULL) {
		return 0xff;
	}

	/* Stamp the primitive opcode into buf->hdr.id where
	 * tl_zbNwkTaskProc() reads it back. Mirrors the vendor's
	 * ((u8 *)arg)[ZB_BUF_HDR_PRIMITIVE_OFFSET] = primitive.
	 */
	((zb_buf_t *)arg)->hdr.id = primitive;

	if (k_msgq_put(zb_taskq_per_layer[layerQ], &task, K_NO_WAIT) != 0) {
		/* Queue full — drop MAC indications, the vendor frees
		 * the carrier buffer in this case.
		 */
		if (layerQ == TL_Q_MAC2NWK) {
			zb_buf_free((zb_buf_t *)arg);
		}
		return 0xff;
	}

	return RET_OK;
}

tl_zb_task_t *tl_zbTaskQPop(u8 idx, tl_zb_task_t *taskInfo)
{
	if (idx >= TL_Q_TYPE_MAX || zb_taskq_per_layer[idx] == NULL ||
	    taskInfo == NULL) {
		return NULL;
	}

	if (k_msgq_get(zb_taskq_per_layer[idx], taskInfo, K_NO_WAIT) != 0) {
		return NULL;
	}

	return taskInfo;
}

/*
 * Number of entries currently queued on the user-task lane. The
 * libzigbee MAC RX path uses this to back-pressure incoming radio
 * frames when the NWK side falls behind.
 */
u8 tl_zbUserTaskQNum(void)
{
	return (u8)k_msgq_num_used_get(&zb_taskq_high2nwk);
}

/*
 * Vendor zb_task_queue.c also defines tl_zbTaskPost (a single-shot
 * callback enqueue) and tl_zbTaskProcedure (drain loop). The
 * Zephyr build keeps the existing Zephyr-flavoured tl_zbTaskPost in
 * platform/zephyr/zb_task_queue_zephyr.c — that one is wired into
 * a k_work-driven worker thread and is shared with the ED build.
 * tl_zbNwkTaskProc() in libzigbee nwk.c handles the per-layer
 * drain on every zb_thread tick.
 */
