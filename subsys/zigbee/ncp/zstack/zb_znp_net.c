/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * The network settings a ZNP host reads and writes as NV items. A host reads
 * the NIB to learn the channel, PAN and keys of the running network, and
 * writes it back to move the coordinator to the network it wants. The values
 * live in the stack, so reads overlay them on the stored item, and a write is
 * applied to the stack when the host starts the network.
 */
#include <string.h>

#include <zephyr/logging/log.h>
#include <zephyr/zigbee/zb_bootstrap.h>

#include "mac/mac.h"
#include "nwk/nwk_data.h"
#include "ss/ss_zdoSecurityME.h"
#include "zb_common.h"
#include "zbapi/zb_initialize.h"
#include "zb_znp_internal.h"

LOG_MODULE_DECLARE(zigbee_znp, CONFIG_ZIGBEE_LOG_LEVEL);

/* The NIB as the host lays it out (packed, as Z-Stack 3.0 sends it). */
#define ZNP_NIB_LEN                    110U
#define ZNP_NIB_SECURITY_LEVEL         12U
#define ZNP_NIB_CAPABILITY_FLAGS       14U
#define ZNP_NIB_DEV_ADDRESS            20U
#define ZNP_NIB_LOGICAL_CHANNEL        22U
#define ZNP_NIB_COORD_ADDRESS          23U
#define ZNP_NIB_COORD_EXT_ADDRESS      25U
#define ZNP_NIB_PAN_ID                 33U
#define ZNP_NIB_STATE                  35U
#define ZNP_NIB_CHANNEL_LIST           36U
#define ZNP_NIB_EXTENDED_PAN_ID        53U
#define ZNP_NIB_KEY_LOADED             61U
#define ZNP_NIB_MANAGER_ADDR           105U
#define ZNP_NIB_UPDATE_ID              109U

#define ZNP_NIB_STATE_ROUTER           8U

/* A network key item: sequence number and key. */
#define KEY_ITEM_LEN               (1U + SEC_KEY_LEN)

/* Set once the host wrote settings that the running network does not have yet. */
static bool host_dirty;

static bool nib_present(void)
{
	return znp_nv_length(ZNP_NV_NIB) == ZNP_NIB_LEN;
}

static const uint8_t *active_key(uint8_t *seq)
{
	uint8_t idx = ss_ib.activeSecureMaterialIndex;

	*seq = ss_ib.activeKeySeqNum;
	return ss_ib.nwkSecurMaterialSet[idx].key;
}

static bool key_is_set(const uint8_t *key)
{
	return !ss_keyIsEmpty(key);
}

void znp_net_sync(void)
{
	uint8_t nib[ZNP_NIB_LEN];
	uint8_t item[KEY_ITEM_LEN];
	uint8_t ieee[8];
	uint8_t on = g_zbNwkCtx.joined ? 1U : 0U;
	uint8_t seq;
	const uint8_t *key;
	size_t n;

	/* Whether the node is on a network is the stack's to say. */
	(void)znp_nv_set_derived(ZNP_NV_BDBNODEISONANETWORK, &on, 1U);

	if (host_dirty || !g_zbNwkCtx.joined) {
		return;
	}

	zb_getLocalExtAddr(ieee);

	if (!nib_present()) {
		memset(nib, 0, sizeof(nib));
		nib[ZNP_NIB_SECURITY_LEVEL] = 5U;
		nib[ZNP_NIB_CAPABILITY_FLAGS] = 0x8EU;
		(void)znp_nv_item_init(ZNP_NV_NIB, ZNP_NIB_LEN, nib, ZNP_NIB_LEN);
	}

	(void)znp_nv_read(ZNP_NV_NIB, 0U, nib, sizeof(nib), &n);

	(void)znp_put_u16(&nib[ZNP_NIB_DEV_ADDRESS], g_zbNIB.nwkAddr);
	nib[ZNP_NIB_LOGICAL_CHANNEL] = g_zbMacPib.phyChannelCur;
	(void)znp_put_u16(&nib[ZNP_NIB_COORD_ADDRESS], 0U);
	memcpy(&nib[ZNP_NIB_COORD_EXT_ADDRESS], ieee, 8U);
	(void)znp_put_u16(&nib[ZNP_NIB_PAN_ID], g_zbNIB.panId);
	nib[ZNP_NIB_STATE] = ZNP_NIB_STATE_ROUTER;
	(void)znp_put_u32(&nib[ZNP_NIB_CHANNEL_LIST], (uint32_t)g_bdbAttrs.primaryChannelSet);
	memcpy(&nib[ZNP_NIB_EXTENDED_PAN_ID], g_zbNIB.extPANId, 8U);
	(void)znp_put_u16(&nib[ZNP_NIB_MANAGER_ADDR], 0U);
	nib[ZNP_NIB_UPDATE_ID] = g_zbNIB.updateId;

	key = active_key(&seq);
	nib[ZNP_NIB_KEY_LOADED] = key_is_set(key) ? 1U : 0U;
	(void)znp_nv_set_derived(ZNP_NV_NIB, nib, sizeof(nib));

	if (key_is_set(key)) {
		uint8_t counted[KEY_ITEM_LEN + 4U];

		item[0] = seq;
		memcpy(&item[1], key, SEC_KEY_LEN);
		(void)znp_nv_set_derived(ZNP_NV_NWK_ACTIVE_KEY_INFO, item, KEY_ITEM_LEN);
		(void)znp_nv_set_derived(ZNP_NV_NWK_ALTERN_KEY_INFO, item, KEY_ITEM_LEN);

		/* The active key with its frame counter. */
		memcpy(counted, item, KEY_ITEM_LEN);
		(void)znp_put_u32(&counted[KEY_ITEM_LEN], ss_ib.outgoingFrameCounter);
		(void)znp_nv_set_derived(ZNP_NV_NWKKEY, counted, sizeof(counted));

		/* The network security material: frame counter and the PAN it belongs to. */
		{
			uint8_t material[ZNP_NWK_SEC_MATERIAL_LEN];

			(void)znp_put_u32(material, ss_ib.outgoingFrameCounter);
			memcpy(&material[4], g_zbNIB.extPANId, 8U);
			(void)znp_nv_set_derived(ZNP_NV_NWK_SEC_MATERIAL_FIRST, material,
						 sizeof(material));
		}
	}

	(void)znp_nv_set_derived(ZNP_NV_EXTADDR, ieee, sizeof(ieee));
	(void)znp_nv_write(ZNP_NV_EXTENDED_PAN_ID, 0U, g_zbNIB.extPANId, 8U);
}

