/* SPDX-License-Identifier: Apache-2.0 */

#include "zb_common_stub.h"

#include <errno.h>
#include <zephyr/logging/log.h>
#include <zephyr/zigbee/zb_bootstrap.h>
#include <zephyr/zigbee/zb_radio_port.h>

LOG_MODULE_DECLARE(zigbee, CONFIG_ZIGBEE_LOG_LEVEL);
extern __attribute__((weak)) volatile uint32_t zb_restore_diag_trace[16];
extern void app_bdb_rejoin_callback_trace_put(uint32_t tag);

#if ZB_ED_ROLE
#define ZB_PLATFORM_BDB_ED_RESTORE 1
#else
#define ZB_PLATFORM_BDB_ED_RESTORE 0
#endif

#if defined(CONFIG_ZIGBEE_BDB)
#if ZB_PLATFORM_BDB_ED_RESTORE
extern void tl_zbNwkEdMinimalSetFixedJoinTarget(u8 channel, u16 panId, u16 shortAddr,
						 const u8 *extPanId, const u8 *nwkKey,
						 const u8 *tcAddr);
extern void tl_zbNwkEdMinimalOperationAbort(void);
#endif
extern void bdb_outgoingFrameCountUpdate(u8 repower);
extern void bdb_zdoAssocDone(zdo_start_device_confirm_t *startDevCnf);
extern void bdb_zdoStartDevCnf(zdo_start_device_confirm_t *startDevCnf);

#define ZB_SHELL_HA_PROFILE_ID 0x0104U
#define ZB_SHELL_HA_DEVICE_ID  0x0000U
#define ZB_SHELL_ENDPOINT      0x01U
#define ZB_SHELL_CLUSTER_BASIC 0x0000U
#define ZB_SHELL_CLUSTER_IDENTIFY 0x0003U

static bool zb_bdb_bootstrap_ready;
static bool zb_bdb_restore_joined_target_pending;
static const u16 zb_shell_in_clusters[] = {
	ZB_SHELL_CLUSTER_BASIC,
	ZB_SHELL_CLUSTER_IDENTIFY,
};

void __weak zb_platform_app_bdb_commissioning_status(uint8_t status, bool joinedNetwork)
{
	ARG_UNUSED(status);
	ARG_UNUSED(joinedNetwork);
}

static void zb_shell_bdb_init_cb(u8 status, u8 joinedNetwork)
{
	ARG_UNUSED(status);
	ARG_UNUSED(joinedNetwork);
}

static void zb_shell_bdb_commissioning_cb(u8 status, void *arg)
{
	ARG_UNUSED(arg);
	app_bdb_rejoin_callback_trace_put((0x21U << 24) |
					  (uint32_t)status |
					  ((uint32_t)(g_zbNwkCtx.joined ? 1U : 0U) << 8));
	zb_platform_app_bdb_commissioning_status((uint8_t)status,
						 g_zbNwkCtx.joined ? true : false);
}

static void zb_shell_bdb_identify_cb(u8 endpoint, u16 srcAddr, u16 identifyTime)
{
	ARG_UNUSED(endpoint);
	ARG_UNUSED(srcAddr);
	ARG_UNUSED(identifyTime);
}

static void zb_shell_bdb_find_bind_cb(findBindDst_t *dst)
{
	ARG_UNUSED(dst);
}

static bdb_appCb_t zb_shell_bdb_cb = {
	.bdbInitCb = zb_shell_bdb_init_cb,
	.bdbcommissioningCb = zb_shell_bdb_commissioning_cb,
	.bdbIdentifyCb = zb_shell_bdb_identify_cb,
	.bdbFindBindSuccessCb = zb_shell_bdb_find_bind_cb,
};

static zdo_appIndCb_t zb_shell_zdo_cb = {
	.zdpAssocDoneCb = bdb_zdoAssocDone,
	.zdpStartDevCnfCb = bdb_zdoStartDevCnf,
};

