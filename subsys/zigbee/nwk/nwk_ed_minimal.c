/* SPDX-License-Identifier: Apache-2.0 */

#include "zb_common_stub.h"

typedef enum {
	NWK_ED_MINIMAL_STATE_IDLE = 0,
	NWK_ED_MINIMAL_STATE_DISCOVERY,
	NWK_ED_MINIMAL_STATE_JOINING,
	NWK_ED_MINIMAL_STATE_REJOIN,
} nwk_ed_minimal_state_t;

typedef struct {
	bool initialized;
	bool warmStart;
	nwk_ed_minimal_state_t state;
	u32 lastScanChannels;
	u8 lastScanDuration;
	u32 lastRejoinScanChannels;
	u8 lastRejoinScanDuration;
	bool rejoinWithBackoff;
	bool parentCandidateValid;
	u16 parentCandidateShortAddr;
	addrExt_t parentCandidateIeee;
	u8 lastJoinStatus;
	bool fixedJoinValid;
	u8 fixedJoinChannel;
	u16 fixedJoinPanId;
	u16 fixedJoinShortAddr;
	extPANId_t fixedJoinExtPanId;
	u8 fixedJoinNwkKey[SEC_KEY_LEN];
	addrExt_t fixedJoinTcAddr;
} nwk_ed_minimal_ctx_t;

static nwk_ed_minimal_ctx_t g_nwkEdCtx;

static void nwk_ed_minimal_runtime_reset(void)
{
	g_nwkEdCtx.state = NWK_ED_MINIMAL_STATE_IDLE;
	g_nwkEdCtx.lastScanChannels = 0U;
	g_nwkEdCtx.lastScanDuration = 0U;
	g_nwkEdCtx.lastRejoinScanChannels = 0U;
	g_nwkEdCtx.lastRejoinScanDuration = 0U;
	g_nwkEdCtx.rejoinWithBackoff = FALSE;
	g_nwkEdCtx.parentCandidateValid = FALSE;
	g_nwkEdCtx.parentCandidateShortAddr = MAC_SHORT_ADDR_NONE;
	ZB_IEEE_ADDR_ZERO(g_nwkEdCtx.parentCandidateIeee);
	g_nwkEdCtx.lastJoinStatus = ZDO_NOT_SUPPORTED;
	g_nwkEdCtx.fixedJoinValid = FALSE;
	g_nwkEdCtx.fixedJoinChannel = 0xFFU;
	g_nwkEdCtx.fixedJoinPanId = MAC_INVALID_PANID;
	g_nwkEdCtx.fixedJoinShortAddr = MAC_SHORT_ADDR_NONE;
	memset(g_nwkEdCtx.fixedJoinExtPanId, 0, sizeof(g_nwkEdCtx.fixedJoinExtPanId));
	memset(g_nwkEdCtx.fixedJoinNwkKey, 0, sizeof(g_nwkEdCtx.fixedJoinNwkKey));
	ZB_IEEE_ADDR_ZERO(g_nwkEdCtx.fixedJoinTcAddr);
}

static void nwk_ed_minimal_reset(bool warmStart)
{
	nwk_ed_minimal_runtime_reset();
	g_nwkEdCtx.initialized = TRUE;
	g_nwkEdCtx.warmStart = warmStart;
}

void tl_zbNwkEdMinimalRuntimeReset(void)
{
	nwk_ed_minimal_runtime_reset();
}

bool tl_zbNwkEdMinimalDiscoveryStart(u32 scanChannels, u8 scanDuration)
{
	if (g_nwkEdCtx.state != NWK_ED_MINIMAL_STATE_IDLE) {
		return FALSE;
	}

	g_nwkEdCtx.state = NWK_ED_MINIMAL_STATE_DISCOVERY;
	g_nwkEdCtx.lastScanChannels = scanChannels;
	g_nwkEdCtx.lastScanDuration = scanDuration;
	g_nwkEdCtx.parentCandidateValid = FALSE;
	return TRUE;
}

void tl_zbNwkEdMinimalDiscoveryStop(void)
{
	if (g_nwkEdCtx.state == NWK_ED_MINIMAL_STATE_DISCOVERY) {
		g_nwkEdCtx.state = NWK_ED_MINIMAL_STATE_IDLE;
	}
}

bool tl_zbNwkEdMinimalAssocJoinStart(void)
{
	if (g_nwkEdCtx.state != NWK_ED_MINIMAL_STATE_IDLE) {
		return FALSE;
	}

	g_nwkEdCtx.state = NWK_ED_MINIMAL_STATE_JOINING;
	return TRUE;
}

bool tl_zbNwkEdMinimalRejoinStart(u32 scanChannels, u8 scanDuration, bool withBackoff)
{
	if (g_nwkEdCtx.state != NWK_ED_MINIMAL_STATE_IDLE) {
		return FALSE;
	}

	g_nwkEdCtx.state = NWK_ED_MINIMAL_STATE_REJOIN;
	g_nwkEdCtx.lastRejoinScanChannels = scanChannels;
	g_nwkEdCtx.lastRejoinScanDuration = scanDuration;
	g_nwkEdCtx.rejoinWithBackoff = withBackoff;
	return TRUE;
}

