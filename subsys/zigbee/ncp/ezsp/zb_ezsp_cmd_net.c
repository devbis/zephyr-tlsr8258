/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * EZSP commands that read and change the network the stack runs: its state, addresses,
 * parameters and keys. The stack keeps what it needs itself; nothing is stored here.
 */
#include <string.h>

#include <zephyr/sys/util.h>
#include <zephyr/zigbee/zb_bootstrap.h>

#include "mac/mac.h"
#include "nwk/nwk_data.h"
#include "platform/zephyr/drv_radio.h"
#include "ss/ss_zdoSecurityME.h"
#include "zb_common.h"
#include "zb_ezsp_frame.h"
#include "zb_ezsp_session.h"

/* The manufacturing tokens a host reads to name the board. */
#define EZSP_MFG_STRING     0x01U
#define EZSP_MFG_BOARD_NAME 0x02U

/* Bits of EmberCurrentSecurityBitmask. */
#define EZSP_SEC_GLOBAL_LINK_KEY 0x0004U
#define EZSP_SEC_HAVE_TC_LINK_KEY 0x0010U

/* SecurityManagerKeyType. */
#define EZSP_KEY_NETWORK 0x01U
#define EZSP_KEY_TC_LINK 0x02U

/* SecurityManagerContext: key type, key index, derived type, EUI-64, network index, flags, alg. */
#define EZSP_SEC_CONTEXT_LEN 18U

#define EZSP_NO_ADDR         0xFFU

/* The radio has no getter: the power the host last set, or the radio default of 0 dBm. */
static int8_t tx_power_dbm;

static bool network_up(void)
{
	return g_zbNwkCtx.joined != 0U;
}

static void put_eui64(struct ezsp_wr *out)
{
	uint8_t ieee[8];

	zb_getLocalExtAddr(ieee);
	ezsp_wr_bytes(out, ieee, sizeof(ieee));
}

static void cmd_network_state(struct ezsp_ctx *c, struct ezsp_rd *in, struct ezsp_wr *out)
{
	ARG_UNUSED(c);
	ARG_UNUSED(in);
	ezsp_wr_u8(out, network_up() ? EMBER_JOINED_NETWORK : EMBER_NO_NETWORK);
}

static void stack_status_up(struct ezsp_ctx *c)
{
	const uint8_t status = EMBER_NETWORK_UP;

	(void)ezsp_session_callback(c->session, EZSP_ID_STACK_STATUS_HANDLER, &status, 1U);
}

static void cmd_network_init(struct ezsp_ctx *c, struct ezsp_rd *in, struct ezsp_wr *out)
{
	(void)ezsp_rd_u16(in);
	if (in->err) {
		return;
	}

	/* A formed network is already running: the stack restored it before the host asked */
	if (!network_up()) {
		ezsp_wr_u8(out, EMBER_NOT_JOINED);
		return;
	}

	ezsp_wr_u8(out, EMBER_SUCCESS);
	c->after = stack_status_up;
}

static void cmd_get_node_id(struct ezsp_ctx *c, struct ezsp_rd *in, struct ezsp_wr *out)
{
	ARG_UNUSED(c);
	ARG_UNUSED(in);
	ezsp_wr_u16(out, zb_getLocalShortAddr());
}

static void cmd_get_eui64(struct ezsp_ctx *c, struct ezsp_rd *in, struct ezsp_wr *out)
{
	ARG_UNUSED(c);
	ARG_UNUSED(in);
	put_eui64(out);
}

static void cmd_get_network_parameters(struct ezsp_ctx *c, struct ezsp_rd *in,
				       struct ezsp_wr *out)
{
	uint8_t zero_pan[8] = {0};
	uint32_t channels = 0U;

	ARG_UNUSED(c);
	ARG_UNUSED(in);

	if (!network_up()) {
		ezsp_wr_u8(out, EMBER_NOT_JOINED);
		ezsp_wr_u8(out, EMBER_UNKNOWN_DEVICE);
		ezsp_wr_bytes(out, zero_pan, sizeof(zero_pan));
		/* panId, power, channel, join method, manager, update ID, channels */
		for (uint8_t i = 0; i < 2U + 1U + 1U + 1U + 2U + 1U + 4U; i++) {
			ezsp_wr_u8(out, 0U);
		}
		return;
	}

	channels = (uint32_t)aps_ib.aps_channel_mask;

	ezsp_wr_u8(out, EMBER_SUCCESS);
	ezsp_wr_u8(out, EMBER_COORDINATOR);
	ezsp_wr_bytes(out, g_zbNIB.extPANId, 8U);
	ezsp_wr_u16(out, g_zbNIB.panId);
	ezsp_wr_u8(out, (uint8_t)tx_power_dbm);
	ezsp_wr_u8(out, g_zbMacPib.phyChannelCur);
	/* The join method of a coordinator is not used */
	ezsp_wr_u8(out, 0U);
	ezsp_wr_u16(out, 0U);
	ezsp_wr_u8(out, g_zbNIB.updateId);
	ezsp_wr_u32(out, channels);
}

static void cmd_get_mfg_token(struct ezsp_ctx *c, struct ezsp_rd *in, struct ezsp_wr *out)
{
	static const char board[] = "Zephyr Zigbee coordinator";
	uint8_t token = ezsp_rd_u8(in);

	ARG_UNUSED(c);

	/* Only the names are kept; every other token reads as empty, as on a chip without it */
	switch (token) {
	case EZSP_MFG_STRING:
		ezsp_wr_u8(out, 6U);
		ezsp_wr_bytes(out, (const uint8_t *)"Zephyr", 6U);
		break;
	case EZSP_MFG_BOARD_NAME:
		ezsp_wr_u8(out, (uint8_t)(sizeof(board) - 1U));
		ezsp_wr_bytes(out, (const uint8_t *)board, sizeof(board) - 1U);
		break;
	default:
		ezsp_wr_u8(out, 0U);
		break;
	}
}

