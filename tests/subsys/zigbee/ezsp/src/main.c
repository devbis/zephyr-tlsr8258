/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * The ASH framing and link layer, the EZSP frame header and the session that joins them, of the
 * EZSP NCP interface. The CRC,
 * RST, RSTACK and randomization vectors are the examples of Silicon Labs UG101.
 *
 * Run: west twister -T tests/subsys/zigbee/ezsp -p native_sim/native/64
 */

#include <errno.h>
#include <string.h>

#include <zephyr/ztest.h>

#include "zb_ezsp_ash.h"
#include "zb_ezsp_frame.h"
#include "zb_ezsp_session.h"

/* What the link sent and delivered. */
static uint8_t tx_bytes[1024];
static size_t tx_len;
static uint8_t rx_bytes[ASH_MAX_DATA];
static size_t rx_len;
static unsigned int rx_count;
static unsigned int reset_count;

static void ops_tx(void *ctx, const uint8_t *bytes, size_t len)
{
	ARG_UNUSED(ctx);
	zassert_true(tx_len + len <= sizeof(tx_bytes));
	memcpy(&tx_bytes[tx_len], bytes, len);
	tx_len += len;
}

static void ops_rx(void *ctx, const uint8_t *data, size_t len)
{
	ARG_UNUSED(ctx);
	memcpy(rx_bytes, data, len);
	rx_len = len;
	rx_count++;
}

static void ops_reset(void *ctx)
{
	ARG_UNUSED(ctx);
	reset_count++;
}

static const struct ash_link_ops ops = {.tx = ops_tx, .rx_data = ops_rx, .reset = ops_reset};
static struct ash_link link;

static void before(void *fixture)
{
	ARG_UNUSED(fixture);
	tx_len = 0U;
	rx_len = 0U;
	rx_count = 0U;
	reset_count = 0U;
	ash_link_init(&link, &ops, NULL);
}

static void feed_frame(uint32_t now, uint8_t ctrl, const uint8_t *data, size_t len)
{
	uint8_t out[ASH_MAX_ENCODED];
	size_t n = ash_frame_encode(out, sizeof(out), ctrl, data, len);

	zassert_true(n > 0U);
	for (size_t i = 0; i < n; i++) {
		ash_link_feed(&link, now, out[i]);
	}
}

/* The frame the link sent last as it would be on the wire, without stuffing: ctrl, data, CRC. */
static size_t sent_frame(uint8_t *ctrl, uint8_t *data, size_t *len, size_t from, size_t *next)
{
	struct ash_parser p;
	size_t i;

	ash_parser_reset(&p);
	for (i = from; i < tx_len; i++) {
		if (ash_parser_feed(&p, tx_bytes[i]) == ASH_FEED_FRAME) {
			*ctrl = p.buf[0];
			*len = p.len - 3U;
			memcpy(data, &p.buf[1], *len);
			if ((*ctrl & 0x80U) == 0U) {
				ash_randomize(data, *len);
			}
			*next = i + 1U;
			return p.len;
		}
	}

	return 0U;
}

static void connect(void)
{
	feed_frame(0U, ASH_CTRL_RST, NULL, 0U);
	tx_len = 0U;
	reset_count = 0U;
}

ZTEST(ezsp, test_crc_vectors)
{
	const uint8_t rst[] = {0xC0};
	const uint8_t rstack[] = {0xC1, 0x02, 0x02};

	zassert_equal(ash_crc16(rst, sizeof(rst)), 0x38BCU);
	zassert_equal(ash_crc16(rstack, sizeof(rstack)), 0x9B7BU);
}

ZTEST(ezsp, test_randomize_vector)
{
	uint8_t data[] = {0x00, 0x00, 0x00, 0x02};
	const uint8_t want[] = {0x42, 0x21, 0xA8, 0x56};

	ash_randomize(data, sizeof(data));
	zassert_mem_equal(data, want, sizeof(want));
	ash_randomize(data, sizeof(data));
	zassert_equal(data[3], 0x02U);
}

