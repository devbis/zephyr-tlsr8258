/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */
/* An EZSP host connection: ASH link, EZSP format state and the outgoing frame queue. */
#include <string.h>

#include "zb_ezsp_session.h"

static void session_tx(void *ctx, const uint8_t *bytes, size_t len)
{
	struct ezsp_session *s = ctx;

	s->tx(s->tx_ctx, bytes, len);
}

static void session_rx_data(void *ctx, const uint8_t *data, size_t len)
{
	struct ezsp_session *s = ctx;
	uint8_t rsp[EZSP_MAX_FRAME];
	size_t n = ezsp_handle(&s->ezsp, data, len, rsp, sizeof(rsp));

	if (n > 0U) {
		(void)ezsp_session_send(s, rsp, n);
	}
}

static void session_reset(void *ctx)
{
	struct ezsp_session *s = ctx;

	/* A new connection: the host starts over in the legacy format and nothing is owed to it */
	ezsp_ctx_reset(&s->ezsp);
	s->head = 0U;
	s->count = 0U;
}

static const struct ash_link_ops session_ops = {
	.tx = session_tx,
	.rx_data = session_rx_data,
	.reset = session_reset,
};

void ezsp_session_init(struct ezsp_session *s,
		       void (*tx)(void *ctx, const uint8_t *bytes, size_t len), void *tx_ctx)
{
	memset(s, 0, sizeof(*s));
	s->tx = tx;
	s->tx_ctx = tx_ctx;
	ezsp_ctx_reset(&s->ezsp);
	ash_link_init(&s->ash, &session_ops, s);
}

void ezsp_session_start(struct ezsp_session *s)
{
	ash_link_start(&s->ash, ASH_RESET_POWER_ON);
}

void ezsp_session_rx(struct ezsp_session *s, uint32_t now_ms, uint8_t byte)
{
	ash_link_feed(&s->ash, now_ms, byte);
}

void ezsp_session_poll(struct ezsp_session *s, uint32_t now_ms)
{
	ash_link_tick(&s->ash, now_ms);

	if (s->count > 0U && ash_link_can_send(&s->ash) &&
	    ash_link_send(&s->ash, now_ms, s->frame[s->head], s->len[s->head]) == 0) {
		s->head = (uint8_t)((s->head + 1U) % EZSP_QUEUE_DEPTH);
		s->count--;
	}
}

bool ezsp_session_send(struct ezsp_session *s, const uint8_t *frame, size_t len)
{
	uint8_t tail;

	if (len > EZSP_MAX_FRAME || s->count >= EZSP_QUEUE_DEPTH) {
		s->dropped++;
		return false;
	}

	tail = (uint8_t)((s->head + s->count) % EZSP_QUEUE_DEPTH);
	memcpy(s->frame[tail], frame, len);
	s->len[tail] = (uint16_t)len;
	s->count++;

	return true;
}
