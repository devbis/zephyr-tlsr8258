/* SPDX-License-Identifier: Apache-2.0 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "zb_common_stub.h"
#include "os/ev_timer.h"

static u8 g_task_post_result;
static bool g_timer_available;
static int g_failures;
static int g_task_post_calls;
static int g_timer_arm_calls;
static tl_zb_callback_t g_posted_task;
static void *g_posted_task_arg;
static ev_timer_callback_t g_posted_timer;
static void *g_posted_timer_arg;
static u32 g_posted_timer_delay_ms;
static ev_timer_event_t g_timer_evt;

u8 tl_zbTaskPost(tl_zb_callback_t fn, void *arg)
{
	g_task_post_calls++;
	g_posted_task = fn;
	g_posted_task_arg = arg;
	return g_task_post_result;
}

void ev_on_timer(ev_timer_event_t *evt, u32 t_ms)
{
	g_timer_arm_calls++;
	g_posted_timer = evt->cb;
	g_posted_timer_arg = evt->data;
	g_posted_timer_delay_ms = t_ms;
	evt->timeout = t_ms;
	evt->used = g_timer_available;
}

#include "../../../subsys/zigbee/nwk/nwk_schedule_fallback.c"

#define EXPECT_TRUE(cond) do { \
	if (!(cond)) { \
		fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
		g_failures++; \
	} \
} while (0)

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
	g_task_post_result = RET_OK;
	g_timer_available = false;
	g_failures = 0;
	g_task_post_calls = 0;
	g_timer_arm_calls = 0;
	g_posted_task = NULL;
	g_posted_task_arg = NULL;
	g_posted_timer = NULL;
	g_posted_timer_arg = NULL;
	g_posted_timer_delay_ms = 0;
	memset(&g_timer_evt, 0, sizeof(g_timer_evt));
}

static int timer_cb(void *arg)
{
	(void)arg;
	return -1;
}

static void task_cb(void *arg)
{
	(void)arg;
}

static bool test_prefers_task_queue_when_available(void)
{
	reset_state();

	EXPECT_TRUE(zb_nwk_schedule_task_or_timer(task_cb, &g_timer_evt, timer_cb,
						    (void *)0x1234, 1U));
	EXPECT_EQ(g_task_post_calls, 1);
	EXPECT_EQ(g_timer_arm_calls, 0);
	EXPECT_TRUE(g_posted_task == task_cb);
	EXPECT_TRUE(g_posted_task_arg == (void *)0x1234);

	return g_failures == 0;
}

static bool test_falls_back_to_timer_when_task_queue_busy(void)
{
	reset_state();
	g_task_post_result = RET_BUSY;
	g_timer_available = true;

	EXPECT_TRUE(zb_nwk_schedule_task_or_timer(task_cb, &g_timer_evt, timer_cb,
						    (void *)0x5678, 1U));
	EXPECT_EQ(g_task_post_calls, 1);
	EXPECT_EQ(g_timer_arm_calls, 1);
	EXPECT_TRUE(g_posted_timer == timer_cb);
	EXPECT_TRUE(g_posted_timer_arg == (void *)0x5678);
	EXPECT_EQ(g_posted_timer_delay_ms, 1);

	return g_failures == 0;
}

static bool test_fails_when_task_queue_busy_and_timer_unavailable(void)
{
	reset_state();
	g_task_post_result = RET_BUSY;

	EXPECT_TRUE(!zb_nwk_schedule_task_or_timer(task_cb, NULL, timer_cb,
						     (void *)0x9abc, 1U));
	EXPECT_EQ(g_task_post_calls, 1);
	EXPECT_EQ(g_timer_arm_calls, 0);

	return g_failures == 0;
}

int main(void)
{
	if (!test_prefers_task_queue_when_available()) {
		return 1;
	}

	if (!test_falls_back_to_timer_when_task_queue_busy()) {
		return 1;
	}

	if (!test_fails_when_task_queue_busy_and_timer_unavailable()) {
		return 1;
	}

	return 0;
}