static af_simple_descriptor_t zb_shell_simple_desc = {
	.app_profile_id = ZB_SHELL_HA_PROFILE_ID,
	.app_dev_id = ZB_SHELL_HA_DEVICE_ID,
	.endpoint = ZB_SHELL_ENDPOINT,
	.app_dev_ver = 1U,
	.reserved = 0U,
	.app_in_cluster_count = 2U,
	.app_out_cluster_count = 0U,
	.app_in_cluster_lst = (u16 *)zb_shell_in_clusters,
	.app_out_cluster_lst = NULL,
};

static struct zb_platform_bdb_fixed_target zb_bootstrap_target;
static struct zb_platform_bdb_join_profile zb_bootstrap_profile;

#if ZB_PLATFORM_BDB_ED_RESTORE
static bool zb_platform_bdb_key_is_set(const u8 *key)
{
	if (key == NULL) {
		return false;
	}

	for (u8 i = 0U; i < SEC_KEY_LEN; i++) {
		if (key[i] != 0U) {
			return true;
		}
	}

	return false;
}

static u8 *zb_platform_bdb_active_nwk_key_get(void)
{
	if (ss_ib.activeSecureMaterialIndex >= SECUR_N_SECUR_MATERIAL) {
		return NULL;
	}

	return ss_ib.nwkSecurMaterialSet[ss_ib.activeSecureMaterialIndex].key;
}

static bool zb_platform_bdb_has_valid_join_context(void)
{
	return g_zbMacPib.panId != MAC_INVALID_PANID &&
	       g_zbMacPib.shortAddress < ZB_MAC_SHORT_ADDR_NOT_ALLOCATED &&
	       g_zbMacPib.coordShortAddress < ZB_MAC_SHORT_ADDR_NOT_ALLOCATED &&
	       g_zbNIB.panId == g_zbMacPib.panId &&
	       g_zbNIB.nwkAddr == g_zbMacPib.shortAddress &&
	       zb_platform_bdb_key_is_set(zb_platform_bdb_active_nwk_key_get());
}

static void zb_platform_bdb_trace_join_context(uint8_t slot)
{
	bool has_valid = zb_platform_bdb_has_valid_join_context();
	bool key_set = zb_platform_bdb_key_is_set(zb_platform_bdb_active_nwk_key_get());

	if (&zb_restore_diag_trace[0] == NULL || slot >= 16U) {
		return;
	}

	zb_restore_diag_trace[slot] = ((uint32_t)g_zbNwkCtx.joined) |
				      ((uint32_t)g_zbNwkCtx.is_factory_new << 8) |
				      ((uint32_t)has_valid << 16) |
				      ((uint32_t)key_set << 24);
	if ((slot + 1U) < 16U) {
		zb_restore_diag_trace[slot + 1U] = ((uint32_t)g_zbMacPib.shortAddress << 16) |
						   (uint32_t)g_zbMacPib.panId;
	}
}

static void zb_platform_bdb_repair_joined_flag_if_needed(void)
{
	zb_platform_bdb_trace_join_context(9U);
	if (g_zbNwkCtx.joined || !zb_platform_bdb_has_valid_join_context()) {
		return;
	}

	LOG_WRN("zb bdb restore: repairing split joined flag for short 0x%04x pan 0x%04x",
		g_zbMacPib.shortAddress, g_zbMacPib.panId);
	g_zbNwkCtx.joined = 1U;
	g_zbNwkCtx.is_factory_new = 0U;
	g_zbNwkCtx.parentIsChanged = 0U;
	g_zbNwkCtx.state = NLME_STATE_IDLE;
	g_zbNwkCtx.user_state = NLME_IDLE;
	g_bdbAttrs.nodeIsOnANetwork = 1U;
	zb_platform_bdb_trace_join_context(11U);
}

