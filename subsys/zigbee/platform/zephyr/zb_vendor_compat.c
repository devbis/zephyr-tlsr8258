/* SPDX-License-Identifier: Apache-2.0 */

#include "drv_hw.h"
#include "drv_security.h"
#include "zb_common_stub.h"

/*
 * Minimal vendor-compatible globals and bootstrap for the current ED-only
 * Zephyr port. The join/interview path uses the open-source ED runtime, but
 * it still expects Telink's config symbols and init ordering to exist.
 */

/* APS / NWK / MAC table sizes and arrays now live in
 * subsys/zigbee/common/zb_config.c (copied from
 * tl_zigbee_sdk/zigbee/common/zb_config.c). MAC_TX_QUEUE_SIZE /
 * g_txQueue stay here because zb_config.c gates them behind
 * ZB_ZEPHYR_TX_QUEUE_IN_VENDOR_COMPAT.
 */
u8 MAC_TX_QUEUE_SIZE = TX_QUEUE_BN;
tx_data_queue g_txQueue[TX_QUEUE_BN];

/* g_zbMacCtx is defined by the libzigbee-derived mac.c port when
 * present, else here as a weak fallback for the ED build.
 */
tl_zb_mac_ctx_t g_zbMacCtx __attribute__((weak));
extern volatile u32 zb_nwk_ed_trace[];

static u8 aps_counter_value;
static u8 aps_handle;

static const tl_zb_mac_pib_t zb_mac_pib_default = {
	.ackWaitDuration = (ZB_MAC_UNIT_BACKOFF_PERIOD + ZB_PHY_TURNROUNDTIME +
			    ZB_PHY_SHR_DURATION + (u16)(6U * ZB_PHY_SYMBOLS_PER_OCTET)),
	.frameRetryNum = ZB_MAC_FRAME_RETRIES_MAX,
	.transactionPersistenceTime = 0x01f4U,
	.panId = MAC_INVALID_PANID,
	.shortAddress = MAC_SHORT_ADDR_BROADCAST,
	.respWaitTime = ZB_MAC_WAIT_RESP_TIME_DEFAULT,
	.phyChannelCur = DEFAULT_CHANNEL,
	.autoReq = 0U,
#if ZB_ED_ROLE
	.minBe = 0U,
	.rxOnWhenIdle = 0U,
#else
	.minBe = 5U,
	.rxOnWhenIdle = 1U,
#endif
	.maxBe = 8U,
	.beaconOrder = 15U,
	.superframeOrder = 0U,
	.maxCsmaBackoffs = 4U,
	.associationPermit = 0U,
	.coordShortAddress = MAC_SHORT_ADDR_NONE,
};

static const nwk_nib_t zb_nwk_nib_default = {
	.addrAlloc = NWK_ADDRESS_ALLOC_METHOD_STOCHASTIC,
	.maxDepth = NWK_MAX_DEPTH,
	.stackProfile = 2U,
	.managerAddr = 0x0000U,
	.leaveReqAllowed = 1U,
	.useMulticast = 0U,
	.panId = MAC_INVALID_PANID,
	.nwkAddr = NWK_BROADCAST_RESERVED,
	.uniqueAddr = 0U,
	.parentInfo = 0U,
	.endDevTimeoutDefault = NWK_ENDDEV_TIMEOUT_DEFAULT,
	.leaveReqWithoutRejoinAllowed = 1U,
};

enum {
	ZB_VENDOR_AES_BLOCK_SIZE = 16,
};

static void zb_vendor_aes_block_xor(u8 *dst, const u8 *src)
{
	for (u8 i = 0U; i < ZB_VENDOR_AES_BLOCK_SIZE; i++) {
		dst[i] ^= src[i];
	}
}

void tl_cryHashFunction(u8 *data, u8 len, u8 *result)
{
	u8 block[ZB_VENDOR_AES_BLOCK_SIZE] = {0};
	u8 pos = 0U;
	u8 idx = 0U;

	memset(result, 0, ZB_VENDOR_AES_BLOCK_SIZE);

	while (idx < len) {
		block[pos++] = data[idx++];
		if (pos == ZB_VENDOR_AES_BLOCK_SIZE) {
			drv_aes_encrypt(result, block, result);
			zb_vendor_aes_block_xor(result, block);
			pos = 0U;
		}
	}

	block[pos++] = 0x80U;

	while (pos != (ZB_VENDOR_AES_BLOCK_SIZE - 2U)) {
		if (pos >= ZB_VENDOR_AES_BLOCK_SIZE) {
			drv_aes_encrypt(result, block, result);
			zb_vendor_aes_block_xor(result, block);
			memset(block, 0, sizeof(block));
			pos = 0U;
		}
		block[pos++] = 0U;
	}

	block[pos++] = (u8)(((u16)len << 3) >> 8);
	block[pos] = (u8)(((u16)len << 3) & 0xffU);

	drv_aes_encrypt(result, block, result);
	zb_vendor_aes_block_xor(result, block);
}

void ss_ttlMAC(u8 len, u8 *input, u8 *key, u8 *hashOut)
{
	u8 hash_in[2 * ZB_VENDOR_AES_BLOCK_SIZE];
	u8 tmp_buf[0x80] = {0};

	if (len > 0x70U) {
		ZB_EXCEPTION_POST(SYS_EXCEPTTION_COMMON_PARAM_ERROR);
		return;
	}

	for (u8 i = 0U; i < ZB_VENDOR_AES_BLOCK_SIZE; i++) {
		hash_in[i] = key[i] ^ 0x5cU;
		tmp_buf[i] = key[i] ^ 0x36U;
	}

	for (u8 i = 0U; i < len; i++) {
		tmp_buf[i + ZB_VENDOR_AES_BLOCK_SIZE] = input[i];
	}

	tl_cryHashFunction(tmp_buf, (u8)(ZB_VENDOR_AES_BLOCK_SIZE + len),
			   hash_in + ZB_VENDOR_AES_BLOCK_SIZE);
	tl_cryHashFunction(hash_in, 2U * ZB_VENDOR_AES_BLOCK_SIZE, hashOut);
}

