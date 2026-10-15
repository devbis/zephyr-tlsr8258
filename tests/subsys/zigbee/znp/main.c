/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>
#include <string.h>

#include <zephyr/ztest.h>

#include "zb_znp_internal.h"

static void before(void *fixture)
{
	ARG_UNUSED(fixture);
	znp_nv_reset();
}

ZTEST_SUITE(znp, NULL, NULL, before, NULL, NULL);

ZTEST(znp, test_missing_item_has_no_length)
{
	zassert_equal(znp_nv_length(0x0021U), 0U);
}

ZTEST(znp, test_init_creates_then_reports_existing)
{
	const uint8_t init[2] = {0x11U, 0x22U};
	uint8_t out[4];
	size_t n;

	zassert_equal(znp_nv_item_init(0x10U, 4U, init, sizeof(init)), ZNP_STATUS_NV_ITEM_UNINIT);
	zassert_equal(znp_nv_length(0x10U), 4U);

	/* The bytes after the initial value are erased flash. */
	zassert_equal(znp_nv_read(0x10U, 0U, out, sizeof(out), &n), ZNP_STATUS_SUCCESS);
	zassert_equal(n, 4U);
	zassert_equal(out[0], 0x11U);
	zassert_equal(out[1], 0x22U);
	zassert_equal(out[2], 0xffU);
	zassert_equal(out[3], 0xffU);

	zassert_equal(znp_nv_item_init(0x10U, 4U, init, sizeof(init)), ZNP_STATUS_SUCCESS);
	zassert_equal(znp_nv_item_init(0x10U, 8U, init, sizeof(init)), ZNP_STATUS_NV_BAD_ITEM_LEN);
}

ZTEST(znp, test_write_and_read_back_at_offset)
{
	const uint8_t init[4] = {0U};
	const uint8_t data[2] = {0xaaU, 0xbbU};
	uint8_t out[4];
	size_t n;

	zassert_equal(znp_nv_item_init(0x20U, 4U, init, sizeof(init)), ZNP_STATUS_NV_ITEM_UNINIT);
	zassert_equal(znp_nv_write(0x20U, 2U, data, sizeof(data)), ZNP_STATUS_SUCCESS);
	zassert_equal(znp_nv_read(0x20U, 2U, out, sizeof(out), &n), ZNP_STATUS_SUCCESS);
	zassert_equal(n, 2U);
	zassert_mem_equal(out, data, sizeof(data));

	/* A write that runs past the end is refused whole. */
	zassert_equal(znp_nv_write(0x20U, 3U, data, sizeof(data)), ZNP_STATUS_INVALID_PARAMETER);
	zassert_equal(znp_nv_write(0x99U, 0U, data, sizeof(data)), ZNP_STATUS_INVALID_PARAMETER);
}

ZTEST(znp, test_delete_keeps_the_other_items)
{
	const uint8_t a = 1U;
	const uint8_t b = 2U;
	const uint8_t c = 3U;
	uint8_t out[16];
	size_t n;

	zassert_equal(znp_nv_item_init(1U, 1U, &a, 1U), ZNP_STATUS_NV_ITEM_UNINIT);
	zassert_equal(znp_nv_item_init(2U, 200U, &b, 1U), ZNP_STATUS_NV_ITEM_UNINIT);
	zassert_equal(znp_nv_item_init(3U, 1U, &c, 1U), ZNP_STATUS_NV_ITEM_UNINIT);

	zassert_equal(znp_nv_delete(2U), ZNP_STATUS_SUCCESS);
	zassert_equal(znp_nv_length(2U), 0U);
	zassert_equal(znp_nv_delete(2U), ZNP_STATUS_INVALID_PARAMETER);

	zassert_equal(znp_nv_read(1U, 0U, out, sizeof(out), &n), ZNP_STATUS_SUCCESS);
	zassert_equal(out[0], 1U);
	zassert_equal(znp_nv_read(3U, 0U, out, sizeof(out), &n), ZNP_STATUS_SUCCESS);
	zassert_equal(out[0], 3U);

	/* The freed space can be used again. */
	zassert_equal(znp_nv_item_init(2U, 200U, &b, 1U), ZNP_STATUS_NV_ITEM_UNINIT);
	zassert_equal(znp_nv_read(3U, 0U, out, sizeof(out), &n), ZNP_STATUS_SUCCESS);
	zassert_equal(out[0], 3U);
}

ZTEST(znp, test_repeated_recreate_does_not_exhaust_the_pool)
{
	const uint8_t init = 0x5aU;

	for (int i = 0; i < 100; i++) {
		zassert_equal(znp_nv_item_init(7U, 512U, &init, 1U), ZNP_STATUS_NV_ITEM_UNINIT);
		zassert_equal(znp_nv_delete(7U), ZNP_STATUS_SUCCESS);
	}
}

