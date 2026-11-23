/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * The room zb_buf_rx_payload_tailroom() reports after a pointer into a received frame.
 *
 * The beacon indication is built by moving the beacon payload further inside the storage of the
 * received frame, so the room has to be measured in the copy the frame was captured into (inline
 * after the buffer, or a block of the snapshot pool), not from the start of the buffer.
 *
 * Run (the inline copy; the pool with -DZIGBEE_TEST_RX_SNAPSHOTS=4):
 *   west twister -T tests/subsys/zigbee/buf_rx_tailroom -p native_sim/native/64
 */

#include <zephyr/ztest.h>

#include "zb_common.h"

#define FRAME_LEN  40U
#define SNAP_SIZE  128U
#define POOL_COUNT CONFIG_ZIGBEE_RX_SNAPSHOTS

/* The diagnostics the buffer pool counts its failures in. */
sys_diagnostics_t g_sysDiags;

static zb_buf_t *buf;

static void before(void *fixture)
{
	ARG_UNUSED(fixture);

	buf = zb_buf_allocate();
	zassert_not_null(buf, "no buffer");
}

static void after(void *fixture)
{
	ARG_UNUSED(fixture);

	zb_buf_free(buf);
	buf = NULL;
}

static uint8_t *capture(zb_buf_t *b)
{
	static const uint8_t frame[FRAME_LEN];

	return zb_buf_rx_payload_capture(b, frame, FRAME_LEN);
}

ZTEST(buf_rx_tailroom, test_invalid_arguments)
{
	uint8_t *frame = capture(buf);

	zassert_not_null(frame);
	zassert_equal(zb_buf_rx_payload_tailroom(NULL, frame), 0U);
	zassert_equal(zb_buf_rx_payload_tailroom(buf, NULL), 0U);
}

ZTEST(buf_rx_tailroom, test_captured_frame)
{
	uint8_t *frame = capture(buf);

	zassert_not_null(frame);
	/* The whole snapshot after the start of the frame, not only the frame. */
	zassert_true(zb_buf_rx_payload_tailroom(buf, frame) >= SNAP_SIZE,
		     "room %zu", zb_buf_rx_payload_tailroom(buf, frame));
	zassert_equal(zb_buf_rx_payload_tailroom(buf, frame) -
			      zb_buf_rx_payload_tailroom(buf, frame + 10),
		      10U);
	zassert_equal(zb_buf_rx_payload_tailroom(buf, frame + SNAP_SIZE - 1U), 1U);
}

ZTEST(buf_rx_tailroom, test_past_the_snapshot)
{
	uint8_t *frame = capture(buf);

	zassert_not_null(frame);
	/* The end of the copy is not a byte of it. */
	zassert_equal(zb_buf_rx_payload_tailroom(buf, frame + SNAP_SIZE), 0U);
}

ZTEST(buf_rx_tailroom, test_payload_of_the_buffer)
{
	zassert_equal(zb_buf_rx_payload_tailroom(buf, buf->buf), sizeof(buf->buf));
	zassert_equal(zb_buf_rx_payload_tailroom(buf, &buf->buf[sizeof(buf->buf) - 1U]), 1U);
	zassert_equal(zb_buf_rx_payload_tailroom(buf, &buf->buf[sizeof(buf->buf)]), 0U);
}

ZTEST(buf_rx_tailroom, test_foreign_pointer)
{
	static uint8_t other[16];

	zassert_equal(zb_buf_rx_payload_tailroom(buf, other), 0U);
}

ZTEST(buf_rx_tailroom, test_frame_of_another_buffer)
{
	zb_buf_t *second = zb_buf_allocate();
	uint8_t *own;
	uint8_t *foreign;

	zassert_not_null(second);
	own = capture(buf);
	foreign = capture(second);
	zassert_not_null(own);
	zassert_not_null(foreign);

	zassert_true(zb_buf_rx_payload_tailroom(buf, own) > 0U);
	zassert_true(zb_buf_rx_payload_tailroom(second, foreign) > 0U);
	/* A frame is the room of its own buffer only. */
	zassert_equal(zb_buf_rx_payload_tailroom(buf, foreign), 0U);
	zassert_equal(zb_buf_rx_payload_tailroom(second, own), 0U);

	zb_buf_free(second);
}

#if POOL_COUNT != 0
ZTEST(buf_rx_tailroom, test_snapshot_given_back_with_the_buffer)
{
	zb_buf_t *second = zb_buf_allocate();
	uint8_t *stale;

	zassert_not_null(second);
	stale = capture(second);
	zassert_not_null(stale);
	zassert_true(zb_buf_rx_payload_tailroom(second, stale) > 0U);

	zb_buf_free(second);
	/* The block is free again: nobody owns it. */
	zassert_equal(zb_buf_rx_payload_tailroom(buf, stale), 0U);
}
#endif

ZTEST_SUITE(buf_rx_tailroom, NULL, NULL, before, after, NULL);