void tl_zbNwkEdMinimalOperationAbort(void)
{
	g_nwkEdCtx.state = NWK_ED_MINIMAL_STATE_IDLE;
	g_nwkEdCtx.rejoinWithBackoff = FALSE;
}

void tl_zbNwkEdMinimalOperationComplete(u8 status)
{
	g_nwkEdCtx.lastJoinStatus = status;
	g_nwkEdCtx.state = NWK_ED_MINIMAL_STATE_IDLE;
	g_nwkEdCtx.rejoinWithBackoff = FALSE;
}

bool tl_zbNwkEdMinimalManagerIdle(void)
{
	return g_nwkEdCtx.state == NWK_ED_MINIMAL_STATE_IDLE;
}

u32 tl_zbNwkEdMinimalLastScanChannelsGet(void)
{
	return g_nwkEdCtx.lastScanChannels;
}

u32 tl_zbNwkEdMinimalLastRejoinScanChannelsGet(void)
{
	return g_nwkEdCtx.lastRejoinScanChannels;
}

void tl_zbNwkEdMinimalParentCandidateSet(u16 parentShortAddr, const addrExt_t parentIeeeAddr)
{
	g_nwkEdCtx.parentCandidateValid = TRUE;
	g_nwkEdCtx.parentCandidateShortAddr = parentShortAddr;
	if (parentIeeeAddr != NULL) {
		ZB_IEEE_ADDR_COPY(g_nwkEdCtx.parentCandidateIeee, parentIeeeAddr);
	} else {
		ZB_IEEE_ADDR_ZERO(g_nwkEdCtx.parentCandidateIeee);
	}
}

void tl_zbNwkEdMinimalParentCandidateClear(void)
{
	g_nwkEdCtx.parentCandidateValid = FALSE;
	g_nwkEdCtx.parentCandidateShortAddr = MAC_SHORT_ADDR_NONE;
	ZB_IEEE_ADDR_ZERO(g_nwkEdCtx.parentCandidateIeee);
}

bool tl_zbNwkEdMinimalParentCandidateGet(u16 *parentShortAddr, addrExt_t parentIeeeAddr)
{
	if (!g_nwkEdCtx.parentCandidateValid) {
		return FALSE;
	}

	if (parentShortAddr != NULL) {
		*parentShortAddr = g_nwkEdCtx.parentCandidateShortAddr;
	}
	if (parentIeeeAddr != NULL) {
		ZB_IEEE_ADDR_COPY(parentIeeeAddr, g_nwkEdCtx.parentCandidateIeee);
	}

	return TRUE;
}

void tl_zbNwkEdMinimalSetFixedJoinTarget(u8 channel, u16 panId, u16 shortAddr,
					 const u8 *extPanId, const u8 *nwkKey, const u8 *tcAddr)
{
	g_nwkEdCtx.fixedJoinValid = TRUE;
	g_nwkEdCtx.fixedJoinChannel = channel;
	g_nwkEdCtx.fixedJoinPanId = panId;
	g_nwkEdCtx.fixedJoinShortAddr = shortAddr;
	if (extPanId != NULL) {
		ZB_EXTPANID_COPY(g_nwkEdCtx.fixedJoinExtPanId, extPanId);
	} else {
		memset(g_nwkEdCtx.fixedJoinExtPanId, 0, sizeof(g_nwkEdCtx.fixedJoinExtPanId));
	}
	if (nwkKey != NULL) {
		memcpy(g_nwkEdCtx.fixedJoinNwkKey, nwkKey, sizeof(g_nwkEdCtx.fixedJoinNwkKey));
	} else {
		memset(g_nwkEdCtx.fixedJoinNwkKey, 0, sizeof(g_nwkEdCtx.fixedJoinNwkKey));
	}
	if (tcAddr != NULL) {
		ZB_IEEE_ADDR_COPY(g_nwkEdCtx.fixedJoinTcAddr, tcAddr);
	} else {
		ZB_IEEE_ADDR_ZERO(g_nwkEdCtx.fixedJoinTcAddr);
	}
}

bool tl_zbNwkEdMinimalGetFixedJoinTarget(u8 *channel, u16 *panId, u16 *shortAddr,
					 extPANId_t extPanId, u8 *nwkKey, addrExt_t tcAddr)
{
	if (!g_nwkEdCtx.fixedJoinValid) {
		return FALSE;
	}

	if (channel != NULL) {
		*channel = g_nwkEdCtx.fixedJoinChannel;
	}
	if (panId != NULL) {
		*panId = g_nwkEdCtx.fixedJoinPanId;
	}
	if (shortAddr != NULL) {
		*shortAddr = g_nwkEdCtx.fixedJoinShortAddr;
	}
	if (extPanId != NULL) {
		ZB_EXTPANID_COPY(extPanId, g_nwkEdCtx.fixedJoinExtPanId);
	}
	if (nwkKey != NULL) {
		memcpy(nwkKey, g_nwkEdCtx.fixedJoinNwkKey, sizeof(g_nwkEdCtx.fixedJoinNwkKey));
	}
	if (tcAddr != NULL) {
		ZB_IEEE_ADDR_COPY(tcAddr, g_nwkEdCtx.fixedJoinTcAddr);
	}

	return TRUE;
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
