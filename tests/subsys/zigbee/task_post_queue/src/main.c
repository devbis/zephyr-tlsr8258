/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Callback queues of the Zigbee thread: transmit completions run before
 * receive parsing, which runs before general callbacks; each lane refuses a
 * post once it is full; one drain runs a bounded number of callbacks.
 */

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include "tl_platform.h"
#include "zb_common.h"

#define POST_DEPTH   64
#define RX_DEPTH     64
#define TX_DEPTH     4
#define DRAIN_BUDGET 32

K_SEM_DEFINE(zb_ev_sem, 0, 1);

static int ss_info_updates;
static int order[3 * POST_DEPTH];
static int ran;

void zdo_ssInfoUpdate(void)
{
	ss_info_updates++;
}

static void record(void *arg)
{
	order[ran++] = POINTER_TO_INT(arg);
}

static void drain_all(void)
{
	while (!zb_taskq_is_empty()) {
		zb_taskq_process();
	}
}

static void reset(void *fixture)
{
	ARG_UNUSED(fixture);

	zb_sched_init();
	ran = 0;
	ss_info_updates = 0;
	k_sem_reset(&zb_ev_sem);
}

ZTEST(task_post_queue, test_post_runs_callback_and_wakes_thread)
{
	zassert_equal(tl_zbTaskPost(record, INT_TO_POINTER(3)), RET_OK);
	zassert_equal(k_sem_count_get(&zb_ev_sem), 1U);
	zb_taskq_process();
	zassert_equal(ran, 1);
	zassert_equal(order[0], 3);
	zassert_true(zb_taskq_is_empty());
	zassert_equal(ss_info_updates, 1);
}

ZTEST(task_post_queue, test_null_callback_is_rejected)
{
	zassert_equal(tl_zbTaskPost(NULL, NULL), RET_INVALID_PARAMETER);
	zassert_equal(tl_zbRxTaskPost(NULL, NULL), RET_INVALID_PARAMETER);
	zassert_equal(tl_zbTxTaskPost(NULL, NULL), RET_INVALID_PARAMETER);
	zassert_true(zb_taskq_is_empty());
}

ZTEST(task_post_queue, test_lanes_refuse_posts_when_full)
{
	for (int i = 0; i < POST_DEPTH; i++) {
		zassert_equal(tl_zbTaskPost(record, INT_TO_POINTER(i)), RET_OK);
	}
	zassert_equal(tl_zbTaskPost(record, NULL), RET_BUSY);

	for (int i = 0; i < RX_DEPTH; i++) {
		zassert_equal(tl_zbRxTaskPost(record, INT_TO_POINTER(i)), RET_OK);
	}
	zassert_equal(tl_zbRxTaskPost(record, NULL), RET_BUSY);

	for (int i = 0; i < TX_DEPTH; i++) {
		zassert_equal(tl_zbTxTaskPost(record, INT_TO_POINTER(i)), RET_OK);
	}
	zassert_equal(tl_zbTxTaskPost(record, NULL), RET_BUSY);
}

ZTEST(task_post_queue, test_tx_before_rx_before_general)
{
	zassert_equal(tl_zbTaskPost(record, INT_TO_POINTER(1)), RET_OK);
	zassert_equal(tl_zbRxTaskPost(record, INT_TO_POINTER(2)), RET_OK);
	zassert_equal(tl_zbTxTaskPost(record, INT_TO_POINTER(3)), RET_OK);
	zassert_equal(tl_zbRxTaskPost(record, INT_TO_POINTER(4)), RET_OK);

	drain_all();
	zassert_equal(ran, 4);
	zassert_equal(order[0], 3);
	zassert_equal(order[1], 2);
	zassert_equal(order[2], 4);
	zassert_equal(order[3], 1);
}

ZTEST(task_post_queue, test_drain_is_bounded)
{
	for (int i = 0; i < POST_DEPTH; i++) {
		zassert_equal(tl_zbTaskPost(record, INT_TO_POINTER(i)), RET_OK);
	}

	zb_taskq_process();
	zassert_equal(ran, DRAIN_BUDGET);
	zassert_false(zb_taskq_is_empty());

	drain_all();
	zassert_equal(ran, POST_DEPTH);
	for (int i = 0; i < POST_DEPTH; i++) {
		zassert_equal(order[i], i);
	}
}

ZTEST(task_post_queue, test_lane_wraps_around)
{
	for (int round = 0; round < 3; round++) {
		for (int i = 0; i < TX_DEPTH; i++) {
			zassert_equal(tl_zbTxTaskPost(record, INT_TO_POINTER(i)), RET_OK);
		}
		drain_all();
	}

	zassert_equal(ran, 3 * TX_DEPTH);
	zassert_equal(order[ran - 1], TX_DEPTH - 1);
}

ZTEST_SUITE(task_post_queue, NULL, NULL, reset, NULL, NULL);
