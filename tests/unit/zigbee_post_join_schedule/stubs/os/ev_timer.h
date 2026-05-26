/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include "zb_common_stub.h"

typedef int (*ev_timer_callback_t)(void *data);

typedef struct ev_timer_event_t {
	ev_timer_callback_t cb;
	void *data;
	u32 timeout;
	u8 used;
} ev_timer_event_t;

void ev_on_timer(ev_timer_event_t *evt, u32 timeout);
