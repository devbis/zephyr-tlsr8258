/* SPDX-License-Identifier: Apache-2.0 */

#include "zb_common_stub.h"

#include <errno.h>
#include <string.h>
#include <zephyr/logging/log.h>
#include <zephyr/zigbee/zb_bootstrap.h>
#include <zephyr/zigbee/zb_radio_port.h>

#if defined(ZB_ROUTER_ROLE)
extern void zdo_router_join_latch_clear(void);
#endif

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

static bool zb_persist_has_live_network(const zb_info_t *info)
{
	if (info == NULL ||
	    info->macPib.panId == MAC_INVALID_PANID ||
	    info->macPib.shortAddress >= ZB_MAC_SHORT_ADDR_NOT_ALLOCATED ||
	    info->nwkNib.panId != info->macPib.panId ||
	    info->nwkNib.nwkAddr != info->macPib.shortAddress) {
		return false;
	}

	return true;
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
	/*
	 * joined is transient in the vendor NWK context. A stale failure/reset can
	 * persist a blob with joined==0 after MAC/NWK association has already
	 * committed a complete network tuple. Restoring that split state as a
	 * fresh router starts a new association, loses the parent pointer, and
	 * leaves the device ACK-only. Explicit leave clears the tuple and removes
	 * this blob, so a complete tuple is the safe restore criterion here.
	 */
	if (!g_zbNwkCtx.joined && zb_persist_has_live_network(&g_zbInfo)) {
		g_zbNwkCtx.joined = 1U;
	}
	zb_persist_normalize_nwk_ctx(&g_zbNwkCtx);
	/*
	 * Restore the radio filter from the same joined PIB that was just
	 * restored.  The radio may be initialised later, so the TLSR8258 port
	 * retains this request and applies it after its data object is cleared.
	 */
	zb_radio_port_update_filters(g_zbInfo.macPib.panId,
				     g_zbInfo.macPib.shortAddress,
				     g_zbInfo.macPib.extAddress);

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

	#if defined(ZB_ED_ROLE) && ZB_ED_ROLE
	tl_zbNwkEdMinimalRuntimeReset();
	#endif

	g_zbNwkCtx.joined = 0U;
	g_zbNwkCtx.is_factory_new = 1U;
	g_zbNwkCtx.parentIsChanged = 0U;
	g_zbNwkCtx.joined_pro = 0U;
	g_zbNwkCtx.joinAccept = 0U;
	g_zbNwkCtx.state = NLME_STATE_IDLE;
	g_zbNwkCtx.user_state = NLME_IDLE;
#if defined(ZB_ROUTER_ROLE)
	zdo_router_join_latch_clear();
#endif
	/* These fields are the router's in-RAM proof that secure joining
	 * completed. Clear them with the network so a stale callback cannot
	 * resurrect a network explicitly removed by the user. */
	g_bdbCtx.edRuntimeReady = 0U;
	g_bdbCtx.tcLinkKeyReady = 0U;
	g_bdbCtx.factoryNew = 1U;
	g_bdbCtx.state = BDB_STATE_IDLE;

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
	/* g_zbInfo is the compatibility snapshot consumed by the vendor-port
	 * MAC/NWK code.  Keep it in lockstep with the cleared PIB/NIB; otherwise
	 * a remove appears successful to Z2M but a later local path can still use
	 * the old PAN/short tuple and persist it again. */
	g_zbInfo.macPib.panId = MAC_INVALID_PANID;
	g_zbInfo.macPib.shortAddress = MAC_SHORT_ADDR_BROADCAST;
	g_zbInfo.macPib.coordShortAddress = MAC_SHORT_ADDR_NONE;
	g_zbInfo.macPib.associatedPanCoord = 0U;
	ZB_IEEE_ADDR_ZERO(g_zbInfo.macPib.coordExtAddress);
	g_zbInfo.nwkNib.panId = MAC_INVALID_PANID;
	g_zbInfo.nwkNib.nwkAddr = NWK_BROADCAST_RESERVED;
	g_zbInfo.nwkNib.depth = 0U;
	g_zbInfo.nwkNib.parentInfo = 0U;
	memset(g_zbInfo.nwkNib.extPANId, 0, sizeof(g_zbInfo.nwkNib.extPANId));

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
	nv_sts_t frame_counter_st;

	ARG_UNUSED(arg);

	zb_persist_register_item_length();
	memset(&blob, 0, sizeof(blob));
	blob.version = ZB_PERSIST_BLOB_VERSION;
	blob.zb_info = g_zbInfo;
	blob.nwk_ctx = g_zbNwkCtx;
	zb_persist_normalize_nwk_ctx(&blob.nwk_ctx);

	/*
	 * Keep the vendor security/runtime blob in sync with the joined-state blob.
	 * Several ED join paths call zb_info_save() directly, bypassing the vendor
	 * BDB persistence helper that would normally store SSIB and frame counters.
	 * Save those dependencies first so reboot never sees a joined blob without
	 * the security context required to restore it.
	 */
	frame_counter_st = nv_nwkFrameCountSaveToFlash(ss_ib.outgoingFrameCounter);
	if (frame_counter_st != NV_SUCC) {
		LOG_WRN("Zigbee frame counter save failed: %d", frame_counter_st);
	}
	zdo_ssInfoSaveToFlash();

	st = nv_flashWriteNew(1, NV_MODULE_ZB_INFO, NV_ITEM_ZB_INFO, sizeof(blob), (u8 *)&blob);
	if (st != NV_SUCC) {
		LOG_WRN("Zigbee persistence save failed: %d", st);
	}
}
