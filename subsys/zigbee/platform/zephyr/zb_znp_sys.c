/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * ZNP SYS, UTIL and APP_CNF commands, the device state reported to the host
 * and the network formation it triggers.
 */
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>
#include <zephyr/zigbee/zb_bootstrap.h>
#include <zephyr/zigbee/zb_znp.h>

#include "zb_common.h"
#include "drv_nv.h"
#include "drv_radio.h"
#include "zbapi/zb_initialize.h"
#include "zb_znp_internal.h"

LOG_MODULE_DECLARE(zigbee_znp, CONFIG_ZIGBEE_LOG_LEVEL);

extern void zb_reset(void);
extern u16 tl_bdbInstallCodeCRC16(u8 *pInstallCode, u8 len);

/* MT capabilities: SYS, AF, ZDO, UTIL, APP and APP_CNF. */
#define ZNP_CAPABILITIES            0x0559U

#define ZNP_DEVICE_TYPE_COORDINATOR 0x01U

#define BDB_MODE_NWK_FORMATION      0x04U
#define BDB_STATUS_SUCCESS          0x00U
#define BDB_STATUS_IN_PROGRESS      0x01U
#define BDB_STATUS_FORMATION_FAILED 0x08U

#define ZNP_STARTUP_CLEAR_CONFIG    BIT(0)
#define ZNP_STARTUP_CLEAR_STATE     BIT(1)

#define ZNP_INSTALL_CODE_WITH_CRC     1U
#define ZNP_INSTALL_CODE_DERIVED_KEY  2U

#define ZNP_NV_READ_CHUNK           240U
#define ZNP_DEVICE_INFO_CHILDREN    16U

/* The NV items every Z-Stack has from the factory, with their default values. */
struct znp_nv_default {
	uint16_t id;
	uint8_t value;
};

static const struct znp_nv_default nv_defaults[] = {
	{ZNP_NV_STARTUP_OPTION, 0x00U},
	{ZNP_NV_LOGICAL_TYPE, 0x00U},
	{0x0030U, 20U},   /* BCAST_DELIVERY_TIME */
	{0x0032U, 0x00U}, /* CONCENTRATOR_ENABLE */
	{0x0033U, 120U},  /* CONCENTRATOR_DISCOVERY */
	{0x0036U, 0x01U}, /* CONCENTRATOR_RC */
	{0x0038U, 255U},  /* SRC_RTG_EXPIRY_TIME */
	{0x003EU, 0x00U}, /* NWK_CHILD_AGE_ENABLE */
	{0x008FU, 0x00U}, /* ZDO_DIRECT_CB */
	{ZNP_NV_PRECFGKEYS_ENABLE, 0x01U},
	{ZNP_NV_BDBNODEISONANETWORK, 0x00U},
};

static uint8_t znp_dev_state = ZNP_DEV_STATE_HOLD;
static bool formation_pending;
static uint32_t bdb_primary_channels;

bool znp_all_ff(const uint8_t *p, size_t len)
{
	for (size_t i = 0; i < len; i++) {
		if (p[i] != 0xffU) {
			return false;
		}
	}

	return true;
}

/*
 * The tables a host rewrites when it restores a network. They are sized for a
 * few devices and start empty.
 */
static void znp_nv_seed_tables(void)
{
	uint8_t empty_addr[ZNP_ADDRMGR_ENTRY_LEN];
	uint8_t zero[ZNP_APS_LINK_KEY_TABLE_LEN];

	memset(zero, 0, sizeof(zero));
	empty_addr[0] = 0x00U;
	memset(&empty_addr[1], 0xff, sizeof(empty_addr) - 1U);

	if (znp_nv_item_init(ZNP_NV_ADDRMGR, ZNP_ADDRMGR_ENTRIES * ZNP_ADDRMGR_ENTRY_LEN, empty_addr,
			     ZNP_ADDRMGR_ENTRY_LEN) == ZNP_STATUS_NV_ITEM_UNINIT) {
		for (uint16_t i = 1U; i < ZNP_ADDRMGR_ENTRIES; i++) {
			(void)znp_nv_write(ZNP_NV_ADDRMGR, i * ZNP_ADDRMGR_ENTRY_LEN, empty_addr,
					   ZNP_ADDRMGR_ENTRY_LEN);
		}
	}

	(void)znp_nv_item_init(ZNP_NV_APS_LINK_KEY_TABLE, ZNP_APS_LINK_KEY_TABLE_LEN, zero,
			       sizeof(zero));
	for (uint16_t id = ZNP_NV_NWK_SEC_MATERIAL_FIRST; id <= ZNP_NV_NWK_SEC_MATERIAL_LAST; id++) {
		(void)znp_nv_item_init(id, ZNP_NWK_SEC_MATERIAL_LEN, zero, ZNP_NWK_SEC_MATERIAL_LEN);
	}
}