ZTEST(ezsp, test_encode_known_frames)
{
	uint8_t out[16];
	const uint8_t rst[] = {0xC0, 0x38, 0xBC, 0x7E};
	const uint8_t rstack[] = {0xC1, 0x02, 0x02, 0x9B, 0x7B, 0x7E};
	const uint8_t rstack_data[] = {0x02, 0x02};
	size_t n = ash_frame_encode(out, sizeof(out), ASH_CTRL_RST, NULL, 0U);

	zassert_equal(n, sizeof(rst));
	zassert_mem_equal(out, rst, n);

	n = ash_frame_encode(out, sizeof(out), ASH_CTRL_RSTACK, rstack_data, sizeof(rstack_data));
	zassert_equal(n, sizeof(rstack));
	zassert_mem_equal(out, rstack, n);
}

ZTEST(ezsp, test_encode_stuffs_reserved_bytes)
{
	uint8_t out[32];
	/* ACK frames are not randomized, so the data of this one is on the wire as it is */
	const uint8_t data[] = {0x7E, 0x11, 0x13, 0x18, 0x1A, 0x7D};
	struct ash_parser p;
	size_t n = ash_frame_encode(out, sizeof(out), 0xC2, data, sizeof(data));
	enum ash_feed_result r = ASH_FEED_MORE;

	zassert_true(n > 0U);
	/* No reserved byte but the closing flag is left */
	for (size_t i = 0; i + 1U < n; i++) {
		if (out[i] == ASH_FLAG || out[i] == ASH_XON || out[i] == ASH_XOFF ||
		    out[i] == ASH_SUBSTITUTE || out[i] == ASH_CANCEL) {
			zassert_unreachable("reserved byte at %zu", i);
		}
	}
	zassert_equal(out[n - 1U], ASH_FLAG);

	ash_parser_reset(&p);
	for (size_t i = 0; i < n; i++) {
		r = ash_parser_feed(&p, out[i]);
	}
	zassert_equal(r, ASH_FEED_FRAME);
	zassert_equal(p.len, 1U + sizeof(data) + 2U);
	zassert_mem_equal(&p.buf[1], data, sizeof(data));
}

ZTEST(ezsp, test_encode_rejects_overflow)
{
	uint8_t out[ASH_MAX_ENCODED];
	uint8_t data[ASH_MAX_DATA + 1U] = {0};

	zassert_equal(ash_frame_encode(out, sizeof(out), 0x00, data, sizeof(data)), 0U);
	zassert_equal(ash_frame_encode(out, 3U, ASH_CTRL_RST, NULL, 0U), 0U);
	zassert_true(ash_frame_encode(out, sizeof(out), 0x00, data, ASH_MAX_DATA) > 0U);
}

ZTEST(ezsp, test_ctrl_types)
{
	zassert_equal(ash_ctrl_type(0x00), ASH_FRAME_DATA);
	zassert_equal(ash_ctrl_type(0x7F), ASH_FRAME_DATA);
	zassert_equal(ash_ctrl_type(0x81), ASH_FRAME_ACK);
	zassert_equal(ash_ctrl_type(0x8D), ASH_FRAME_ACK);
	zassert_equal(ash_ctrl_type(0xA3), ASH_FRAME_NAK);
	zassert_equal(ash_ctrl_type(0xC0), ASH_FRAME_RST);
	zassert_equal(ash_ctrl_type(0xC1), ASH_FRAME_RSTACK);
	zassert_equal(ash_ctrl_type(0xC2), ASH_FRAME_ERROR);
	zassert_equal(ash_ctrl_type(0xC3), ASH_FRAME_UNKNOWN);
}

