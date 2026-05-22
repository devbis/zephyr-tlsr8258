/* SPDX-License-Identifier: Apache-2.0 */

#include "zb_common_stub.h"

#include <errno.h>
#include <zephyr/logging/log.h>
#include <zephyr/zigbee/zb_bootstrap.h>
#include <zephyr/zigbee/zb_radio_port.h>

LOG_MODULE_DECLARE(zigbee, CONFIG_ZIGBEE_LOG_LEVEL);

#if defined(CONFIG_ZIGBEE_BDB)
extern void tl_zbNwkEdMinimalSetFixedJoinTarget(u8 channel, u16 panId, u16 shortAddr,
						 const u8 *extPanId, const u8 *nwkKey,
						 const u8 *tcAddr);
extern void tl_zbNwkEdMinimalPollRestart(u32 timeoutMs);
extern void tl_zbNwkEdMinimalPollEnsure(void);
extern void bdb_zdoStartDevCnf(zdo_start_device_confirm_t *startDevCnf);

#define ZB_SHELL_HA_PROFILE_ID 0x0104U
#define ZB_SHELL_HA_DEVICE_ID  0x0000U
#define ZB_SHELL_ENDPOINT      0x01U
#define ZB_SHELL_CLUSTER_BASIC 0x0000U
#define ZB_SHELL_CLUSTER_IDENTIFY 0x0003U

static bool zb_bdb_bootstrap_ready;
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

static void zb_platform_bdb_repair_joined_flag_if_needed(void)
{
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
}

static void zb_platform_bdb_drop_stale_joined_state_if_needed(void)
{
	int rc;

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

static void zb_platform_bdb_restore_joined_target(void)
{
	u8 *nwkKey = NULL;

	zb_platform_bdb_repair_joined_flag_if_needed();
	if (!g_zbNwkCtx.joined ||
	    g_zbMacPib.panId == MAC_INVALID_PANID ||
	    g_zbMacPib.coordShortAddress == MAC_SHORT_ADDR_NONE ||
	    !zb_platform_bdb_has_valid_join_context()) {
		return;
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
	tl_zbNwkEdMinimalPollRestart(zdo_af_get_syn_rate());
	zb_radio_port_update_filters(g_zbMacPib.panId, g_zbMacPib.shortAddress,
				     g_zbMacPib.extAddress);
	tl_zbNwkEdMinimalPollEnsure();
	zb_info_save(NULL);
}

static void zb_platform_bdb_apply_fixed_target(void)
{
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
		ss_securityModeSet(SS_SEMODE_CENTRALIZED);
		ZB_IEEE_ADDR_COPY(ss_ib.trust_center_address, zb_bootstrap_profile.tc_addr);
	}
}

#endif

int zb_platform_bdb_init_default(void)
{
#if !defined(CONFIG_ZIGBEE_BDB)
	return -ENOTSUP;
#else
	u32 frameCounter = 0U;

	if (zb_bdb_bootstrap_ready) {
		zb_platform_bdb_restore_joined_target();
		return 0;
	}

	(void)zdo_ssInfoInit();
	if (nv_nwkFrameCountFromFlash(&frameCounter) == NV_SUCC) {
		ss_ib.outgoingFrameCounter = frameCounter;
	}

	zb_platform_bdb_drop_stale_joined_state_if_needed();
	zb_platform_bdb_repair_joined_flag_if_needed();
	zb_platform_bdb_restore_joined_target();

	tl_bdbAttrInit();
	memset(&g_bdbCtx, 0, sizeof(g_bdbCtx));
	g_bdbCtx.bdbAppCb = &zb_shell_bdb_cb;
	g_bdbCtx.simpleDesc = &zb_shell_simple_desc;
	g_bdbCtx.factoryNew = g_zbNwkCtx.is_factory_new ? 1U : 0U;
	if (af_simpleDescGet(zb_shell_simple_desc.endpoint) == NULL &&
	    !af_endpointRegister(zb_shell_simple_desc.endpoint, &zb_shell_simple_desc, NULL, NULL)) {
		LOG_WRN("zb bdb init: endpoint %u register failed", zb_shell_simple_desc.endpoint);
	}
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

	LOG_INF("zb bdb steer: request");
	if (zb_platform_bdb_init_default() != 0) {
		LOG_ERR("zb bdb steer: init failed");
		return 0xFFU;
	}

	LOG_INF("zb bdb steer: start");
	status = bdb_networkSteerStart();
	LOG_INF("zb bdb steer start status=0x%02x", status);
	return status;
#endif
}
