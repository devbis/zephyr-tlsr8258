/*
 * Copyright 2026 libzigbee
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "zb_common.h"
#include "aps.h"
#include "aps_data.h"
#include "mac_indirect_data.h"
#include "zb_nwk_core.h"
#include "nwk_brc.h"
#include "nwk_route_disc.h"
#include "nwk_routing.h"
#include "zb_nwk_neighbor.h"
#include "second_clock.h"
#include "gp_internal.h"
#include "ss_apsSecurityME.h"
#include "zdp_services.h"

const ev_timer_callback_t timeoutsCb[] = {
	apsDuplicatePeriodic,
	apsAckPeriodic,
#if defined(ZB_COORDINATOR_ROLE)
	(ev_timer_callback_t)ss_tcKeyPairPeriodic,
#endif
#if defined(ZB_ROUTER_ROLE)
	macIndirPeriodic,
	nwkRouteDiscPeriodic,
	nwkRoutingTabPeriodic,
	nwkBrcPeriodic,
	nwk_linkStPeriodic,
	(ev_timer_callback_t)nwkNebManagePeriodic, /* Intentional vendor ABI/codegen match. */
	apsParentAnncePeriodic,
	gpDataIndDuplicatePeriodic,
#endif
	NULL,
};

static ev_timer_event_t secondTimer;
u32 g_secondCnt = 0;

int secondClockPeriodic(void *arg)
{
	(void)arg;

	if (g_secondCnt + 1U != 0U) {
		g_secondCnt++;
	}

	for (u8 i = 0U; timeoutsCb[i] != NULL; i++) {
		timeoutsCb[i](NULL);
	}

	return 0;
}

_attribute_no_inline_ void secondClockInit(void)
{
	secondTimer.cb = secondClockPeriodic;
	ev_on_timer(&secondTimer, 1000U);
}

void secondClockStop(void)
{
	ev_unon_timer(&secondTimer);
}

void secondClockRun(void)
{
	if (!ev_timer_exist(&secondTimer)) {
		secondClockInit();
	}
}
