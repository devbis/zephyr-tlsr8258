/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * EZSP commands that need nothing from the stack: nop, echo, the configuration, value and policy
 * stores, the manufacturer code. A host writes a long list of configuration values and policies
 * on start-up and reads some back; they are kept here so that the host sees what it set. The
 * stack takes what it needs from them where it uses them.
 */
#include <zephyr/sys/util.h>

#include "zb_ezsp_frame.h"

static void cmd_nop(struct ezsp_ctx *c, struct ezsp_rd *in, struct ezsp_wr *out)
{
	ARG_UNUSED(c);
	ARG_UNUSED(in);
	ARG_UNUSED(out);
}

static void cmd_echo(struct ezsp_ctx *c, struct ezsp_rd *in, struct ezsp_wr *out)
{
	uint8_t len = ezsp_rd_u8(in);
	const uint8_t *data = ezsp_rd_bytes(in, len);

	ARG_UNUSED(c);
	ezsp_wr_u8(out, len);
	if (data != NULL) {
		ezsp_wr_bytes(out, data, len);
	}
}

static void cmd_set_manufacturer_code(struct ezsp_ctx *c, struct ezsp_rd *in, struct ezsp_wr *out)
{
	uint16_t code = ezsp_rd_u16(in);

	ARG_UNUSED(out);
	if (!in->err) {
		c->manufacturer_code = code;
	}
}

static int cfg_find(const struct ezsp_ctx *c, uint8_t id)
{
	for (uint8_t i = 0; i < c->cfg_count; i++) {
		if (c->cfg[i].id == id) {
			return i;
		}
	}

	return -1;
}

static void cmd_get_config(struct ezsp_ctx *c, struct ezsp_rd *in, struct ezsp_wr *out)
{
	uint8_t id = ezsp_rd_u8(in);
	int i = cfg_find(c, id);

	if (i < 0) {
		ezsp_wr_u8(out, EZSP_ERROR_INVALID_ID);
		ezsp_wr_u16(out, 0U);
		return;
	}

	ezsp_wr_u8(out, EZSP_SUCCESS);
	ezsp_wr_u16(out, c->cfg[i].value);
}

static void cmd_set_config(struct ezsp_ctx *c, struct ezsp_rd *in, struct ezsp_wr *out)
{
	uint8_t id = ezsp_rd_u8(in);
	uint16_t value = ezsp_rd_u16(in);
	int i = cfg_find(c, id);

	/* A command that is too short changes nothing */
	if (in->err) {
		return;
	}

	if (i < 0) {
		if (c->cfg_count >= EZSP_CFG_MAX) {
			ezsp_wr_u8(out, EZSP_ERROR_OUT_OF_MEMORY);
			return;
		}
		i = c->cfg_count++;
		c->cfg[i].id = id;
	}

	c->cfg[i].value = value;
	ezsp_wr_u8(out, EZSP_SUCCESS);
}

static int val_find(const struct ezsp_ctx *c, uint8_t id)
{
	for (uint8_t i = 0; i < c->val_count; i++) {
		if (c->val[i].id == id) {
			return i;
		}
	}

	return -1;
}

static void cmd_get_value(struct ezsp_ctx *c, struct ezsp_rd *in, struct ezsp_wr *out)
{
	uint8_t id = ezsp_rd_u8(in);
	int i = val_find(c, id);

	if (i < 0) {
		ezsp_wr_u8(out, EZSP_ERROR_INVALID_ID);
		ezsp_wr_u8(out, 0U);
		return;
	}

	ezsp_wr_u8(out, EZSP_SUCCESS);
	ezsp_wr_u8(out, c->val[i].len);
	ezsp_wr_bytes(out, c->val[i].data, c->val[i].len);
}

static void cmd_set_value(struct ezsp_ctx *c, struct ezsp_rd *in, struct ezsp_wr *out)
{
	uint8_t id = ezsp_rd_u8(in);
	uint8_t len = ezsp_rd_u8(in);
	const uint8_t *data = ezsp_rd_bytes(in, len);
	int i;

	if (in->err) {
		return;
	}

	if (len > EZSP_VALUE_LEN) {
		ezsp_wr_u8(out, EZSP_ERROR_INVALID_VALUE);
		return;
	}

	i = val_find(c, id);
	if (i < 0) {
		if (c->val_count >= EZSP_VALUE_MAX) {
			ezsp_wr_u8(out, EZSP_ERROR_OUT_OF_MEMORY);
			return;
		}
		i = c->val_count++;
		c->val[i].id = id;
	}

	c->val[i].len = len;
	memcpy(c->val[i].data, data, len);
	ezsp_wr_u8(out, EZSP_SUCCESS);
}

