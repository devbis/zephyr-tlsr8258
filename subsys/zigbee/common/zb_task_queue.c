/*
 * Copyright 2026 libzigbee
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "zb_common.h"
#include "ev.h"
#include "ev_buffer.h"
#include <stdint.h>

static tl_zbtaskq_stack_t g_zbTaskQ[TL_Q_TYPE_MAX];

u8 T_DBG_taskQPush_idx = 0;
uintptr_t T_DBG_taskQPush_cb = 0;
uintptr_t T_DBG_taskQPush_data = 0;
u8 T_DBG_taskQPop_idx = 0;
uintptr_t T_DBG_taskQPop_cb = 0;
uintptr_t T_DBG_taskQPop_data = 0;
u8 T_exceptTaskPost[2] = {0};

u8 buf_type_get(void *arg)
{
	if (is_zb_buf(arg)) {
		return 0;
	}

	if (is_ev_buf(arg)) {
		return 1;
	}

	return 2;
}

tl_zb_task_t *tl_zbTaskQPop(u8 idx, tl_zb_task_t *taskInfo)
{
	tl_zb_task_t *task = NULL;
	u32 irq;

	taskInfo->tlCb = NULL;
	taskInfo->data = NULL;

	irq = drv_disable_irq();

	if (idx == 0U) {
		if (taskQ_user.wptr != taskQ_user.rptr) {
			u8 mask = (u8)(ZB_TASKQ_USERUSE_SIZE - 1U);
			task = &taskQ_user.evt[taskQ_user.rptr & mask];
			taskQ_user.rptr++;
		}
	} else {
		tl_zbtaskq_stack_t *queue = &g_zbTaskQ[idx - 1U];

		if (queue->wptr != queue->rptr) {
			task = &queue->evt[queue->rptr & (TL_ZBTASKQ_STACKUSE_SIZE - 1U)];
			queue->rptr++;
		}
	}

	if (task != NULL) {
		taskInfo->data = task->data;
		taskInfo->tlCb = task->tlCb;

		if (buf_type_get(task->data) == 0U) {
			const zb_buf_t *buf = (const zb_buf_t *)task->data;

			if (buf->hdr.used == 0U) {
				T_DBG_taskQPop_idx = idx;
				T_DBG_taskQPop_cb = (uintptr_t)task->tlCb;
				T_DBG_taskQPop_data = (uintptr_t)task->data;
				/* vendor: "tmovs r1, #19" - SYS_EXCEPTTION_ZB_BUFFER_EXCEPTION */
				ZB_EXCEPTION_POST(SYS_EXCEPTTION_ZB_BUFFER_EXCEPTION);
			}
		}
	}

	drv_restore_irq(irq);

	return task;
}

_attribute_ram_code_ u8 tl_zbTaskQPush(u8 idx, tl_zb_task_t *task)
{
	tl_zb_task_t *slot = NULL;
	u32 irq = drv_disable_irq();

	if (idx == 0U) {
		/* "1c: tsubs r0,r1,r0; 1e: tcmp r0,r3; 20: tjge" - the vendor library
		 * does not truncate the difference and compares it signed. */
		if ((int)taskQ_user.wptr - (int)taskQ_user.rptr >= (int)ZB_TASKQ_USERUSE_SIZE) {
			drv_restore_irq(irq);
			return ZB_RET_OVERFLOW;
		}

		slot = &taskQ_user.evt[taskQ_user.wptr & (u8)(ZB_TASKQ_USERUSE_SIZE - 1U)];
		taskQ_user.wptr++;
	} else {
		tl_zbtaskq_stack_t *queue = &g_zbTaskQ[idx - 1U];

		/* "44: tsubs r3,r1,r3; 46: tcmp r3,#15; 48: tjgt" */
		if ((int)queue->wptr - (int)queue->rptr > (TL_ZBTASKQ_STACKUSE_SIZE - 1)) {
			drv_restore_irq(irq);
			return ZB_RET_OVERFLOW;
		}

		slot = &queue->evt[queue->wptr & (TL_ZBTASKQ_STACKUSE_SIZE - 1U)];
		queue->wptr++;
	}

	slot->tlCb = task->tlCb;
	slot->data = task->data;

	if (buf_type_get(task->data) == 0U) {
		const zb_buf_t *buf = (const zb_buf_t *)task->data;

		if (buf->hdr.used == 0U) {
			T_DBG_taskQPush_idx = idx;
			T_DBG_taskQPush_cb = (uintptr_t)task->tlCb;
			T_DBG_taskQPush_data = (uintptr_t)task->data;
			/* vendor: "tmovs r1, #19" - SYS_EXCEPTTION_ZB_BUFFER_EXCEPTION */
			ZB_EXCEPTION_POST(SYS_EXCEPTTION_ZB_BUFFER_EXCEPTION);
		}
	}

	drv_restore_irq(irq);

	return ZB_RET_OK;
}