void znp_nv_seed_missing(void)
{
	uint8_t ff[16];
	uint8_t ieee[8];

	memset(ff, 0xff, sizeof(ff));
	zb_getLocalExtAddr(ieee);

	for (size_t i = 0; i < ARRAY_SIZE(nv_defaults); i++) {
		(void)znp_nv_item_init(nv_defaults[i].id, 1U, &nv_defaults[i].value, 1U);
	}

	(void)znp_nv_item_init(ZNP_NV_EXTADDR, 8U, ieee, 8U);
	(void)znp_nv_item_init(ZNP_NV_PANID, 2U, ff, 2U);
	(void)znp_nv_item_init(ZNP_NV_CHANLIST, 4U, ff, 4U);
	(void)znp_nv_item_init(ZNP_NV_PRECFGKEY, 16U, ff, 16U);
	(void)znp_nv_item_init(ZNP_NV_EXTENDED_PAN_ID, 8U, ff, 8U);
	(void)znp_nv_item_init(ZNP_NV_APS_USE_EXT_PANID, 8U, ff, 8U);
	/* The network key items exist, empty, before a network is formed. */
	memset(ff, 0, sizeof(ff));
	(void)znp_nv_item_init(ZNP_NV_NWK_ACTIVE_KEY_INFO, 17U, ff, 16U);
	(void)znp_nv_item_init(ZNP_NV_NWK_ALTERN_KEY_INFO, 17U, ff, 16U);
	(void)znp_nv_item_init(ZNP_NV_NWKKEY, 21U, ff, 16U);
	znp_nv_seed_tables();
}

/* All items back to their factory values. */
static void znp_nv_seed(void)
{
	znp_nv_reset();
	znp_nv_seed_missing();
}

/* Items whose value is the stack's own state, refreshed before the host looks. */
static void znp_nv_sync(void)
{
	uint8_t ieee[8];

	/* A host that deletes the address expects the hardware one back. */
	zb_getLocalExtAddr(ieee);
	(void)znp_nv_set_derived(ZNP_NV_EXTADDR, ieee, sizeof(ieee));
	znp_net_sync();
	znp_tables_sync();
}

uint8_t znp_device_state(void)
{
	return znp_dev_state;
}

void znp_state_change(uint8_t state)
{
	znp_dev_state = state;
	znp_areq(ZNP_SUBSYS_ZDO, 0xC0U, &state, 1U);
}

static void bdb_notify(uint8_t status, uint8_t mode, uint8_t remaining)
{
	uint8_t d[3] = {status, mode, remaining};

	znp_areq(ZNP_SUBSYS_APP_CNF, 0x80U, d, sizeof(d));
}

static void znp_reset_ind(uint8_t reason)
{
	uint8_t d[6] = {reason,
			ZNP_TRANSPORT_REV,
			ZNP_PRODUCT_ID,
			ZNP_VERSION_MAJOR,
			ZNP_VERSION_MINOR,
			ZNP_VERSION_MAINT};

	znp_areq(ZNP_SUBSYS_SYS, 0x80U, d, sizeof(d));
}

/* SYS */

