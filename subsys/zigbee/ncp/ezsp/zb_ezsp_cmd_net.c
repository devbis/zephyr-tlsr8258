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

#include "drv_nv.h"
#include "drv_radio.h"
#include "mac/mac.h"
#include "ev_timer.h"
#include "nwk/includes/nwk_addr_map.h"
#include "nwk/includes/nwk_neighbor.h"
#include "nwk/nwk_data.h"
#include "nwk/nwk_routing.h"
#include "ss/ss_apsSecurityME.h"
#include "ss/ss_zdoSecurityME.h"
#include "zb_common.h"
#include "zb_ezsp_frame.h"
#include "zb_ezsp_internal.h"
#include "zb_ezsp_session.h"
#include "zbapi/zb_initialize.h"

/* The manufacturing tokens a host reads to name the board. */
#define EZSP_MFG_STRING     0x01U
#define EZSP_MFG_BOARD_NAME 0x02U

/* Bits of EmberCurrentSecurityBitmask. */
#define EZSP_SEC_GLOBAL_LINK_KEY 0x0004U
#define EZSP_SEC_HAVE_TC_LINK_KEY 0x0010U

/* SecurityManagerKeyType. */
#define EZSP_KEY_NETWORK 0x01U
#define EZSP_KEY_TC_LINK 0x02U

/* Bits of EmberInitialSecurityBitmask. */
#define EZSP_INIT_SEC_HAVE_NETWORK_KEY 0x0200U

/*
 * SecurityManagerContext: key type, key index, derived type, EUI-64, network index, flags and the
 * algorithm permission. Zigbee2MQTT sends the context without the last field, so only the part
 * before it is required.
 */
#define EZSP_SEC_CONTEXT_MIN_LEN 14U

#define EZSP_NO_ADDR         0xFFU

/* The radio has no getter: the power the host last set, or the radio default of 0 dBm. */
static int8_t tx_power_dbm;

/* Set from formNetwork until the stack reports whether the formation worked. */
static bool formation_pending;

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
	/*
	 * A network that is being formed counts as joined: the formation takes the stack a while
	 * and the host, homed among them, asks for the state as soon as formNetwork answered. The
	 * result comes as stackStatusHandler, and a failed formation takes the state back.
	 */
	ezsp_wr_u8(out, (network_up() || formation_pending) ? EMBER_JOINED_NETWORK
							    : EMBER_NO_NETWORK);
}

static void stack_status_up(struct ezsp_ctx *c)
{
	const uint8_t status = EMBER_NETWORK_UP;

	(void)ezsp_session_callback(c->session, EZSP_ID_STACK_STATUS_HANDLER, &status, 1U);
}

