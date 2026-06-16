/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Zephyr port of the vendor ev_timer backend.
 *
 * The previous k_work_delayable-backed implementation armed timers correctly
 * on TLSR8258, but the delayed callbacks never expired on hardware.  The
 * vendor stack drives its timer list directly from clock_time(); keep that
 * model here so Zigbee discovery/join timeouts progress independently of the
 * broken delayed-work path.
 */
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/zigbee/zb_types.h>

#include "drv_hw.h"
#include "drv_radio.h"
#include "ev_buffer.h"
#include "ev_rtc.h"
#include "ev_timer.h"
#include "utlist.h"

extern struct k_sem zb_ev_sem;

typedef struct {
	ev_timer_event_t *timer_head;
	ev_timer_event_t *timer_nearest;
} ev_timer_ctrl_t;

static ev_timer_ctrl_t ev_timer_ctrl;
static u32 prev_sys_tick;
static u32 rem_sys_tick_us;

__attribute__((weak)) void ev_rtc_update(u32 update_time_ms)
{
	ARG_UNUSED(update_time_ms);
}

static void ev_timer_nearest_update(void)
{
	ev_timer_event_t *timer_evt = ev_timer_ctrl.timer_head;

	ev_timer_ctrl.timer_nearest = ev_timer_ctrl.timer_head;
	while (timer_evt != NULL) {
		if (ev_timer_ctrl.timer_nearest == NULL ||
		    timer_evt->timeout < ev_timer_ctrl.timer_nearest->timeout) {
			ev_timer_ctrl.timer_nearest = timer_evt;
		}
		timer_evt = timer_evt->next;
	}
}

static void ev_timer_runtime_clear(ev_timer_event_t *evt)
{
	if (evt == NULL) {
		return;
	}

	evt->isRunning = 0U;
	evt->curSysTick = 0U;
}

static void ev_timer_execute_cb(void)
{
	ev_timer_event_t *timer_evt = ev_timer_ctrl.timer_head;
	ev_timer_event_t *prev_head = timer_evt;

	while (timer_evt != NULL) {
		if (timer_evt->timeout == 0U) {
			s32 next_timeout;

			timer_evt->isBusy = 1U;
			next_timeout = timer_evt->cb(timer_evt->data);
			if (next_timeout < 0) {
				/*
				 * One-shot timer: callback returned -1, meaning it
				 * does not want to be rescheduled. `ev_unon_timer`
				 * removes the node from the list but does NOT free
				 * the ev_buf slab block — only `ev_timer_taskCancel`
				 * does that. With only 4 group-0 buffer slabs, a
				 * handful of one-shot timer expiries (ACK-wait,
				 * post-assoc DATA_REQ poll, mac_csmaStart backoff,
				 * etc.) exhaust the pool and every subsequent
				 * `ev_timer_taskPost` returns NULL — silently
				 * dropping the DATA_REQ poll, retry timers, and so
				 * on. Free the slab block here so one-shot timers
				 * don't leak. Capture the next pointer before
				 * freeing so the iteration can continue past the
				 * removed node.
				 */
				ev_timer_event_t *next_evt = timer_evt->next;

				ev_unon_timer(timer_evt);
				timer_evt->used = 0U;
				if (is_ev_buf(timer_evt)) {
					ev_buf_free((u8 *)timer_evt);
				}
				/* prev_head was timer_head when we started; both
				 * may have shifted if the callback posted/canceled
				 * other timers. Recover by restarting from head
				 * when our local copy went stale.
				 */
				if (prev_head != ev_timer_ctrl.timer_head) {
					timer_evt = ev_timer_ctrl.timer_head;
					prev_head = timer_evt;
				} else {
					timer_evt = next_evt;
				}
				continue;
			} else if (next_timeout == 0) {
				timer_evt->timeout = timer_evt->period;
			} else {
				timer_evt->period = (u32)next_timeout;
				timer_evt->timeout = timer_evt->period;
			}
			timer_evt->isBusy = 0U;

			if (prev_head != ev_timer_ctrl.timer_head) {
				timer_evt = ev_timer_ctrl.timer_head;
				prev_head = timer_evt;
			} else {
				timer_evt = timer_evt->next;
			}
		} else {
			timer_evt = timer_evt->next;
		}
	}

	ev_timer_nearest_update();
}

void ev_timer_init(void)
{
	memset(&ev_timer_ctrl, 0, sizeof(ev_timer_ctrl));
	prev_sys_tick = clock_time();
	rem_sys_tick_us = 0U;
}

void ev_timer_setPrevSysTick(u32 tick)
{
	prev_sys_tick = tick;
	rem_sys_tick_us = 0U;
}