ZTEST(ezsp, test_parser_damage)
{
	struct ash_parser p;
	const uint8_t good[] = {0xC0, 0x38, 0xBC, 0x7E};

	ash_parser_reset(&p);
	/* Flags between frames and flow control bytes in a frame are ignored */
	zassert_equal(ash_parser_feed(&p, ASH_FLAG), ASH_FEED_MORE);
	zassert_equal(ash_parser_feed(&p, 0xC0), ASH_FEED_MORE);
	zassert_equal(ash_parser_feed(&p, ASH_XON), ASH_FEED_MORE);
	zassert_equal(ash_parser_feed(&p, 0x38), ASH_FEED_MORE);
	zassert_equal(ash_parser_feed(&p, ASH_XOFF), ASH_FEED_MORE);
	zassert_equal(ash_parser_feed(&p, 0xBC), ASH_FEED_MORE);
	zassert_equal(ash_parser_feed(&p, ASH_FLAG), ASH_FEED_FRAME);

	/* A wrong CRC */
	ash_parser_reset(&p);
	ash_parser_feed(&p, 0xC0);
	ash_parser_feed(&p, 0x38);
	ash_parser_feed(&p, 0xBD);
	zassert_equal(ash_parser_feed(&p, ASH_FLAG), ASH_FEED_BAD);

	/* Too short for a CRC */
	ash_parser_feed(&p, 0xC0);
	zassert_equal(ash_parser_feed(&p, ASH_FLAG), ASH_FEED_BAD);

	/* Substitute byte: a line error in the frame */
	for (size_t i = 0; i < 2U; i++) {
		ash_parser_feed(&p, good[i]);
	}
	ash_parser_feed(&p, ASH_SUBSTITUTE);
	ash_parser_feed(&p, good[2]);
	zassert_equal(ash_parser_feed(&p, ASH_FLAG), ASH_FEED_BAD);

	/* Cancel: the frame is dropped silently, with its flag */
	ash_parser_feed(&p, 0xC0);
	ash_parser_feed(&p, ASH_CANCEL);
	zassert_equal(ash_parser_feed(&p, ASH_FLAG), ASH_FEED_MORE);

	/* The next frame is intact */
	for (size_t i = 0; i + 1U < sizeof(good); i++) {
		ash_parser_feed(&p, good[i]);
	}
	zassert_equal(ash_parser_feed(&p, ASH_FLAG), ASH_FEED_FRAME);
}

ZTEST(ezsp, test_parser_overlong_frame)
{
	struct ash_parser p;

	ash_parser_reset(&p);
	for (size_t i = 0; i < ASH_MAX_RAW + 10U; i++) {
		ash_parser_feed(&p, 0x55);
	}
	zassert_equal(ash_parser_feed(&p, ASH_FLAG), ASH_FEED_BAD);
	/* and the parser recovers */
	ash_parser_feed(&p, 0xC0);
	ash_parser_feed(&p, 0x38);
	ash_parser_feed(&p, 0xBC);
	zassert_equal(ash_parser_feed(&p, ASH_FLAG), ASH_FEED_FRAME);
}

ZTEST(ezsp, test_start_sends_rstack)
{
	const uint8_t want[] = {0xC1, 0x02, 0x02, 0x9B, 0x7B, 0x7E};

	ash_link_start(&link, ASH_RESET_POWER_ON);
	zassert_equal(tx_len, sizeof(want));
	zassert_mem_equal(tx_bytes, want, sizeof(want));
	zassert_false(ash_link_can_send(&link));
}

ZTEST(ezsp, test_rst_connects)
{
	uint8_t ctrl;
	uint8_t data[8];
	size_t len;
	size_t next;

	feed_frame(0U, ASH_CTRL_RST, NULL, 0U);
	zassert_equal(reset_count, 1U);
	zassert_true(sent_frame(&ctrl, data, &len, 0U, &next) > 0U);
	zassert_equal(ctrl, ASH_CTRL_RSTACK);
	zassert_equal(len, 2U);
	zassert_equal(data[0], ASH_VERSION);
	zassert_equal(data[1], ASH_RESET_SOFTWARE);
	zassert_true(ash_link_can_send(&link));
}

ZTEST(ezsp, test_data_before_rst_is_ignored)
{
	const uint8_t d[] = {1, 2, 3};

	feed_frame(0U, 0x00, d, sizeof(d));
	zassert_equal(rx_count, 0U);
	zassert_equal(tx_len, 0U);
	zassert_equal(ash_link_send(&link, 0U, d, sizeof(d)), -ENOTCONN);
}

