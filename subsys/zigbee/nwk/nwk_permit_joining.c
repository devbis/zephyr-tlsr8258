/*
 * Copyright 2026 libzigbee
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "zb_common.h"
#include "ev_timer.h"
#include "nwk_data.h"
#include "nwk_permit_joining.h"
#include "zdo_nwk_manager.h"

#if defined(ZB_ROUTER_ROLE)
ev_timer_event_t *permitJoinTimerEvt = NULL;

int nwk_permitJoinTimeout(void *arg)
{
	(void)arg;

	g_zbMacPib.associationPermit = 0;
	g_zbNwkCtx.permit_join = 0;
	permitJoinTimerEvt = NULL;

	return -1;
}

void zb_NwkNlmePermitJoiningRequestHandler(void *arg)
{
	nlme_permitJoining_req_t *req = (nlme_permitJoining_req_t *)arg;
	nlme_permitJoining_cnf_t *cnf = (nlme_permitJoining_cnf_t *)arg;

	if (!g_zbNIB.capabilityInfo.devType || !g_zbNwkCtx.joined || !g_zbNwkCtx.joinAccept) {
		cnf->status = NWK_STATUS_INVALID_REQUEST;
		zb_TaskPost(zdo_nlmePermitJoinCnf, arg);
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
						       (uint32_t)req->permitDuration * 1000U);
	} else {
		g_zbMacPib.associationPermit = 1;
	}

	zb_NwkBeaconPayloadUpdate();
	cnf->status = NWK_STATUS_SUCCESS;
	zb_TaskPost(zdo_nlmePermitJoinCnf, arg);
}
#endif
