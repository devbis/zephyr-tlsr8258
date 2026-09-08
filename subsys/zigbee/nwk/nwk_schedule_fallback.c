/* SPDX-License-Identifier: Apache-2.0 */

#include "nwk_schedule_fallback.h"

#include <string.h>

bool zb_nwk_schedule_task_or_timer(tl_zb_callback_t task_cb,
				   ev_timer_event_t *timer_evt,
				   ev_timer_callback_t timer_cb,
				   void *arg,
				   u32 delay_ms)
{
	u8 rc;

	if (task_cb == NULL || timer_cb == NULL) {
		return false;
	}

	rc = TL_SCHEDULE_TASK(task_cb, arg);
	if (rc == RET_OK) {
		return true;
	}

	if (rc != RET_BUSY) {
		return false;
	}

	if (timer_evt == NULL) {
		return false;
	}

	if (timer_evt->cb == NULL) {
		memset(timer_evt, 0, sizeof(*timer_evt));
		timer_evt->cb = timer_cb;
	} else if (timer_evt->cb != timer_cb) {
		return false;
	}

	timer_evt->data = arg;
	ev_on_timer(timer_evt, delay_ms);
	return true;
}