ev_timer_event_t *ev_timer_nearestGet(void)
{
	return ev_timer_ctrl.timer_nearest;
}

bool ev_timer_enough(void)
{
	return true;
}

bool ev_timer_exist(ev_timer_event_t *evt)
{
	ev_timer_event_t *timer_evt = ev_timer_ctrl.timer_head;

	while (timer_evt != NULL) {
		if (timer_evt == evt) {
			return true;
		}
		timer_evt = timer_evt->next;
	}

	return false;
}

void ev_on_timer(ev_timer_event_t *evt, u32 timeout)
{
	ev_timer_event_t *existing = NULL;
	u32 irq_state;

	if (evt == NULL) {
		return;
	}

	evt->period = timeout;
	irq_state = drv_disable_irq();
	LIST_EXIST(ev_timer_ctrl.timer_head, evt, existing);
	if (existing != NULL) {
		existing->timeout = evt->period;
	} else {
		if (evt->isRunning == 0U) {
			evt->curSysTick = clock_time();
		}
		evt->timeout = evt->period;
		LIST_ADD(ev_timer_ctrl.timer_head, evt);
	}
	ev_timer_nearest_update();
	drv_restore_irq(irq_state);
	k_sem_give(&zb_ev_sem);
}

void ev_unon_timer(ev_timer_event_t *evt)
{
	ev_timer_event_t *existing = NULL;
	u32 irq_state;

	if (evt == NULL) {
		return;
	}

	LIST_EXIST(ev_timer_ctrl.timer_head, evt, existing);
	if (existing == NULL) {
		return;
	}

	irq_state = drv_disable_irq();
	ev_timer_runtime_clear(evt);
	LIST_DELETE(ev_timer_ctrl.timer_head, evt);
	ev_timer_nearest_update();
	drv_restore_irq(irq_state);
}

ev_timer_event_t *ev_timer_taskPost(ev_timer_callback_t func, void *arg, u32 t_ms)
{
	ev_timer_event_t *timer_evt = (ev_timer_event_t *)ev_buf_allocate(sizeof(ev_timer_event_t));

	if (timer_evt == NULL) {
		return NULL;
	}

	memset(timer_evt, 0, sizeof(*timer_evt));
	timer_evt->cb = func;
	timer_evt->data = arg;
	timer_evt->used = 1U;
	ev_on_timer(timer_evt, t_ms);

	return timer_evt;
}

u8 ev_timer_taskCancel(ev_timer_event_t **evt)
{
	ev_timer_event_t *timer_evt;

	if (evt == NULL || *evt == NULL) {
		return FAILURE;
	}

	timer_evt = *evt;
	if (timer_evt->isBusy) {
		return FAILURE;
	}

	ev_unon_timer(timer_evt);
	timer_evt->used = 0U;
	if (is_ev_buf(timer_evt)) {
		ev_buf_free((u8 *)timer_evt);
	}
	*evt = NULL;

	return SUCCESS;
}

void ev_timer_update(u32 update_time_ms)
{
	ev_timer_event_t *timer_evt;
	u32 irq_state;

	if (update_time_ms == 0U) {
		return;
	}

	irq_state = drv_disable_irq();
	ev_rtc_update(update_time_ms);
	timer_evt = ev_timer_ctrl.timer_head;
	while (timer_evt != NULL) {
		u32 elapsed_ms;

		if (timer_evt->isRunning) {
			elapsed_ms = update_time_ms;
		} else {
			u32 cur_sys_tick = clock_time();
			u32 elapsed_us =
				clock_cycles_to_us((u32)(cur_sys_tick - timer_evt->curSysTick));

			elapsed_ms = elapsed_us / 1000U;
			timer_evt->isRunning = 1U;
		}

		if (timer_evt->timeout > elapsed_ms) {
			timer_evt->timeout -= elapsed_ms;
		} else {
			timer_evt->timeout = 0U;
		}

		timer_evt = timer_evt->next;
	}
	drv_restore_irq(irq_state);
}

void ev_timer_process(void)
{
	u32 curr_sys_tick = clock_time();
	u32 elapsed_us;
	u32 update_time_ms;

	if (curr_sys_tick == prev_sys_tick) {
		return;
	}

	elapsed_us = clock_cycles_to_us((u32)(curr_sys_tick - prev_sys_tick));
	prev_sys_tick = curr_sys_tick;
	update_time_ms = elapsed_us / 1000U;
	rem_sys_tick_us += elapsed_us % 1000U;
	update_time_ms += rem_sys_tick_us / 1000U;
	rem_sys_tick_us %= 1000U;

	if (update_time_ms != 0U) {
		ev_timer_update(update_time_ms);
	}

	ev_timer_execute_cb();
}