static void sys_reset_req(const uint8_t *d, uint8_t len)
{
	uint8_t opt = 0U;
	size_t n;

	ARG_UNUSED(d);
	ARG_UNUSED(len);

	(void)znp_nv_read(ZNP_NV_STARTUP_OPTION, 0U, &opt, 1U, &n);

	if ((opt & ZNP_STARTUP_CLEAR_STATE) != 0U) {
		/* Forget the network, as the stack's own factory reset does. */
		LOG_INF("znp: clearing the network");
		/*
		 * The stack's factory reset, without its BDB step: that step
		 * restarts BDB on the descriptor of an earlier init, which a
		 * node that never started commissioning does not have.
		 */
		addrExt_t ieee;

		zb_getLocalExtAddr(ieee);
		(void)nv_resetToFactoryNew();
		zb_reset();
		/* The MAC reset drops the node's address; it is not part of the network. */
		ZB_IEEE_ADDR_COPY(g_zbMacPib.extAddress, ieee);
		zb_platform_bdb_runtime_reset();
		znp_net_reset();
		znp_nv_seed();
		znp_persist_flush();
	} else if ((opt & ZNP_STARTUP_CLEAR_CONFIG) != 0U) {
		znp_nv_seed();
		znp_net_reset();
		znp_persist_flush();
	}

	formation_pending = false;
	znp_dev_state = g_zbNwkCtx.joined ? ZNP_DEV_STATE_COORDINATOR : ZNP_DEV_STATE_HOLD;
	znp_reset_ind(0x01U);
}

static void sys_ping(const uint8_t *d, uint8_t len)
{
	uint8_t rsp[2];

	ARG_UNUSED(d);
	ARG_UNUSED(len);

	(void)znp_put_u16(rsp, ZNP_CAPABILITIES);
	znp_srsp(ZNP_SUBSYS_SYS, 0x01U, rsp, sizeof(rsp));
}

static void sys_version(const uint8_t *d, uint8_t len)
{
	uint8_t rsp[15];
	uint8_t *p = rsp;

	ARG_UNUSED(d);
	ARG_UNUSED(len);

	*p++ = ZNP_TRANSPORT_REV;
	*p++ = ZNP_PRODUCT_ID;
	*p++ = ZNP_VERSION_MAJOR;
	*p++ = ZNP_VERSION_MINOR;
	*p++ = ZNP_VERSION_MAINT;
	p = znp_put_u32(p, ZNP_CODE_REVISION);
	*p++ = 0U; /* not a bootloader build */
	p = znp_put_u32(p, 0U);
	znp_srsp(ZNP_SUBSYS_SYS, 0x02U, rsp, (size_t)(p - rsp));
}

static void sys_get_ext_addr(const uint8_t *d, uint8_t len)
{
	uint8_t ieee[8];

	ARG_UNUSED(d);
	ARG_UNUSED(len);

	zb_getLocalExtAddr(ieee);
	znp_srsp(ZNP_SUBSYS_SYS, 0x04U, ieee, sizeof(ieee));
}

static void sys_nv_item_init(const uint8_t *d, uint8_t len)
{
	uint8_t status;

	if (len < 5U || len < 5U + d[4]) {
		znp_srsp_status(ZNP_SUBSYS_SYS, 0x07U, ZNP_STATUS_INVALID_PARAMETER);
		return;
	}

	status = znp_nv_item_init(znp_get_u16(d), znp_get_u16(&d[2]), &d[5], d[4]);
	znp_srsp_status(ZNP_SUBSYS_SYS, 0x07U, status);
}

static void nv_read_rsp(uint8_t cmd, uint16_t id, uint16_t offset)
{
	uint8_t rsp[2U + ZNP_NV_READ_CHUNK];
	size_t n = 0U;
	uint8_t status;

	znp_nv_sync();
	status = znp_nv_read(id, offset, &rsp[2], ZNP_NV_READ_CHUNK, &n);
	rsp[0] = status;
	rsp[1] = (uint8_t)n;
	znp_srsp(ZNP_SUBSYS_SYS, cmd, rsp, 2U + n);
}

static void sys_nv_read(const uint8_t *d, uint8_t len)
{
	if (len < 3U) {
		znp_srsp_status(ZNP_SUBSYS_SYS, 0x08U, ZNP_STATUS_INVALID_PARAMETER);
		return;
	}

	nv_read_rsp(0x08U, znp_get_u16(d), d[2]);
}

