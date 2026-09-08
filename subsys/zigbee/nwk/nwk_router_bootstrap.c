/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Zephyr-native FFD bootstrap for the vendor-derived NWK runtime.
 *
 * Provides the FFD startup seam that BDB calls when
 * CONFIG_ZIGBEE_ROUTER=y or CONFIG_ZIGBEE_COORDINATOR=y. Profile and restored
 * state are applied here,
 * then startup is handed to the vendor-derived zdo_nwkRouterStart() /
 * MLME-START path. This keeps formation, beacon-request, association,
 * permit-join, link-status and indirect-data behavior in the ported
 * NWK/MAC sources instead of maintaining a second static runtime.
 *
 * Reference (do not copy verbatim): vendor-derived libzigbee
 * src/nwk_formation.c, src/nwk_permit_joining.c, src/nwk_brc.c and
 * src/zdo_nwk_manager.c. Zephyr-specific code is limited to profile/NV
 * preparation and radio filter/persistence hooks.
 */

#include "zb_common_stub.h"
#include "mac/includes/tl_zb_mac.h"
#include "mac/includes/tl_zb_mac_pib.h"

#include <errno.h>
#include <string.h>
#include <zephyr/logging/log.h>
#include <zephyr/random/random.h>
#include <zephyr/sys/printk.h>
#include <zephyr/zigbee/zb_bootstrap.h>
#include <zephyr/zigbee/zb_config.h>
#include <zephyr/zigbee/zb_radio_port.h>

LOG_MODULE_REGISTER(zigbee_nwk_router_bootstrap, CONFIG_ZIGBEE_LOG_LEVEL);

/* Defaults match the existing zigbee_shell fixed-target ED profile so a
 * locally-formed router and an externally-driven ED can share a PAN for
 * loopback testing on a single host.
 */
#define NWK_ROUTER_MINIMAL_DEFAULT_PAN_ID    0xc6c6U
#define NWK_ROUTER_MINIMAL_DEFAULT_NWK_ADDR  0x0000U  /* coordinator */
#define NWK_ROUTER_MINIMAL_DEFAULT_CHANNEL   DEFAULT_CHANNEL

extern zdo_appIndCb_t *zdoAppIndCbLst;
extern u8 zb_zdoSendDevAnnance(void);
extern volatile u32 zb_nwk_ed_trace[];
extern zdo_status_t zdo_nwkRouterStart(void);

static bool nwk_router_started;
static bool nwk_router_bootstrap_announce_sent;
static ev_timer_event_t *nwk_router_persist_save_evt;

static int nwk_router_bootstrap_deferred_save_timer(void *arg)
{
	ARG_UNUSED(arg);
	nwk_router_persist_save_evt = NULL;
	zb_info_save(NULL);
	return -1; /* one-shot */
}

void zb_router_schedule_persistence_save(void)
{
	if (nwk_router_persist_save_evt != NULL || !g_zbNwkCtx.joined ||
	    g_zbMacPib.panId == MAC_INVALID_PANID ||
	    g_zbMacPib.shortAddress >= ZB_MAC_SHORT_ADDR_NOT_ALLOCATED) {
		return;
	}

	/* Flash writes mask RF interrupts on TLSR8258. Wait until the
	 * association/interview traffic has drained before saving the joined
	 * PIB, but make sure the learned short address survives reboot. */
	nwk_router_persist_save_evt = TL_ZB_TIMER_SCHEDULE(
		nwk_router_bootstrap_deferred_save_timer, NULL, 15000U);
}

static void nwk_router_bootstrap_fill_nwk_key(uint8_t key[SEC_KEY_LEN])
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

