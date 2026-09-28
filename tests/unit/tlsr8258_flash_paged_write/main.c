/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>
#include <string.h>

#include <zephyr/ztest.h>

/* The loop lives in the TC32 .ram_code section, which a host object format may not
 * accept: take the header's place so that the attribute expands to nothing.
 */
#define ZEPHYR_DRIVERS_FLASH_TLSR8258_PAGED_WRITE_H_
#define TLSR8258_FLASH_PAGED_EXEC
int tlsr8258_flash_write_pages(void *ctx, uint32_t addr, const uint8_t *buf, size_t len);
#include "flash_tlsr8258_paged_write.c"

#define MAX_CALLS 4

static int watchdog_feed_calls;
static int writer_calls;
static int writer_fail_at = -1;
static uint32_t writer_addr[MAX_CALLS];
static size_t writer_len[MAX_CALLS];
static uint8_t writer_buf[MAX_CALLS][TLSR8258_FLASH_PAGE_SIZE];
static int calls_seen_by_watchdog[MAX_CALLS];

void tlsr8258_flash_watchdog_clear(void)
{
	calls_seen_by_watchdog[watchdog_feed_calls] = writer_calls;
	watchdog_feed_calls++;
}

int tlsr8258_flash_write_page_locked(void *ctx, uint32_t addr, const uint8_t *buf, size_t len)
{
	ARG_UNUSED(ctx);

	zassert_true(writer_calls < MAX_CALLS);
	writer_addr[writer_calls] = addr;
	writer_len[writer_calls] = len;
	memcpy(writer_buf[writer_calls], buf, len);
	if (writer_calls++ == writer_fail_at) {
		return -EIO;
	}

	return 0;
}

static void reset_state(void *fixture)
{
	ARG_UNUSED(fixture);

	watchdog_feed_calls = 0;
	writer_calls = 0;
	writer_fail_at = -1;
	memset(writer_addr, 0, sizeof(writer_addr));
	memset(writer_len, 0, sizeof(writer_len));
	memset(writer_buf, 0, sizeof(writer_buf));
	memset(calls_seen_by_watchdog, 0, sizeof(calls_seen_by_watchdog));
}

ZTEST(tlsr8258_flash_paged_write, test_write_split_at_page_boundary)
{
	static const uint8_t payload[20] = {
		0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19,
	};

	zassert_equal(tlsr8258_flash_write_pages(NULL, 250U, payload, sizeof(payload)), 0);
	zassert_equal(writer_calls, 2);
	zassert_equal(writer_addr[0], 250U);
	zassert_equal(writer_len[0], 6U);
	zassert_equal(writer_addr[1], 256U);
	zassert_equal(writer_len[1], 14U);
	zassert_mem_equal(writer_buf[0], payload, 6U);
	zassert_mem_equal(writer_buf[1], payload + 6, 14U);
}

ZTEST(tlsr8258_flash_paged_write, test_watchdog_fed_before_each_page_program)
{
	static const uint8_t payload[20];

	zassert_equal(tlsr8258_flash_write_pages(NULL, 250U, payload, sizeof(payload)), 0);
	zassert_equal(watchdog_feed_calls, 2);
	zassert_equal(calls_seen_by_watchdog[0], 0);
	zassert_equal(calls_seen_by_watchdog[1], 1);
}

ZTEST(tlsr8258_flash_paged_write, test_write_stops_at_first_error)
{
	static const uint8_t payload[3 * TLSR8258_FLASH_PAGE_SIZE];

	writer_fail_at = 1;
	zassert_equal(tlsr8258_flash_write_pages(NULL, 0U, payload, sizeof(payload)), -EIO);
	zassert_equal(writer_calls, 2);
}

ZTEST(tlsr8258_flash_paged_write, test_empty_write_touches_nothing)
{
	zassert_equal(tlsr8258_flash_write_pages(NULL, 0U, NULL, 0U), 0);
	zassert_equal(writer_calls, 0);
	zassert_equal(watchdog_feed_calls, 0);
}

ZTEST_SUITE(tlsr8258_flash_paged_write, NULL, NULL, reset_state, NULL, NULL);