static void sys_nv_read_ext(const uint8_t *d, uint8_t len)
{
	if (len < 4U) {
		znp_srsp_status(ZNP_SUBSYS_SYS, 0x1CU, ZNP_STATUS_INVALID_PARAMETER);
		return;
	}

	nv_read_rsp(0x1CU, znp_get_u16(d), znp_get_u16(&d[2]));
}

static void sys_nv_write(const uint8_t *d, uint8_t len)
{
	if (len < 4U || len < 4U + d[3]) {
		znp_srsp_status(ZNP_SUBSYS_SYS, 0x09U, ZNP_STATUS_INVALID_PARAMETER);
		return;
	}

	znp_net_note_write(znp_get_u16(d));
	znp_srsp_status(ZNP_SUBSYS_SYS, 0x09U,
			znp_nv_write(znp_get_u16(d), d[2], &d[4], d[3]));
}

static void sys_nv_write_ext(const uint8_t *d, uint8_t len)
{
	uint16_t n;

	if (len < 6U) {
		znp_srsp_status(ZNP_SUBSYS_SYS, 0x1DU, ZNP_STATUS_INVALID_PARAMETER);
		return;
	}

	n = znp_get_u16(&d[4]);
	if (len < 6U + n) {
		znp_srsp_status(ZNP_SUBSYS_SYS, 0x1DU, ZNP_STATUS_INVALID_PARAMETER);
		return;
	}

	znp_net_note_write(znp_get_u16(d));
	znp_srsp_status(ZNP_SUBSYS_SYS, 0x1DU,
			znp_nv_write(znp_get_u16(d), znp_get_u16(&d[2]), &d[6], n));
}

static void sys_nv_delete(const uint8_t *d, uint8_t len)
{
	if (len < 4U) {
		znp_srsp_status(ZNP_SUBSYS_SYS, 0x12U, ZNP_STATUS_INVALID_PARAMETER);
		return;
	}

	znp_net_note_write(znp_get_u16(d));
	znp_srsp_status(ZNP_SUBSYS_SYS, 0x12U, znp_nv_delete(znp_get_u16(d)));
}

static void sys_nv_length(const uint8_t *d, uint8_t len)
{
	uint8_t rsp[2];

	if (len < 2U) {
		znp_srsp_status(ZNP_SUBSYS_SYS, 0x13U, ZNP_STATUS_INVALID_PARAMETER);
		return;
	}

	znp_nv_sync();
	(void)znp_put_u16(rsp, znp_nv_length(znp_get_u16(d)));
	znp_srsp(ZNP_SUBSYS_SYS, 0x13U, rsp, sizeof(rsp));
}

static void sys_set_tx_power(const uint8_t *d, uint8_t len)
{
	int8_t dbm;

	if (len < 1U) {
		znp_srsp_status(ZNP_SUBSYS_SYS, 0x14U, ZNP_STATUS_INVALID_PARAMETER);
		return;
	}

	/* Z-Stack 3.0 answers with the power it applied. */
	dbm = (int8_t)d[0];
	znp_srsp_status(ZNP_SUBSYS_SYS, 0x14U,
			(zb_radio_tx_power_dbm_set(dbm) == 0) ? (uint8_t)dbm : 0U);
}

/* UTIL */

static void util_get_device_info(const uint8_t *d, uint8_t len)
{
	uint8_t rsp[1 + 8 + 2 + 1 + 1 + 1 + 2 * ZNP_DEVICE_INFO_CHILDREN];
	uint16_t children[ZNP_DEVICE_INFO_CHILDREN];
	uint8_t *p = rsp;
	uint8_t n;

	ARG_UNUSED(d);
	ARG_UNUSED(len);

	/* A network the stack restored is up without the host starting it. */
	if (g_zbNwkCtx.joined && znp_dev_state == ZNP_DEV_STATE_HOLD) {
		znp_dev_state = ZNP_DEV_STATE_COORDINATOR;
	}

	*p++ = ZNP_STATUS_SUCCESS;
	zb_getLocalExtAddr(p);
	p += 8;
	p = znp_put_u16(p, zb_getLocalShortAddr());
	*p++ = ZNP_DEVICE_TYPE_COORDINATOR;
	*p++ = znp_dev_state;
	n = znp_tables_children(children, ZNP_DEVICE_INFO_CHILDREN);
	*p++ = n;
	for (uint8_t i = 0U; i < n; i++) {
		p = znp_put_u16(p, children[i]);
	}
	znp_srsp(ZNP_SUBSYS_UTIL, 0x00U, rsp, (size_t)(p - rsp));
}

