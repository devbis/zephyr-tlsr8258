/* SPDX-License-Identifier: Apache-2.0 */

#include "zb_common_stub.h"

typedef struct {
	bool initialized;
	bool warmStart;
} nwk_ed_minimal_ctx_t;

static nwk_ed_minimal_ctx_t g_nwkEdCtx;

static void nwk_ed_minimal_reset(bool warmStart)
{
	g_nwkEdCtx.initialized = TRUE;
	g_nwkEdCtx.warmStart = warmStart;
}

void tl_zbNwkInit(u8 coldReset)
{
	nwk_ed_minimal_reset(coldReset ? FALSE : TRUE);
}

void tl_zbNwkNlmeResetRequestHandler(void *arg)
{
	nlme_reset_req_t *pReq = (nlme_reset_req_t *)arg;
	nlme_reset_cnf_t cnf = {.status = NWK_STATUS_SUCCESS};

	nwk_ed_minimal_reset((pReq != NULL) ? pReq->warmStart : FALSE);

	if (zdoAppIndCbLst != NULL && zdoAppIndCbLst->zdpResetCnfCb != NULL) {
		zdoAppIndCbLst->zdpResetCnfCb(&cnf);
	}
}

void tl_zbNwkTaskProc(void)
{
}
