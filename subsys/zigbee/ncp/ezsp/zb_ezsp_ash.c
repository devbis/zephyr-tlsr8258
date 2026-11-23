/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */
/* ASH framing and link layer of the EZSP NCP interface, see zb_ezsp_ash.h. */
#include <errno.h>
#include <string.h>

#include <zephyr/sys/util.h>

#include "zb_ezsp_ash.h"

/* DATA: 0 frmNum(3) reTx(1) ackNum(3). ACK and NAK: 10x0 nRdy(1) ackNum(3). */
#define ASH_DATA_FRM_SHIFT 4U
#define ASH_DATA_RETX      BIT(3)
#define ASH_NUM_MASK       0x07U
#define ASH_CTRL_ACK       0x80U
#define ASH_CTRL_NAK       0xA0U

#define ASH_RAND_SEED      0x42U
#define ASH_RAND_POLY      0xB8U

uint16_t ash_crc16(const uint8_t *data, size_t len)
{
	uint16_t crc = 0xFFFFU;

	for (size_t i = 0; i < len; i++) {
		crc ^= (uint16_t)((uint16_t)data[i] << 8);
		for (uint8_t bit = 0; bit < 8U; bit++) {
			if ((crc & 0x8000U) != 0U) {
				crc = (uint16_t)((crc << 1) ^ 0x1021U);
			} else {
				crc = (uint16_t)(crc << 1);
			}
		}
	}

	return crc;
}

void ash_randomize(uint8_t *data, size_t len)
{
	uint8_t rnd = ASH_RAND_SEED;

	for (size_t i = 0; i < len; i++) {
		data[i] ^= rnd;
		if ((rnd & 1U) != 0U) {
			rnd = (uint8_t)((rnd >> 1) ^ ASH_RAND_POLY);
		} else {
			rnd >>= 1;
		}
	}
}

enum ash_frame_type ash_ctrl_type(uint8_t ctrl)
{
	if ((ctrl & 0x80U) == 0U) {
		return ASH_FRAME_DATA;
	}

	switch (ctrl & 0xE0U) {
	case ASH_CTRL_ACK:
		return ASH_FRAME_ACK;
	case ASH_CTRL_NAK:
		return ASH_FRAME_NAK;
	default:
		break;
	}

	switch (ctrl) {
	case ASH_CTRL_RST:
		return ASH_FRAME_RST;
	case ASH_CTRL_RSTACK:
		return ASH_FRAME_RSTACK;
	case ASH_CTRL_ERROR:
		return ASH_FRAME_ERROR;
	default:
		return ASH_FRAME_UNKNOWN;
	}
}

static bool ash_reserved(uint8_t b)
{
	return b == ASH_FLAG || b == ASH_ESCAPE || b == ASH_XON || b == ASH_XOFF ||
	       b == ASH_SUBSTITUTE || b == ASH_CANCEL;
}

size_t ash_frame_encode(uint8_t *out, size_t out_size, uint8_t ctrl, const uint8_t *data,
			size_t len)
{
	uint8_t raw[ASH_MAX_RAW];
	uint16_t crc;
	size_t raw_len;
	size_t n = 0U;

	if (len > ASH_MAX_DATA) {
		return 0U;
	}

	raw[0] = ctrl;
	if (len > 0U) {
		memcpy(&raw[1], data, len);
		if ((ctrl & 0x80U) == 0U) {
			ash_randomize(&raw[1], len);
		}
	}
	raw_len = 1U + len;
	crc = ash_crc16(raw, raw_len);
	raw[raw_len++] = (uint8_t)(crc >> 8);
	raw[raw_len++] = (uint8_t)(crc & 0xFFU);

	for (size_t i = 0; i < raw_len; i++) {
		if (ash_reserved(raw[i])) {
			if (n + 2U > out_size) {
				return 0U;
			}
			out[n++] = ASH_ESCAPE;
			out[n++] = raw[i] ^ 0x20U;
		} else {
			if (n + 1U > out_size) {
				return 0U;
			}
			out[n++] = raw[i];
		}
	}

	if (n + 1U > out_size) {
		return 0U;
	}
	out[n++] = ASH_FLAG;

	return n;
}

