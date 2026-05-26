/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include "zb_common_stub.h"
#include "os/ev_timer.h"
#include <stdbool.h>

bool zb_nwk_schedule_task_or_timer(tl_zb_callback_t task_cb,
				   ev_timer_event_t *timer_evt,
				   ev_timer_callback_t timer_cb,
				   void *arg,
				   u32 delay_ms);
