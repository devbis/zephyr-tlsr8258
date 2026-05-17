/* SPDX-License-Identifier: Apache-2.0 */
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/toolchain.h>
#include <zephyr/zigbee/zb_bootstrap.h>
#include <zephyr/zigbee/zb_types.h>
#include "zb_radio_smoke.h"
#include "drv_radio.h"
#include "ev_timer.h"
#include "ev_poll.h"
#include "ev_buffer.h"
#include "zb_common_stub.h"

LOG_MODULE_REGISTER(zigbee, CONFIG_ZIGBEE_LOG_LEVEL);

extern void rf_init(void);

/* Semaphore used to wake the Zigbee thread when events are ready.
 * Also signalled by ev_timer_work_handler after each timer fires. */
K_SEM_DEFINE(zb_ev_sem, 0, 1);

static bool zb_bootstrap_done;
static bool zb_core_init_done;
static bool zb_commissioning_pending;
static bool zb_waiting_for_radio_log;

void __weak zb_platform_app_bootstrap_ready(void)
{
}

bool __weak zb_platform_app_enable_radio_smoke_probe(void)
{
	return false;
}

bool __weak zb_platform_app_should_start_commissioning(void)
{
	return false;
}

void __weak zb_platform_app_start_commissioning(void)
{
}

bool __weak zb_platform_app_get_fixed_join_target(struct zb_platform_bdb_fixed_target *target)
{
	ARG_UNUSED(target);
	return false;
}

bool __weak zb_platform_app_get_join_profile(struct zb_platform_bdb_join_profile *profile)
{
	ARG_UNUSED(profile);
	return false;
}

static void zb_core_bootstrap_once(void)
{
	if (zb_bootstrap_done) {
		return;
	}

	if (!zb_core_init_done) {
		/* Deterministic ED bootstrap order. */
		ev_buf_init();
		ev_timer_init();
		zdo_init();
		af_init();
		rf_init();
		zb_core_init_done = true;
	}

	zb_radio_init();
	if (!zb_radio_is_ready()) {
		if (!zb_waiting_for_radio_log) {
			LOG_WRN("Zigbee bootstrap waiting for radio readiness");
			zb_waiting_for_radio_log = true;
		}
		return;
	}

	if (zb_waiting_for_radio_log) {
		LOG_INF("Zigbee radio ready; completing bootstrap");
		zb_waiting_for_radio_log = false;
	}

	zb_platform_app_bootstrap_ready();

	if (zb_platform_app_enable_radio_smoke_probe()) {
		zb_radio_smoke_probe();
	}

	if (zb_platform_app_should_start_commissioning()) {
		LOG_INF("Zigbee commissioning trigger queued");
		zb_commissioning_pending = true;
	} else {
		LOG_INF("Zigbee commissioning trigger not requested");
	}

	zb_bootstrap_done = true;
}

static void zb_process_deferred_commissioning(void)
{
	if (!zb_bootstrap_done || !zb_commissioning_pending) {
		return;
	}

	zb_commissioning_pending = false;
	LOG_INF("Zigbee commissioning trigger requested");
	zb_platform_app_start_commissioning();
}

static void zb_thread_fn(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	LOG_INF("Zigbee thread started");

	while (1) {
		if (!zb_bootstrap_done) {
			zb_core_bootstrap_once();
			if (!zb_bootstrap_done) {
				k_sleep(K_MSEC(10));
				continue;
			}
		}

		/* Wait up to 10 ms so poll handlers run even without events */
		k_sem_take(&zb_ev_sem, K_MSEC(10));
		ev_poll_process();
		zb_process_deferred_commissioning();
	}
}

K_THREAD_DEFINE(zb_thread,
		CONFIG_ZIGBEE_STACK_SIZE,
		zb_thread_fn,
		NULL, NULL, NULL,
		CONFIG_ZIGBEE_THREAD_PRIO,
		0, 0);