static void cmd_get_extended_value(struct ezsp_ctx *c, struct ezsp_rd *in, struct ezsp_wr *out)
{
	ARG_UNUSED(c);
	(void)ezsp_rd_u8(in);
	(void)ezsp_rd_u32(in);
	/* No extended value is kept */
	ezsp_wr_u8(out, EZSP_ERROR_INVALID_ID);
	ezsp_wr_u8(out, 0U);
}

static void cmd_get_policy(struct ezsp_ctx *c, struct ezsp_rd *in, struct ezsp_wr *out)
{
	uint8_t id = ezsp_rd_u8(in);

	if (id >= EZSP_POLICY_MAX) {
		ezsp_wr_u8(out, EZSP_ERROR_INVALID_ID);
		ezsp_wr_u8(out, 0U);
		return;
	}

	ezsp_wr_u8(out, EZSP_SUCCESS);
	ezsp_wr_u8(out, c->policy[id]);
}

static void cmd_set_policy(struct ezsp_ctx *c, struct ezsp_rd *in, struct ezsp_wr *out)
{
	uint8_t id = ezsp_rd_u8(in);
	uint8_t decision = ezsp_rd_u8(in);

	if (in->err) {
		return;
	}

	if (id >= EZSP_POLICY_MAX) {
		ezsp_wr_u8(out, EZSP_ERROR_INVALID_ID);
		return;
	}

	c->policy[id] = decision;
	ezsp_wr_u8(out, EZSP_SUCCESS);
}

/* Configuration IDs of UG100 whose values a host reads at start-up. */
#define EZSP_CFG_PACKET_BUFFER_COUNT 0x01U
#define EZSP_CFG_ADDRESS_TABLE_SIZE  0x05U
#define EZSP_CFG_MULTICAST_TABLE_SIZE 0x06U
#define EZSP_CFG_STACK_PROFILE       0x0cU
#define EZSP_CFG_SECURITY_LEVEL      0x0dU
#define EZSP_CFG_TC_ADDRESS_CACHE    0x19U
#define EZSP_CFG_SUPPORTED_NETWORKS  0x2dU

void ezsp_core_defaults(struct ezsp_ctx *c)
{
	static const struct {
		uint8_t id;
		uint16_t value;
	} defaults[] = {
		{EZSP_CFG_PACKET_BUFFER_COUNT, 36U},
		{EZSP_CFG_ADDRESS_TABLE_SIZE, 16U},
		{EZSP_CFG_MULTICAST_TABLE_SIZE, 16U},
		{EZSP_CFG_STACK_PROFILE, 2U},
		{EZSP_CFG_SECURITY_LEVEL, 5U},
		{EZSP_CFG_TC_ADDRESS_CACHE, 2U},
		{EZSP_CFG_SUPPORTED_NETWORKS, 1U},
	};

	for (size_t i = 0; i < ARRAY_SIZE(defaults); i++) {
		c->cfg[c->cfg_count].id = defaults[i].id;
		c->cfg[c->cfg_count].value = defaults[i].value;
		c->cfg_count++;
	}
}

static const struct ezsp_cmd core_cmds[] = {
	{EZSP_ID_NOP, cmd_nop},
	{EZSP_ID_ECHO, cmd_echo},
	{EZSP_ID_SET_MANUFACTURER_CODE, cmd_set_manufacturer_code},
	{EZSP_ID_GET_CONFIGURATION_VALUE, cmd_get_config},
	{EZSP_ID_SET_CONFIGURATION_VALUE, cmd_set_config},
	{EZSP_ID_GET_VALUE, cmd_get_value},
	{EZSP_ID_SET_VALUE, cmd_set_value},
	{EZSP_ID_GET_EXTENDED_VALUE, cmd_get_extended_value},
	{EZSP_ID_GET_POLICY, cmd_get_policy},
	{EZSP_ID_SET_POLICY, cmd_set_policy},
};

const struct ezsp_cmd_set ezsp_core_cmds = {
	.cmds = core_cmds,
	.count = ARRAY_SIZE(core_cmds),
};
