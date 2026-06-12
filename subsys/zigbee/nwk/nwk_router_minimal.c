/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Zephyr-native router-mode NWK runtime — minimal "static formation" path.
 *
 * Companion to nwk_ed_minimal.c. Provides the entry point that BDB calls
 * when CONFIG_ZIGBEE_ROUTER=y. The current implementation performs a
 * static, single-shot formation: pick (or take) a fixed PAN ID,
 * extended PAN ID, NWK key and short address, push them into the MAC
 * PIB / NWK NIB / security IB, configure the radio filter chain, and
 * signal BDB success.
 *
 * This is intentionally NOT a full router runtime: it does not run a
 * MAC ED/active scan, does not transmit beacons, does not respond to
 * AssocRequest, and does not broadcast link-status. Those layers
 * require MAC MLME support (MLME-START, beacon TX path, indirect
 * pending tables) which is not wired in subsys/zigbee/mac/ yet.
 *
 * Reference (do not copy verbatim): vendor-derived libzigbee
 * src/nwk_formation.c, src/nwk_permit_joining.c, src/nwk_brc.c. The
 * symbol shape and the "set MAC PIB, then notify ZDO" sequence mirror
 * the vendor's nwk_formationStartCnfHandler() success branch.
 */

#include "zb_common_stub.h"
#include "mac/includes/tl_zb_mac.h"
#include "mac/includes/tl_zb_mac_pib.h"

#include <errno.h>
#include <string.h>
#include <zephyr/logging/log.h>
#include <zephyr/random/random.h>
#include <zephyr/zigbee/zb_bootstrap.h>
#include <zephyr/zigbee/zb_config.h>
#include <zephyr/zigbee/zb_radio_port.h>

LOG_MODULE_REGISTER(zigbee_nwk_router_minimal, CONFIG_ZIGBEE_LOG_LEVEL);

/* Defaults match the existing zigbee_shell fixed-target ED profile so a
 * locally-formed router and an externally-driven ED can share a PAN for
 * loopback testing on a single host.
 */
#define NWK_ROUTER_MINIMAL_DEFAULT_PAN_ID    0xc6c6U
#define NWK_ROUTER_MINIMAL_DEFAULT_NWK_ADDR  0x0000U  /* coordinator */
#define NWK_ROUTER_MINIMAL_DEFAULT_CHANNEL   DEFAULT_CHANNEL

extern zdo_appIndCb_t *zdoAppIndCbLst;

static bool nwk_router_minimal_started;

static void nwk_router_minimal_fill_nwk_key(uint8_t key[SEC_KEY_LEN])
{
	/* drv_u32Rand() is the vendor RNG hook wired through to
	 * Zephyr's entropy source via drv_rand_zephyr.c (or to the
	 * hardware RNG / xoshiro / timer source when one of the
	 * Kconfig-selected generators is enabled).
	 */
	for (uint8_t i = 0; i < SEC_KEY_LEN; i += 4U) {
		uint32_t w = drv_u32Rand();

		memcpy(&key[i], &w, sizeof(w));
	}
}

static void nwk_router_minimal_apply_pib(uint8_t channel, uint16_t pan_id,
					 uint16_t short_addr,
					 const uint8_t ext_pan_id[EXT_ADDR_LEN])
{
	g_zbMacPib.phyChannelCur = channel;
	g_zbMacPib.panId = pan_id;
	g_zbMacPib.shortAddress = short_addr;
	g_zbMacPib.coordShortAddress = short_addr;
	g_zbMacPib.rxOnWhenIdle = 1U;
	g_zbMacPib.associationPermit = 0U;

	g_zbNIB.panId = pan_id;
	g_zbNIB.nwkAddr = short_addr;
	memcpy(g_zbNIB.extPANId, ext_pan_id, EXT_ADDR_LEN);
	memcpy(g_zbNIB.ieeeAddr, g_zbMacPib.extAddress, EXT_ADDR_LEN);
	g_zbNIB.updateId = 0U;
}

