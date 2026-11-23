/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "zb_ezsp_ash.h"
#include "zb_ezsp_frame.h"

/*
 * One EZSP host connection: the ASH link, the EZSP format state and a queue of frames that wait for
 * the ASH window. A response and a callback leave through the same queue, so the host sees them in
 * the order they were produced. Like the layers below it, it touches no UART and no clock.
 */

#ifdef CONFIG_ZIGBEE_NCP_EZSP_QUEUE_DEPTH
#define EZSP_QUEUE_DEPTH CONFIG_ZIGBEE_NCP_EZSP_QUEUE_DEPTH
#else
#define EZSP_QUEUE_DEPTH 4U
#endif

struct ezsp_session {
	struct ash_link ash;
	struct ezsp_ctx ezsp;
	void (*tx)(void *ctx, const uint8_t *bytes, size_t len);
	void *tx_ctx;
	uint8_t head;
	uint8_t count;
	uint16_t len[EZSP_QUEUE_DEPTH];
	uint8_t frame[EZSP_QUEUE_DEPTH][EZSP_MAX_FRAME];
	/* Frames dropped because the queue was full. */
	uint32_t dropped;
};

void ezsp_session_init(struct ezsp_session *s,
		       void (*tx)(void *ctx, const uint8_t *bytes, size_t len), void *tx_ctx);
/* Tell the host that the NCP has started. */
void ezsp_session_start(struct ezsp_session *s);
void ezsp_session_rx(struct ezsp_session *s, uint32_t now_ms, uint8_t byte);
/* Retransmit, and send the next queued frame once the window is free. */
void ezsp_session_poll(struct ezsp_session *s, uint32_t now_ms);
/* Queue an EZSP frame for the host. Returns false, and drops it, when the queue is full. */
bool ezsp_session_send(struct ezsp_session *s, const uint8_t *frame, size_t len);