void znp_net_note_write(uint16_t id)
{
	if (id == ZNP_NV_NIB || id == ZNP_NV_NWK_ACTIVE_KEY_INFO || id == ZNP_NV_NWK_SEC_MATERIAL_FIRST) {
		host_dirty = true;
	}
}

void znp_net_reset(void)
{
	host_dirty = false;
}

/*
 * Make @p key the network key. The stack's own preconfigured-key path is not
 * used: it marks the key as already known to every device, so the trust
 * center would transport an all-zero key to joiners.
 */
static void network_key_set(const uint8_t *key, uint8_t seq)
{
	ss_zdoNwkKeyConfigure((u8 *)key, seq, TRUE);
	ss_zdoUseKey(seq);
	ss_ib.preConfiguredKeyType = SS_PRECONFIGURED_NOKEY;
}

/* Settings for a network that has not been formed yet. */
void znp_net_formation_config(void)
{
	uint8_t v[16];
	size_t n;

	if (znp_nv_read(ZNP_NV_PRECFGKEY, 0U, v, 16U, &n) == ZNP_STATUS_SUCCESS && n == 16U &&
	    !ss_keyIsEmpty(v) && !znp_all_ff(v, 16U)) {
		network_key_set(v, 0U);
	}

	/* Formation takes the extended PAN ID from the APS layer's setting. */
	if (znp_nv_read(ZNP_NV_APS_USE_EXT_PANID, 0U, v, 8U, &n) == ZNP_STATUS_SUCCESS && n == 8U &&
	    !znp_all_ff(v, 8U) && !ZB_EXTPANID_IS_ZERO(v)) {
		zb_apsExtPanidSet(v);
		ZB_EXTPANID_COPY(g_zbNIB.extPANId, v);
	}

	if (znp_nv_read(ZNP_NV_PANID, 0U, v, 2U, &n) == ZNP_STATUS_SUCCESS && n == 2U &&
	    znp_get_u16(v) < 0xFFFEU) {
		g_zbNIB.panId = znp_get_u16(v);
	}
}

/* Move the running network to the settings the host wrote. */
void znp_net_apply_host(void)
{
	uint8_t nib[ZNP_NIB_LEN];
	uint8_t item[KEY_ITEM_LEN];
	size_t n;
	uint8_t channel;
	uint16_t pan;

	if (!host_dirty) {
		return;
	}
	host_dirty = false;

	if (znp_nv_read(ZNP_NV_NIB, 0U, nib, sizeof(nib), &n) != ZNP_STATUS_SUCCESS ||
	    n != sizeof(nib)) {
		return;
	}

	channel = nib[ZNP_NIB_LOGICAL_CHANNEL];
	pan = znp_get_u16(&nib[ZNP_NIB_PAN_ID]);
	if (channel < TL_ZB_MAC_CHANNEL_START || channel > TL_ZB_MAC_CHANNEL_STOP || pan >= 0xFFFEU) {
		return;
	}

	LOG_INF("znp: applying host network ch %u pan 0x%04x", channel, pan);

	tl_zbMacChannelSet(channel);
	g_zbMacPib.phyChannelCur = channel;
	g_zbMacPib.panId = pan;
	g_zbNIB.panId = pan;
	ZB_EXTPANID_COPY(g_zbNIB.extPANId, &nib[ZNP_NIB_EXTENDED_PAN_ID]);
	g_zbNIB.updateId = nib[ZNP_NIB_UPDATE_ID];
	aps_ib.aps_channel_mask = znp_get_u16(&nib[ZNP_NIB_CHANNEL_LIST]) |
				  ((uint32_t)znp_get_u16(&nib[ZNP_NIB_CHANNEL_LIST + 2U]) << 16);

	if (znp_nv_read(ZNP_NV_NWK_ACTIVE_KEY_INFO, 0U, item, sizeof(item), &n) ==
		    ZNP_STATUS_SUCCESS &&
	    n == sizeof(item)) {
		/* This stack keeps two key slots, so only sequence numbers 0 and 1 map. */
		network_key_set(&item[1], item[0] & 1U);
	}

	if (znp_nv_read(ZNP_NV_NWK_SEC_MATERIAL_FIRST, 0U, item, ZNP_NWK_SEC_MATERIAL_LEN, &n) ==
		    ZNP_STATUS_SUCCESS &&
	    n == ZNP_NWK_SEC_MATERIAL_LEN) {
		uint32_t counter = (uint32_t)znp_get_u16(item) | ((uint32_t)znp_get_u16(&item[2]) << 16);

		if (counter > ss_ib.outgoingFrameCounter) {
			ss_ib.outgoingFrameCounter = counter;
		}
	}

	tl_zbNwkBeaconPayloadUpdate();

	/* The stack keeps what it was told only once it has written it down. */
	zb_info_save(NULL);
}