ZTEST(znp, test_export_import_keeps_host_items_and_skips_stack_items)
{
	const uint8_t host[3] = {1U, 2U, 3U};
	const uint8_t stack[4] = {9U, 9U, 9U, 9U};
	uint8_t blob[ZNP_NV_EXPORT_MAX];
	uint8_t out[4];
	size_t len;
	size_t n;

	zassert_equal(znp_nv_item_init(ZNP_NV_PANID, sizeof(host), host, sizeof(host)),
		      ZNP_STATUS_NV_ITEM_UNINIT);
	zassert_equal(znp_nv_set_derived(ZNP_NV_NIB, stack, sizeof(stack)), ZNP_STATUS_SUCCESS);

	len = znp_nv_export(blob, sizeof(blob));
	zassert_true(len > 0U);

	znp_nv_reset();
	zassert_true(znp_nv_import(blob, len));

	zassert_equal(znp_nv_read(ZNP_NV_PANID, 0U, out, sizeof(out), &n), ZNP_STATUS_SUCCESS);
	zassert_equal(n, sizeof(host));
	zassert_mem_equal(out, host, sizeof(host));
	/* The NIB is rebuilt from the stack, not restored. */
	zassert_equal(znp_nv_length(ZNP_NV_NIB), 0U);
}

ZTEST(znp, test_import_rejects_a_damaged_copy)
{
	const uint8_t host = 1U;
	uint8_t blob[ZNP_NV_EXPORT_MAX];
	size_t len;

	zassert_equal(znp_nv_item_init(ZNP_NV_PANID, 1U, &host, 1U), ZNP_STATUS_NV_ITEM_UNINIT);
	len = znp_nv_export(blob, sizeof(blob));

	zassert_false(znp_nv_import(blob, len - 1U));
	blob[0] = 0xffU;
	zassert_false(znp_nv_import(blob, len));
	zassert_false(znp_nv_import(blob, 1U));
}

ZTEST(znp, test_generation_counts_only_changes_worth_saving)
{
	const uint8_t a = 1U;
	const uint8_t b = 2U;
	uint32_t gen;

	zassert_equal(znp_nv_item_init(ZNP_NV_PANID, 1U, &a, 1U), ZNP_STATUS_NV_ITEM_UNINIT);
	gen = znp_nv_generation();

	/* The same value again, and a stack-owned item, do not count. */
	zassert_equal(znp_nv_write(ZNP_NV_PANID, 0U, &a, 1U), ZNP_STATUS_SUCCESS);
	zassert_equal(znp_nv_set_derived(ZNP_NV_NIB, &b, 1U), ZNP_STATUS_SUCCESS);
	zassert_equal(znp_nv_generation(), gen);

	zassert_equal(znp_nv_write(ZNP_NV_PANID, 0U, &b, 1U), ZNP_STATUS_SUCCESS);
	zassert_not_equal(znp_nv_generation(), gen);
}

ZTEST(znp, test_link_key_matches_the_hosts_derivation)
{
	/* Reference values computed by zigpy-znp's compute_key() for seed 00..0f. */
	static const uint8_t expected[3][16] = {
		{0x01, 0x03, 0x01, 0x07, 0x01, 0x03, 0x01, 0x0f, 0x09, 0x0b, 0x09, 0x0f, 0x09, 0x0b,
		 0x09, 0x07},
		{0x02, 0x06, 0x06, 0x02, 0x02, 0x0e, 0x0e, 0x02, 0x0a, 0x0e, 0x0e, 0x0a, 0x0a, 0x06,
		 0x06, 0x0a},
		{0x0e, 0x02, 0x02, 0x06, 0x06, 0x02, 0x02, 0x0e, 0x06, 0x0a, 0x0a, 0x0e, 0x0e, 0x0a,
		 0x0a, 0x06},
	};
	static const uint8_t shifts[3] = {0U, 3U, 15U};
	static const uint8_t ieee[8] = {1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U};
	uint8_t seed[16];
	uint8_t key[16];

	for (uint8_t i = 0U; i < sizeof(seed); i++) {
		seed[i] = i;
	}

	for (size_t i = 0; i < ARRAY_SIZE(shifts); i++) {
		znp_tclk_key(seed, shifts[i], ieee, key);
		zassert_mem_equal(key, expected[i], sizeof(key), "shift %u", shifts[i]);
	}
}

static enum znp_feed_result feed_all(struct znp_parser *p, const uint8_t *bytes, size_t len)
{
	enum znp_feed_result res = ZNP_FEED_MORE;

