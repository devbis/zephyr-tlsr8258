/* SPDX-License-Identifier: Apache-2.0 */

#include "zb_common_stub.h"

#include <errno.h>
#include <string.h>
#include <zephyr/logging/log.h>
#include <zephyr/zigbee/zb_bootstrap.h>
#include <zephyr/zigbee/zb_radio_port.h>

LOG_MODULE_REGISTER(zigbee_persist, CONFIG_ZIGBEE_LOG_LEVEL);

#define ZB_PERSIST_BLOB_VERSION 2U

extern void tl_zbNwkEdMinimalRuntimeReset(void);

typedef struct {
	u8 version;
	u8 reserved[3];
	zb_info_t zb_info;
	nwk_ctx_t nwk_ctx;
} zb_persist_blob_t;

static bool zb_persist_len_registered;

static void zb_persist_register_item_length(void)
{
	if (zb_persist_len_registered) {
		return;
	}

	(void)nv_itemLengthCheckAdd(NV_ITEM_ZB_INFO, sizeof(zb_persist_blob_t));
	zb_persist_len_registered = true;
}

static void zb_persist_normalize_nwk_ctx(nwk_ctx_t *ctx)
{
	if (ctx == NULL) {
		return;
	}

	/*
	 * The vendor stack restores joined runtime from persisted MAC/NWK/security
	 * state, not from transient NWK state-machine flags. Avoid reviving stale
	 * join sub-states after reboot.
	 */
	ctx->parentIsChanged = 0U;
	ctx->joined_pro = 0U;
	ctx->joinAccept = 0U;
	ctx->state = NLME_STATE_IDLE;
	ctx->user_state = NLME_IDLE;
	ctx->is_factory_new = ctx->joined ? 0U : 1U;
}

int zb_platform_restore_persistent_state(void)
{
	zb_persist_blob_t blob;
	nv_sts_t st;

	zb_persist_register_item_length();
	memset(&blob, 0, sizeof(blob));

	st = nv_flashReadNew(1, NV_MODULE_ZB_INFO, NV_ITEM_ZB_INFO, sizeof(blob), (u8 *)&blob);
	if (st != NV_SUCC) {
		return -ENOENT;
	}
	if (blob.version != ZB_PERSIST_BLOB_VERSION) {
		return -EINVAL;
	}

	g_zbInfo = blob.zb_info;
	g_zbNwkCtx = blob.nwk_ctx;
	zb_persist_normalize_nwk_ctx(&g_zbNwkCtx);

	return 0;
}

int zb_platform_clear_persistent_state(void)
{
	nv_sts_t st;

	zb_persist_register_item_length();
	st = nv_flashSingleItemRemove(NV_MODULE_ZB_INFO, NV_ITEM_ZB_INFO,
				      sizeof(zb_persist_blob_t));
	if (st != NV_SUCC && st != NV_ITEM_NOT_FOUND && st != NV_NO_MEDIA) {
		LOG_WRN("Zigbee persistence clear failed: %d", st);
	}

	tl_zbNwkEdMinimalRuntimeReset();

	g_zbNwkCtx.joined = 0U;
	g_zbNwkCtx.is_factory_new = 1U;
	g_zbNwkCtx.parentIsChanged = 0U;
	g_zbNwkCtx.joined_pro = 0U;
	g_zbNwkCtx.joinAccept = 0U;
	g_zbNwkCtx.state = NLME_STATE_IDLE;
	g_zbNwkCtx.user_state = NLME_IDLE;

	g_zbMacPib.panId = MAC_INVALID_PANID;
	g_zbMacPib.shortAddress = MAC_SHORT_ADDR_BROADCAST;
	g_zbMacPib.coordShortAddress = MAC_SHORT_ADDR_NONE;
	g_zbMacPib.associatedPanCoord = 0U;
	ZB_IEEE_ADDR_ZERO(g_zbMacPib.coordExtAddress);

	g_zbNIB.panId = MAC_INVALID_PANID;
	g_zbNIB.nwkAddr = NWK_BROADCAST_RESERVED;
	g_zbNIB.depth = 0U;
	g_zbNIB.parentInfo = 0U;
	memset(g_zbNIB.extPANId, 0, sizeof(g_zbNIB.extPANId));

	memset(ss_ib.nwkSecurMaterialSet, 0, sizeof(ss_ib.nwkSecurMaterialSet));
	ss_ib.securityLevel = 0U;
	ss_ib.activeSecureMaterialIndex = 0U;
	ss_ib.activeKeySeqNum = 0U;
	ZB_IEEE_ADDR_INVALID(ss_ib.trust_center_address);
	aps_ib.aps_authenticated = 0U;
	aps_ib.aps_use_insecure_join = TRUE;
	g_bdbAttrs.nodeIsOnANetwork = 0U;
	g_bdbCtx.factoryNew = 1U;
	BDB_STATE_SET(BDB_STATE_IDLE);

	zb_radio_port_update_filters(MAC_INVALID_PANID, MAC_SHORT_ADDR_BROADCAST,
				     g_zbMacPib.extAddress);

	return (st == NV_SUCC || st == NV_ITEM_NOT_FOUND || st == NV_NO_MEDIA) ? 0 : -EIO;
}

void zb_info_save(void *arg)
{
	zb_persist_blob_t blob;
	nv_sts_t st;

	ARG_UNUSED(arg);

	zb_persist_register_item_length();
	memset(&blob, 0, sizeof(blob));
	blob.version = ZB_PERSIST_BLOB_VERSION;
	blob.zb_info = g_zbInfo;
	blob.nwk_ctx = g_zbNwkCtx;
	zb_persist_normalize_nwk_ctx(&blob.nwk_ctx);

	st = nv_flashWriteNew(1, NV_MODULE_ZB_INFO, NV_ITEM_ZB_INFO, sizeof(blob), (u8 *)&blob);
	if (st != NV_SUCC) {
		LOG_WRN("Zigbee persistence save failed: %d", st);
	}
}
