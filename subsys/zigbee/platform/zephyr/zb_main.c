/* SPDX-License-Identifier: Apache-2.0 */
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/toolchain.h>
#include <zephyr/zigbee/zb_bootstrap.h>
#include <zephyr/zigbee/zb_types.h>
#include <string.h>
#include "zb_radio_smoke.h"
#include "drv_radio.h"
#include "ev_timer.h"
#include "ev_poll.h"
#include "ev_buffer.h"
#include "zb_common_stub.h"

LOG_MODULE_REGISTER(zigbee, CONFIG_ZIGBEE_LOG_LEVEL);

extern void rf_init(void);

volatile u32 zb_ieee_addr_trace[4] = {
	0x49454545U, 0U, 0U, 0U,
};
volatile u8 zb_ieee_addr_bytes[8];

static const addrExt_t zb_fixed_ieee_addr = {
	0x02, 0x00, 0x02, 0x50, 0xe0, 0x38, 0xc1, 0xa4,
};

static bool zb_ieee_addr_is_zero(const u8 *addr)
{
	size_t i;

	if (addr == NULL) {
		return true;
	}

	for (i = 0; i < sizeof(addrExt_t); i++) {
		if (addr[i] != 0U) {
			return false;
		}
	}

	return true;
}

static void zb_ieee_addr_reverse(u8 *addr)
{
	size_t i;

	for (i = 0U; i < sizeof(addrExt_t) / 2U; i++) {
		u8 tmp = addr[i];

		addr[i] = addr[sizeof(addrExt_t) - 1U - i];
		addr[sizeof(addrExt_t) - 1U - i] = tmp;
	}
}

static void zb_init_ieee_addr_once(void)
{
	addrExt_t addr = {0};
	bool have_primary;

	if (!zb_ieee_addr_is_zero(g_zbMacPib.extAddress)) {
		memcpy((void *)zb_ieee_addr_bytes, g_zbMacPib.extAddress,
		       sizeof(g_zbMacPib.extAddress));
		zb_ieee_addr_trace[1] = 2U;
		LOG_HEXDUMP_INF(g_zbMacPib.extAddress, sizeof(g_zbMacPib.extAddress),
				"Zigbee IEEE address already set");
		return;
	}

	have_primary = drv_get_primary_ieee_addr(addr);
	zb_ieee_addr_trace[1] = have_primary ? 1U : 3U;
	if (!have_primary) {
		zb_ieee_addr_trace[2] = 0x46495831U;
		memcpy(g_zbMacPib.extAddress, zb_fixed_ieee_addr,
		       sizeof(g_zbMacPib.extAddress));
		memcpy((void *)zb_ieee_addr_bytes, zb_fixed_ieee_addr,
		       sizeof(zb_fixed_ieee_addr));
		LOG_HEXDUMP_INF(zb_fixed_ieee_addr, sizeof(zb_fixed_ieee_addr),
				"Zigbee IEEE address from fixed fallback");
		return;
	}

	/* hwinfo returns canonical EUI-64 byte order; the Zigbee stack stores it
	 * in the reversed internal order used by the Telink MAC paths.
	 */
	zb_ieee_addr_reverse(addr);
	memcpy(g_zbMacPib.extAddress, addr, sizeof(addr));
	memcpy((void *)zb_ieee_addr_bytes, addr, sizeof(addr));
	LOG_HEXDUMP_INF(addr, sizeof(addr), "Zigbee IEEE address");
}

/* Semaphore used to wake the Zigbee thread when events are ready.
 * Also signalled by ev_timer_work_handler after each timer fires. */
K_SEM_DEFINE(zb_ev_sem, 0, 1);

static bool zb_bootstrap_done;
static bool zb_core_init_done;
static bool zb_commissioning_pending;
static bool zb_waiting_for_radio_log;
volatile u32 zb_main_trace[8] = {
	0x4d41494eU, 0U, 0U, 0U, 0U, 0U, 0U, 0U,
};

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

	zb_main_trace[0] = 0x7100U;

	if (!zb_core_init_done) {
		/* Deterministic ED bootstrap order. */
		ev_buf_init();
		ev_timer_init();
		zdo_init();
		af_init();
		zb_init_ieee_addr_once();
		rf_init();
		zb_core_init_done = true;
		zb_main_trace[1] = 0x7101U;
	}

	zb_radio_init();
	if (!zb_radio_is_ready()) {
		zb_main_trace[2] = 0x71eeU;
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
	zb_main_trace[2] = 0x7102U;

	zb_platform_app_bootstrap_ready();
	zb_main_trace[3] = 0x7103U;

	if (zb_platform_app_enable_radio_smoke_probe()) {
		zb_radio_smoke_probe();
	}

	if (zb_platform_app_should_start_commissioning()) {
		LOG_INF("Zigbee commissioning trigger queued");
		zb_commissioning_pending = true;
		zb_main_trace[4] = 0x7104U;
	} else {
		LOG_INF("Zigbee commissioning trigger not requested");
		zb_main_trace[4] = 0x7105U;
	}

	zb_bootstrap_done = true;
	zb_main_trace[5] = 0x7106U;
}

static void zb_process_deferred_commissioning(void)
{
	if (!zb_bootstrap_done || !zb_commissioning_pending) {
		return;
	}

	zb_commissioning_pending = false;
	zb_main_trace[6] = 0x7107U;
	LOG_INF("Zigbee commissioning trigger requested");
	zb_platform_app_start_commissioning();
	zb_main_trace[7] = 0x7108U;
}

static void zb_thread_fn(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	zb_main_trace[0] = 0x7000U;
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
