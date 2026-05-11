/* SPDX-License-Identifier: Apache-2.0 */
#include <zephyr/kernel.h>
#include <zephyr/zigbee/zb_types.h>
#include "ev_poll.h"

ev_poll_t ev_poll[EV_POLL_MAX];

void ev_on_poll(ev_poll_e e, ev_poll_callback_t cb)
{
	if ((int)e >= EV_POLL_MAX) {
		return;
	}
	ev_poll[(int)e].cb    = cb;
	ev_poll[(int)e].valid = 1;
}

void ev_enable_poll(ev_poll_e e)
{
	if ((int)e < EV_POLL_MAX) {
		ev_poll[(int)e].valid = 1;
	}
}

void ev_disable_poll(ev_poll_e e)
{
	if ((int)e < EV_POLL_MAX) {
		ev_poll[(int)e].valid = 0;
	}
}

void ev_poll_process(void)
{
	for (int i = 0; i < EV_POLL_MAX; i++) {
		if (ev_poll[i].valid && ev_poll[i].cb != NULL) {
			ev_poll[i].cb();
		}
	}
}