static void nwk_router_bootstrap_apply_pib(uint8_t channel, uint16_t pan_id,
					 uint16_t short_addr,
					 const uint8_t ext_pan_id[EXT_ADDR_LEN])
{
	g_zbMacPib.phyChannelCur = channel;
	g_zbMacPib.panId = pan_id;
	g_zbMacPib.shortAddress = short_addr;
	g_zbMacPib.coordShortAddress = short_addr;
	g_zbMacPib.rxOnWhenIdle = 1U;
	g_zbMacPib.associationPermit = 0U;

	/* Keep the libzigbee compatibility snapshot used by MAC/NWK TX in sync
	 * with the MAC PIB programmed above, including after NVS restore. */
	g_zbInfo.macPib.phyChannelCur = channel;
	g_zbInfo.macPib.panId = pan_id;
	g_zbInfo.macPib.shortAddress = short_addr;
	g_zbInfo.macPib.coordShortAddress = short_addr;
	g_zbInfo.macPib.rxOnWhenIdle = 1U;

	g_zbNIB.panId = pan_id;
	g_zbNIB.nwkAddr = short_addr;
	memcpy(g_zbNIB.extPANId, ext_pan_id, EXT_ADDR_LEN);
	memcpy(g_zbNIB.ieeeAddr, g_zbMacPib.extAddress, EXT_ADDR_LEN);
	g_zbNIB.updateId = 0U;
}

