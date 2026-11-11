/*
 * Copyright 2026 libzigbee
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "zb_common.h"
#include "ev.h"
#include "ev_buffer.h"
#include <stdint.h>

static zb_taskq_stack_t g_zbTaskQ[ZB_Q_TYPE_MAX];

uint8_t T_DBG_taskQPush_idx = 0;
uintptr_t T_DBG_taskQPush_cb = 0;
uintptr_t T_DBG_taskQPush_data = 0;
uint8_t T_DBG_taskQPop_idx = 0;
uintptr_t T_DBG_taskQPop_cb = 0;
uintptr_t T_DBG_taskQPop_data = 0;
uint8_t T_exceptTaskPost[2] = {0};

uint8_t buf_type_get(void *arg)
{
	if (is_zb_buf(arg)) {
		return 0;
	}

	if (is_ev_buf(arg)) {
		return 1;
	}

	return 2;
}

zb_task_t *zb_TaskQPop(uint8_t idx, zb_task_t *taskInfo)
{
	zb_task_t *task = NULL;
	uint32_t irq;

	taskInfo->tlCb = NULL;
	taskInfo->data = NULL;

	irq = drv_disable_irq();

	if (idx == 0U) {
		if (taskQ_user.wptr != taskQ_user.rptr) {
			uint8_t mask = (uint8_t)(ZB_TASKQ_USERUSE_SIZE - 1U);
			task = &taskQ_user.evt[taskQ_user.rptr & mask];
			taskQ_user.rptr++;
		}
	} else {
		zb_taskq_stack_t *queue = &g_zbTaskQ[idx - 1U];

		if (queue->wptr != queue->rptr) {
			task = &queue->evt[queue->rptr & (ZB_ZBTASKQ_STACKUSE_SIZE - 1U)];
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
				/* A queued buffer must still be in use. */
				ZB_EXCEPTION_POST(SYS_EXCEPTTION_ZB_BUFFER_EXCEPTION);
			}
		}
	}

	drv_restore_irq(irq);

	return task;
}

_attribute_ram_code_ uint8_t zb_TaskQPush(uint8_t idx, zb_task_t *task)
{
	zb_task_t *slot = NULL;
	uint32_t irq = drv_disable_irq();

	if (idx == 0U) {
		/* The vendor library does not truncate the difference and compares it signed. */
		if ((int)taskQ_user.wptr - (int)taskQ_user.rptr >= (int)ZB_TASKQ_USERUSE_SIZE) {
			drv_restore_irq(irq);
			return ZB_RET_OVERFLOW;
		}

		slot = &taskQ_user.evt[taskQ_user.wptr & (uint8_t)(ZB_TASKQ_USERUSE_SIZE - 1U)];
		taskQ_user.wptr++;
	} else {
		zb_taskq_stack_t *queue = &g_zbTaskQ[idx - 1U];

		/* The difference is compared signed against the queue depth. */
		if ((int)queue->wptr - (int)queue->rptr > (ZB_ZBTASKQ_STACKUSE_SIZE - 1)) {
			drv_restore_irq(irq);
			return ZB_RET_OVERFLOW;
		}

		slot = &queue->evt[queue->wptr & (ZB_ZBTASKQ_STACKUSE_SIZE - 1U)];
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
			/* A queued buffer must still be in use. */
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

	for (uint8_t i = 0; i < ZB_TASKQ_USERUSE_SIZE; i++) {
		taskQ_user.evt[i].tlCb = NULL;
		taskQ_user.evt[i].data = NULL;
	}

	memset(g_zbTaskQ, 0, sizeof(g_zbTaskQ));
}

uint8_t zb_UserTaskQNum(void)
{
	return (uint8_t)(taskQ_user.wptr - taskQ_user.rptr);
}

void zb_TaskProcedure(void)
{
	zb_task_t taskInfo;

	zb_MacTaskProc();
	zb_NwkTaskProc();

	if (zb_TaskQPop(0, &taskInfo) != NULL) {
		taskInfo.tlCb(taskInfo.data);
	}

	zdo_ssInfoUpdate();
}

uint8_t zb_isTaskDone(void)
{
	uint8_t *queue = (uint8_t *)g_zbTaskQ;
	uint8_t *end = queue + (ZB_Q_TYPE_MAX - 1U) * sizeof(zb_taskq_stack_t);

	/* The vendor object compares the first byte of each queue block with the
	 * byte immediately before it.  Preserve that
	 * observed layout/behavior instead of substituting the apparent wptr/rptr
	 * fields at offsets 128/129. */
	for (; queue != end; queue += sizeof(zb_taskq_stack_t)) {
		if (queue[0] != queue[-1]) {
			return 0;
		}
	}

	return taskQ_user.wptr == taskQ_user.rptr;
}

uint8_t zb_TaskPost(zb_callback_t func, void *arg)
{
	zb_task_t taskInfo;
	uint8_t status;

	taskInfo.tlCb = func;
	taskInfo.data = arg;

	status = zb_TaskQPush(0, &taskInfo);
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
		ev_buf_free((uint8_t *)arg);
		break;
	default:
		ZB_EXCEPTION_POST(SYS_EXCEPTTION_COMMON_TASK_POST);
		break;
	}

	return status;
}

uint8_t zb_PrimitivePost(uint8_t layerQ, uint8_t primitive, void *arg)
{
	zb_task_t taskInfo;
	uint8_t status;

	T_exceptTaskPost[0] = layerQ;
	T_exceptTaskPost[1] = primitive;

	if (arg == NULL) {
		ZB_EXCEPTION_POST(SYS_EXCEPTTION_ZB_BUFFER_OVERFLOWN);
	}

	((zb_buf_t *)arg)->hdr.id = primitive;

	taskInfo.tlCb = NULL;
	taskInfo.data = arg;

	status = zb_TaskQPush(layerQ, &taskInfo);
	if (status != ZB_RET_OK) {
		if (layerQ == ZB_Q_MAC2NWK) {
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
