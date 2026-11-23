/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * ASH, the asynchronous serial framing of EZSP (Silicon Labs UG101): byte stuffing, CRC-16,
 * randomized data and a go-back-N acknowledgment scheme. This is the NCP side with a transmit
 * window of one frame. Nothing here touches a UART or a clock: bytes go in through
 * ash_link_feed(), frames leave through the tx callback and the caller passes the time.
 */

/* Largest data field of a DATA frame. */
#define ASH_MAX_DATA       220U
/* Control byte, data and CRC of a frame, before byte stuffing. */
#define ASH_MAX_RAW        (1U + ASH_MAX_DATA + 2U)
/* A frame in which every byte is escaped, with the closing flag. */
#define ASH_MAX_ENCODED    (2U * ASH_MAX_RAW + 1U)

#define ASH_ACK_TIMEOUT_MS 800U
#define ASH_MAX_RETRIES    4U

/* Special bytes. */
#define ASH_FLAG           0x7EU
#define ASH_ESCAPE         0x7DU
#define ASH_XON            0x11U
#define ASH_XOFF           0x13U
#define ASH_SUBSTITUTE     0x18U
#define ASH_CANCEL         0x1AU

/* Control bytes that are not DATA, ACK or NAK. */
#define ASH_CTRL_RST       0xC0U
#define ASH_CTRL_RSTACK    0xC1U
#define ASH_CTRL_ERROR     0xC2U

/* Version carried by RSTACK and ERROR. */
#define ASH_VERSION        0x02U

/* Reset and error codes. */
#define ASH_RESET_POWER_ON 0x02U
#define ASH_RESET_SOFTWARE 0x0BU
#define ASH_ERROR_TIMEOUTS 0x51U

enum ash_frame_type {
	ASH_FRAME_DATA,
	ASH_FRAME_ACK,
	ASH_FRAME_NAK,
	ASH_FRAME_RST,
	ASH_FRAME_RSTACK,
	ASH_FRAME_ERROR,
	ASH_FRAME_UNKNOWN,
};

/* CRC-16/CCITT-FALSE (polynomial 0x1021, initial value 0xffff) of @p len bytes. */
uint16_t ash_crc16(const uint8_t *data, size_t len);

/* XOR @p len bytes with the pseudo-random sequence of DATA frames, in place; it undoes itself. */
void ash_randomize(uint8_t *data, size_t len);

/*
 * Write the frame with its CRC, stuffing and closing flag to @p out of @p out_size bytes. The
 * data of a DATA frame (control bit 7 clear) is randomized on the way. Returns the length, or 0
 * when the data or the buffer is too small.
 */
size_t ash_frame_encode(uint8_t *out, size_t out_size, uint8_t ctrl, const uint8_t *data,
			size_t len);

enum ash_frame_type ash_ctrl_type(uint8_t ctrl);

/* Receive-side frame splitter. */
struct ash_parser {
	uint8_t buf[ASH_MAX_RAW];
	size_t len;
	bool escape;
	bool error;
	/* The frame of the last ASH_FEED_FRAME is still in buf; the next byte starts a new one. */
	bool done;
};

enum ash_feed_result {
	ASH_FEED_MORE,
	/* A frame with a good CRC is in buf: control byte first, then data, then the CRC. */
	ASH_FEED_FRAME,
	/* The frame was damaged, cancelled by the host, or too long. */
	ASH_FEED_BAD,
};

void ash_parser_reset(struct ash_parser *p);
enum ash_feed_result ash_parser_feed(struct ash_parser *p, uint8_t byte);

struct ash_link;

struct ash_link_ops {
	/* Send encoded bytes to the host. */
	void (*tx)(void *ctx, const uint8_t *bytes, size_t len);
	/* A DATA frame arrived in order; the data is valid for the call only. */
	void (*rx_data)(void *ctx, const uint8_t *data, size_t len);
	/* The host reset the link (RST), after RSTACK has been sent. */
	void (*reset)(void *ctx);
};

struct ash_link {
	const struct ash_link_ops *ops;
	void *ctx;
	struct ash_parser parser;
	bool connected;
	/* Number of the next DATA frame the host sends. */
	uint8_t rx_seq;
	/* Number of the DATA frame in flight, or of the next one to send. */
	uint8_t tx_seq;
	bool in_flight;
	uint8_t retries;
	uint32_t sent_at;
	size_t flight_len;
	uint8_t flight[ASH_MAX_DATA];
};

void ash_link_init(struct ash_link *l, const struct ash_link_ops *ops, void *ctx);
/* Announce the start-up to the host with an unsolicited RSTACK. */
void ash_link_start(struct ash_link *l, uint8_t reset_code);
void ash_link_feed(struct ash_link *l, uint32_t now_ms, uint8_t byte);
/* Retransmit an unacknowledged frame once the timeout passed; call it from the poll. */
void ash_link_tick(struct ash_link *l, uint32_t now_ms);
/*
 * Send a DATA frame. Returns -ENOTCONN before the host sent RST or for data that is too long, and
 * -EBUSY while the previous frame is not acknowledged.
 */
int ash_link_send(struct ash_link *l, uint32_t now_ms, const uint8_t *data, size_t len);
bool ash_link_can_send(const struct ash_link *l);
