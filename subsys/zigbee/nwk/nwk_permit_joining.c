/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Permit-join state machine for the router build.
 *
 * Adapted from libzigbee/src/nwk_permit_joining.c. The vendor file is
 * 52 LOC and the only non-trivial adaptation is the include layout:
 * we pull zb_common_stub.h + the nwk_internal.h forward declarations
 * directly instead of the vendor zb_local.h aggregator. The state
 * transitions (g_zbMacPib.associationPermit / g_zbNwkCtx.permit_join,
 * ev_timer_taskPost / Cancel pacing, tl_zbNwkBeaconPayloadUpdate after
 * each change) are kept intact so the behavior matches the vendor.
 */

#include "zb_common_stub.h"
#include "os/ev_timer.h"

#include <stdbool.h>

#if defined(ZB_ROUTER_ROLE) && ZB_ROUTER_ROLE

extern void zdo_nlmePermitJoinCnf(void *arg);
extern void tl_zbNwkBeaconPayloadUpdate(void);

ev_timer_event_t *permitJoinTimerEvt;

static int nwk_permitJoinTimeout(void *arg)
{
	ARG_UNUSED(arg);

	g_zbMacPib.associationPermit = 0;
	g_zbNwkCtx.permit_join = 0;
	permitJoinTimerEvt = NULL;

	return -1;
}

void tl_zbNwkNlmePermitJoiningRequestHandler(void *arg)
{
	nlme_permitJoining_req_t *req = (nlme_permitJoining_req_t *)arg;
	nlme_permitJoining_cnf_t *cnf = (nlme_permitJoining_cnf_t *)arg;

	if (!g_zbNIB.capabilityInfo.devType || !g_zbNwkCtx.joined ||
	    !g_zbNwkCtx.joinAccept) {
		cnf->status = NWK_STATUS_INVALID_REQUEST;
		tl_zbTaskPost(zdo_nlmePermitJoinCnf, arg);
		return;
	}

	if (permitJoinTimerEvt != NULL) {
		ev_timer_taskCancel(&permitJoinTimerEvt);
	}

	g_zbNwkCtx.permit_join = (req->permitDuration != 0U) ? 1U : 0U;

	if (req->permitDuration == 0U) {
		g_zbMacPib.associationPermit = 0;
	} else if (req->permitDuration != 0xffU) {
		g_zbMacPib.associationPermit = 1;
		permitJoinTimerEvt = ev_timer_taskPost(nwk_permitJoinTimeout, NULL,
						      (u32)req->permitDuration * 1000U);
	} else {
		g_zbMacPib.associationPermit = 1;
	}

	tl_zbNwkBeaconPayloadUpdate();
	cnf->status = NWK_STATUS_SUCCESS;
	tl_zbTaskPost(zdo_nlmePermitJoinCnf, arg);
}

#endif /* ZB_ROUTER_ROLE */