static void util_get_nv_info(const uint8_t *d, uint8_t len)
{
	uint8_t rsp[1 + 8 + 4 + 2 + 1 + 16];
	uint8_t *p = rsp;
	size_t n;

	ARG_UNUSED(d);
	ARG_UNUSED(len);

	znp_nv_sync();
	*p++ = ZNP_STATUS_SUCCESS;
	zb_getLocalExtAddr(p);
	p += 8;
	(void)znp_nv_read(ZNP_NV_CHANLIST, 0U, p, 4U, &n);
	p += 4;
	(void)znp_nv_read(ZNP_NV_PANID, 0U, p, 2U, &n);
	p += 2;
	*p++ = 0U;
	(void)znp_nv_read(ZNP_NV_PRECFGKEY, 0U, p, 16U, &n);
	p += 16;
	znp_srsp(ZNP_SUBSYS_UTIL, 0x01U, rsp, (size_t)(p - rsp));
}

static void util_led_control(const uint8_t *d, uint8_t len)
{
	ARG_UNUSED(d);
	ARG_UNUSED(len);

	znp_srsp_status(ZNP_SUBSYS_UTIL, 0x0AU, ZNP_STATUS_SUCCESS);
}

static void util_time_alive(const uint8_t *d, uint8_t len)
{
	uint8_t rsp[4];

	ARG_UNUSED(d);
	ARG_UNUSED(len);

	(void)znp_put_u32(rsp, (uint32_t)(k_uptime_get() / 1000));
	znp_srsp(ZNP_SUBSYS_UTIL, 0x09U, rsp, sizeof(rsp));
}

/* APP_CNF */

static void znp_apply_network_config(void)
{
	/* The BDB must be initialized before its attributes are overridden. */
	(void)zb_platform_bdb_init_default();

	if (bdb_primary_channels != 0U) {
		g_bdbAttrs.primaryChannelSet = bdb_primary_channels;
		g_bdbAttrs.secondaryChannelSet = 0U;
	}

	znp_net_formation_config();
}

/*
 * Start the network, whichever way the host asked: a restored network is
 * announced as up, a new one is formed from the host's settings.
 */
uint8_t znp_network_start(uint8_t mode)
{
	if (g_zbNwkCtx.joined) {
		/* A formed network is not formed again, but takes the host's settings. */
		znp_net_apply_host();
		znp_state_change(ZNP_DEV_STATE_COORDINATOR);
		bdb_notify(BDB_STATUS_FORMATION_FAILED, mode, 0U);
		return ZNP_NETWORK_RESTORED;
	}

	znp_state_change(ZNP_DEV_STATE_STARTING);
	/* The formation is not over while a mode remains. */
	bdb_notify(BDB_STATUS_IN_PROGRESS, mode, mode);

	znp_apply_network_config();
	formation_pending = true;
	if (zb_platform_bdb_network_formation_start() != 0U) {
		formation_pending = false;
		znp_state_change(ZNP_DEV_STATE_HOLD);
		bdb_notify(BDB_STATUS_FORMATION_FAILED, mode, 0U);
		return ZNP_NETWORK_NOT_STARTED;
	}

	return ZNP_NETWORK_NEW;
}

static void app_cnf_start_commissioning(const uint8_t *d, uint8_t len)
{
	uint8_t mode = (len >= 1U) ? d[0] : 0U;

	znp_srsp_status(ZNP_SUBSYS_APP_CNF, 0x05U, ZNP_STATUS_SUCCESS);

	if ((mode & BDB_MODE_NWK_FORMATION) == 0U) {
		bdb_notify(BDB_STATUS_SUCCESS, mode, 0U);
		return;
	}

	(void)znp_network_start(mode);
}