ZTEST(ezsp, test_data_in_order)
{
	const uint8_t d0[] = {0x01, 0x02, 0x03};
	const uint8_t d1[] = {0x04};
	uint8_t ctrl;
	uint8_t data[8];
	size_t len;
	size_t next;

	connect();
	feed_frame(0U, 0x00, d0, sizeof(d0));
	zassert_equal(rx_count, 1U);
	zassert_equal(rx_len, sizeof(d0));
	zassert_mem_equal(rx_bytes, d0, sizeof(d0));
	zassert_true(sent_frame(&ctrl, data, &len, 0U, &next) > 0U);
	/* ACK, with the number of the next frame */
	zassert_equal(ctrl, 0x81U);

	feed_frame(0U, 0x10, d1, sizeof(d1));
	zassert_equal(rx_count, 2U);
	zassert_true(sent_frame(&ctrl, data, &len, next, &next) > 0U);
	zassert_equal(ctrl, 0x82U);
}

ZTEST(ezsp, test_sequence_wraps)
{
	const uint8_t d[] = {0x55};

	connect();
	for (uint8_t i = 0; i < 10U; i++) {
		feed_frame(0U, (uint8_t)((i & 7U) << 4), d, sizeof(d));
	}
	zassert_equal(rx_count, 10U);
}

ZTEST(ezsp, test_out_of_order_is_nakked)
{
	const uint8_t d[] = {0x01};
	uint8_t ctrl;
	uint8_t data[8];
	size_t len;
	size_t next;

	connect();
	/* Frame 2 when 0 is due */
	feed_frame(0U, 0x20, d, sizeof(d));
	zassert_equal(rx_count, 0U);
	zassert_true(sent_frame(&ctrl, data, &len, 0U, &next) > 0U);
	zassert_equal(ctrl, 0xA0U);
}

ZTEST(ezsp, test_duplicate_is_acked_once)
{
	const uint8_t d[] = {0x01};
	uint8_t ctrl;
	uint8_t data[8];
	size_t len;
	size_t next;

	connect();
	feed_frame(0U, 0x00, d, sizeof(d));
	tx_len = 0U;
	/* The host did not see our ACK and sends frame 0 again with reTx set */
	feed_frame(0U, 0x08, d, sizeof(d));
	zassert_equal(rx_count, 1U);
	zassert_true(sent_frame(&ctrl, data, &len, 0U, &next) > 0U);
	zassert_equal(ctrl, 0x81U);
}

ZTEST(ezsp, test_bad_crc_is_nakked)
{
	uint8_t ctrl;
	uint8_t data[8];
	size_t len;
	size_t next;

	connect();
	ash_link_feed(&link, 0U, 0x00);
	ash_link_feed(&link, 0U, 0x01);
	ash_link_feed(&link, 0U, 0x02);
	ash_link_feed(&link, 0U, 0x03);
	ash_link_feed(&link, 0U, ASH_FLAG);
	zassert_equal(rx_count, 0U);
	zassert_true(sent_frame(&ctrl, data, &len, 0U, &next) > 0U);
	zassert_equal(ctrl, 0xA0U);
}

ZTEST(ezsp, test_send_and_ack)
{
	const uint8_t d[] = {0xAA, 0xBB};
	uint8_t ctrl;
	uint8_t data[8];
	size_t len;
	size_t next;

	connect();
	zassert_equal(ash_link_send(&link, 100U, d, sizeof(d)), 0);
	zassert_true(sent_frame(&ctrl, data, &len, 0U, &next) > 0U);
	/* DATA, frame 0, ack number 0, no retransmission */
	zassert_equal(ctrl, 0x00U);
	zassert_equal(len, sizeof(d));
	zassert_mem_equal(data, d, sizeof(d));
	zassert_equal(ash_link_send(&link, 100U, d, sizeof(d)), -EBUSY);

	feed_frame(110U, 0x81, NULL, 0U);
	zassert_true(ash_link_can_send(&link));
	tx_len = 0U;
	zassert_equal(ash_link_send(&link, 120U, d, sizeof(d)), 0);
	zassert_true(sent_frame(&ctrl, data, &len, 0U, &next) > 0U);
	zassert_equal(ctrl, 0x10U);
}

ZTEST(ezsp, test_ack_number_in_data_frame_acks)
{
	const uint8_t d[] = {0xAA};
	const uint8_t in[] = {0x01};

	connect();
	zassert_equal(ash_link_send(&link, 0U, d, sizeof(d)), 0);
	/* Host DATA frame 0 that acknowledges our frame 0 */
	feed_frame(10U, 0x01, in, sizeof(in));
	zassert_equal(rx_count, 1U);
	zassert_true(ash_link_can_send(&link));
}

