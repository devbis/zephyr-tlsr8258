/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/sys/util.h>

#define ZNP_SOF            0xFEU
#define ZNP_MAX_DATA       250U

/* Command types, bits 7:5 of the first command byte. */
#define ZNP_TYPE_SREQ      1U
#define ZNP_TYPE_AREQ      2U
#define ZNP_TYPE_SRSP      3U

/* Subsystems, bits 4:0 of the first command byte. */
#define ZNP_SUBSYS_RPC_ERR 0x00U
#define ZNP_SUBSYS_SYS     0x01U
#define ZNP_SUBSYS_AF      0x04U
#define ZNP_SUBSYS_ZDO     0x05U
#define ZNP_SUBSYS_UTIL    0x07U
#define ZNP_SUBSYS_APP     0x09U
#define ZNP_SUBSYS_APP_CNF 0x0FU

/* Status codes shared by the SRSPs. */
#define ZNP_STATUS_SUCCESS           0x00U
#define ZNP_STATUS_FAILURE           0x01U
#define ZNP_STATUS_INVALID_PARAMETER 0x02U
#define ZNP_STATUS_NV_ITEM_UNINIT    0x09U
#define ZNP_STATUS_NV_OPER_FAILED    0x0AU
#define ZNP_STATUS_NV_BAD_ITEM_LEN   0x0CU
#define ZNP_STATUS_MEM_ERROR         0x10U
#define ZNP_STATUS_BUFFER_FULL       0x11U

/* RPC error codes of the SRSP sent for an unknown request. */
#define ZNP_RPC_INVALID_SUBSYSTEM    0x01U
#define ZNP_RPC_INVALID_COMMAND      0x02U
#define ZNP_RPC_INVALID_PARAMETER    0x03U
#define ZNP_RPC_INVALID_LENGTH       0x04U

/* Z-Stack product and version reported to the host. */
#define ZNP_TRANSPORT_REV  0x02U
/* 2 is Z-Stack 3.0.x, which keeps its tables in plain OSAL NV items; 1 is 3.30+. */
#define ZNP_PRODUCT_ID     0x02U
#define ZNP_VERSION_MAJOR  0x02U
#define ZNP_VERSION_MINOR  0x07U
#define ZNP_VERSION_MAINT  0x02U
#define ZNP_CODE_REVISION  20210708UL

/* Z-Stack device states (ZDO_STATE_CHANGE_IND). */
#define ZNP_DEV_STATE_HOLD             0x00U
#define ZNP_DEV_STATE_INIT             0x01U
#define ZNP_DEV_STATE_STARTING         0x08U
#define ZNP_DEV_STATE_COORDINATOR      0x09U

