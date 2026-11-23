/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "zb_ezsp_proto.h"

/*
 * EZSP frames (Silicon Labs UG100), protocol version 13. A host starts in the legacy format, a
 * sequence number, one frame control byte and a one byte frame ID, to send the version command,
 * and goes on in the extended format, with a second frame control byte and a two byte frame ID,
 * once the NCP reported a version of 8 or more.
 */

#define EZSP_PROTOCOL_VERSION    13U
/* Stack type "mesh" and the stack version 7.4.4.0 that goes with protocol version 13. */
#define EZSP_STACK_TYPE_MESH     2U
#define EZSP_STACK_VERSION       0x7440U
/* First protocol version that has the extended format. */
#define EZSP_EXTENDED_MIN_VER    8U

/* Frame control byte, low. */
#define EZSP_FC_RESPONSE         0x80U
#define EZSP_FC_ASYNC_CALLBACK   0x10U
#define EZSP_FC_NETWORK_SHIFT    5U
#define EZSP_FC_NETWORK_MASK     0x03U
/* Frame control byte, high, of the extended format. */
#define EZSP_FC_HI_EXTENDED      0x01U

/* Frame IDs. */
#define EZSP_ID_VERSION          0x0000U
#define EZSP_ID_INVALID_COMMAND  0x0058U

/* Longest EZSP frame: the largest ASH data field. */
#define EZSP_MAX_FRAME           220U
/* Header of the extended format. */
#define EZSP_EXT_HEADER_LEN      5U
#define EZSP_LEG_HEADER_LEN      3U

struct ezsp_frame {
	uint8_t seq;
	uint8_t fc_lo;
	uint8_t fc_hi;
	uint16_t id;
	bool extended;
	const uint8_t *params;
	size_t params_len;
};

/* Configuration values and policies the host set; a reset takes them back to the defaults. */
#define EZSP_CFG_MAX     32U
#define EZSP_VALUE_MAX   8U
#define EZSP_VALUE_LEN   8U
#define EZSP_POLICY_MAX  13U

struct ezsp_session;

struct ezsp_ctx {
	/* The session this context belongs to; kept over a reset. */
	struct ezsp_session *session;
	/* Sequence number of the command being handled, or handled last. */
	uint8_t last_seq;
	/* Run once the response of the current command is queued, and then cleared. */
	void (*after)(struct ezsp_ctx *c);
	/* Whether the host went on to the extended format. */
	bool extended;
	struct {
		uint8_t id;
		uint16_t value;
	} cfg[EZSP_CFG_MAX];
	uint8_t cfg_count;
	struct {
		uint8_t id;
		uint8_t len;
		uint8_t data[EZSP_VALUE_LEN];
	} val[EZSP_VALUE_MAX];
	uint8_t val_count;
	uint8_t policy[EZSP_POLICY_MAX];
	uint16_t manufacturer_code;
};

/*
 * A command handler. It reads the parameters from @p in and writes the response parameters to
 * @p out; the header is added by ezsp_handle(). A command that is too short or a response that
 * does not fit is answered with invalidCommand.
 */
typedef void (*ezsp_cmd_fn)(struct ezsp_ctx *c, struct ezsp_rd *in, struct ezsp_wr *out);

struct ezsp_cmd {
	uint16_t id;
	ezsp_cmd_fn fn;
};

struct ezsp_cmd_set {
	const struct ezsp_cmd *cmds;
	size_t count;
};

/* The commands that need nothing from the stack: nop, echo, configuration, values and policies. */
extern const struct ezsp_cmd_set ezsp_core_cmds;
/* The commands that act on the stack (zb_ezsp_cmd_net.c); in the build with the stack only. */
extern const struct ezsp_cmd_set ezsp_net_cmds;

/* Seed the configuration values the host reads before it sets them. */
void ezsp_core_defaults(struct ezsp_ctx *c);

void ezsp_ctx_reset(struct ezsp_ctx *c);

/*
 * Split a command received from the host. The extended format is taken when the context is, or
 * when the frame is a version command in that format. Returns false for a frame that is too
 * short.
 */
bool ezsp_frame_parse(const struct ezsp_ctx *c, const uint8_t *data, size_t len,
		      struct ezsp_frame *f);

/* Write the header of a response to @p cmd with frame ID @p id; returns its length. */
size_t ezsp_frame_header(uint8_t *out, const struct ezsp_frame *cmd, uint16_t id);

/*
 * Write a response to command @p cmd, in the format of the command, into @p out of @p out_size
 * bytes, with frame ID @p id and the given parameters. Returns the length, or 0 when it does not
 * fit.
 */
size_t ezsp_frame_response(uint8_t *out, size_t out_size, const struct ezsp_frame *cmd, uint16_t id,
			   const uint8_t *params, size_t params_len);

/*
 * Handle one command and write the response to @p out. Returns the response length, or 0 when the
 * frame is malformed and is dropped.
 */
size_t ezsp_handle(struct ezsp_ctx *c, const uint8_t *data, size_t len, uint8_t *out,
		   size_t out_size);