ZTEST(ezsp, test_stale_ack_does_not_release)
{
	const uint8_t d[] = {0xAA};

	connect();
	zassert_equal(ash_link_send(&link, 0U, d, sizeof(d)), 0);
	feed_frame(1U, 0x80, NULL, 0U);
	zassert_false(ash_link_can_send(&link));
}

ZTEST(ezsp, test_nak_retransmits)
{
	const uint8_t d[] = {0xAA};
	uint8_t ctrl;
	uint8_t data[8];
	size_t len;
	size_t next;

	connect();
	zassert_equal(ash_link_send(&link, 0U, d, sizeof(d)), 0);
	tx_len = 0U;
	feed_frame(5U, 0xA0, NULL, 0U);
	zassert_true(sent_frame(&ctrl, data, &len, 0U, &next) > 0U);
	/* The same frame, with the retransmission flag */
	zassert_equal(ctrl, 0x08U);
	zassert_mem_equal(data, d, sizeof(d));
}

ZTEST(ezsp, test_timeout_retransmits_then_fails)
{
	const uint8_t d[] = {0xAA};
	uint8_t ctrl;
	uint8_t data[8];
	size_t len;
	size_t next;
	uint32_t now = 1000U;

	connect();
	zassert_equal(ash_link_send(&link, now, d, sizeof(d)), 0);
	tx_len = 0U;

	ash_link_tick(&link, now + ASH_ACK_TIMEOUT_MS - 1U);
	zassert_equal(tx_len, 0U);

	for (unsigned int i = 0; i < ASH_MAX_RETRIES; i++) {
		now += ASH_ACK_TIMEOUT_MS;
		tx_len = 0U;
		ash_link_tick(&link, now);
		zassert_true(sent_frame(&ctrl, data, &len, 0U, &next) > 0U);
		zassert_equal(ctrl, 0x08U, "retry %u", i);
	}

	now += ASH_ACK_TIMEOUT_MS;
	tx_len = 0U;
	ash_link_tick(&link, now);
	zassert_true(sent_frame(&ctrl, data, &len, 0U, &next) > 0U);
	zassert_equal(ctrl, ASH_CTRL_ERROR);
	zassert_equal(data[1], ASH_ERROR_TIMEOUTS);
	zassert_false(ash_link_can_send(&link));
}

ZTEST(ezsp, test_timeout_time_wraps)
{
	const uint8_t d[] = {0xAA};

	connect();
	zassert_equal(ash_link_send(&link, 0xFFFFFF00U, d, sizeof(d)), 0);
	tx_len = 0U;
	ash_link_tick(&link, 0xFFFFFF00U + ASH_ACK_TIMEOUT_MS);
	zassert_true(tx_len > 0U);
}

ZTEST(ezsp, test_rst_restarts_numbering)
{
	const uint8_t d[] = {0x01};

	connect();
	feed_frame(0U, 0x00, d, sizeof(d));
	zassert_equal(rx_count, 1U);
	connect();
	/* After a new RST the host starts at frame 0 again */
	feed_frame(0U, 0x00, d, sizeof(d));
	zassert_equal(rx_count, 2U);
}

ZTEST(ezsp, test_error_frame_disconnects)
{
	const uint8_t e[] = {ASH_VERSION, 0x51};

	connect();
	feed_frame(0U, ASH_CTRL_ERROR, e, sizeof(e));
	zassert_false(ash_link_can_send(&link));
}

ZTEST(ezsp, test_data_is_derandomized)
{
	/* The version command as UG101 shows it on the wire */
	const uint8_t cmd[] = {0x00, 0x00, 0x00, 0x02};
	uint8_t wire[] = {0x00, 0x42, 0x21, 0xA8, 0x56};
	uint8_t raw[8];
	size_t n;

	connect();
	/* control byte, the randomized data and a CRC over them, then the flag */
	memcpy(raw, wire, sizeof(wire));
	{
		uint16_t crc = ash_crc16(raw, sizeof(wire));

		raw[5] = (uint8_t)(crc >> 8);
		raw[6] = (uint8_t)crc;
	}
	n = 7U;
	for (size_t i = 0; i < n; i++) {
		ash_link_feed(&link, 0U, raw[i]);
	}
	ash_link_feed(&link, 0U, ASH_FLAG);
	zassert_equal(rx_count, 1U);
	zassert_equal(rx_len, sizeof(cmd));
	zassert_mem_equal(rx_bytes, cmd, sizeof(cmd));
}