static bool nwk_router_bootstrap_resolve_profile(uint8_t *channel,
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

static bool nwk_router_bootstrap_has_restored_state(void)
{
	/* zb_platform_restore_persistent_state() loads g_zbInfo (with the
	 * nested macPib/nwkNib) and g_zbNwkCtx from NVS at boot. A fully
	 * formed router shows joined=1 and a real PAN ID; a fresh device
	 * has joined=0 or pan=0xffff.
	 */
	return g_zbNwkCtx.joined &&
	       g_zbMacPib.panId != MAC_INVALID_PANID &&
	       g_zbMacPib.panId != 0U;
}

uint8_t zb_routerStart(void)
{
	uint8_t ext_pan_id[EXT_ADDR_LEN];
	uint8_t nwk_key[SEC_KEY_LEN] = {0};
	uint8_t channel;
	uint16_t pan_id;
	uint16_t short_addr;
	bool key_provided;
	bool from_app;
	bool restored = false;
	int rc;

	if (nwk_router_started) {
		LOG_DBG("zb_routerStart: already started, ignoring");
		return 0U;
	}

	if (nwk_router_bootstrap_has_restored_state()) {
		/* Reboot with valid NVS state: keep restored PIB/NIB and
		 * just reprogram the radio. Skip key generation and skip
		 * zb_info_save (state is already persisted from the prior
		 * boot's formation).
		 */
		restored = true;
		channel = g_zbMacPib.phyChannelCur;
		pan_id = g_zbMacPib.panId;
		short_addr = g_zbMacPib.shortAddress;
		memcpy(ext_pan_id, g_zbNIB.extPANId, EXT_ADDR_LEN);
		from_app = false;
		key_provided = true; /* keep restored key */
		/* Older NVS images were written before the router capability was
		 * forced awake. Re-apply the runtime router PIB before any TX; the
		 * MAC completion path uses this value to decide whether RF may stop. */
		nwk_router_bootstrap_apply_pib(channel, pan_id, short_addr, ext_pan_id);
	} else {
		from_app = nwk_router_bootstrap_resolve_profile(&channel, &pan_id,
							      &short_addr, ext_pan_id,
							      nwk_key, &key_provided);

		if (!key_provided) {
			nwk_router_bootstrap_fill_nwk_key(nwk_key);
		}

		nwk_router_bootstrap_apply_pib(channel, pan_id, short_addr, ext_pan_id);

		/* Store the network key into the security IB so the APS
		 * encrypt/decrypt path can find it. Slot 0 / seqNum 0
		 * mirrors the vendor stack's freshly-formed-network
		 * convention.
		 */
		memcpy(ss_ib.nwkSecurMaterialSet[0].key, nwk_key, SEC_KEY_LEN);
		ss_ib.nwkSecurMaterialSet[0].keySeqNum = 0U;
		ss_ib.activeKeySeqNum = 0U;
		memset(nwk_key, 0, sizeof(nwk_key));
	}

	rc = zb_radio_port_set_channel(channel);
	if (rc != 0) {
		LOG_ERR("zb_routerStart: set_channel failed (%d)", rc);
		return 1U;
	}
	zb_radio_port_update_filters(pan_id, short_addr, g_zbMacPib.extAddress);
	(void)zb_radio_port_set_trx_state(ZB_RADIO_PORT_TRX_RX, channel);

	/* The vendor start path owns the joined transition and the start
	 * confirmation. Keeping joined clear until MLME-START succeeds avoids
	 * exposing a half-started FFD to ZDO and BDB. */
	g_zbNwkCtx.joined = 0U;
	g_zbNwkCtx.is_factory_new = 0U;
	g_zbNwkCtx.user_state = NLME_IDLE;

	rc = zdo_nwkRouterStart();
	if (rc != ZDO_SUCCESS) {
		LOG_ERR("zb_routerStart: vendor start failed (%d)", rc);
		return 1U;
	}

	nwk_router_started = true;

	LOG_INF("zb router %s: pan 0x%04x ch %u short 0x%04x%s%s",
		restored ? "restored" : "formed",
		pan_id, channel, short_addr,
		restored ? "" : (from_app ? " (app-profile)" : " (default)"),
		restored ? "" : (key_provided ? " key=from-app" : " key=generated"));

	return 0U;
}

extern void tl_zbNwkBeaconPayloadUpdate(void);

/*
 * Open this router as a PARENT so a new device can join THROUGH it.
 *
 * The application-facing parenting request is the point at which the
 * standard BDB post-join Mgmt_Permit_Joining policy is applied. It flips the
 * router into the parent-active state the vendor MAC expects:
 *   - devType!=0 + beaconPayloadLen!=0  -> tl_zbMacBeaconRequestCb TXes a beacon
 *   - associationPermit=1               -> beacon superframe advertises permit
 *   - joinAccept=1                      -> NLME permit-join precondition
 *   - joined=1 (already) + !joined_pro  -> tl_zbMlmeCmdBeaconReqRecvd accepts
 * BO/SO=15 marks a non-beacon-enabled (on-demand) network.
 *
 * tl_zbNwkBeaconPayloadUpdate() rebuilds the payload struct but does NOT set
 * beaconPayloadLen, so we set it here. permit_duration==0 closes the window.
 */
void zb_router_enable_parenting(u8 permit_duration)
{
	zb_nwk_ed_trace[51]++;
	/* A router must remain awake after every unicast TX so it can ACK
	 * interview, leave, and rejoin traffic from the coordinator. */
	g_zbMacPib.rxOnWhenIdle = 1U;
	g_zbInfo.macPib.rxOnWhenIdle = 1U;
	g_zbNIB.capabilityInfo.rcvOnWhenIdle = 1U;
	g_zbNIB.capabilityInfo.devType = 1U;   /* FFD / router-capable */
	g_zbNwkCtx.joinAccept = 1U;
	g_zbNwkCtx.permit_join = (permit_duration != 0U) ? 1U : 0U;
	g_zbMacPib.beaconOrder = 15U;
	g_zbMacPib.superframeOrder = 15U;
	g_zbMacPib.associationPermit = (permit_duration != 0U) ? 1U : 0U;

	tl_zbNwkBeaconPayloadUpdate();
	g_zbMacPib.beaconPayloadLen = (u8)sizeof(g_zbMacPib.beaconPayload);

	/* Persistent restore does not pass through zdo_startup_complete(), so
	 * announce the current short address when the router is made active. The
	 * application callback is the joined-state gate; do not duplicate that
	 * check here because restored g_zbNwkCtx can be populated later. */
	if (!nwk_router_bootstrap_announce_sent) {
		if (zb_zdoSendDevAnnance() == ZDO_SUCCESS) {
			nwk_router_bootstrap_announce_sent = true;
		}
	}
}
