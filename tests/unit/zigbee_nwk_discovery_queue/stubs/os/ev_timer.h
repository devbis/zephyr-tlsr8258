/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include "zb_common_stub.h"

typedef int (*ev_timer_callback_t)(void *arg);

typedef struct ev_timer_event_t {
	ev_timer_callback_t cb;
	void *data;
	u32 timeout;
	u32 period;
	u32 curSysTick;
	u8 isRunning;
	u8 isBusy;
	u8 used;
	struct ev_timer_event_t *next;
} ev_timer_event_t;

void ev_on_timer(ev_timer_event_t *evt, u32 timeout);
void ev_unon_timer(ev_timer_event_t *evt);