u8 ss_keyHash(u8 *padV, u8 *key, u8 *hashOut)
{
	ss_ttlMAC(1U, padV, key, hashOut);
	return RET_OK;
}

void ss_mmoHash(u8 *data, u8 len, u8 *result)
{
	tl_cryHashFunction(data, len, result);
}

static void zb_binding_table_reset(void)
{
	/* Sized via APS_BINDING_TABLE_NUM rather than sizeof(table) because
	 * the array storage moved to common/zb_config.c (the array is
	 * declared extern aps_binding_entry_t g_apsBindingTbl[] in
	 * aps_api.h, which is an incomplete type at this TU).
	 */
	memset(g_apsBindingTbl, 0,
	       APS_BINDING_TABLE_NUM * sizeof(aps_binding_entry_t));
}

static void zb_tx_cache_reset(void)
{
	memset(aps_txCache_tbl, 0,
	       APS_TX_CACHE_TABLE_NUM * sizeof(aps_tx_cache_list_t));
}

static void zb_nwk_capability_apply_runtime_defaults(void)
{
#if ZB_ED_ROLE
	/*
	 * The minimal Zephyr port currently commissions only as a Zigbee end
	 * device.  Make the association capability explicit instead of relying on
	 * zero-initialized bitfields: the secure-join interview depends on the
	 * Security Capability bit being set.
	 */
	g_zbNIB.capabilityInfo.altPanCoord = 0U;
	g_zbNIB.capabilityInfo.devType = 0U;
	g_zbNIB.capabilityInfo.rcvOnWhenIdle = g_zbMacPib.rxOnWhenIdle ? 1U : 0U;
	g_zbNIB.capabilityInfo.secuCapability = 1U;
	g_zbNIB.capabilityInfo.allocAddr = 1U;
#endif
}

static void zb_mac_pib_apply_runtime_defaults(bool cold_reset)
{
	if (cold_reset) {
		g_zbMacPib = zb_mac_pib_default;
		g_zbNIB = zb_nwk_nib_default;
		g_zbMacPib.seqNum = 0x5aU;
		g_zbMacPib.beaconSeqNum = 0xa5U;
	}

	g_zbMacPib.associationPermit = 0U;
	g_zbMacPib.frameTotalWaitTime = 800U;
	zb_nwk_capability_apply_runtime_defaults();

	memset(&g_zbMacCtx, 0, sizeof(g_zbMacCtx));
	g_zbMacCtx.curChannel = g_zbMacPib.phyChannelCur;

	tl_zbMacChannelSet(g_zbMacPib.phyChannelCur);
}

#if !defined(ZB_ROUTER_ROLE) || !ZB_ROUTER_ROLE
/* Router build uses the libzigbee-derived mac.c::tl_zbMacInit which
 * resets every MAC primitive table. ED keeps the lightweight version.
 */
void tl_zbMacInit(u8 coldReset)
{
	zb_mac_pib_apply_runtime_defaults(coldReset ? true : false);
}
#endif

u8 aps_get_current_counter_value(void)
{
	return aps_counter_value;
}

u8 aps_get_counter_value(void)
{
	return aps_counter_value++;
}

void aps_init(void)
{
	u8 group_nv_status;

	zb_nwk_ed_trace[14] = 0xA6A00001U;
	aps_counter_value = 0x7aU;
	aps_handle = 0U;

	if (aps_ib.aps_channel_mask == 0U) {
		aps_ib.aps_channel_mask = (1UL << DEFAULT_CHANNEL);
	}

	aps_ib.aps_designated_coordinator = FALSE;
	aps_ib.aps_parent_announce_timer = 0U;
	aps_ib.aps_nonmember_radius = 2U;
	aps_ib.aps_interframe_delay = APS_INTERFRAME_DELAY;
	aps_ib.aps_max_window_size = APS_MAX_WINDOW_SIZE ? APS_MAX_WINDOW_SIZE : 1U;
	aps_ib.aps_fragment_payload_size = APS_FRAGMEMT_PAYLOAD_SIZE;
	aps_ib.aps_use_insecure_join = TRUE;
	aps_ib.aps_authenticated = FALSE;
	aps_ib.aps_updateDevice_holdApsSecurity = FALSE;
	aps_ib.aps_zdo_restricted_mode = FALSE;
	zb_nwk_ed_trace[14] = 0xA6A00002U;

	group_nv_status = aps_groupTblNvInit();
	zb_nwk_ed_trace[14] = 0xA6A00003U | group_nv_status;
	if (group_nv_status != NV_SUCC) {
		aps_groupTblReset();
		zb_nwk_ed_trace[14] = 0xA6A00004U | group_nv_status;
	} else {
		aps_init_group_num_set();
		zb_nwk_ed_trace[14] = 0xA6A00005U;
	}

	zb_binding_table_reset();
	zb_nwk_ed_trace[14] = 0xA6A00006U;
	zb_tx_cache_reset();
	zb_nwk_ed_trace[14] = 0xA6A00007U;
}
