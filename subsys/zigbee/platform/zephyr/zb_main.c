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
#include "zdo/zdo_api.h"

LOG_MODULE_REGISTER(zigbee, CONFIG_ZIGBEE_LOG_LEVEL);

void k_sys_fatal_error_handler(unsigned int reason, const struct arch_esf *esf)
{
	ARG_UNUSED(reason);
	ARG_UNUSED(esf);
	for (;;) {
		/* spin on fatal error rather than rebooting */
	}
}

extern void rf_init(void);
extern void aps_init(void);
extern void tl_zbMacInit(u8 coldReset);
extern void tl_zbNwkInit(u8 coldReset);
#if defined(CONFIG_ZIGBEE_ROUTER)
extern void tl_zbMacTaskProc(void);
#endif

static const addrExt_t zb_fixed_ieee_addr = {
	/*
	 * Experimental: bump to ...05 to bypass cached Ember NCP child-table
	 * state for our previous IEEE.  SWS dump showed Z2M sending NWK-encrypted
	 * frames to us right after AssocResp success — i.e. the coordinator
	 * thought we were already joined with an NWK key, so it skipped sending
	 * an unsolicited Transport-Key.  Z2M device/remove + bridge restart did
	 * not clear that state, suggesting it lives in the Ember adapter NVRAM
	 * and only a brand-new IEEE will look like a fresh joiner.
	 */
#if defined(CONFIG_ARCH_POSIX)
	/* native_sim host_socket_coordinator hardcodes the joiner IEEE as
	 * a4:c1:38:e0:50:02:00:02 in its Transport-Key dstExtAddr; match it so
	 * ss_apsTransportKeyCmdHandle's ext_addr_is_local() check accepts the key.
	 * The low byte is build-configurable (ZB_FIXED_IEEE_LOW, default 0x02) so a
	 * second native_sim node (e.g. an ED joining THROUGH a router) can run on the
	 * same medium with a distinct IEEE. */
	CONFIG_ZIGBEE_NATIVE_SIM_IEEE_LOW, 0x00, 0x02, 0x50, 0xe0, 0x38, 0xc1, 0xa4,
#else
	/* Bumped ...06 -> ...08 -> ...0c: a brand-new IEEE looks like a fresh joiner
	 * to the Z2M/Ember TC, which caches a known IEEE as "already joined" and
	 * stops sending the unsolicited Transport-Key. Sniffer 2026-06-29 confirmed
	 * ...08 now stuck in assoc-SUCCESS-but-no-TK loop (Ember NVRAM still had it
	 * even after a Z2M device delete), so bump again to a fresh value. */
	0x0c, 0x00, 0x02, 0x50, 0xe0, 0x38, 0xc1, 0xa4,
#endif
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
static bool zb_persistent_rejoin_in_progress;
static uint32_t zb_persistent_rejoin_started_ms;
static uint32_t zb_last_commission_retry_ms;

#define ZB_COMMISSION_RETRY_POLL_MS 5000U
/*
 * Maximum time the bootstrap will block on a persisted-state rejoin before
 * giving up and letting fresh commissioning run.  Stale joined=1 with no
 * usable Transport Key would otherwise keep the device parked here forever
 * because the NWK manager never returns to IDLE.
 */
#define ZB_PERSISTENT_REJOIN_GIVEUP_MS 12000U

/*
 * Link watchdog: if the device thinks it is joined but TX activity stops
 * succeeding for an extended period (a parent that no longer accepts our
 * frames, MAC/security state desync after a debugger halt, etc.), trigger
 * an NWK Rejoin. Rejoin preserves the network key and joined credentials,
 * just refreshes the parent link — exactly what we want after probe-rs
 * disconnect leaves the chip in a "zombie joined" state.
 */
#define ZB_LINK_BAD_TX_FAIL_WINDOW_MS  10000U   /* 10 s of no progress */
#define ZB_LINK_BAD_TX_FAIL_DELTA      8U       /* and at least N new failures */
#define ZB_LINK_REJOIN_BACKOFF_MS      60000U   /* don't re-fire within 60 s */
#define ZB_LINK_REJOIN_SCAN_DURATION   3U       /* short scan: 60ms × 16 ch */

extern bool tl_zbNwkEdMinimalRejoinStart(u32 scanChannels, u8 scanDuration, bool withBackoff);
extern bool zb_isDeviceJoinedNwk(void);

static uint32_t zb_link_last_tx_success_count;
static uint32_t zb_link_last_tx_success_ms;
static uint32_t zb_link_last_tx_fail_snapshot;
static uint32_t zb_link_last_rejoin_attempt_ms;
static bool zb_link_baseline_set;

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

		/* Deterministic ED bootstrap order. */
		ev_buf_init();
		ev_timer_init();
		tl_zbMacInit(cold_reset);
		tl_zbNwkInit(cold_reset);
		aps_init();
		af_init();
		zdo_init();
		(void)zb_platform_restore_persistent_state();
		/*
		 * Boot-time snapshot of NVS-restored Zigbee state. Lets us tell
		 * from the very first RTT lines whether the chip thinks it's
		 * already in a network (rejoin path) or starting fresh (steering
		 * from scratch). Without this we have to read trace counters
		 * post-mortem to disambiguate.
		 */
		LOG_INF("zb nvs restore: joined=%u short=0x%04x pan=0x%04x "
			"coord=0x%04x nwk_addr=0x%04x",
			(unsigned)g_zbNwkCtx.joined,
			(unsigned)g_zbMacPib.shortAddress,
			(unsigned)g_zbMacPib.panId,
			(unsigned)g_zbMacPib.coordShortAddress,
			(unsigned)g_zbNIB.nwkAddr);
		zb_platform_apply_runtime_ieee_addr();
		/*
		 * Push the runtime IEEE address into the TLSR8258 radio's HW
		 * filter immediately. Otherwise filter_ieee_addr stays all-
		 * zero until either the factory-reset path or the join-success
		 * path runs zb_radio_port_update_filters, and the radio's
		 * auto-ACK for ASSOCIATION-RESPONSE (sent to our extaddr by
		 * the coordinator before we've been assigned a short addr)
		 * fails its filter_ieee_addr memcmp — coordinator retries 4×
		 * and we miss the short address allocation.
		 * tlsr8258_iface_init() does this too via net_if_set_link_addr,
		 * but our shell sample doesn't bring up a Zephyr net iface, so
		 * iface_init never runs. Filter PAN stays 0xFFFF (wildcard)
		 * which is correct for the joining phase.
		 */
		zb_radio_port_update_filters(MAC_INVALID_PANID,
					     MAC_SHORT_ADDR_BROADCAST,
					     g_zbMacPib.extAddress);
		rf_init();
		drv_enable_irq();
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

	if (zb_platform_bdb_service_persistent_rejoin()) {
		uint32_t started = k_uptime_get_32();

		zb_persistent_rejoin_in_progress = true;
		zb_persistent_rejoin_started_ms = (started == 0U) ? 1U : started;
	}

	if (zb_platform_app_enable_radio_smoke_probe()) {
		zb_radio_smoke_probe();
	}

	if (!zb_persistent_rejoin_in_progress &&
	    zb_platform_app_should_start_commissioning()) {
		zb_commissioning_pending = true;
	} else {
	}

	zb_bootstrap_done = true;
}