/* EZSP */

static size_t ezsp_run(struct ezsp_ctx *c, const uint8_t *cmd, size_t len, uint8_t *out)
{
	return ezsp_handle(c, cmd, len, out, 64U);
}

ZTEST(ezsp, test_version_legacy)
{
	struct ezsp_ctx c;
	uint8_t out[64];
	const uint8_t cmd[] = {0x00, 0x00, 0x00, 0x04};
	const uint8_t want[] = {0x00, 0x80, 0x00, 13, 2, 0x40, 0x74};

	ezsp_ctx_reset(&c);
	zassert_equal(ezsp_run(&c, cmd, sizeof(cmd), out), sizeof(want));
	zassert_mem_equal(out, want, sizeof(want));
	/* Asked for a version below 8: still the legacy format */
	zassert_false(c.extended);
}

ZTEST(ezsp, test_version_goes_extended)
{
	struct ezsp_ctx c;
	uint8_t out[64];
	const uint8_t legacy[] = {0x00, 0x00, 0x00, 13};
	const uint8_t ext[] = {0x01, 0x00, 0x01, 0x00, 0x00, 13};
	const uint8_t want[] = {0x01, 0x80, 0x01, 0x00, 0x00, 13, 2, 0x40, 0x74};

	ezsp_ctx_reset(&c);
	zassert_equal(ezsp_run(&c, legacy, sizeof(legacy), out), 7U);
	zassert_true(c.extended);

	zassert_equal(ezsp_run(&c, ext, sizeof(ext), out), sizeof(want));
	zassert_mem_equal(out, want, sizeof(want));
}

ZTEST(ezsp, test_extended_version_as_first_command)
{
	struct ezsp_ctx c;
	uint8_t out[64];
	const uint8_t ext[] = {0x07, 0x00, 0x01, 0x00, 0x00, 13};

	ezsp_ctx_reset(&c);
	zassert_equal(ezsp_run(&c, ext, sizeof(ext), out), 9U);
	zassert_equal(out[0], 0x07U);
	zassert_equal(out[2], 0x01U);
	zassert_equal(out[5], 13U);
}

ZTEST(ezsp, test_network_index_is_echoed)
{
	struct ezsp_ctx c = {.extended = true};
	uint8_t out[64];
	const uint8_t cmd[] = {0x02, 0x20, 0x01, 0x00, 0x00, 13};

	zassert_true(ezsp_run(&c, cmd, sizeof(cmd), out) > 0U);
	zassert_equal(out[1], 0xA0U);
}

ZTEST(ezsp, test_unknown_command)
{
	struct ezsp_ctx c = {.extended = true};
	uint8_t out[64];
	const uint8_t cmd[] = {0x05, 0x00, 0x01, 0x34, 0x12, 0xAA};
	const uint8_t want[] = {0x05, 0x80, 0x01, 0x58, 0x00, 0x58};

	zassert_equal(ezsp_run(&c, cmd, sizeof(cmd), out), sizeof(want));
	zassert_mem_equal(out, want, sizeof(want));
}

ZTEST(ezsp, test_unknown_command_legacy)
{
	struct ezsp_ctx c;
	uint8_t out[64];
	const uint8_t cmd[] = {0x05, 0x00, 0x34};
	const uint8_t want[] = {0x05, 0x80, 0x58, 0x58};

	ezsp_ctx_reset(&c);
	zassert_equal(ezsp_run(&c, cmd, sizeof(cmd), out), sizeof(want));
	zassert_mem_equal(out, want, sizeof(want));
}