void ash_parser_reset(struct ash_parser *p)
{
	p->len = 0U;
	p->escape = false;
	p->error = false;
	p->done = false;
}

enum ash_feed_result ash_parser_feed(struct ash_parser *p, uint8_t byte)
{
	size_t len;
	bool error;

	if (p->done) {
		ash_parser_reset(p);
	}

	switch (byte) {
	case ASH_FLAG:
		len = p->len;
		error = p->error;
		p->len = 0U;
		p->escape = false;
		p->error = false;
		if (len == 0U && !error) {
			/* Back-to-back flags, or the flag after a cancel */
			return ASH_FEED_MORE;
		}
		if (error || len < 3U) {
			return ASH_FEED_BAD;
		}
		if (ash_crc16(p->buf, len) != 0U) {
			return ASH_FEED_BAD;
		}
		p->len = len;
		p->done = true;
		return ASH_FEED_FRAME;
	case ASH_CANCEL:
		/* Everything since the last flag is dropped, and so is the flag that ends it */
		p->len = 0U;
		p->escape = false;
		p->error = false;
		return ASH_FEED_MORE;
	case ASH_SUBSTITUTE:
		p->error = true;
		p->escape = false;
		return ASH_FEED_MORE;
	case ASH_XON:
	case ASH_XOFF:
		return ASH_FEED_MORE;
	case ASH_ESCAPE:
		p->escape = true;
		return ASH_FEED_MORE;
	default:
		break;
	}

	if (p->escape) {
		byte ^= 0x20U;
		p->escape = false;
	}

	if (p->len >= sizeof(p->buf)) {
		p->error = true;
	} else {
		p->buf[p->len++] = byte;
	}

	return ASH_FEED_MORE;
}

static void link_send_frame(struct ash_link *l, uint8_t ctrl, const uint8_t *data, size_t len)
{
	uint8_t out[ASH_MAX_ENCODED];
	size_t n = ash_frame_encode(out, sizeof(out), ctrl, data, len);

	if (n > 0U) {
		l->ops->tx(l->ctx, out, n);
	}
}

static void link_send_ack(struct ash_link *l)
{
	link_send_frame(l, (uint8_t)(ASH_CTRL_ACK | l->rx_seq), NULL, 0U);
}

static void link_send_nak(struct ash_link *l)
{
	link_send_frame(l, (uint8_t)(ASH_CTRL_NAK | l->rx_seq), NULL, 0U);
}

static void link_send_flight(struct ash_link *l, uint32_t now_ms, bool retx)
{
	uint8_t ctrl = (uint8_t)((l->tx_seq << ASH_DATA_FRM_SHIFT) | l->rx_seq);

	if (retx) {
		ctrl |= ASH_DATA_RETX;
	}
	link_send_frame(l, ctrl, l->flight, l->flight_len);
	l->sent_at = now_ms;
	/* The frame carries the ackNum of everything taken so far */
	l->ack_pending = false;
}

static void link_fail(struct ash_link *l, uint8_t code)
{
	uint8_t data[2] = {ASH_VERSION, code};

	link_send_frame(l, ASH_CTRL_ERROR, data, sizeof(data));
	l->connected = false;
	l->in_flight = false;
}

void ash_link_init(struct ash_link *l, const struct ash_link_ops *ops, void *ctx)
{
	memset(l, 0, sizeof(*l));
	l->ops = ops;
	l->ctx = ctx;
	ash_parser_reset(&l->parser);
}

void ash_link_start(struct ash_link *l, uint8_t reset_code)
{
	uint8_t data[2] = {ASH_VERSION, reset_code};

	l->connected = false;
	l->in_flight = false;
	link_send_frame(l, ASH_CTRL_RSTACK, data, sizeof(data));
}

/* The host acknowledges frames up to, not including, @p ack. */
static void link_ack(struct ash_link *l, uint8_t ack)
{
	if (l->in_flight && ack == ((l->tx_seq + 1U) & ASH_NUM_MASK)) {
		l->in_flight = false;
		l->retries = 0U;
		l->tx_seq = ack;
	}
}