static void zb_platform_bdb_drop_stale_joined_state_if_needed(void)
{
	int rc;

	zb_platform_bdb_trace_join_context(13U);
	if (!g_zbNwkCtx.joined || zb_platform_bdb_has_valid_join_context()) {
		return;
	}

	LOG_WRN("zb bdb restore: dropping stale joined context short=0x%04x pan=0x%04x key_idx=%u",
		g_zbMacPib.shortAddress, g_zbMacPib.panId, ss_ib.activeSecureMaterialIndex);
	rc = zb_platform_clear_persistent_state();
	if (rc != 0) {
		LOG_WRN("zb bdb restore: persistence clear failed rc=%d", rc);
	}
}

static bool zb_platform_bdb_restore_joined_target(void)
{
	u8 *nwkKey = NULL;
	int rc;

	zb_platform_bdb_repair_joined_flag_if_needed();
	if (!g_zbNwkCtx.joined ||
	    g_zbMacPib.panId == MAC_INVALID_PANID ||
	    g_zbMacPib.coordShortAddress == MAC_SHORT_ADDR_NONE ||
	    !zb_platform_bdb_has_valid_join_context()) {
		return false;
	}

	if (ss_ib.activeSecureMaterialIndex < SECUR_N_SECUR_MATERIAL) {
		nwkKey = ss_ib.nwkSecurMaterialSet[ss_ib.activeSecureMaterialIndex].key;
	}

	tl_zbNwkEdMinimalSetFixedJoinTarget(g_zbMacPib.phyChannelCur,
					    g_zbMacPib.panId,
					    g_zbMacPib.coordShortAddress,
					    g_zbNIB.extPANId,
					    nwkKey,
					    ss_ib.trust_center_address);
	zb_radio_port_update_filters(g_zbMacPib.panId, g_zbMacPib.shortAddress,
				     g_zbMacPib.extAddress);
	rc = zb_platform_radio_start_on_channel(g_zbMacPib.phyChannelCur);
	if (rc != 0) {
		LOG_WRN("zb bdb restore: radio start failed on channel %u rc=%d",
			g_zbMacPib.phyChannelCur, rc);
		return false;
	}

	bdb_outgoingFrameCountUpdate(1U);
	zb_rejoinSecModeSet(REJOIN_SECURITY);
	if (zdo_nwkRejoinStart((u32)1U << g_zbMacPib.phyChannelCur,
			       zdo_cfg_attributes.config_nwk_scan_duration) != ZDO_SUCCESS) {
		LOG_WRN("zb bdb restore: secure rejoin start failed on channel %u",
			g_zbMacPib.phyChannelCur);
		return false;
	}

	/*
	 * Skip the synchronous zb_info_save() that the vendor pattern would do
	 * here. On TLSR8258 + Zephyr NVS, calling zb_info_save during the rejoin
	 * start hot path crashes the chip (RF IRQs race against the flash
	 * arch_irq_lock window; RTT log captures end with "rejoin request sent
	 * ..." and the chip resets ~1-3 s later). The Zephyr-side join chain
	 * already defers persistence via the 15 s timer scheduled from
	 * nwk_ed_minimal_complete_join() on a real interview completion, which
	 * is the only point where saving a joined blob actually matters across
	 * power cycles. Re-stamping the saved blob from the rejoin path was
	 * never needed for re-restoring state because the blob persists across
	 * the rejoin attempt unchanged.
	 */
	return true;
}
#endif

bool zb_platform_bdb_service_persistent_rejoin(void)
{
#if !defined(CONFIG_ZIGBEE_BDB)
	return false;
#elif !ZB_PLATFORM_BDB_ED_RESTORE
	return false;
#else
	if (!zb_bdb_restore_joined_target_pending) {
		return false;
	}

	if (!zdo_ifZdoNwkManagerIdle()) {
		return false;
	}

	if (!zb_platform_bdb_restore_joined_target()) {
		if (!g_zbNwkCtx.joined || !zb_platform_bdb_has_valid_join_context()) {
			zb_bdb_restore_joined_target_pending = false;
		}
		return false;
	}

	zb_bdb_restore_joined_target_pending = false;
	return true;
#endif
}