ZTEST(ezsp, test_malformed_frames_are_dropped)
{
	struct ezsp_ctx c;
	uint8_t out[64];
	const uint8_t short_legacy[] = {0x00, 0x00};
	const uint8_t short_ext[] = {0x00, 0x00, 0x01, 0x00};
	const uint8_t no_version[] = {0x00, 0x00, 0x00};

	ezsp_ctx_reset(&c);
	zassert_equal(ezsp_run(&c, short_legacy, sizeof(short_legacy), out), 0U);
	zassert_equal(ezsp_run(&c, no_version, sizeof(no_version), out), 0U);
	c.extended = true;
	zassert_equal(ezsp_run(&c, short_ext, sizeof(short_ext), out), 0U);
}

ZTEST(ezsp, test_response_does_not_fit)
{
	struct ezsp_ctx c = {.extended = true};
	uint8_t out[8];
	const uint8_t cmd[] = {0x00, 0x00, 0x01, 0x00, 0x00, 13};

	zassert_equal(ezsp_handle(&c, cmd, sizeof(cmd), out, sizeof(out)), 0U);
}

ZTEST(ezsp, test_ctx_reset)
{
	struct ezsp_ctx c = {.extended = true};

	ezsp_ctx_reset(&c);
	zassert_false(c.extended);
}

/* The session: a host talking ASH and EZSP to it */

static struct ezsp_session sess;
static uint8_t host_rx[2048];
static size_t host_rx_len;

static void sess_tx(void *ctx, const uint8_t *bytes, size_t len)
{
	ARG_UNUSED(ctx);
	zassert_true(host_rx_len + len <= sizeof(host_rx));
	memcpy(&host_rx[host_rx_len], bytes, len);
	host_rx_len += len;
}

static void host_send(uint32_t now, uint8_t ctrl, const uint8_t *data, size_t len)
{
	uint8_t out[ASH_MAX_ENCODED];
	size_t n = ash_frame_encode(out, sizeof(out), ctrl, data, len);

	zassert_true(n > 0U);
	for (size_t i = 0; i < n; i++) {
		ezsp_session_rx(&sess, now, out[i]);
	}
}

/* The next frame the host received, from offset @p from of what it got; 0 when there is none. */
static size_t host_frame(uint8_t *ctrl, uint8_t *data, size_t *len, size_t *from)
{
	struct ash_parser p;

	ash_parser_reset(&p);
	for (size_t i = *from; i < host_rx_len; i++) {
		if (ash_parser_feed(&p, host_rx[i]) == ASH_FEED_FRAME) {
			*ctrl = p.buf[0];
			*len = p.len - 3U;
			memcpy(data, &p.buf[1], *len);
			if ((*ctrl & 0x80U) == 0U) {
				ash_randomize(data, *len);
			}
			*from = i + 1U;
			return p.len;
		}
	}

	return 0U;
}

static void sess_before(void *fixture)
{
	ARG_UNUSED(fixture);
	host_rx_len = 0U;
	ezsp_session_init(&sess, sess_tx, NULL);
}

ZTEST(ezsp_session, test_start_announces)
{
	uint8_t ctrl;
	uint8_t data[8];
	size_t len;
	size_t from = 0U;

	ezsp_session_start(&sess);
	zassert_true(host_frame(&ctrl, data, &len, &from) > 0U);
	zassert_equal(ctrl, ASH_CTRL_RSTACK);
	zassert_equal(data[1], ASH_RESET_POWER_ON);
}

ZTEST(ezsp_session, test_version_over_ash)
{
	const uint8_t cmd[] = {0x00, 0x00, 0x00, 13};
	const uint8_t want[] = {0x00, 0x80, 0x00, 13, 2, 0x40, 0x74};
	uint8_t ctrl;
	uint8_t data[ASH_MAX_DATA];
	size_t len;
	size_t from = 0U;

	host_send(0U, ASH_CTRL_RST, NULL, 0U);
	zassert_true(host_frame(&ctrl, data, &len, &from) > 0U);
	zassert_equal(ctrl, ASH_CTRL_RSTACK);

	host_send(1U, 0x00, cmd, sizeof(cmd));
	/* The ACK of the command... */
	zassert_true(host_frame(&ctrl, data, &len, &from) > 0U);
	zassert_equal(ctrl, 0x81U);
	/* ...and nothing else until the poll sends the response */
	zassert_equal(host_frame(&ctrl, data, &len, &from), 0U);
	ezsp_session_poll(&sess, 2U);
	zassert_true(host_frame(&ctrl, data, &len, &from) > 0U);
	/* DATA frame 0, acknowledging the host frame 0 */
	zassert_equal(ctrl, 0x01U);
	zassert_equal(len, sizeof(want));
	zassert_mem_equal(data, want, sizeof(want));
	zassert_true(sess.ezsp.extended);
}