void zb_sched_init(void)
{
	taskQ_user.wptr = 0;
	taskQ_user.rptr = 0;

	for (u8 i = 0; i < ZB_TASKQ_USERUSE_SIZE; i++) {
		taskQ_user.evt[i].tlCb = NULL;
		taskQ_user.evt[i].data = NULL;
	}

	memset(g_zbTaskQ, 0, sizeof(g_zbTaskQ));
}

u8 tl_zbUserTaskQNum(void)
{
	return (u8)(taskQ_user.wptr - taskQ_user.rptr);
}

void tl_zbTaskProcedure(void)
{
	tl_zb_task_t taskInfo;

	tl_zbMacTaskProc();
	tl_zbNwkTaskProc();

	if (tl_zbTaskQPop(0, &taskInfo) != NULL) {
		taskInfo.tlCb(taskInfo.data);
	}

	zdo_ssInfoUpdate();
}

u8 zb_isTaskDone(void)
{
	u8 *queue = (u8 *)g_zbTaskQ;
	u8 *end = queue + (TL_Q_TYPE_MAX - 1U) * sizeof(tl_zbtaskq_stack_t);

	/* The vendor object compares the first byte of each queue block with the
	 * byte immediately before it ("8: tsubs r2,r3,#1").  Preserve that
	 * observed layout/behavior instead of substituting the apparent wptr/rptr
	 * fields at offsets 128/129. */
	for (; queue != end; queue += sizeof(tl_zbtaskq_stack_t)) {
		if (queue[0] != queue[-1]) {
			return 0;
		}
	}

	return taskQ_user.wptr == taskQ_user.rptr;
}

u8 tl_zbTaskPost(tl_zb_callback_t func, void *arg)
{
	tl_zb_task_t taskInfo;
	u8 status;

	taskInfo.tlCb = func;
	taskInfo.data = arg;

	status = tl_zbTaskQPush(0, &taskInfo);
	if (status == ZB_RET_OK) {
		return status;
	}

	switch (buf_type_get(arg)) {
	case 0:
		ZB_EXCEPTION_POST(SYS_EXCEPTTION_ZB_TASK_POST);
		zb_buf_free((zb_buf_t *)arg);
		break;
	case 1:
		ZB_EXCEPTION_POST(SYS_EXCEPTTION_EV_TASK_POST);
		ev_buf_free((u8 *)arg);
		break;
	default:
		ZB_EXCEPTION_POST(SYS_EXCEPTTION_COMMON_TASK_POST);
		break;
	}

	return status;
}

u8 tl_zbPrimitivePost(u8 layerQ, u8 primitive, void *arg)
{
	tl_zb_task_t taskInfo;
	u8 status;

	T_exceptTaskPost[0] = layerQ;
	T_exceptTaskPost[1] = primitive;

	if (arg == NULL) {
		ZB_EXCEPTION_POST(SYS_EXCEPTTION_ZB_BUFFER_OVERFLOWN);
	}

	((zb_buf_t *)arg)->hdr.id = primitive;

	taskInfo.tlCb = NULL;
	taskInfo.data = arg;

	status = tl_zbTaskQPush(layerQ, &taskInfo);
	if (status != ZB_RET_OK) {
		if (layerQ == TL_Q_MAC2NWK) {
			if (primitive == MAC_MLME_BEACON_NOTIFY_IND ||
			    primitive == MAC_MCPS_DATA_IND) {
				zb_buf_free((zb_buf_t *)arg);
			}
		} else {
			ZB_EXCEPTION_POST(SYS_EXCEPTTION_ZB_TASK_POST);
		}
	}

	return status;
}