void zb_platform_bdb_abandon_persistent_rejoin(void)
{
#if !defined(CONFIG_ZIGBEE_BDB)
	return;
#elif !ZB_PLATFORM_BDB_ED_RESTORE
	return;
#else
	LOG_WRN("zb bdb restore: abandoning persistent rejoin, falling back to fresh commissioning");

	zb_bdb_restore_joined_target_pending = false;

	/*
	 * Tear down any in-flight NWK rejoin/discovery operation and ZDO
	 * back-off state so the manager returns to IDLE.
	 */
	zdo_nwkRejoinWithBackOffStop();
	tl_zbNwkEdMinimalOperationAbort();

	/*
	 * Wipe the persisted joined context: it cannot be trusted (we never
	 * actually finished the previous interview) and would otherwise cause
	 * the same stale rejoin attempt on every retry.  This also clears the
	 * stale fixed-target left over from the previous attempt.
	 */
	(void)zb_platform_clear_persistent_state();
	g_bdbAttrs.nodeIsOnANetwork = 0U;
	BDB_STATE_SET(BDB_STATE_IDLE);
	g_bdbAttrs.commissioningStatus = BDB_COMMISSION_STA_SUCCESS;
#endif
}

static void zb_platform_bdb_apply_fixed_target(void)
{
#if !ZB_PLATFORM_BDB_ED_RESTORE
	return;
#else
	const u8 *tc_addr = NULL;

	if (g_zbNwkCtx.joined) {
		return;
	}

	memset(&zb_bootstrap_target, 0, sizeof(zb_bootstrap_target));
	if (!zb_platform_app_get_fixed_join_target(&zb_bootstrap_target)) {
		return;
	}

	if (zb_bootstrap_target.channel < 11U || zb_bootstrap_target.channel > 26U) {
		return;
	}

	if (zb_bootstrap_target.tc_addr_valid) {
		tc_addr = zb_bootstrap_target.tc_addr;
	}

	tl_zbNwkEdMinimalSetFixedJoinTarget(zb_bootstrap_target.channel,
					    zb_bootstrap_target.pan_id,
					    zb_bootstrap_target.short_addr,
					    zb_bootstrap_target.ext_pan_id,
					    zb_bootstrap_target.network_key,
					    tc_addr);
	if (zb_platform_bdb_key_is_set(zb_bootstrap_target.network_key)) {
		zb_preConfigNwkKey(zb_bootstrap_target.network_key, FALSE);
	}
	g_bdbAttrs.primaryChannelSet = ((u32)1U << zb_bootstrap_target.channel);
	g_bdbAttrs.secondaryChannelSet = 0U;
#endif
}

static void zb_platform_bdb_apply_join_profile(void)
{
	if (g_zbNwkCtx.joined) {
		return;
	}

	memset(&zb_bootstrap_profile, 0, sizeof(zb_bootstrap_profile));
	if (!zb_platform_app_get_join_profile(&zb_bootstrap_profile)) {
		return;
	}

	if (zb_bootstrap_profile.channel_mask != 0U) {
		g_bdbAttrs.primaryChannelSet = zb_bootstrap_profile.channel_mask;
		g_bdbAttrs.secondaryChannelSet = 0U;
	}

	if (zb_bootstrap_profile.network_key_valid) {
		zb_preConfigNwkKey(zb_bootstrap_profile.network_key, FALSE);
	}

	if (zb_bootstrap_profile.tc_addr_valid) {
#if ZB_PLATFORM_BDB_ED_RESTORE
		ss_securityModeSet(SS_SEMODE_CENTRALIZED);
		ZB_IEEE_ADDR_COPY(ss_ib.trust_center_address, zb_bootstrap_profile.tc_addr);
#endif
	}
}

#endif

/*
 * Optional hook for the application to register endpoints and initialize
 * ZCL before BDB starts.  The zigbee_shell sample overrides this to call
 * app_profile_register(), giving real zcl_init / af_endpointRegister /
 * zcl_register wiring.  A no-op default allows the subsystem to compile
 * standalone without requiring the sample-layer symbols.
 */
