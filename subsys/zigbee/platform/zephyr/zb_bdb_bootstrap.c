/* SPDX-License-Identifier: Apache-2.0 */

#include "zb_common_stub.h"

#include <errno.h>
#include <zephyr/zigbee/zb_bootstrap.h>

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
	if (zb_platform_bdb_init_default() != 0) {
		return 0xFFU;
	}

	return bdb_networkSteerStart();
#endif
}
