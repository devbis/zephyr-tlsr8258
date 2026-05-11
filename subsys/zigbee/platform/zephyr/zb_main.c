/* SPDX-License-Identifier: Apache-2.0 */
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/zigbee/zb_types.h>
#include "ev_timer.h"
#include "ev_poll.h"
#include "ev_buffer.h"

LOG_MODULE_REGISTER(zigbee, CONFIG_ZIGBEE_LOG_LEVEL);

/* Semaphore used to wake the Zigbee thread when events are ready.
 * Also signalled by ev_timer_work_handler after each timer fires. */
K_SEM_DEFINE(zb_ev_sem, 0, 1);
extern void zb_radio_smoke_probe(void);

static void zb_thread_fn(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	ev_buf_init();
	ev_timer_init();
	zb_radio_smoke_probe();

	LOG_INF("Zigbee thread started");

	while (1) {
		/* Wait up to 10 ms so poll handlers run even without events */
		k_sem_take(&zb_ev_sem, K_MSEC(10));
		ev_poll_process();
	}
}

K_THREAD_DEFINE(zb_thread,
		CONFIG_ZIGBEE_STACK_SIZE,
		zb_thread_fn,
		NULL, NULL, NULL,
		CONFIG_ZIGBEE_THREAD_PRIO,
		0, 0);