static inline uint16_t znp_get_u16(const uint8_t *p)
{
	return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static inline uint32_t znp_get_u32(const uint8_t *p)
{
	return (uint32_t)znp_get_u16(p) | ((uint32_t)znp_get_u16(&p[2]) << 16);
}

static inline uint8_t *znp_put_u16(uint8_t *p, uint16_t v)
{
	p[0] = (uint8_t)(v & 0xffU);
	p[1] = (uint8_t)(v >> 8);
	return p + 2;
}

static inline uint8_t *znp_put_u32(uint8_t *p, uint32_t v)
{
	p = znp_put_u16(p, (uint16_t)(v & 0xffffU));
	return znp_put_u16(p, (uint16_t)(v >> 16));
}

/* Framing (zb_znp_frame.c). */
enum znp_rx_state {
	ZNP_RX_SOF,
	ZNP_RX_LEN,
	ZNP_RX_CMD0,
	ZNP_RX_CMD1,
	ZNP_RX_DATA,
	ZNP_RX_FCS,
};

struct znp_parser {
	enum znp_rx_state state;
	uint8_t len;
	uint8_t cmd0;
	uint8_t cmd1;
	uint8_t pos;
	uint8_t fcs;
	uint8_t data[ZNP_MAX_DATA];
};

enum znp_feed_result {
	ZNP_FEED_MORE,
	ZNP_FEED_FRAME,
	ZNP_FEED_BAD_FCS,
};

void znp_parser_reset(struct znp_parser *p);
/* On ZNP_FEED_FRAME, cmd0, cmd1, data and len of @p p describe the frame. */
enum znp_feed_result znp_parser_feed(struct znp_parser *p, uint8_t byte);
/* @p out holds ZNP_MAX_DATA + 5 bytes at most; returns the frame length. */
size_t znp_frame_encode(uint8_t *out, uint8_t type, uint8_t subsys, uint8_t cmd,
			const uint8_t *data, size_t len);

/* Transport (zb_znp_uart.c). */
void znp_send(uint8_t type, uint8_t subsys, uint8_t cmd, const uint8_t *data, size_t len);

static inline void znp_srsp(uint8_t subsys, uint8_t cmd, const uint8_t *data, size_t len)
{
	znp_send(ZNP_TYPE_SRSP, subsys, cmd, data, len);
}

static inline void znp_srsp_status(uint8_t subsys, uint8_t cmd, uint8_t status)
{
	znp_srsp(subsys, cmd, &status, 1U);
}

static inline void znp_areq(uint8_t subsys, uint8_t cmd, const uint8_t *data, size_t len)
{
	znp_send(ZNP_TYPE_AREQ, subsys, cmd, data, len);
}

/*
 * A command handler. @p data is the request payload. The handler answers an
 * SREQ with exactly one SRSP and may send AREQs afterwards.
 */
struct znp_cmd {
	uint8_t subsys;
	uint8_t cmd;
	void (*fn)(const uint8_t *data, uint8_t len);
};

/* Each module looks the request up in its table; false means not handled. */
bool znp_sys_handle(uint8_t subsys, uint8_t cmd, const uint8_t *data, uint8_t len);
bool znp_zdo_handle(uint8_t subsys, uint8_t cmd, const uint8_t *data, uint8_t len);
bool znp_tables_handle(uint8_t subsys, uint8_t cmd, const uint8_t *data, uint8_t len);
uint8_t znp_tables_children(uint16_t *out, uint8_t max);
void znp_tables_sync(void);
void znp_tables_nv_written(uint16_t id);

/* The unique link key of a device that a seed and shift stand for (zb_znp_keys.c). */
void znp_tclk_key(const uint8_t seed[16], uint8_t shift, const uint8_t ieee[8], uint8_t key[16]);
bool znp_af_handle(uint8_t subsys, uint8_t cmd, const uint8_t *data, uint8_t len);

static inline bool znp_cmd_dispatch(const struct znp_cmd *table, size_t count, uint8_t subsys,
				    uint8_t cmd, const uint8_t *data, uint8_t len)
{
	for (size_t i = 0; i < count; i++) {
		if (table[i].subsys == subsys && table[i].cmd == cmd) {
			table[i].fn(data, len);
			return true;
		}
	}

	return false;
}

/* NV items (zb_znp_nv.c). */
uint16_t znp_nv_length(uint16_t id);
uint8_t znp_nv_item_init(uint16_t id, uint16_t len, const uint8_t *init, uint8_t init_len);
uint8_t znp_nv_read(uint16_t id, uint16_t offset, uint8_t *out, size_t max, size_t *out_len);
uint8_t znp_nv_write(uint16_t id, uint16_t offset, const uint8_t *data, size_t len);
uint8_t znp_nv_delete(uint16_t id);
void znp_nv_reset(void);
#define ZNP_NV_EXPORT_MAX 4096U

/* Counts changes the host made to items that are worth keeping. */
uint32_t znp_nv_generation(void);
size_t znp_nv_export(uint8_t *buf, size_t max);
bool znp_nv_import(const uint8_t *buf, size_t len);
/* Store a stack-owned value, creating or resizing the item; not a host change. */
uint8_t znp_nv_set_derived(uint16_t id, const uint8_t *data, size_t len);

/* Flash copy of the NV items (zb_znp_persist.c). */
bool znp_persist_load(void);
void znp_persist_service(void);
void znp_persist_flush(void);

/* Item IDs the stack glue reads or writes itself. */
#define ZNP_NV_STARTUP_OPTION      0x0003U
#define ZNP_NV_LOGICAL_TYPE        0x0087U
#define ZNP_NV_PANID               0x0083U
#define ZNP_NV_CHANLIST            0x0084U
#define ZNP_NV_PRECFGKEY           0x0062U
#define ZNP_NV_PRECFGKEYS_ENABLE   0x0063U
#define ZNP_NV_EXTADDR             0x0001U
#define ZNP_NV_EXTENDED_PAN_ID     0x002DU
#define ZNP_NV_APS_USE_EXT_PANID   0x0047U
#define ZNP_NV_NIB                 0x0021U
#define ZNP_NV_NWK_ACTIVE_KEY_INFO 0x003AU
#define ZNP_NV_NWK_ALTERN_KEY_INFO 0x003BU
#define ZNP_NV_NWKKEY              0x0082U
#define ZNP_NV_BDBNODEISONANETWORK 0x0055U
#define ZNP_NV_CONCENTRATOR_ENABLE    0x0032U
#define ZNP_NV_CONCENTRATOR_DISCOVERY 0x0033U
#define ZNP_NV_ADDRMGR             0x0023U
#define ZNP_NV_APS_LINK_KEY_TABLE  0x004CU
#define ZNP_NV_NWK_SEC_MATERIAL_FIRST 0x0075U
#define ZNP_NV_NWK_SEC_MATERIAL_LAST  0x0078U

/* Layout of the tables a host restores. */
#define ZNP_ADDRMGR_ENTRIES        32U
#define ZNP_ADDRMGR_ENTRY_LEN      11U
#define ZNP_APS_LINK_KEY_TABLE_LEN (2U + 32U * 5U)
#define ZNP_NWK_SEC_MATERIAL_LEN   12U

/* Hashed trust center link keys: one seed, and a table of device entries. */
#define ZNP_NV_TCLK_SEED           0x0101U
#define ZNP_NV_TCLK_FIRST          0x0111U
#define ZNP_TCLK_ENTRIES           32U
#define ZNP_TCLK_ENTRY_LEN         19U
#define ZNP_TCLK_KEY_DEFAULT       0xFFU

/* State shared between the ZNP modules (zb_znp_sys.c). */
void znp_state_change(uint8_t state);
void znp_sys_start(void);

/* ZDO_STARTUP_FROM_APP states. */
#define ZNP_NETWORK_RESTORED    0U
#define ZNP_NETWORK_NEW         1U
#define ZNP_NETWORK_NOT_STARTED 2U

uint8_t znp_network_start(uint8_t mode);
void znp_routing_apply(void);
void znp_nv_seed_missing(void);
uint8_t znp_device_state(void);

/* Network settings as NV items (zb_znp_net.c). */
void znp_net_sync(void);
void znp_net_note_write(uint16_t id);
void znp_net_reset(void);
void znp_net_formation_config(void);
void znp_net_apply_host(void);
bool znp_all_ff(const uint8_t *p, size_t len);