static void link_data(struct ash_link *l, uint32_t now_ms, uint8_t ctrl, uint8_t *data,
		      size_t len)
{
	uint8_t frm = (ctrl >> ASH_DATA_FRM_SHIFT) & ASH_NUM_MASK;

	link_ack(l, ctrl & ASH_NUM_MASK);

	if (frm == l->rx_seq) {
		ash_randomize(data, len);
		l->rx_seq = (l->rx_seq + 1U) & ASH_NUM_MASK;
		/* A response to this frame, if there is one, carries the ACK */
		l->ack_pending = true;
		l->ack_due = now_ms + ASH_ACK_DELAY_MS;
		l->ops->rx_data(l->ctx, data, len);
	} else if (frm == ((l->rx_seq - 1U) & ASH_NUM_MASK) && (ctrl & ASH_DATA_RETX) != 0U) {
		/* A retransmission of a frame already taken: the ACK was lost */
		l->ack_pending = false;
		link_send_ack(l);
	} else {
		link_send_nak(l);
	}
}

static void link_frame(struct ash_link *l, uint32_t now_ms)
{
	uint8_t ctrl = l->parser.buf[0];
	uint8_t *data = &l->parser.buf[1];
	size_t len = l->parser.len - 3U;

	switch (ash_ctrl_type(ctrl)) {
	case ASH_FRAME_RST: {
		uint8_t rstack[2] = {ASH_VERSION, ASH_RESET_SOFTWARE};

		l->connected = true;
		l->rx_seq = 0U;
		l->tx_seq = 0U;
		l->in_flight = false;
		l->ack_pending = false;
		l->retries = 0U;
		link_send_frame(l, ASH_CTRL_RSTACK, rstack, sizeof(rstack));
		if (l->ops->reset != NULL) {
			l->ops->reset(l->ctx);
		}
		break;
	}
	case ASH_FRAME_DATA:
		if (l->connected) {
			link_data(l, now_ms, ctrl, data, len);
		}
		break;
	case ASH_FRAME_ACK:
		if (l->connected) {
			link_ack(l, ctrl & ASH_NUM_MASK);
		}
		break;
	case ASH_FRAME_NAK:
		/* The host still waits for the frame in flight */
		if (l->connected && l->in_flight && (ctrl & ASH_NUM_MASK) == l->tx_seq) {
			link_send_flight(l, now_ms, true);
		}
		break;
	case ASH_FRAME_ERROR:
		l->connected = false;
		l->in_flight = false;
		break;
	default:
		break;
	}
}

void ash_link_feed(struct ash_link *l, uint32_t now_ms, uint8_t byte)
{
	switch (ash_parser_feed(&l->parser, byte)) {
	case ASH_FEED_FRAME:
		link_frame(l, now_ms);
		break;
	case ASH_FEED_BAD:
		if (l->connected) {
			link_send_nak(l);
		}
		break;
	default:
		break;
	}
}

void ash_link_tick(struct ash_link *l, uint32_t now_ms)
{
	if (l->connected && l->ack_pending && (int32_t)(now_ms - l->ack_due) >= 0) {
		l->ack_pending = false;
		link_send_ack(l);
	}

	if (!l->connected || !l->in_flight || (now_ms - l->sent_at) < ASH_ACK_TIMEOUT_MS) {
		return;
	}

	if (l->retries >= ASH_MAX_RETRIES) {
		link_fail(l, ASH_ERROR_TIMEOUTS);
		return;
	}

	l->retries++;
	link_send_flight(l, now_ms, true);
}

bool ash_link_can_send(const struct ash_link *l)
{
	return l->connected && !l->in_flight;
}

int ash_link_send(struct ash_link *l, uint32_t now_ms, const uint8_t *data, size_t len)
{
	if (!l->connected || len > ASH_MAX_DATA) {
		return -ENOTCONN;
	}

	if (l->in_flight) {
		return -EBUSY;
	}

	memcpy(l->flight, data, len);
	l->flight_len = len;
	l->in_flight = true;
	l->retries = 0U;
	link_send_flight(l, now_ms, false);

	return 0;
}