static void app_cnf_set_channel(const uint8_t *d, uint8_t len)
{
	if (len < 5U) {
		znp_srsp_status(ZNP_SUBSYS_APP_CNF, 0x08U, ZNP_STATUS_INVALID_PARAMETER);
		return;
	}

	if (d[0] != 0U) {
		bdb_primary_channels = (uint32_t)znp_get_u16(&d[1]) |
				       ((uint32_t)znp_get_u16(&d[3]) << 16);
	}
	znp_srsp_status(ZNP_SUBSYS_APP_CNF, 0x08U, ZNP_STATUS_SUCCESS);
}

static void app_cnf_ok(uint8_t cmd)
{
	znp_srsp_status(ZNP_SUBSYS_APP_CNF, cmd, ZNP_STATUS_SUCCESS);
}

static void app_cnf_default_ok(const uint8_t *d, uint8_t len)
{
	ARG_UNUSED(d);
	ARG_UNUSED(len);

	/* Settings this port has no use for are accepted and ignored. */
	app_cnf_ok(0x01U);
}

static void app_cnf_allow_rejoin(const uint8_t *d, uint8_t len)
{
	ARG_UNUSED(d);
	ARG_UNUSED(len);

	app_cnf_ok(0x03U);
}

static void app_cnf_set_centralized_key(const uint8_t *d, uint8_t len)
{
	ARG_UNUSED(d);
	ARG_UNUSED(len);

	app_cnf_ok(0x07U);
}

static void app_cnf_tc_key_exchange(const uint8_t *d, uint8_t len)
{
	ARG_UNUSED(d);
	ARG_UNUSED(len);

	app_cnf_ok(0x09U);
}

static void app_cnf_set_frame_counter(const uint8_t *d, uint8_t len)
{
	ARG_UNUSED(d);
	ARG_UNUSED(len);

	app_cnf_ok(0xFFU);
}

/*
 * Pre-install the link key of a device that joins with an install code. The
 * code arrives with its CRC (format 1) or already turned into the key that
 * the trust center uses (format 2).
 */
static void app_cnf_add_install_code(const uint8_t *d, uint8_t len)
{
	ss_dev_pair_set_t pair;
	const uint8_t *code = &d[9];
	size_t code_len;

	if (len < 9U + SEC_KEY_LEN) {
		znp_srsp_status(ZNP_SUBSYS_APP_CNF, 0x04U, ZNP_STATUS_INVALID_PARAMETER);
		return;
	}
	code_len = len - 9U;

	if (d[0] == ZNP_INSTALL_CODE_WITH_CRC && code_len == SEC_KEY_LEN + 2U) {
		if (tl_bdbInstallCodeCRC16((u8 *)code, SEC_KEY_LEN) !=
		    znp_get_u16(&code[SEC_KEY_LEN])) {
			znp_srsp_status(ZNP_SUBSYS_APP_CNF, 0x04U, ZNP_STATUS_INVALID_PARAMETER);
			return;
		}

		bdb_preInstallCodeAdd((u8 *)&d[1], (u8 *)code);
	} else if (d[0] == ZNP_INSTALL_CODE_DERIVED_KEY && code_len == SEC_KEY_LEN) {
		/* A device keeps one key, so a new one replaces the old. */
		(void)ss_devKeyPairDelete((u8 *)&d[1]);

		memset(&pair, 0, sizeof(pair));
		memcpy(pair.device_address, &d[1], 8U);
		memcpy(pair.linkKey, code, SEC_KEY_LEN);
		pair.apsLinkKeyType = SS_UNIQUE_LINK_KEY;
		pair.keyAttr = SS_UNVERIFIED_KEY;
		ss_devKeyPairSave(&pair);
	} else {
		znp_srsp_status(ZNP_SUBSYS_APP_CNF, 0x04U, ZNP_STATUS_INVALID_PARAMETER);
		return;
	}

	znp_srsp_status(ZNP_SUBSYS_APP_CNF, 0x04U, ZNP_STATUS_SUCCESS);
}

/* While set, only a device with a pre-installed key may join. */
static void app_cnf_join_uses_install_code(const uint8_t *d, uint8_t len)
{
	if (len < 1U) {
		znp_srsp_status(ZNP_SUBSYS_APP_CNF, 0x06U, ZNP_STATUS_INVALID_PARAMETER);
		return;
	}

	g_bdbAttrs.joinUsesInstallCodeKey = (d[0] != 0U) ? 1U : 0U;
	znp_srsp_status(ZNP_SUBSYS_APP_CNF, 0x06U, ZNP_STATUS_SUCCESS);
}