static bool nwk_router_minimal_resolve_profile(uint8_t *channel,
					       uint16_t *pan_id,
					       uint16_t *short_addr,
					       uint8_t ext_pan_id[EXT_ADDR_LEN],
					       uint8_t nwk_key[SEC_KEY_LEN],
					       bool *key_provided)
{
	struct zb_platform_bdb_fixed_target target;
	const uint8_t zero_ext[EXT_ADDR_LEN] = {0};
	const uint8_t zero_key[SEC_KEY_LEN] = {0};

	*channel = NWK_ROUTER_MINIMAL_DEFAULT_CHANNEL;
	*pan_id = NWK_ROUTER_MINIMAL_DEFAULT_PAN_ID;
	*short_addr = NWK_ROUTER_MINIMAL_DEFAULT_NWK_ADDR;
	memcpy(ext_pan_id, g_zbMacPib.extAddress, EXT_ADDR_LEN);
	*key_provided = false;

	if (!zb_platform_app_get_fixed_join_target(&target)) {
		return false;
	}

	if (target.channel != 0U) {
		*channel = target.channel;
	}
	if (target.pan_id != 0U && target.pan_id != MAC_INVALID_PANID) {
		*pan_id = target.pan_id;
	}
	if (target.short_addr != 0U &&
	    target.short_addr != MAC_SHORT_ADDR_NONE) {
		*short_addr = target.short_addr;
	}
	if (memcmp(target.ext_pan_id, zero_ext, EXT_ADDR_LEN) != 0) {
		memcpy(ext_pan_id, target.ext_pan_id, EXT_ADDR_LEN);
	}
	if (memcmp(target.network_key, zero_key, SEC_KEY_LEN) != 0) {
		memcpy(nwk_key, target.network_key, SEC_KEY_LEN);
		*key_provided = true;
	}

	return true;
}

uint8_t zb_routerStart(void)
{
	zdo_start_device_confirm_t cnf;
	uint8_t ext_pan_id[EXT_ADDR_LEN];
	uint8_t nwk_key[SEC_KEY_LEN] = {0};
	uint8_t channel;
	uint16_t pan_id;
	uint16_t short_addr;
	bool key_provided;
	bool from_app;
	int rc;

	if (nwk_router_minimal_started) {
		LOG_DBG("zb_routerStart: already started, ignoring");
		return 0U;
	}

	from_app = nwk_router_minimal_resolve_profile(&channel, &pan_id,
						      &short_addr, ext_pan_id,
						      nwk_key, &key_provided);

	if (!key_provided) {
		nwk_router_minimal_fill_nwk_key(nwk_key);
	}

	nwk_router_minimal_apply_pib(channel, pan_id, short_addr, ext_pan_id);

	/* Store the network key into the security IB so the APS
	 * encrypt/decrypt path can find it. Slot 0 / seqNum 0 mirrors the
	 * vendor stack's freshly-formed-network convention.
	 */
	memcpy(ss_ib.nwkSecurMaterialSet[0].key, nwk_key, SEC_KEY_LEN);
	ss_ib.nwkSecurMaterialSet[0].keySeqNum = 0U;
	ss_ib.activeKeySeqNum = 0U;
	memset(nwk_key, 0, sizeof(nwk_key));

	rc = zb_radio_port_set_channel(channel);
	if (rc != 0) {
		LOG_ERR("zb_routerStart: set_channel failed (%d)", rc);
		return 1U;
	}
	zb_radio_port_update_filters(pan_id, short_addr, g_zbMacPib.extAddress);
	(void)zb_radio_port_set_trx_state(ZB_RADIO_PORT_TRX_RX, channel);

	g_zbNwkCtx.joined = 1U;
	g_zbNwkCtx.is_factory_new = 0U;
	g_zbNwkCtx.user_state = NLME_IDLE;

	nwk_router_minimal_started = true;

	LOG_INF("zb router formed (%s): pan 0x%04x ch %u short 0x%04x key %s",
		from_app ? "app-profile" : "default",
		pan_id, channel, short_addr,
		key_provided ? "from-app" : "generated");

	/* Hand the synthesized confirm to BDB so the application sees a
	 * successful commissioning event and registers its endpoint.
	 */
	memset(&cnf, 0, sizeof(cnf));
	cnf.status = 0; /* ZDO_SUCCESS */
	cnf.channel_num = channel;
	cnf.pan_id = pan_id;
	cnf.short_addr = short_addr;

	if (zdoAppIndCbLst != NULL && zdoAppIndCbLst->zdpStartDevCnfCb != NULL) {
		zdoAppIndCbLst->zdpStartDevCnfCb(&cnf);
	} else {
		LOG_WRN("zb router: no zdpStartDevCnfCb registered");
	}

	return 0U;
}