	for (size_t i = 0; i < len; i++) {
		res = znp_parser_feed(p, bytes[i]);
	}

	return res;
}

ZTEST(znp, test_frame_round_trip)
{
	static const uint8_t payload[] = {1U, 2U, 3U, 4U};
	uint8_t frame[ZNP_MAX_DATA + 5U];
	struct znp_parser p;
	size_t n;

	znp_parser_reset(&p);
	n = znp_frame_encode(frame, ZNP_TYPE_SREQ, ZNP_SUBSYS_SYS, 0x08U, payload, sizeof(payload));
	zassert_equal(n, sizeof(payload) + 5U);
	zassert_equal(frame[0], ZNP_SOF);
	zassert_equal(frame[2], 0x21U); /* SREQ, SYS */

	zassert_equal(feed_all(&p, frame, n), ZNP_FEED_FRAME);
	zassert_equal(p.cmd0, 0x21U);
	zassert_equal(p.cmd1, 0x08U);
	zassert_equal(p.len, sizeof(payload));
	zassert_mem_equal(p.data, payload, sizeof(payload));
}

ZTEST(znp, test_frame_without_payload)
{
	uint8_t frame[ZNP_MAX_DATA + 5U];
	struct znp_parser p;
	size_t n;

	znp_parser_reset(&p);
	n = znp_frame_encode(frame, ZNP_TYPE_SRSP, ZNP_SUBSYS_UTIL, 0x00U, NULL, 0U);
	zassert_equal(n, 5U);
	zassert_equal(feed_all(&p, frame, n), ZNP_FEED_FRAME);
	zassert_equal(p.len, 0U);
}

ZTEST(znp, test_bad_checksum_is_rejected_and_the_parser_resyncs)
{
	static const uint8_t payload[2] = {7U, 8U};
	uint8_t frame[ZNP_MAX_DATA + 5U];
	struct znp_parser p;
	size_t n;

	znp_parser_reset(&p);
	n = znp_frame_encode(frame, ZNP_TYPE_SREQ, ZNP_SUBSYS_SYS, 0x01U, payload, sizeof(payload));
	frame[n - 1U] ^= 0xffU;
	zassert_equal(feed_all(&p, frame, n), ZNP_FEED_BAD_FCS);

	/* The next frame is parsed as usual. */
	n = znp_frame_encode(frame, ZNP_TYPE_SREQ, ZNP_SUBSYS_SYS, 0x01U, payload, sizeof(payload));
	zassert_equal(feed_all(&p, frame, n), ZNP_FEED_FRAME);
}

ZTEST(znp, test_noise_before_a_frame_is_skipped)
{
	static const uint8_t noise[] = {0xefU, 0xefU, 0x00U, 0x12U};
	uint8_t frame[ZNP_MAX_DATA + 5U];
	struct znp_parser p;
	size_t n;

	znp_parser_reset(&p);
	zassert_equal(feed_all(&p, noise, sizeof(noise)), ZNP_FEED_MORE);

	n = znp_frame_encode(frame, ZNP_TYPE_SREQ, ZNP_SUBSYS_SYS, 0x01U, NULL, 0U);
	zassert_equal(feed_all(&p, frame, n), ZNP_FEED_FRAME);
}

ZTEST(znp, test_oversized_length_drops_the_frame)
{
	static const uint8_t oversized[] = {ZNP_SOF, ZNP_MAX_DATA + 1U};
	uint8_t frame[ZNP_MAX_DATA + 5U];
	struct znp_parser p;
	size_t n;

	znp_parser_reset(&p);
	zassert_equal(feed_all(&p, oversized, sizeof(oversized)), ZNP_FEED_MORE);

	/* The parser is back to waiting for a start byte. */
	n = znp_frame_encode(frame, ZNP_TYPE_SREQ, ZNP_SUBSYS_SYS, 0x02U, NULL, 0U);
	zassert_equal(feed_all(&p, frame, n), ZNP_FEED_FRAME);
}

ZTEST(znp, test_largest_frame)
{
	uint8_t payload[ZNP_MAX_DATA];
	uint8_t frame[ZNP_MAX_DATA + 5U];
	struct znp_parser p;
	size_t n;

	memset(payload, 0xa5, sizeof(payload));
	znp_parser_reset(&p);
	n = znp_frame_encode(frame, ZNP_TYPE_AREQ, ZNP_SUBSYS_AF, 0x81U, payload, sizeof(payload));
	zassert_equal(n, sizeof(frame));
	zassert_equal(feed_all(&p, frame, n), ZNP_FEED_FRAME);
	zassert_equal(p.len, ZNP_MAX_DATA);
	zassert_mem_equal(p.data, payload, sizeof(payload));
}