void __weak zb_platform_app_register_endpoints(void)
{
}

int zb_platform_bdb_init_default(void)
{
#if !defined(CONFIG_ZIGBEE_BDB)
	return -ENOTSUP;
#else
	u32 frameCounter = 0U;
	af_simple_descriptor_t *registered_desc;

	if (zb_bdb_bootstrap_ready) {
		return 0;
	}

	(void)zdo_ssInfoInit();
	if (nv_nwkFrameCountFromFlash(&frameCounter) == NV_SUCC) {
		ss_ib.outgoingFrameCounter = frameCounter;
	}

#if ZB_PLATFORM_BDB_ED_RESTORE
	zb_platform_bdb_drop_stale_joined_state_if_needed();
	zb_platform_bdb_repair_joined_flag_if_needed();
	zb_bdb_restore_joined_target_pending =
		g_zbNwkCtx.joined && zb_platform_bdb_has_valid_join_context();
#else
	zb_bdb_restore_joined_target_pending = false;
#endif

	/* Let the application register its endpoint and initialize ZCL.
	 * This must run before BDB attribute init so g_bdbCtx.simpleDesc
	 * can point at the app-owned descriptor. */
	zb_platform_app_register_endpoints();

	tl_bdbAttrInit();
	memset(&g_bdbCtx, 0, sizeof(g_bdbCtx));
	g_bdbCtx.bdbAppCb = &zb_shell_bdb_cb;

	/* Prefer the descriptor the app just registered; fall back to the
	 * built-in shell descriptor if the app did not register anything. */
	registered_desc = af_simpleDescGet(zb_shell_simple_desc.endpoint);
	if (registered_desc != NULL) {
		g_bdbCtx.simpleDesc = registered_desc;
	} else {
		g_bdbCtx.simpleDesc = &zb_shell_simple_desc;
		if (!af_endpointRegister(zb_shell_simple_desc.endpoint,
					 &zb_shell_simple_desc, NULL, NULL)) {
			LOG_WRN("zb bdb init: endpoint %u register failed",
				zb_shell_simple_desc.endpoint);
		}
	}

	g_bdbCtx.factoryNew = g_zbNwkCtx.is_factory_new ? 1U : 0U;
	zdo_zdpCbTblRegister(&zb_shell_zdo_cb);

	g_bdbAttrs.nodeIsOnANetwork = g_zbNwkCtx.joined ? 1U : 0U;
	g_bdbAttrs.commissioningStatus = BDB_COMMISSION_STA_SUCCESS;
	zb_platform_bdb_apply_fixed_target();
	zb_platform_bdb_apply_join_profile();
	BDB_STATE_SET(BDB_STATE_IDLE);

	zb_bdb_bootstrap_ready = true;
	return 0;
#endif
}

uint8_t zb_platform_bdb_network_steer_start(void)
{
#if !defined(CONFIG_ZIGBEE_BDB)
	return 0xFFU;
#else
	u8 status;

	if (zb_platform_bdb_init_default() != 0) {
		LOG_ERR("zb bdb steer: init failed");
		return 0xFFU;
	}

#if defined(CONFIG_ZIGBEE_ROUTER)
	{
		uint8_t scan_channel = (g_zbMacPib.phyChannelCur != 0U)
			? g_zbMacPib.phyChannelCur
			: (uint8_t)CONFIG_ZIGBEE_CHANNEL;
		int rc = zb_platform_radio_start_on_channel(scan_channel);

		printk("zb bdb steer: radio start ch=%u rc=%d\n",
		       scan_channel, rc);
	}
#endif

	printk("zb bdb steer: calling bdb_networkSteerStart\n");
	status = bdb_networkSteerStart();
	printk("zb bdb steer: bdb_networkSteerStart status=%u\n", status);
	return status;
#endif
}
