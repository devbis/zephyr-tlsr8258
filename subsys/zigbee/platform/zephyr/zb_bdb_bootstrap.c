/* SPDX-License-Identifier: Apache-2.0 */

#include "zb_common_stub.h"

#include <errno.h>
#include <zephyr/logging/log.h>
#include <zephyr/zigbee/zb_bootstrap.h>

LOG_MODULE_DECLARE(zigbee, CONFIG_ZIGBEE_LOG_LEVEL);

#if defined(CONFIG_ZIGBEE_BDB)
extern void tl_zbNwkEdMinimalSetFixedJoinTarget(u8 channel, u16 panId, u16 shortAddr,
						 const u8 *extPanId, const u8 *nwkKey,
						 const u8 *tcAddr);

#define ZB_SHELL_HA_PROFILE_ID 0x0104U
#define ZB_SHELL_HA_DEVICE_ID  0x0000U
#define ZB_SHELL_ENDPOINT      0x01U

static bool zb_bdb_bootstrap_ready;

static void zb_shell_bdb_init_cb(u8 status, u8 joinedNetwork)
{
	ARG_UNUSED(status);
	ARG_UNUSED(joinedNetwork);
}

static void zb_shell_bdb_commissioning_cb(u8 status, void *arg)
{
	ARG_UNUSED(status);
	ARG_UNUSED(arg);
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

static af_simple_descriptor_t zb_shell_simple_desc = {
	.app_profile_id = ZB_SHELL_HA_PROFILE_ID,
	.app_dev_id = ZB_SHELL_HA_DEVICE_ID,
	.endpoint = ZB_SHELL_ENDPOINT,
	.app_dev_ver = 1U,
	.reserved = 0U,
	.app_in_cluster_count = 0U,
	.app_out_cluster_count = 0U,
	.app_in_cluster_lst = NULL,
	.app_out_cluster_lst = NULL,
};

static void zb_platform_bdb_apply_fixed_target(void)
{
	struct zb_platform_bdb_fixed_target target;
	const u8 *tc_addr = NULL;

	if (g_zbNwkCtx.joined) {
		return;
	}

	memset(&target, 0, sizeof(target));
	if (!zb_platform_app_get_fixed_join_target(&target)) {
		return;
	}

	if (target.channel < 11U || target.channel > 26U) {
		LOG_WRN("zb bdb fixed target ignored: invalid channel %u", target.channel);
		return;
	}

	if (target.tc_addr_valid) {
		tc_addr = target.tc_addr;
	}

	tl_zbNwkEdMinimalSetFixedJoinTarget(target.channel,
					    target.pan_id,
					    target.short_addr,
					    target.ext_pan_id,
					    target.network_key,
					    tc_addr);
	g_bdbAttrs.primaryChannelSet = ((u32)1U << target.channel);
	g_bdbAttrs.secondaryChannelSet = 0U;

	LOG_INF("zb bdb fixed target applied: ch=%u pan=0x%04x parent=0x%04x",
		target.channel, target.pan_id, target.short_addr);
}

static void zb_platform_bdb_apply_join_profile(void)
{
	struct zb_platform_bdb_join_profile profile;

	if (g_zbNwkCtx.joined) {
		return;
	}

	memset(&profile, 0, sizeof(profile));
	if (!zb_platform_app_get_join_profile(&profile)) {
		return;
	}

	if (profile.channel_mask != 0U) {
		g_bdbAttrs.primaryChannelSet = profile.channel_mask;
		g_bdbAttrs.secondaryChannelSet = 0U;
		LOG_INF("zb bdb join profile applied: channels=0x%08x",
			(unsigned int)profile.channel_mask);
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
		return 0;
	}

	(void)zb_platform_restore_persistent_state();
	(void)zdo_ssInfoInit();
	if (nv_nwkFrameCountFromFlash(&frameCounter) == NV_SUCC) {
		ss_ib.outgoingFrameCounter = frameCounter;
	}

	if (g_zbNwkCtx.joined &&
	    g_zbMacPib.panId != MAC_INVALID_PANID &&
	    g_zbMacPib.coordShortAddress != MAC_SHORT_ADDR_NONE) {
		u8 *nwkKey = NULL;

		if (ss_ib.activeSecureMaterialIndex < SECUR_N_SECUR_MATERIAL) {
			nwkKey = ss_ib.nwkSecurMaterialSet[ss_ib.activeSecureMaterialIndex].key;
		}
		tl_zbNwkEdMinimalSetFixedJoinTarget(g_zbMacPib.phyChannelCur,
						    g_zbMacPib.panId,
						    g_zbMacPib.coordShortAddress,
						    g_zbNIB.extPANId,
						    nwkKey,
						    ss_ib.trust_center_address);
	}

	tl_bdbAttrInit();
	memset(&g_bdbCtx, 0, sizeof(g_bdbCtx));
	g_bdbCtx.bdbAppCb = &zb_shell_bdb_cb;
	g_bdbCtx.simpleDesc = &zb_shell_simple_desc;
	g_bdbCtx.factoryNew = g_zbNwkCtx.is_factory_new ? 1U : 0U;

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
