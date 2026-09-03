/* SPDX-License-Identifier: Apache-2.0 */
/*
 * rx-off End-Device poll glue for the experimental CONFIG_ZIGBEE_ED
 * build (ED on the full libzigbee stack).
 *
 * An rx-off ED must poll its parent (MAC DATA_REQ) to fetch frames the parent /
 * trust center holds for it (most importantly the Transport-Key the TC defers
 * right after association, then the interview requests). In the libzigbee stack
 * that polling is ALREADY driven natively:
 *   - zdo_set_pollRate(500) is armed right after association
 *     (zdo_nwk_manager.c), so pollRateCb -> zdo_syncReq polls the parent every
 *     500 ms while rx-off; and
 *   - nwk_data.c issues an immediate quick-poll whenever a MAC ACK reports
 *     frame-pending, so queued frames are drained back-to-back.
 * That native machinery is self-paced (one poll per timer tick / per pending
 * frame). A second, independent poll driver here would issue polls faster than
 * native_sim confirms them; each in-flight poll holds a zb_buf, so the pool
 * (ZB_BUF_POOL_NUM=18) saturates within seconds and aps_ack_send() then fails
 * to allocate an APS-ACK buffer for the interview requests — stalling the
 * interview. The Zephyr ED adapter therefore leaves polling to the native
 * libzigbee state machine and only supplies ED-specific lifecycle hooks.
 *
 * The functions below are the ED-specific parts of the port.  The regular
 * join, rejoin and poll state machines remain in the vendor-derived NWK/ZDO
 * files; this file only owns the Zephyr lifecycle/bootstrap hooks that have
 * no vendor translation unit of their own.
 */
#include "zb_common_stub.h"
#include "zb_local.h"

extern void zdo_nlme_leave_confirm_cb(void *arg);

void tl_zbNwkNlmeLeaveRequestHandler(void *arg)
{
	nlme_leave_cnf_t *cnf = (nlme_leave_cnf_t *)arg;

	memset(cnf, 0, sizeof(*cnf));
	memcpy(cnf->deviceAddr, g_zbInfo.macPib.extAddress, EXT_ADDR_LEN);
	cnf->status = NWK_STATUS_SUCCESS;
	g_zbNwkCtx.joined = 0U;
	g_zbNwkCtx.is_factory_new = 1U;
	tl_zbTaskPost(zdo_nlme_leave_confirm_cb, arg);
}

void zb_ed_operation_abort(void)
{
	zdo_nwkDiscoveryStop();
	zdo_nwkRejoinWithBackOffStop();
	if (zdo_nwk_mngr()->savedBuf != NULL) {
		zb_buf_free((zb_buf_t *)zdo_nwk_mngr()->savedBuf);
		zdo_nwk_mngr()->savedBuf = NULL;
	}
	zdo_nwk_mngr()->state = 0U; /* ZDO_NWK_MGR_STATE_IDLE */
	g_zbNwkCtx.user_state = NLME_IDLE;
}

void zb_ed_runtime_reset(void)
{
	zb_ed_operation_abort();
	g_zbNwkCtx.joined = 0U;
	g_zbNwkCtx.is_factory_new = 1U;
	g_zbNwkCtx.parentIsChanged = 0U;
	g_zbNwkCtx.user_state = NLME_IDLE;
	aps_ib.aps_authenticated = FALSE;
	aps_ib.aps_use_insecure_join = TRUE;
}

void zb_ed_fixed_join_target(u8 channel, u16 pan_id, u16 short_addr,
				     const u8 *ext_pan_id, const u8 *nwk_key,
				     const u8 *tc_addr)
{
	tl_zbMacChannelSet(channel);
	g_zbInfo.macPib.panId = pan_id;
	g_zbInfo.nwkNib.panId = pan_id;
	g_zbInfo.macPib.shortAddress = short_addr;
	g_zbInfo.nwkNib.nwkAddr = short_addr;
	if (ext_pan_id != NULL) {
		memcpy(aps_ib.aps_use_ext_panid, ext_pan_id, EXT_ADDR_LEN);
		memcpy(g_zbInfo.nwkNib.extPANId, ext_pan_id, EXT_ADDR_LEN);
	}
	if (tc_addr != NULL) {
		memcpy(g_zbInfo.macPib.coordExtAddress, tc_addr, EXT_ADDR_LEN);
		memcpy(ss_ib.trust_center_address, tc_addr, EXT_ADDR_LEN);
	}
	if (nwk_key != NULL) {
		zb_preConfigNwkKey((u8 *)nwk_key, FALSE);
	}
}