static void zb_process_deferred_persistent_rejoin(void)
{
	if (!zb_bootstrap_done) {
		return;
	}

	if (zb_isDeviceJoinedNwk() && zdo_ifZdoNwkManagerIdle()) {
		zb_persistent_rejoin_in_progress = false;
		zb_persistent_rejoin_started_ms = 0U;
	}

	if (zb_persistent_rejoin_in_progress) {
		/*
		 * Give up if the persisted-state rejoin keeps the NWK manager
		 * non-idle past the budget without delivering a real join.
		 * Otherwise stale joined=1 in NV blocks commissioning forever.
		 */
		uint32_t elapsed = k_uptime_get_32() - zb_persistent_rejoin_started_ms;

		if (zb_persistent_rejoin_started_ms != 0U &&
		    elapsed > ZB_PERSISTENT_REJOIN_GIVEUP_MS &&
		    !zb_isDeviceJoinedNwk()) {
			zb_platform_bdb_abandon_persistent_rejoin();
			zb_persistent_rejoin_in_progress = false;
			zb_persistent_rejoin_started_ms = 0U;
		}
		return;
	}

	if (zb_platform_bdb_service_persistent_rejoin()) {
		uint32_t started = k_uptime_get_32();

		zb_persistent_rejoin_in_progress = true;
		zb_persistent_rejoin_started_ms = (started == 0U) ? 1U : started;
	}
}

static void zb_process_deferred_commissioning(void)
{
	if (!zb_bootstrap_done || !zb_commissioning_pending) {
		return;
	}

	if (!zdo_ifZdoNwkManagerIdle()) {
		return;
	}

	zb_commissioning_pending = false;
	zb_platform_app_start_commissioning();
}

static void zb_requeue_commissioning_if_needed(void)
{
	uint32_t now_ms;

	if (!zb_bootstrap_done || zb_commissioning_pending || zb_isDeviceJoinedNwk()) {
		return;
	}

	if (!zb_platform_app_should_start_commissioning()) {
		return;
	}

	now_ms = k_uptime_get_32();
	if ((zb_last_commission_retry_ms != 0U) &&
	    ((now_ms - zb_last_commission_retry_ms) < ZB_COMMISSION_RETRY_POLL_MS)) {
		return;
	}

	zb_last_commission_retry_ms = now_ms;
	zb_commissioning_pending = true;
}

