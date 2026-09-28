/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "zb_common.h"
#include "os/ev_timer.h"
#include <stdbool.h>

bool zb_nwk_schedule_task_or_timer(zb_callback_t task_cb,
				   ev_timer_event_t *timer_evt,
				   ev_timer_callback_t timer_cb,
				   void *arg,
				   uint32_t delay_ms);
