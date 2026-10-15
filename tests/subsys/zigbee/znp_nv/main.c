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

ZTEST_SUITE(znp_nv, NULL, NULL, before, NULL, NULL);

ZTEST(znp_nv, test_missing_item_has_no_length)
{
	zassert_equal(znp_nv_length(0x0021U), 0U);
}

ZTEST(znp_nv, test_init_creates_then_reports_existing)
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

ZTEST(znp_nv, test_write_and_read_back_at_offset)
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

ZTEST(znp_nv, test_delete_keeps_the_other_items)
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

ZTEST(znp_nv, test_repeated_recreate_does_not_exhaust_the_pool)
{
	const uint8_t init = 0x5aU;

	for (int i = 0; i < 100; i++) {
		zassert_equal(znp_nv_item_init(7U, 512U, &init, 1U), ZNP_STATUS_NV_ITEM_UNINIT);
		zassert_equal(znp_nv_delete(7U), ZNP_STATUS_SUCCESS);
	}
}
