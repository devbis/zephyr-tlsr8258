/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */
/* EZSP frame header and the commands that need no stack: version and invalidCommand. */
#include <string.h>

#include <zephyr/sys/util.h>

#include "zb_ezsp_frame.h"

static const struct ezsp_cmd_set *const cmd_sets[] = {
	&ezsp_core_cmds,
#ifdef CONFIG_ZIGBEE_EZSP_NCP
	&ezsp_net_cmds,
	&ezsp_aps_cmds,
#endif
};

void ezsp_ctx_reset(struct ezsp_ctx *c)
{
	struct ezsp_session *session = c->session;

	memset(c, 0, sizeof(*c));
	c->session = session;
	ezsp_core_defaults(c);
}

static ezsp_cmd_fn cmd_find(uint16_t id)
{
	for (size_t s = 0; s < ARRAY_SIZE(cmd_sets); s++) {
		for (size_t i = 0; i < cmd_sets[s]->count; i++) {
			if (cmd_sets[s]->cmds[i].id == id) {
				return cmd_sets[s]->cmds[i].fn;
			}
		}
	}

	return NULL;
}

static size_t invalid_command(const struct ezsp_frame *f, uint8_t status, uint8_t *out,
			      size_t out_size)
{
	return ezsp_frame_response(out, out_size, f, EZSP_ID_INVALID_COMMAND, &status, 1U);
}

bool ezsp_frame_parse(const struct ezsp_ctx *c, const uint8_t *data, size_t len,
		      struct ezsp_frame *f)
{
	bool extended = c->extended;

	/* A version command in the extended format: header, id 0 and the desired version */
	if (!extended && len == (EZSP_EXT_HEADER_LEN + 1U) && data[2] == EZSP_FC_HI_EXTENDED &&
	    data[3] == 0U && data[4] == 0U) {
		extended = true;
	}

	if (len < (extended ? EZSP_EXT_HEADER_LEN : EZSP_LEG_HEADER_LEN)) {
		return false;
	}

	f->seq = data[0];
	f->fc_lo = data[1];
	f->extended = extended;
	if (extended) {
		f->fc_hi = data[2];
		f->id = (uint16_t)(data[3] | ((uint16_t)data[4] << 8));
		f->params = &data[EZSP_EXT_HEADER_LEN];
		f->params_len = len - EZSP_EXT_HEADER_LEN;
	} else {
		f->fc_hi = 0U;
		f->id = data[2];
		f->params = &data[EZSP_LEG_HEADER_LEN];
		f->params_len = len - EZSP_LEG_HEADER_LEN;
	}

	return true;
}

size_t ezsp_frame_header(uint8_t *out, const struct ezsp_frame *cmd, uint16_t id)
{
	out[0] = cmd->seq;
	/* The network index of the command comes back; nothing else of its control byte does */
	out[1] = (uint8_t)(EZSP_FC_RESPONSE |
			   (cmd->fc_lo & (EZSP_FC_NETWORK_MASK << EZSP_FC_NETWORK_SHIFT)));
	if (cmd->extended) {
		out[2] = EZSP_FC_HI_EXTENDED;
		out[3] = (uint8_t)(id & 0xFFU);
		out[4] = (uint8_t)(id >> 8);
		return EZSP_EXT_HEADER_LEN;
	}

	out[2] = (uint8_t)id;

	return EZSP_LEG_HEADER_LEN;
}

size_t ezsp_frame_response(uint8_t *out, size_t out_size, const struct ezsp_frame *cmd, uint16_t id,
			   const uint8_t *params, size_t params_len)
{
	size_t header = cmd->extended ? EZSP_EXT_HEADER_LEN : EZSP_LEG_HEADER_LEN;

	if (header + params_len > out_size) {
		return 0U;
	}

	(void)ezsp_frame_header(out, cmd, id);
	if (params_len > 0U) {
		memcpy(&out[header], params, params_len);
	}

	return header + params_len;
}

size_t ezsp_handle(struct ezsp_ctx *c, const uint8_t *data, size_t len, uint8_t *out,
		   size_t out_size)
{
	struct ezsp_frame f;
	ezsp_cmd_fn fn;

	if (!ezsp_frame_parse(c, data, len, &f)) {
		return 0U;
	}

	c->last_seq = f.seq;

	if (f.id == EZSP_ID_VERSION) {
		uint8_t rsp[4] = {EZSP_PROTOCOL_VERSION, EZSP_STACK_TYPE_MESH,
				  (uint8_t)(EZSP_STACK_VERSION & 0xFFU),
				  (uint8_t)(EZSP_STACK_VERSION >> 8)};
		size_t n;

		/*
		 * The version the host wants is not needed to answer. homed sends its first request
		 * without it, and the NCP reports its own version whatever it is.
		 */
		uint8_t desired = (f.params_len >= 1U) ? f.params[0] : 0U;

		n = ezsp_frame_response(out, out_size, &f, EZSP_ID_VERSION, rsp, sizeof(rsp));
		/* The host answers in the extended format if it knows it and so do we */
		if (n > 0U && desired >= EZSP_EXTENDED_MIN_VER) {
			c->extended = true;
		}
		return n;
	}

	fn = cmd_find(f.id);
	if (fn == NULL) {
		return invalid_command(&f, EZSP_ERROR_INVALID_FRAME_ID, out, out_size);
	}

	{
		size_t header = f.extended ? EZSP_EXT_HEADER_LEN : EZSP_LEG_HEADER_LEN;
		struct ezsp_rd in;
		struct ezsp_wr wr;
		size_t n;

		if (header > out_size) {
			return 0U;
		}
		ezsp_rd_init(&in, f.params, f.params_len);
		ezsp_wr_init(&wr, &out[header], out_size - header);
		fn(c, &in, &wr);
		if (in.err) {
			return invalid_command(&f, EZSP_ERROR_TRUNCATED, out, out_size);
		}
		if (wr.err) {
			return invalid_command(&f, EZSP_ERROR_OVERFLOW, out, out_size);
		}
		/* The parameters are in place; the header goes in front of them */
		n = ezsp_frame_header(out, &f, f.id);
		return n + wr.len;
	}
}
