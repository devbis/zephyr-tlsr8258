/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Zephyr-native replacement for libzigbee second_clock.c.
 *
 * The vendor implementation advances g_secondCnt once per second and
 * fans out to a list of periodic callbacks
 * (apsDuplicatePeriodic / apsAckPeriodic / macIndirPeriodic /
 * nwkRouteDiscPeriodic / nwkRoutingTabPeriodic / nwkBrcPeriodic /
 * nwk_linkStPeriodic / nwkNebManagePeriodic / apsParentAnncePeriodic /
 * gpDataIndDuplicatePeriodic). It drives ageing of every router-side
 * table, broadcast expiry, link-status emission, etc.
 *
 * Zephyr port runs the same callbacks off a k_timer instead of the
 * vendor ev_timer / hardware-timer chain that depends on rf_init
 * having scheduled the 1 s tick. The k_timer is started from
 * secondClockRun() (the entrypoint the libzigbee zb_init() chain
 * still calls).
 */

#include <zephyr/kernel.h>

#include "zb_common_stub.h"
#include "os/ev_timer.h"

u32 g_secondCnt;

extern int apsDuplicatePeriodic(void *arg);
extern int apsAckPeriodic(void *arg);
#if defined(ZB_ROUTER_ROLE)
extern int macIndirPeriodic(void *arg);
extern int nwkRouteDiscPeriodic(void *arg);
extern int nwkRoutingTabPeriodic(void *arg);
extern int nwkBrcPeriodic(void *arg);
extern int nwk_linkStPeriodic(void *arg);
extern int apsParentAnncePeriodic(void *arg);
/* nwkNebManagePeriodic lives in the deferred portion of nwk_neighbor.c
 * (NV-restore variant); provide a weak no-op so the second tick can
 * call it once the full port lands.
 */
__attribute__((weak)) void nwkNebManagePeriodic(void) {}
#endif

/*
 * Fan-out table mirrors libzigbee/src/second_clock.c::timeoutsCb.
 * nwkNebManagePeriodic and the GP duplicate-periodic callbacks are
 * left out: the former returns void and would need a cast (vendor
 * does the cast but the call is best invoked separately); the
 * latter belongs to the GP TU which isn't built for router.
 */
static const ev_timer_callback_t zb_second_periodic_cb[] = {
	apsDuplicatePeriodic,
	apsAckPeriodic,
#if defined(ZB_ROUTER_ROLE)
	macIndirPeriodic,
	nwkRouteDiscPeriodic,
	nwkRoutingTabPeriodic,
	nwkBrcPeriodic,
	nwk_linkStPeriodic,
	apsParentAnncePeriodic,
#endif
	NULL,
};

static void zb_second_tick(struct k_timer *timer)
{
	ARG_UNUSED(timer);

	if (g_secondCnt + 1U != 0U) {
		g_secondCnt++;
	}

	for (u8 i = 0; zb_second_periodic_cb[i] != NULL; i++) {
		(void)zb_second_periodic_cb[i](NULL);
	}

#if defined(ZB_ROUTER_ROLE)
	nwkNebManagePeriodic();
#endif
}

K_TIMER_DEFINE(zb_second_timer, zb_second_tick, NULL);

void secondClockInit(void)
{
	k_timer_start(&zb_second_timer, K_SECONDS(1), K_SECONDS(1));
}

void secondClockStop(void)
{
	k_timer_stop(&zb_second_timer);
}

void secondClockRun(void)
{
	/* Idempotent: k_timer_start with the same period is a no-op
	 * if already running, but be explicit to match the vendor's
	 * "start once" contract from zb_init().
	 */
	secondClockInit();
}