static const struct znp_cmd sys_cmds[] = {
	{ZNP_SUBSYS_SYS, 0x00U, sys_reset_req},
	{ZNP_SUBSYS_SYS, 0x01U, sys_ping},
	{ZNP_SUBSYS_SYS, 0x02U, sys_version},
	{ZNP_SUBSYS_SYS, 0x04U, sys_get_ext_addr},
	{ZNP_SUBSYS_SYS, 0x07U, sys_nv_item_init},
	{ZNP_SUBSYS_SYS, 0x08U, sys_nv_read},
	{ZNP_SUBSYS_SYS, 0x09U, sys_nv_write},
	{ZNP_SUBSYS_SYS, 0x12U, sys_nv_delete},
	{ZNP_SUBSYS_SYS, 0x13U, sys_nv_length},
	{ZNP_SUBSYS_SYS, 0x14U, sys_set_tx_power},
	{ZNP_SUBSYS_SYS, 0x1CU, sys_nv_read_ext},
	{ZNP_SUBSYS_SYS, 0x1DU, sys_nv_write_ext},
	{ZNP_SUBSYS_UTIL, 0x00U, util_get_device_info},
	{ZNP_SUBSYS_UTIL, 0x01U, util_get_nv_info},
	{ZNP_SUBSYS_UTIL, 0x09U, util_time_alive},
	{ZNP_SUBSYS_UTIL, 0x0AU, util_led_control},
	{ZNP_SUBSYS_APP_CNF, 0x01U, app_cnf_default_ok},
	{ZNP_SUBSYS_APP_CNF, 0x03U, app_cnf_allow_rejoin},
	{ZNP_SUBSYS_APP_CNF, 0x04U, app_cnf_add_install_code},
	{ZNP_SUBSYS_APP_CNF, 0x05U, app_cnf_start_commissioning},
	{ZNP_SUBSYS_APP_CNF, 0x06U, app_cnf_join_uses_install_code},
	{ZNP_SUBSYS_APP_CNF, 0x07U, app_cnf_set_centralized_key},
	{ZNP_SUBSYS_APP_CNF, 0x08U, app_cnf_set_channel},
	{ZNP_SUBSYS_APP_CNF, 0x09U, app_cnf_tc_key_exchange},
	{ZNP_SUBSYS_APP_CNF, 0xFFU, app_cnf_set_frame_counter},
};

bool znp_sys_handle(uint8_t subsys, uint8_t cmd, const uint8_t *data, uint8_t len)
{
	return znp_cmd_dispatch(sys_cmds, ARRAY_SIZE(sys_cmds), subsys, cmd, data, len);
}

void zb_znp_bdb_commissioning_status(uint8_t status, bool joined_network)
{
	uint8_t one = 1U;

	if (!formation_pending) {
		return;
	}

	if (joined_network) {
		formation_pending = false;
		(void)znp_nv_write(ZNP_NV_BDBNODEISONANETWORK, 0U, &one, 1U);
		zb_info_save(NULL);
		znp_state_change(ZNP_DEV_STATE_COORDINATOR);
		bdb_notify(BDB_STATUS_SUCCESS, BDB_MODE_NWK_FORMATION, 0U);
	} else if (status != 0U) {
		formation_pending = false;
		znp_state_change(ZNP_DEV_STATE_HOLD);
		bdb_notify(BDB_STATUS_FORMATION_FAILED, BDB_MODE_NWK_FORMATION, 0U);
	}
}

bool zb_znp_host_controls_commissioning(void)
{
	return true;
}

/* Called once the UART is attached, so the host sees the usual power-up indication. */
void znp_sys_start(void)
{
	/* The host's items survive a restart; items a newer build adds are created empty. */
	if (znp_persist_load()) {
		LOG_INF("znp: restored the saved NV items");
	}
	znp_nv_seed_missing();
	znp_dev_state = ZNP_DEV_STATE_HOLD;
	znp_reset_ind(0x00U);
}