static void cmd_get_current_security_state(struct ezsp_ctx *c, struct ezsp_rd *in,
					   struct ezsp_wr *out)
{
	ARG_UNUSED(c);
	ARG_UNUSED(in);

	ezsp_wr_u8(out, network_up() ? EMBER_SUCCESS : EMBER_NOT_JOINED);
	ezsp_wr_u16(out, EZSP_SEC_GLOBAL_LINK_KEY | EZSP_SEC_HAVE_TC_LINK_KEY);
	/* The trust center is this node */
	put_eui64(out);
}

static const uint8_t *active_network_key(uint8_t *seq)
{
	*seq = ss_ib.activeKeySeqNum;

	return ss_ib.nwkSecurMaterialSet[ss_ib.activeSecureMaterialIndex].key;
}

static void cmd_get_network_key_info(struct ezsp_ctx *c, struct ezsp_rd *in, struct ezsp_wr *out)
{
	uint8_t seq;
	bool set = network_up() && !ss_keyIsEmpty(active_network_key(&seq));

	ARG_UNUSED(c);
	ARG_UNUSED(in);

	active_network_key(&seq);
	ezsp_wr_u32(out, SL_STATUS_OK);
	ezsp_wr_u8(out, set ? 1U : 0U);
	/* The stack keeps one key in use; the alternate one is not reported */
	ezsp_wr_u8(out, 0U);
	ezsp_wr_u8(out, set ? seq : 0U);
	ezsp_wr_u8(out, 0U);
	ezsp_wr_u32(out, set ? ss_ib.outgoingFrameCounter : 0U);
}

static void cmd_export_key(struct ezsp_ctx *c, struct ezsp_rd *in, struct ezsp_wr *out)
{
	uint8_t key_type = ezsp_rd_u8(in);
	uint8_t seq;

	ARG_UNUSED(c);
	/* The rest of the context names a key by index and address; only two types are kept */
	(void)ezsp_rd_bytes(in, EZSP_SEC_CONTEXT_LEN - 1U);
	if (in->err) {
		return;
	}

	if (key_type == EZSP_KEY_NETWORK && network_up()) {
		ezsp_wr_bytes(out, active_network_key(&seq), SEC_KEY_LEN);
		ezsp_wr_u32(out, SL_STATUS_OK);
	} else if (key_type == EZSP_KEY_TC_LINK && ss_ib.tcLinkKey != NULL) {
		ezsp_wr_bytes(out, ss_ib.tcLinkKey, SEC_KEY_LEN);
		ezsp_wr_u32(out, SL_STATUS_OK);
	} else {
		uint8_t none[SEC_KEY_LEN] = {0};

		ezsp_wr_bytes(out, none, sizeof(none));
		ezsp_wr_u32(out, SL_STATUS_NOT_FOUND);
	}
}

static void cmd_clear_key_table(struct ezsp_ctx *c, struct ezsp_rd *in, struct ezsp_wr *out)
{
	ARG_UNUSED(c);
	ARG_UNUSED(in);
	/* The stack keeps no link key table the host could have filled */
	ezsp_wr_u8(out, EMBER_SUCCESS);
}

static void cmd_set_source_route_discovery_mode(struct ezsp_ctx *c, struct ezsp_rd *in,
						struct ezsp_wr *out)
{
	ARG_UNUSED(c);
	(void)ezsp_rd_u8(in);
	/* The time until the next many-to-one route request; none is scheduled here */
	ezsp_wr_u32(out, 0U);
}

static void cmd_set_radio_power(struct ezsp_ctx *c, struct ezsp_rd *in, struct ezsp_wr *out)
{
	int8_t dbm = (int8_t)ezsp_rd_u8(in);

	ARG_UNUSED(c);
	if (in->err) {
		return;
	}

	if (zb_radio_tx_power_dbm_set(dbm) != 0) {
		ezsp_wr_u8(out, EMBER_BAD_ARGUMENT);
		return;
	}

	tx_power_dbm = dbm;
	ezsp_wr_u8(out, EMBER_SUCCESS);
}

static const struct ezsp_cmd net_cmds[] = {
	{EZSP_ID_NETWORK_STATE, cmd_network_state},
	{EZSP_ID_NETWORK_INIT, cmd_network_init},
	{EZSP_ID_GET_NODE_ID, cmd_get_node_id},
	{EZSP_ID_GET_EUI64, cmd_get_eui64},
	{EZSP_ID_GET_NETWORK_PARAMETERS, cmd_get_network_parameters},
	{EZSP_ID_GET_MFG_TOKEN, cmd_get_mfg_token},
	{EZSP_ID_GET_CURRENT_SECURITY_STATE, cmd_get_current_security_state},
	{EZSP_ID_GET_NETWORK_KEY_INFO, cmd_get_network_key_info},
	{EZSP_ID_EXPORT_KEY, cmd_export_key},
	{EZSP_ID_CLEAR_KEY_TABLE, cmd_clear_key_table},
	{EZSP_ID_SET_SOURCE_ROUTE_DISC, cmd_set_source_route_discovery_mode},
	{EZSP_ID_SET_RADIO_POWER, cmd_set_radio_power},
};

const struct ezsp_cmd_set ezsp_net_cmds = {
	.cmds = net_cmds,
	.count = ARRAY_SIZE(net_cmds),
};
