/* SPDX-License-Identifier: Apache-2.0 */
/*
 * ev_timer backed by k_work_delayable.
 *
 * ev_on_timer   → k_work_reschedule(&evt->zb_work, K_MSEC(timeout))
 * ev_unon_timer → k_work_cancel_delayable(&evt->zb_work)
 *
 * The k_work handler calls evt->cb(evt->data).
 * If cb returns >= 0, the timer is rescheduled for evt->period ms (periodic).
 * If cb returns < 0 (TL_ZB_TIMER_CANCEL pattern), the timer stops.
 */
#include <zephyr/kernel.h>
#include <zephyr/zigbee/zb_types.h>
#include "ev_timer.h"
#include "ev_buffer.h"

/* Semaphore defined in zb_main.c — signalled to wake the Zigbee thread */
extern struct k_sem zb_ev_sem;

static void ev_timer_work_handler(struct k_work *work)
{
	struct k_work_delayable *dwork = k_work_delayable_from_work(work);
	ev_timer_event_t *evt = CONTAINER_OF(dwork, ev_timer_event_t, zb_work);

	if (!evt->used || !evt->isRunning) {
		return;
	}

	evt->isRunning = 0;
	int ret = 0;

	if (evt->cb != NULL) {
		ret = evt->cb(evt->data);
	}

	/* Periodic: non-negative return means reschedule. */
	if (ret >= 0 && evt->period > 0 && evt->used) {
		evt->isRunning = 1;
		k_work_reschedule(&evt->zb_work, K_MSEC(evt->period));
	}

	k_sem_give(&zb_ev_sem);
}

void ev_timer_init(void)
{
	/* Timers are initialised on first use (ev_on_timer) */
}

void ev_on_timer(ev_timer_event_t *evt, u32 timeout)
{
	if (evt == NULL) {
		return;
	}
	if (!evt->used) {
		k_work_init_delayable(&evt->zb_work, ev_timer_work_handler);
		evt->used = 1;
	}
	evt->timeout   = timeout;
	evt->period    = timeout;
	evt->isRunning = 1;
	k_work_reschedule(&evt->zb_work, K_MSEC(timeout));
}

void ev_unon_timer(ev_timer_event_t *evt)
{
	if (evt == NULL || !evt->used) {
		return;
	}
	evt->isRunning = 0;
	k_work_cancel_delayable(&evt->zb_work);
}

bool ev_timer_exist(ev_timer_event_t *evt)
{
	return evt != NULL && evt->used && evt->isRunning;
}

bool ev_timer_enough(void)
{
	return true;
}

void ev_timer_process(void)
{
	/* No-op: k_work_q handles firing */
}

void ev_timer_update(u32 updateTime)
{
	ARG_UNUSED(updateTime);
}

void ev_timer_setPrevSysTick(u32 tick)
{
	ARG_UNUSED(tick);
}

ev_timer_event_t *ev_timer_nearestGet(void)
{
	return NULL;
}

ev_timer_event_t *ev_timer_taskPost(ev_timer_callback_t func, void *arg, u32 t_ms)
{
	/* Allocate from ev_buffer group that fits ev_timer_event_t.
	 * ev_timer_event_t is ~80 bytes with k_work_delayable; use group 2 (152B). */
	u8 *buf = ev_buf_allocate(sizeof(ev_timer_event_t));

	if (buf == NULL) {
		return NULL;
	}
	ev_timer_event_t *evt = (ev_timer_event_t *)buf;

	memset(evt, 0, sizeof(*evt));
	evt->cb   = func;
	evt->data = arg;
	ev_on_timer(evt, t_ms);
	return evt;
}

u8 ev_timer_taskCancel(ev_timer_event_t **evt)
{
	struct k_work_sync cancel_sync;
	bool was_used;
	ev_timer_event_t *timer_evt;

	if (evt == NULL || *evt == NULL) {
		return FAILURE;
	}

	if (k_is_in_isr()) {
		return FAILURE;
	}

	timer_evt = *evt;
	was_used = timer_evt->used;
	timer_evt->isRunning = 0;
	timer_evt->used = 0;
	if (was_used) {
		(void)k_work_cancel_delayable_sync(&timer_evt->zb_work, &cancel_sync);
	}

	ev_buf_free((u8 *)timer_evt);
	*evt = NULL;
	return SUCCESS;
}
