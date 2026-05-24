/* SPDX-License-Identifier: Apache-2.0 */
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/toolchain.h>
#include <zephyr/zigbee/zb_bootstrap.h>
#include <zephyr/zigbee/zb_types.h>
#include <string.h>
#include "zb_radio_smoke.h"
#include "drv_hw.h"
#include "drv_radio.h"
#include "ev_timer.h"
#include "ev_poll.h"
#include "ev_buffer.h"
#include "zb_common_stub.h"

LOG_MODULE_REGISTER(zigbee, CONFIG_ZIGBEE_LOG_LEVEL);

extern void rf_init(void);
extern void aps_init(void);
extern void tl_zbMacInit(u8 coldReset);
extern void tl_zbNwkInit(u8 coldReset);

static const addrExt_t zb_fixed_ieee_addr = {
	0x02, 0x00, 0x02, 0x50, 0xe0, 0x38, 0xc1, 0xa4,
};

void zb_platform_apply_runtime_ieee_addr(void)
{
	memcpy(g_zbMacPib.extAddress, zb_fixed_ieee_addr, sizeof(g_zbMacPib.extAddress));
}

const uint8_t *zb_platform_runtime_ieee_addr_get(void)
{
	return zb_fixed_ieee_addr;
}

/* Semaphore used to wake the Zigbee thread when events are ready.
 * Also signalled by ev_timer_work_handler after each timer fires. */
K_SEM_DEFINE(zb_ev_sem, 0, 1);

static bool zb_bootstrap_done;
static bool zb_core_init_done;
static bool zb_commissioning_pending;
static bool zb_waiting_for_radio_log;
extern volatile u32 zb_nwk_ed_trace[];

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

void __weak zb_platform_app_network_left(void)
{
}

void __weak bdb_ed_runtime_join_complete(void)
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
		u8 cold_reset = TRUE;

		zb_nwk_ed_trace[15] = 0xA5A00001U;
		/* Deterministic ED bootstrap order. */
		ev_buf_init();
		zb_nwk_ed_trace[15] = 0xA5A00101U;
		ev_timer_init();
		zb_nwk_ed_trace[15] = 0xA5A00102U;
		tl_zbMacInit(cold_reset);
		zb_nwk_ed_trace[15] = 0xA5A00103U;
		tl_zbNwkInit(cold_reset);
		zb_nwk_ed_trace[15] = 0xA5A00104U;
		aps_init();
		zb_nwk_ed_trace[15] = 0xA5A00105U;
		af_init();
		zb_nwk_ed_trace[15] = 0xA5A00106U;
		zdo_init();
		zb_nwk_ed_trace[15] = 0xA5A00107U;
		(void)zb_platform_restore_persistent_state();
		zb_nwk_ed_trace[15] = 0xA5A00108U;
		zb_platform_apply_runtime_ieee_addr();
		zb_nwk_ed_trace[15] = 0xA5A00109U;
		rf_init();
		zb_nwk_ed_trace[15] = 0xA5A0010AU;
		drv_enable_irq();
		zb_nwk_ed_trace[15] = 0xA5A0010BU;
		zb_core_init_done = true;
		zb_nwk_ed_trace[15] = 0xA5A00002U;
	}

	zb_radio_init();
	zb_nwk_ed_trace[15] = 0xA5A00003U;
	if (!zb_radio_is_ready()) {
		zb_nwk_ed_trace[14]++;
		zb_nwk_ed_trace[15] = 0xA5A00004U;
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
	zb_nwk_ed_trace[15] = 0xA5A00005U;

	zb_platform_app_bootstrap_ready();
	zb_nwk_ed_trace[15] = 0xA5B00001U;

	if (zb_platform_app_enable_radio_smoke_probe()) {
		zb_radio_smoke_probe();
	}

	if (zb_platform_app_should_start_commissioning()) {
		zb_nwk_ed_trace[15] = 0xA5B00002U;
		zb_commissioning_pending = true;
	} else {
		zb_nwk_ed_trace[15] = 0xA5B00003U;
	}

	zb_bootstrap_done = true;
}

static void zb_process_deferred_commissioning(void)
{
	if (!zb_bootstrap_done || !zb_commissioning_pending) {
		return;
	}

	zb_nwk_ed_trace[15] = 0xA5B00004U;
	zb_commissioning_pending = false;
	zb_platform_app_start_commissioning();
	zb_nwk_ed_trace[15] = 0xA5B00006U;
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
			ev_timer_process();
			ev_poll_process();
			if (!zb_bootstrap_done) {
				k_yield();
				continue;
			}
		}

		ev_timer_process();
		ev_poll_process();
		zb_process_deferred_commissioning();

		if (k_sem_take(&zb_ev_sem, K_NO_WAIT) == 0) {
			zb_nwk_ed_trace[15] = 0xA5B00007U;
			continue;
		}

		if (ev_timer_nearestGet() != NULL) {
			/*
			 * Keep vendor-style timer progress alive even when no external
			 * event wakes the Zigbee thread.  TLSR8258 hardware proved the
			 * Zephyr delayed-work timeout path was not expiring here.
			 */
			zb_nwk_ed_trace[15] = 0xA5B00008U;
			k_yield();
			k_busy_wait(1000);
			continue;
		}

		zb_nwk_ed_trace[15] = 0xA5B00009U;
		(void)k_sem_take(&zb_ev_sem, K_FOREVER);
	}
}

K_THREAD_DEFINE(zb_thread,
		CONFIG_ZIGBEE_STACK_SIZE,
		zb_thread_fn,
		NULL, NULL, NULL,
		CONFIG_ZIGBEE_THREAD_PRIO,
		0, 0);