static void zb_link_watchdog_tick(void)
{
	struct zb_platform_radio_diag_snapshot snap;
	uint32_t now_ms;

	if (!zb_bootstrap_done || !zb_isDeviceJoinedNwk()) {
		return;
	}
	if (zb_platform_radio_diag_get(&snap) < 0) {
		return;
	}

	now_ms = k_uptime_get_32();

	/* First call after join → seed the baseline, no decision yet. */
	if (!zb_link_baseline_set) {
		zb_link_last_tx_success_count = snap.tx_success;
		zb_link_last_tx_success_ms = now_ms;
		zb_link_last_tx_fail_snapshot = snap.tx_failures;
		zb_link_baseline_set = true;
		return;
	}

	/* Successful TX since last tick → bookmark and reset window. */
	if (snap.tx_success != zb_link_last_tx_success_count) {
		zb_link_last_tx_success_count = snap.tx_success;
		zb_link_last_tx_success_ms = now_ms;
		zb_link_last_tx_fail_snapshot = snap.tx_failures;
		return;
	}

	uint32_t since_last_success_ms = now_ms - zb_link_last_tx_success_ms;
	uint32_t fail_delta = snap.tx_failures - zb_link_last_tx_fail_snapshot;

	if (fail_delta < ZB_LINK_BAD_TX_FAIL_DELTA) {
		return;
	}
	if (since_last_success_ms < ZB_LINK_BAD_TX_FAIL_WINDOW_MS) {
		return;
	}
	if ((zb_link_last_rejoin_attempt_ms != 0U) &&
	    ((now_ms - zb_link_last_rejoin_attempt_ms) < ZB_LINK_REJOIN_BACKOFF_MS)) {
		return;
	}

	LOG_WRN("zb link watchdog: joined but no TX success in %u ms "
		"(failures+=%u) — triggering NWK rejoin",
		(unsigned)since_last_success_ms, (unsigned)fail_delta);

	zb_link_last_rejoin_attempt_ms = now_ms;
	/* Reset baseline so the next window is measured from now. */
	zb_link_last_tx_fail_snapshot = snap.tx_failures;

#if !defined(CONFIG_ZIGBEE_ROUTER)
	uint32_t scan_mask = (((u32)1U << (TL_ZB_MAC_CHANNEL_STOP + 1U)) -
			      ((u32)1U << TL_ZB_MAC_CHANNEL_START));
	if (!tl_zbNwkEdMinimalRejoinStart(scan_mask, ZB_LINK_REJOIN_SCAN_DURATION, false)) {
		LOG_WRN("zb link watchdog: rejoin start rejected (state busy)");
	}
#endif
}

/*
 * Radio RX poll hook, called every ZB-loop tick. The ZB thread is a no-yield
 * busy-loop, so preemptible driver RX threads are starved; a polled radio
 * backend (native_sim socket medium) overrides this to drain its RX from the
 * ZB thread context. Real IRQ-driven radios (TLSR8258) leave it a no-op.
 */
__weak void zb_platform_radio_rx_poll(void)
{
}

static void zb_thread_fn(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	LOG_INF("Zigbee thread started");

	/*
	 * No k_yield() / k_sem_take(FOREVER) in this loop.  Under TLSR8258
	 * Zephyr, k_yield() through z_sched_yield() corrupts the ready-queue
	 * link pointers after the Transport-Key install (PC pins inside
	 * unready_thread on a store to [NULL+4]) and wedges the chip with
	 * reg_irq_en stuck at 0.  Busy-wait-only pacing leaves the ZB thread
	 * permanently runnable, lets the higher-priority system tick / RF IRQ
	 * preempt as normal, and stays out of the scheduler's spinlock path
	 * that triggers the corruption.  Net wall-clock is unchanged vs the
	 * yield+busy_wait combo; we just pay extra CPU cycles per poll loop
	 * during idle (~1 ms granularity is fine for this thread).
	 */
	while (1) {
		if (!zb_bootstrap_done) {
			zb_core_bootstrap_once();
			ev_timer_process();
			ev_poll_process();
			if (!zb_bootstrap_done) {
				k_busy_wait(1000);
				continue;
			}
		}

		ev_timer_process();
		ev_poll_process();
		zb_platform_radio_rx_poll();
#if defined(CONFIG_ZIGBEE_ROUTER) || defined(CONFIG_ZIGBEE_ED_LIBZIGBEE)
		/* The router and the libzigbee-based ED both pull in the
		 * libzigbee NWK / MAC primitive dispatcher via
		 * tl_zbNwkTaskProc(); drain the per-layer task queues on every
		 * tick so high→NWK, MAC→NWK, and NWK→MAC primitives (active
		 * scan requests, beacon-notify indications, association
		 * requests, NLDE confirms) are delivered to their handlers.
		 * The default (minimal) ED keeps its lightweight
		 * nwk_ed_minimal poll path and doesn't need this drain.
		 */
		tl_zbNwkTaskProc();
		tl_zbMacTaskProc();
#endif
		zb_process_deferred_persistent_rejoin();
		zb_process_deferred_commissioning();
		zb_requeue_commissioning_if_needed();
		zb_link_watchdog_tick();
		if (zb_commissioning_pending) {
			k_busy_wait(1000);
			continue;
		}

		if (k_sem_take(&zb_ev_sem, K_NO_WAIT) == 0) {
			continue;
		}

		k_busy_wait(1000);
	}
}

K_THREAD_DEFINE(zb_thread,
		CONFIG_ZIGBEE_STACK_SIZE,
		zb_thread_fn,
		NULL, NULL, NULL,
		CONFIG_ZIGBEE_THREAD_PRIO,
		0, 0);