ZTEST(ezsp_session, test_queue_waits_for_the_window)
{
	const uint8_t a[] = {0xA1};
	const uint8_t b[] = {0xB2, 0xB3};
	uint8_t ctrl;
	uint8_t data[ASH_MAX_DATA];
	size_t len;
	size_t from = 0U;

	host_send(0U, ASH_CTRL_RST, NULL, 0U);
	host_rx_len = 0U;

	zassert_true(ezsp_session_send(&sess, a, sizeof(a)));
	zassert_true(ezsp_session_send(&sess, b, sizeof(b)));
	ezsp_session_poll(&sess, 1U);
	zassert_true(host_frame(&ctrl, data, &len, &from) > 0U);
	zassert_equal(ctrl, 0x00U);
	zassert_equal(len, sizeof(a));

	/* The second frame waits for the ACK of the first */
	ezsp_session_poll(&sess, 2U);
	zassert_equal(host_frame(&ctrl, data, &len, &from), 0U);
	host_send(3U, 0x81, NULL, 0U);
	ezsp_session_poll(&sess, 4U);
	zassert_true(host_frame(&ctrl, data, &len, &from) > 0U);
	zassert_equal(ctrl, 0x10U);
	zassert_equal(len, sizeof(b));
	zassert_mem_equal(data, b, sizeof(b));
}

ZTEST(ezsp_session, test_queue_full_drops)
{
	const uint8_t f[] = {1};

	for (unsigned int i = 0; i < EZSP_QUEUE_DEPTH; i++) {
		zassert_true(ezsp_session_send(&sess, f, sizeof(f)));
	}
	zassert_false(ezsp_session_send(&sess, f, sizeof(f)));
	zassert_equal(sess.dropped, 1U);
	zassert_false(ezsp_session_send(&sess, f, EZSP_MAX_FRAME + 1U));
}

ZTEST(ezsp_session, test_queue_keeps_order_when_wrapping)
{
	uint8_t ctrl;
	uint8_t data[ASH_MAX_DATA];
	size_t len;
	size_t from = 0U;

	host_send(0U, ASH_CTRL_RST, NULL, 0U);
	host_rx_len = 0U;

	for (uint8_t i = 0; i < 3U * EZSP_QUEUE_DEPTH; i++) {
		uint8_t seq = (uint8_t)((i + 1U) & 7U);

		ezsp_session_send(&sess, &i, 1U);
		ezsp_session_poll(&sess, i);
		zassert_true(host_frame(&ctrl, data, &len, &from) > 0U);
		zassert_equal(data[0], i);
		host_send(i, (uint8_t)(0x80U | seq), NULL, 0U);
	}
}

ZTEST(ezsp_session, test_rst_flushes_the_queue_and_format)
{
	const uint8_t f[] = {1};
	uint8_t ctrl;
	uint8_t data[ASH_MAX_DATA];
	size_t len;
	size_t from = 0U;

	host_send(0U, ASH_CTRL_RST, NULL, 0U);
	sess.ezsp.extended = true;
	zassert_true(ezsp_session_send(&sess, f, sizeof(f)));

	host_rx_len = 0U;
	host_send(1U, ASH_CTRL_RST, NULL, 0U);
	zassert_false(sess.ezsp.extended);
	ezsp_session_poll(&sess, 2U);
	/* Only the RSTACK, no frame of the old connection */
	zassert_true(host_frame(&ctrl, data, &len, &from) > 0U);
	zassert_equal(ctrl, ASH_CTRL_RSTACK);
	zassert_equal(host_frame(&ctrl, data, &len, &from), 0U);
}

ZTEST_SUITE(ezsp, NULL, NULL, before, NULL, NULL);
ZTEST_SUITE(ezsp_session, NULL, NULL, sess_before, NULL, NULL);