static void cmd_network_init(struct ezsp_ctx *c, struct ezsp_rd *in, struct ezsp_wr *out)
{
	/*
	 * The options are not used. Zigbee2MQTT sends the command without them, which the firmware
	 * of a chip accepts, so they are not required.
	 */
	if (in->left >= 2U) {
		(void)ezsp_rd_u16(in);
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
	(void)ezsp_rd_bytes(in, EZSP_SEC_CONTEXT_MIN_LEN - 1U);
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

/* The security state the host gave before it formed the network: only the network key matters. */
static struct {
	bool have_key;
	uint8_t key[SEC_KEY_LEN];
	uint8_t seq;
} init_sec;

static void cmd_set_initial_security_state(struct ezsp_ctx *c, struct ezsp_rd *in,
					   struct ezsp_wr *out)
{
	uint16_t bitmask = ezsp_rd_u16(in);
	const uint8_t *preconfigured = ezsp_rd_bytes(in, SEC_KEY_LEN);
	const uint8_t *network = ezsp_rd_bytes(in, SEC_KEY_LEN);
	uint8_t seq = ezsp_rd_u8(in);

	ARG_UNUSED(c);
	ARG_UNUSED(preconfigured);
	/* The trust center address that follows is this node's own */
	(void)ezsp_rd_bytes(in, 8U);
	if (in->err) {
		return;
	}

	/*
	 * The trust center link key stays the stack's default: a host that derives it from a hash
	 * of its own cannot make the stack use that key for the devices that join.
	 */
	init_sec.have_key = (bitmask & EZSP_INIT_SEC_HAVE_NETWORK_KEY) != 0U;
	if (init_sec.have_key) {
		memcpy(init_sec.key, network, SEC_KEY_LEN);
		init_sec.seq = seq;
	}

	ezsp_wr_u8(out, EMBER_SUCCESS);
}

/*
 * Make @p key the network key. The stack's own preconfigured-key path is not used: it marks the
 * key as already known to every device, so the trust center would transport an all-zero key to
 * joiners.
 */
static void network_key_set(const uint8_t *key, uint8_t seq)
{
	ss_zdoNwkKeyConfigure((uint8_t *)key, seq, TRUE);
	ss_zdoUseKey(seq);
	ss_ib.preConfiguredKeyType = SS_PRECONFIGURED_NOKEY;
}

static void cmd_form_network(struct ezsp_ctx *c, struct ezsp_rd *in, struct ezsp_wr *out)
{
	const uint8_t *ext_pan = ezsp_rd_bytes(in, 8U);
	uint16_t pan = ezsp_rd_u16(in);
	int8_t power = (int8_t)ezsp_rd_u8(in);
	uint8_t channel = ezsp_rd_u8(in);

	ARG_UNUSED(c);
	/* The join method, the network manager, the update ID and the channel mask are not used */
	(void)ezsp_rd_bytes(in, 1U + 2U + 1U + 4U);
	if (in->err) {
		return;
	}

	if (network_up() || formation_pending) {
		ezsp_wr_u8(out, EMBER_INVALID_CALL);
		return;
	}

	if (channel < ZB_MAC_CHANNEL_START || channel > ZB_MAC_CHANNEL_STOP) {
		ezsp_wr_u8(out, EMBER_BAD_ARGUMENT);
		return;
	}

	if (zb_radio_tx_power_dbm_set(power) != 0) {
		ezsp_wr_u8(out, EMBER_PHY_INVALID_POWER);
		return;
	}
	tx_power_dbm = power;

	/* The BDB must be initialized before its attributes are overridden */
	(void)zb_platform_bdb_init_default();
	g_bdbAttrs.primaryChannelSet = BIT(channel);
	g_bdbAttrs.secondaryChannelSet = 0U;

	if (init_sec.have_key) {
		network_key_set(init_sec.key, init_sec.seq);
	}

	if (!ZB_EXTPANID_IS_ZERO(ext_pan)) {
		zb_apsExtPanidSet((uint8_t *)ext_pan);
		ZB_EXTPANID_COPY(g_zbNIB.extPANId, ext_pan);
	}

	if (pan < 0xFFFEU) {
		g_zbNIB.panId = pan;
	}

	formation_pending = true;
	if (zb_platform_bdb_network_formation_start() != 0U) {
		formation_pending = false;
		ezsp_wr_u8(out, EMBER_ERR_FATAL);
		return;
	}

	/* Whether the formation worked is reported by stackStatusHandler */
	ezsp_wr_u8(out, EMBER_SUCCESS);
}

void zb_ezsp_bdb_commissioning_status(uint8_t status, bool joined_network)
{
	uint8_t stack_status;

	if (!formation_pending) {
		return;
	}

	if (joined_network) {
		formation_pending = false;
		zb_info_save(NULL);
		stack_status = EMBER_NETWORK_UP;
	} else if (status != 0U) {
		formation_pending = false;
		stack_status = EMBER_NETWORK_DOWN;
	} else {
		return;
	}

	(void)zb_ezsp_callback(EZSP_ID_STACK_STATUS_HANDLER, &stack_status, 1U);
}

/* Forget the network, as the stack's own factory reset does, and keep the node's address. */
static void network_wipe(void)
{
	addrExt_t ieee;

	/*
	 * The stack's factory reset, without its BDB step: that step restarts BDB on the descriptor
	 * of an earlier init, which a node that never started commissioning does not have.
	 */
	zb_getLocalExtAddr(ieee);
	(void)nv_resetToFactoryNew();
	zb_reset();
	/* The MAC reset drops the node's address; it is not part of the network */
	ZB_IEEE_ADDR_COPY(g_zbMacPib.extAddress, ieee);
	zb_platform_bdb_runtime_reset();
	init_sec.have_key = false;
	formation_pending = false;
}

static void stack_status_down(struct ezsp_ctx *c)
{
	const uint8_t status = EMBER_NETWORK_DOWN;

	(void)ezsp_session_callback(c->session, EZSP_ID_STACK_STATUS_HANDLER, &status, 1U);
}

static void cmd_leave_network(struct ezsp_ctx *c, struct ezsp_rd *in, struct ezsp_wr *out)
{
	/*
	 * Options of the leave in the later protocol versions; a coordinator takes none.
	 * Zigbee2MQTT sends the command without them.
	 */
	if (in->left >= 1U) {
		(void)ezsp_rd_u8(in);
	}

	if (!network_up()) {
		ezsp_wr_u8(out, EMBER_INVALID_CALL);
		return;
	}

	network_wipe();
	ezsp_wr_u8(out, EMBER_SUCCESS);
	c->after = stack_status_down;
}

static void cmd_token_factory_reset(struct ezsp_ctx *c, struct ezsp_rd *in, struct ezsp_wr *out)
{
	ARG_UNUSED(c);
	ARG_UNUSED(out);
	/* Whether to keep the outgoing frame counter and the boot counter */
	(void)ezsp_rd_u8(in);
	(void)ezsp_rd_u8(in);
	if (in->err) {
		return;
	}

	network_wipe();
}

static void cmd_permit_joining(struct ezsp_ctx *c, struct ezsp_rd *in, struct ezsp_wr *out)
{
	uint8_t duration = ezsp_rd_u8(in);
	uint8_t seq = 0U;

	ARG_UNUSED(c);
	if (in->err) {
		return;
	}

	if (!network_up()) {
		ezsp_wr_u8(out, EMBER_INVALID_CALL);
		return;
	}

	ezsp_wr_u8(out, zb_mgmtPermitJoinReq(zb_getLocalShortAddr(), duration, 1U, &seq, NULL) ==
					ZDO_SUCCESS
				? EMBER_SUCCESS
				: EMBER_INVALID_CALL);
}

/* A table entry that is not used reads as the invalid address and node ID. */
#define EZSP_NODE_ID_UNUSED 0xFFFFU

/* EmberNodeType of the children the stack keeps. */
#define EZSP_NODE_ROUTER      2U
#define EZSP_NODE_END_DEVICE  3U

static void cmd_export_link_key_by_index(struct ezsp_ctx *c, struct ezsp_rd *in,
					 struct ezsp_wr *out)
{
	static const uint8_t none[8U + SEC_KEY_LEN + 2U + 4U + 4U + 2U];

	ARG_UNUSED(c);
	(void)ezsp_rd_u8(in);
	/*
	 * The stack keeps the link keys of the devices that have a unique one, but not in a table
	 * this command could index; the host sees an empty table.
	 */
	ezsp_wr_bytes(out, none, sizeof(none));
	ezsp_wr_u32(out, SL_STATUS_NOT_FOUND);
}

/* The child at @p index of the neighbor table, or NULL. */
static const zb_normal_neighbor_entry_t *child_at(uint8_t index)
{
	const zb_normal_neighbor_entry_t *e = g_zb_neighborTbl.activeHead;

	for (; e != NULL; e = e->activeNext) {
		if (e->used != 0U && e->relationship == NEIGHBOR_IS_CHILD) {
			if (index == 0U) {
				return e;
			}
			index--;
		}
	}

	return NULL;
}

static void cmd_get_child_data(struct ezsp_ctx *c, struct ezsp_rd *in, struct ezsp_wr *out)
{
	const zb_normal_neighbor_entry_t *e = child_at(ezsp_rd_u8(in));
	uint16_t nwk;
	uint8_t ieee[8] = {0};

	ARG_UNUSED(c);
	if (in->err) {
		return;
	}

	if (e == NULL) {
		static const uint8_t none[8U + 1U + 2U + 3U + 4U];

		ezsp_wr_u8(out, EMBER_NOT_JOINED);
		ezsp_wr_bytes(out, none, sizeof(none));
		return;
	}

	nwk = zb_shortAddrByIdx(e->addrmapIdx);
	(void)zb_getExtAddrByNwkAddr(nwk, ieee);

	ezsp_wr_u8(out, EMBER_SUCCESS);
	ezsp_wr_bytes(out, ieee, sizeof(ieee));
	ezsp_wr_u8(out, (e->deviceType == NWK_DEVICE_TYPE_ROUTER) ? EZSP_NODE_ROUTER
								  : EZSP_NODE_END_DEVICE);
	ezsp_wr_u16(out, nwk);
	/* The PHY, the power and the timeouts of the child are not kept in this form */
	ezsp_wr_u8(out, 0U);
	ezsp_wr_u8(out, 0U);
	ezsp_wr_u8(out, 0U);
	ezsp_wr_u32(out, 0U);
}

/*
 * The address table the host fills for the devices whose long poll timeout it wants set. The stack
 * keeps its own address map, which the lookups read; this table is only what the host put in it.
 */
#define EZSP_ADDRESS_TABLE_SIZE 16U

static struct {
	uint8_t eui64[8];
	uint16_t id;
	bool extended_timeout;
} address_table[EZSP_ADDRESS_TABLE_SIZE];
static bool address_table_ready;

static void address_table_init(void)
{
	if (address_table_ready) {
		return;
	}

	for (size_t i = 0; i < ARRAY_SIZE(address_table); i++) {
		memset(address_table[i].eui64, 0xFF, 8U);
		address_table[i].id = EZSP_NODE_ID_UNUSED;
	}
	address_table_ready = true;
}

static int address_table_find(const uint8_t *eui64)
{
	for (size_t i = 0; i < ARRAY_SIZE(address_table); i++) {
		if (address_table[i].id != EZSP_NODE_ID_UNUSED &&
		    memcmp(address_table[i].eui64, eui64, 8U) == 0) {
			return (int)i;
		}
	}

	return -1;
}

static void cmd_get_address_table_eui64(struct ezsp_ctx *c, struct ezsp_rd *in,
					struct ezsp_wr *out)
{
	uint8_t index = ezsp_rd_u8(in);

	ARG_UNUSED(c);
	address_table_init();
	if (in->err) {
		return;
	}

	ezsp_wr_bytes(out, address_table[MIN(index, EZSP_ADDRESS_TABLE_SIZE - 1U)].eui64, 8U);
	if (index >= EZSP_ADDRESS_TABLE_SIZE) {
		/* Past the table: the entry reads as unused */
		out->len -= 8U;
		for (uint8_t i = 0; i < 8U; i++) {
			ezsp_wr_u8(out, 0xFFU);
		}
	}
}

static void cmd_get_address_table_node_id(struct ezsp_ctx *c, struct ezsp_rd *in,
					  struct ezsp_wr *out)
{
	uint8_t index = ezsp_rd_u8(in);

	ARG_UNUSED(c);
	address_table_init();
	ezsp_wr_u16(out, (index < EZSP_ADDRESS_TABLE_SIZE) ? address_table[index].id
							   : EZSP_NODE_ID_UNUSED);
}

static void cmd_replace_address_table_entry(struct ezsp_ctx *c, struct ezsp_rd *in,
					    struct ezsp_wr *out)
{
	uint8_t index = ezsp_rd_u8(in);
	const uint8_t *eui64 = ezsp_rd_bytes(in, 8U);
	uint16_t id = ezsp_rd_u16(in);
	bool extended = ezsp_rd_u8(in) != 0U;

	ARG_UNUSED(c);
	address_table_init();
	if (in->err) {
		return;
	}

	if (index >= EZSP_ADDRESS_TABLE_SIZE) {
		static const uint8_t none[8U + 2U + 1U];

		ezsp_wr_u8(out, EMBER_INDEX_OUT_OF_RANGE);
		ezsp_wr_bytes(out, none, sizeof(none));
		return;
	}

	ezsp_wr_u8(out, EMBER_SUCCESS);
	ezsp_wr_bytes(out, address_table[index].eui64, 8U);
	ezsp_wr_u16(out, address_table[index].id);
	ezsp_wr_u8(out, address_table[index].extended_timeout ? 1U : 0U);

	memcpy(address_table[index].eui64, eui64, 8U);
	address_table[index].id = id;
	address_table[index].extended_timeout = extended;
}

static void cmd_lookup_node_id_by_eui64(struct ezsp_ctx *c, struct ezsp_rd *in,
					struct ezsp_wr *out)
{
	const uint8_t *ieee = ezsp_rd_bytes(in, 8U);
	uint16_t nwk = EZSP_NODE_ID_UNUSED;

	ARG_UNUSED(c);
	if (in->err) {
		return;
	}

	if (zb_getNwkAddrByExtAddr((uint8_t *)ieee, &nwk) != 0U) {
		nwk = EZSP_NODE_ID_UNUSED;
	}
	ezsp_wr_u16(out, nwk);
}

static void cmd_lookup_eui64_by_node_id(struct ezsp_ctx *c, struct ezsp_rd *in,
					struct ezsp_wr *out)
{
	uint16_t nwk = ezsp_rd_u16(in);
	uint8_t ieee[8] = {0};

	ARG_UNUSED(c);
	if (in->err) {
		return;
	}

	if (zb_getExtAddrByNwkAddr(nwk, ieee) != 0U) {
		static const uint8_t none[8];

		ezsp_wr_u8(out, EMBER_NOT_FOUND);
		ezsp_wr_bytes(out, none, sizeof(none));
		return;
	}

	ezsp_wr_u8(out, EMBER_SUCCESS);
	ezsp_wr_bytes(out, ieee, sizeof(ieee));
}

/*
 * The coordinator, as a concentrator, announces itself with a many-to-one route request now and
 * then, so that routers keep a route back to it. The host sets the period.
 */
static uint32_t concentrator_period_ms;
static struct ev_timer_event_t *concentrator_timer;

static bool many_to_one_discovery(uint8_t radius)
{
	nlme_routeDisc_req_t *req;
	zb_buf_t *buf = zb_buf_allocate();

	if (buf == NULL) {
		return false;
	}

	req = (nlme_routeDisc_req_t *)buf;
	memset(req, 0, sizeof(*req));
	req->dstAddr = NWK_BROADCAST_ROUTER_COORDINATOR;
	req->dstAddrMode = ADDR_MODE_NONE;
	req->radius = radius;
	buf->hdr.used = 1U;

	if (zb_PrimitivePost(ZB_Q_HIGH2NWK, NWK_NLME_ROUTE_DISCOVERY_REQ, buf) != RET_OK) {
		zb_buf_free(buf);
		return false;
	}

	return true;
}

static int concentrator_tick(void *arg)
{
	ARG_UNUSED(arg);

	concentrator_timer = NULL;
	if (concentrator_period_ms == 0U || !network_up()) {
		return -1;
	}

	(void)many_to_one_discovery(g_zbNIB.concentratorRadius);
	concentrator_timer = ev_timer_taskPost(concentrator_tick, NULL, concentrator_period_ms);

	return -1;
}

static void cmd_set_concentrator(struct ezsp_ctx *c, struct ezsp_rd *in, struct ezsp_wr *out)
{
	bool on = ezsp_rd_u8(in) != 0U;
	uint16_t type = ezsp_rd_u16(in);
	uint16_t min_time = ezsp_rd_u16(in);
	uint16_t max_time = ezsp_rd_u16(in);
	uint8_t hops;

	ARG_UNUSED(c);
	ARG_UNUSED(type);
	ARG_UNUSED(min_time);
	/* The route error and delivery failure thresholds, which the stack does not use */
	(void)ezsp_rd_u8(in);
	(void)ezsp_rd_u8(in);
	hops = ezsp_rd_u8(in);
	if (in->err) {
		return;
	}

	g_zbNIB.isConcentrator = on ? 1U : 0U;
	g_zbNIB.concentratorDiscoveryTime = (uint8_t)MIN(max_time, 0xFFU);
	if (hops != 0U) {
		g_zbNIB.concentratorRadius = hops;
	}
	concentrator_period_ms = on ? (uint32_t)max_time * 1000U : 0U;

	if (concentrator_timer != NULL) {
		(void)ev_timer_taskCancel(&concentrator_timer);
		concentrator_timer = NULL;
	}
	if (concentrator_period_ms != 0U && network_up()) {
		concentrator_timer = ev_timer_taskPost(concentrator_tick, NULL,
						       concentrator_period_ms);
	}

	ezsp_wr_u8(out, EMBER_SUCCESS);
}

/*
 * A link key for one device to join with, such as one derived from its install code. A key for any
 * device (the wildcard address) is the trust center's default key, which the stack already uses.
 */
static void cmd_import_transient_key(struct ezsp_ctx *c, struct ezsp_rd *in, struct ezsp_wr *out)
{
	static const uint8_t wildcard[8] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
	const uint8_t *eui64 = ezsp_rd_bytes(in, 8U);
	const uint8_t *key = ezsp_rd_bytes(in, SEC_KEY_LEN);
	ss_dev_pair_set_t pair;

	ARG_UNUSED(c);
	/* The flags of the key context */
	(void)ezsp_rd_u8(in);
	if (in->err) {
		return;
	}

	if (memcmp(eui64, wildcard, sizeof(wildcard)) == 0) {
		ezsp_wr_u32(out, (ss_ib.tcLinkKey != NULL &&
				  memcmp(key, ss_ib.tcLinkKey, SEC_KEY_LEN) == 0)
					 ? SL_STATUS_OK
					 : SL_STATUS_NOT_AVAILABLE);
		return;
	}

	/* A device keeps one key, so a new one replaces the old */
	(void)ss_devKeyPairDelete((uint8_t *)eui64);

	memset(&pair, 0, sizeof(pair));
	memcpy(pair.device_address, eui64, 8U);
	memcpy(pair.linkKey, key, SEC_KEY_LEN);
	pair.apsLinkKeyType = SS_UNIQUE_LINK_KEY;
	pair.keyAttr = SS_UNVERIFIED_KEY;
	ss_devKeyPairSave(&pair);

	ezsp_wr_u32(out, SL_STATUS_OK);
}

/* Whether the host asked for the long poll timeout for a device it put in the address table. */
static void cmd_set_extended_timeout(struct ezsp_ctx *c, struct ezsp_rd *in, struct ezsp_wr *out)
{
	const uint8_t *eui64 = ezsp_rd_bytes(in, 8U);
	bool extended = ezsp_rd_u8(in) != 0U;
	int i;

	ARG_UNUSED(c);
	ARG_UNUSED(out);
	address_table_init();
	if (in->err) {
		return;
	}

	i = address_table_find(eui64);
	if (i >= 0) {
		address_table[i].extended_timeout = extended;
	}
}

static void cmd_get_extended_timeout(struct ezsp_ctx *c, struct ezsp_rd *in, struct ezsp_wr *out)
{
	const uint8_t *eui64 = ezsp_rd_bytes(in, 8U);
	int i;

	ARG_UNUSED(c);
	address_table_init();
	if (in->err) {
		return;
	}

	i = address_table_find(eui64);
	ezsp_wr_u8(out, (i >= 0 && address_table[i].extended_timeout) ? 1U : 0U);
}

/*
 * Drop the transient link keys. The keys of devices that are about to join are kept by the stack
 * until the device joins or leaves, and nothing here can tell which of them are transient.
 */
static void cmd_clear_transient_link_keys(struct ezsp_ctx *c, struct ezsp_rd *in,
					  struct ezsp_wr *out)
{
	ARG_UNUSED(c);
	ARG_UNUSED(in);
	ARG_UNUSED(out);
}

static const struct ezsp_cmd net_cmds[] = {
	{EZSP_ID_CLEAR_TRANSIENT_LINK_KEYS, cmd_clear_transient_link_keys},
	{EZSP_ID_SET_EXTENDED_TIMEOUT, cmd_set_extended_timeout},
	{EZSP_ID_GET_EXTENDED_TIMEOUT, cmd_get_extended_timeout},
	{EZSP_ID_IMPORT_TRANSIENT_KEY, cmd_import_transient_key},
	{EZSP_ID_SET_CONCENTRATOR, cmd_set_concentrator},
	{EZSP_ID_EXPORT_LINK_KEY_BY_INDEX, cmd_export_link_key_by_index},
	{EZSP_ID_GET_CHILD_DATA, cmd_get_child_data},
	{EZSP_ID_REPLACE_ADDRESS_TABLE_ENTRY, cmd_replace_address_table_entry},
	{EZSP_ID_GET_ADDRESS_TABLE_EUI64, cmd_get_address_table_eui64},
	{EZSP_ID_GET_ADDRESS_TABLE_NODE_ID, cmd_get_address_table_node_id},
	{EZSP_ID_LOOKUP_NODE_ID_BY_EUI64, cmd_lookup_node_id_by_eui64},
	{EZSP_ID_LOOKUP_EUI64_BY_NODE_ID, cmd_lookup_eui64_by_node_id},
	{EZSP_ID_SET_INITIAL_SECURITY_STATE, cmd_set_initial_security_state},
	{EZSP_ID_FORM_NETWORK, cmd_form_network},
	{EZSP_ID_LEAVE_NETWORK, cmd_leave_network},
	{EZSP_ID_TOKEN_FACTORY_RESET, cmd_token_factory_reset},
	{EZSP_ID_PERMIT_JOINING, cmd_permit_joining},
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
