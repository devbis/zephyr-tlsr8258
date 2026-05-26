/* SPDX-License-Identifier: Apache-2.0 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef int (*tlsr8258_flash_page_writer_t)(void *ctx, uint32_t addr, const uint8_t *buf,
					       size_t len);
typedef void (*tlsr8258_watchdog_feed_t)(void *ctx);

int tlsr8258_flash_write_pages(void *ctx, uint32_t addr, const uint8_t *buf, size_t len,
			       tlsr8258_flash_page_writer_t writer,
			       tlsr8258_watchdog_feed_t watchdog_feed);

static int g_failures;
static int g_watchdog_feed_calls;
static int g_writer_calls;
static uint32_t g_writer_addr[4];
static size_t g_writer_len[4];
static uint8_t g_writer_buf[4][32];

#define EXPECT_EQ(actual, expected) do { \
	long long _actual = (long long)(actual); \
	long long _expected = (long long)(expected); \
	if (_actual != _expected) { \
		fprintf(stderr, "FAIL %s:%d: %s=%lld expected %lld\n", __FILE__, __LINE__, \
			#actual, _actual, _expected); \
		g_failures++; \
	} \
} while (0)

static void reset_state(void)
{
	g_failures = 0;
	g_watchdog_feed_calls = 0;
	g_writer_calls = 0;
	memset(g_writer_addr, 0, sizeof(g_writer_addr));
	memset(g_writer_len, 0, sizeof(g_writer_len));
	memset(g_writer_buf, 0, sizeof(g_writer_buf));
}

static void watchdog_feed(void *ctx)
{
	(void)ctx;
	g_watchdog_feed_calls++;
}

static int writer(void *ctx, uint32_t addr, const uint8_t *buf, size_t len)
{
	(void)ctx;
	g_writer_addr[g_writer_calls] = addr;
	g_writer_len[g_writer_calls] = len;
	memcpy(g_writer_buf[g_writer_calls], buf, len);
	g_writer_calls++;
	return 0;
}

static int test_feeds_watchdog_before_each_page_program(void)
{
	static const uint8_t payload[20] = {
		0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
		10, 11, 12, 13, 14, 15, 16, 17, 18, 19,
	};

	reset_state();

	EXPECT_EQ(tlsr8258_flash_write_pages(NULL, 250U, payload, sizeof(payload), writer,
					       watchdog_feed), 0);
	EXPECT_EQ(g_watchdog_feed_calls, 2);
	EXPECT_EQ(g_writer_calls, 2);
	EXPECT_EQ(g_writer_addr[0], 250U);
	EXPECT_EQ(g_writer_len[0], 6U);
	EXPECT_EQ(g_writer_addr[1], 256U);
	EXPECT_EQ(g_writer_len[1], 14U);
	EXPECT_EQ(memcmp(g_writer_buf[0], payload, 6U), 0);
	EXPECT_EQ(memcmp(g_writer_buf[1], payload + 6, 14U), 0);

	return g_failures == 0 ? 0 : 1;
}

int main(void)
{
	return test_feeds_watchdog_before_each_page_program();
}
